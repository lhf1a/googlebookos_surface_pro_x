#pragma once

#include "sq2-c2-enc-interface.h"
#include "sq2-v4l2-enc-session.h"

#include <C2Component.h>
#include <C2PlatformSupport.h>
#include <SimpleC2Interface.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Sq2C2V4l2Encoder final
    : public C2Component,
      public std::enable_shared_from_this<Sq2C2V4l2Encoder> {
public:
    Sq2C2V4l2Encoder(const std::shared_ptr<C2ComponentInterface> &intf,
                     const std::shared_ptr<Sq2EncInterface> &encIntf)
        : mIntf(intf), mEncIntf(encIntf) {}

    ~Sq2C2V4l2Encoder() override {
        release();
    }

    c2_status_t setListener_vb(const std::shared_ptr<Listener> &listener,
                               c2_blocking_t) override {
        std::lock_guard<std::mutex> lk(mStateMu);
        if (mState == State::RELEASED) return C2_BAD_STATE;
        if (mState == State::RUNNING && listener) return C2_BAD_STATE;
        mListener = listener;
        return C2_OK;
    }

    c2_status_t queue_nb(std::list<std::unique_ptr<C2Work>> *const items) override {
        if (!items) return C2_BAD_VALUE;
        std::lock_guard<std::mutex> lk(mStateMu);
        if (mState != State::RUNNING) return C2_BAD_STATE;
        while (!items->empty()) {
            mQueue.push_back(std::move(items->front()));
            items->pop_front();
        }
        mCv.notify_all();
        return C2_OK;
    }

    c2_status_t announce_nb(const std::vector<C2WorkOutline> &) override {
        return C2_OMITTED;
    }

    c2_status_t flush_sm(flush_mode_t mode,
                         std::list<std::unique_ptr<C2Work>> *const flushedWork) override {
        (void)mode;
        std::unique_lock<std::mutex> lk(mStateMu);
        if (mState != State::RUNNING && mState != State::STOPPED) return C2_BAD_STATE;
        mFlushing = true;
        mCv.notify_all();
        mFlushDoneCv.wait(lk, [&]() { return !mInProcessLoop; });

        if (flushedWork) {
            while (!mQueue.empty()) {
                flushedWork->push_back(std::move(mQueue.front()));
                mQueue.pop_front();
            }
            for (auto &kv : mInFlight) {
                flushedWork->push_back(std::move(kv.second));
            }
        }
        mQueue.clear();
        mInFlight.clear();
        mSession.closeSession();
        mSentCsd = false;
        mSawEos = false;
        mFlushing = false;
        return C2_OK;
    }

    c2_status_t drain_nb(drain_mode_t mode) override {
        if (mode == DRAIN_CHAIN) return C2_OMITTED;
        std::lock_guard<std::mutex> lk(mStateMu);
        if (mState != State::RUNNING) return C2_BAD_STATE;
        mDrainRequested = true;
        if (mode == DRAIN_COMPONENT_WITH_EOS) mDrainWithEos = true;
        mCv.notify_all();
        return C2_OK;
    }

    c2_status_t start() override {
        std::lock_guard<std::mutex> lk(mStateMu);
        if (mState == State::RUNNING) return C2_DUPLICATE;
        if (mState != State::STOPPED) return C2_BAD_STATE;
        mState = State::RUNNING;
        mStopRequested = false;
        mFlushing = false;
        mSawEos = false;
        mSentCsd = false;
        mDrainRequested = false;
        mDrainWithEos = false;
        mWorker = std::thread(&Sq2C2V4l2Encoder::workerLoop, this);
        return C2_OK;
    }

    c2_status_t stop() override {
        {
            std::lock_guard<std::mutex> lk(mStateMu);
            if (mState == State::STOPPED) return C2_DUPLICATE;
            if (mState != State::RUNNING) return C2_BAD_STATE;
            mStopRequested = true;
            mCv.notify_all();
        }
        if (mWorker.joinable()) mWorker.join();
        std::lock_guard<std::mutex> lk(mStateMu);
        mQueue.clear();
        mInFlight.clear();
        mSession.closeSession();
        mOutputPool.reset();
        mSentCsd = false;
        mSawEos = false;
        mState = State::STOPPED;
        return C2_OK;
    }

    c2_status_t reset() override {
        {
            std::lock_guard<std::mutex> lk(mStateMu);
            if (mState == State::RELEASED) return C2_BAD_STATE;
            mStopRequested = true;
            mCv.notify_all();
        }
        if (mWorker.joinable()) mWorker.join();
        std::lock_guard<std::mutex> lk(mStateMu);
        mQueue.clear();
        mInFlight.clear();
        mSession.closeSession();
        mOutputPool.reset();
        mSentCsd = false;
        mSawEos = false;
        mState = State::STOPPED;
        return C2_OK;
    }

    c2_status_t release() override {
        {
            std::lock_guard<std::mutex> lk(mStateMu);
            if (mState == State::RELEASED) return C2_DUPLICATE;
            mStopRequested = true;
            mCv.notify_all();
        }
        if (mWorker.joinable()) mWorker.join();
        std::lock_guard<std::mutex> lk(mStateMu);
        mQueue.clear();
        mInFlight.clear();
        mSession.closeSession();
        mOutputPool.reset();
        mListener.reset();
        mState = State::RELEASED;
        return C2_OK;
    }

    std::shared_ptr<C2ComponentInterface> intf() override {
        return mIntf;
    }

private:
    enum class State { STOPPED, RUNNING, RELEASED };

    static size_t findAnnexBCsdEnd(Sq2EncCodec codec, const uint8_t *data, size_t len) {
        if (codec == Sq2EncCodec::VP8) return 0;
        size_t i = 0;
        while (i + 3 < len) {
            size_t scLen = 0;
            if (data[i] == 0 && data[i + 1] == 0) {
                if (data[i + 2] == 1) scLen = 3;
                else if (i + 4 < len && data[i + 2] == 0 && data[i + 3] == 1) scLen = 4;
            }
            if (scLen > 0) {
                uint8_t hdr = data[i + scLen];
                if (codec == Sq2EncCodec::H264) {
                    uint8_t naluType = hdr & 0x1F;
                    if (naluType >= 1 && naluType <= 5) return i;
                } else if (codec == Sq2EncCodec::HEVC) {
                    uint8_t naluType = (hdr >> 1) & 0x3F;
                    if (naluType < 32) return i;
                }
                i += scLen + 1;
            } else {
                ++i;
            }
        }
        return 0;
    }

    bool ensureSessionAndPool() {
        if (!mOutputPool) {
            c2_status_t st = android::GetCodec2BlockPool(
                    mEncIntf->outputPoolId(), shared_from_this(), &mOutputPool);
            if (st != C2_OK || !mOutputPool) {
                fprintf(stderr, "SQ2_C2_ENC GetCodec2BlockPool failed st=%d\n", st);
                return false;
            }
        }
        if (!mSession.isOpen()) {
            mActiveParams = mEncIntf->snapshotParams();
            if (!mSession.openSession(mActiveParams)) {
                fprintf(stderr, "SQ2_C2_ENC openSession failed errno=%d (%s)\n", errno, strerror(errno));
                return false;
            }
        }
        return true;
    }

    std::shared_ptr<C2Buffer> makeLinearBuffer(const uint8_t *bytes, size_t size, bool isSync) {
        if (!mOutputPool) return nullptr;
        size_t allocSz = std::max<size_t>(size, 64);
        std::shared_ptr<C2LinearBlock> block;
        c2_status_t st = mOutputPool->fetchLinearBlock(
                allocSz, {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block);
        if (st != C2_OK || !block) return nullptr;
        C2WriteView w = block->map().get();
        if (w.error() != C2_OK) return nullptr;
        if (size > 0) memcpy(w.data(), bytes, size);
        std::shared_ptr<C2Buffer> buf = C2Buffer::CreateLinearBuffer(block->share(0, size, C2Fence()));
        if (buf && isSync) {
            buf->setInfo(std::make_shared<C2StreamPictureTypeMaskInfo::output>(
                    0u, C2Config::SYNC_FRAME));
        }
        return buf;
    }

    void emitWorkDone(std::unique_ptr<C2Work> work, c2_status_t result) {
        work->result = result;
        work->workletsProcessed = 1u;
        std::shared_ptr<Listener> l;
        {
            std::lock_guard<std::mutex> lk(mStateMu);
            l = mListener;
        }
        if (l) {
            std::list<std::unique_ptr<C2Work>> done;
            done.push_back(std::move(work));
            l->onWorkDone_nb(shared_from_this(), std::move(done));
        }
    }

    bool submitWorkInput(std::unique_ptr<C2Work> work) {
        if (work->worklets.empty()) {
            work->worklets.emplace_back(new C2Worklet);
        }
        bool isEos = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0;
        if (work->input.buffers.empty()) {
            if (isEos && !mSession.isOpen()) {
                work->worklets.front()->output.flags = C2FrameData::FLAG_END_OF_STREAM;
                work->worklets.front()->output.buffers.clear();
                work->worklets.front()->output.ordinal = work->input.ordinal;
                emitWorkDone(std::move(work), C2_OK);
                mSawEos = true;
                return true;
            }
            if (!ensureSessionAndPool()) return false;
            int64_t tag = mNextTagUs++;
            mInFlight[tag] = std::move(work);
            if (isEos) mSession.queueEos(tag);
            return true;
        }

        if (!ensureSessionAndPool()) return false;

        uint32_t targetBitrate = mEncIntf->getBitrateValue();
        if (targetBitrate != mActiveParams.bitrate) {
            mActiveParams.bitrate = targetBitrate;
            mSession.updateBitrate(targetBitrate);
        }
        bool forceSync = mEncIntf->consumeSyncRequest();

        const auto &inBuf = work->input.buffers.front();
        if (!inBuf || inBuf->data().type() != C2BufferData::GRAPHIC) {
            fprintf(stderr, "SQ2_C2_ENC input not GRAPHIC\n");
            return false;
        }
        const C2GraphicView view = inBuf->data().graphicBlocks().front().map().get();
        if (view.error() != C2_OK) {
            fprintf(stderr, "SQ2_C2_ENC view.error=%d\n", view.error());
            return false;
        }

        int bufIdx = mSession.acquireInputBuffer(1000);
        if (bufIdx < 0) {
            fprintf(stderr, "SQ2_C2_ENC acquireInputBuffer failed\n");
            return false;
        }
        void *dst = mSession.inputBufferPtr(bufIdx);
        if (!dst) {
            fprintf(stderr, "SQ2_C2_ENC inputBufferPtr null\n");
            return false;
        }

        const C2PlanarLayout &layout = view.layout();
        const uint32_t vw = view.width();
        const uint32_t vh = view.height();
        if (layout.type == C2PlanarLayout::TYPE_RGBA || layout.type == C2PlanarLayout::TYPE_RGB) {
            const uint8_t *rPtr = view.data()[C2PlanarLayout::PLANE_R];
            const uint8_t *gPtr = view.data()[C2PlanarLayout::PLANE_G];
            const uint8_t *bPtr = view.data()[C2PlanarLayout::PLANE_B];
            mSession.fillNv12FromRgba(dst, rPtr, gPtr, bPtr,
                                      layout.planes[C2PlanarLayout::PLANE_R].colInc,
                                      layout.planes[C2PlanarLayout::PLANE_R].rowInc,
                                      vw, vh);
        } else if (layout.type == C2PlanarLayout::TYPE_YUV) {
            mSession.fillNv12FromYuv(dst,
                                     view.data()[C2PlanarLayout::PLANE_Y],
                                     view.data()[C2PlanarLayout::PLANE_U],
                                     view.data()[C2PlanarLayout::PLANE_V],
                                     layout.planes[C2PlanarLayout::PLANE_Y].rowInc,
                                     layout.planes[C2PlanarLayout::PLANE_U].rowInc,
                                     layout.planes[C2PlanarLayout::PLANE_V].rowInc,
                                     layout.planes[C2PlanarLayout::PLANE_U].colInc,
                                     layout.planes[C2PlanarLayout::PLANE_V].colInc,
                                     vw, vh);
        } else {
            fprintf(stderr, "SQ2_C2_ENC unsupported layout.type=%d\n", layout.type);
            return false;
        }

        int64_t tag = mNextTagUs++;
        if (!mSession.queueInputFrame(bufIdx, tag, forceSync)) {
            fprintf(stderr, "SQ2_C2_ENC queueInputFrame failed\n");
            return false;
        }
        mInFlight[tag] = std::move(work);
        if (isEos) {
            mSession.queueEos(tag);
        }
        return true;
    }

    void drainPackets(int timeoutMs) {
        Sq2EncodedPacket pkt;
        while (true) {
            int rc = mSession.dequeueOutput(pkt, mInFlight.empty() ? 0 : timeoutMs);
            if (rc == 2) break;
            if (rc < 0) {
                mInFlight.clear();
                mSession.closeSession();
                std::shared_ptr<Listener> l;
                { std::lock_guard<std::mutex> lk(mStateMu); l = mListener; }
                if (l) l->onError_nb(shared_from_this(), C2_CORRUPTED);
                break;
            }
            if (rc == 0) break;
            timeoutMs = 0;

            std::unique_ptr<C2Work> work;
            auto it = mInFlight.find(pkt.timestampUs);
            if (it != mInFlight.end()) {
                work = std::move(it->second);
                mInFlight.erase(it);
            } else if (!mInFlight.empty()) {
                it = mInFlight.begin();
                work = std::move(it->second);
                mInFlight.erase(it);
            }
            if (!work) {
                if (pkt.eos) mSawEos = true;
                continue;
            }

            const uint8_t *outBytes = pkt.data.data();
            size_t outLen = pkt.data.size();
            if (!mSentCsd && outLen > 0) {
                size_t csdEnd = findAnnexBCsdEnd(mActiveParams.codec, outBytes, outLen);
                if (csdEnd > 0) {
                    auto csd = C2StreamInitDataInfo::output::AllocUnique(csdEnd, 0u);
                    if (csd) {
                        memcpy(csd->m.value, outBytes, csdEnd);
                        work->worklets.front()->output.configUpdate.push_back(std::move(csd));
                    }
                    mSentCsd = true;
                    if (!mActiveParams.prependHeader && csdEnd <= outLen) {
                        outBytes += csdEnd;
                        outLen -= csdEnd;
                    }
                }
            }

            work->worklets.front()->output.buffers.clear();
            if (outLen > 0) {
                auto c2buf = makeLinearBuffer(outBytes, outLen, pkt.keyFrame);
                if (!c2buf) {
                    emitWorkDone(std::move(work), C2_NO_MEMORY);
                    continue;
                }
                work->worklets.front()->output.buffers.push_back(std::move(c2buf));
            }
            bool workInputEos = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0;
            work->worklets.front()->output.flags =
                    (pkt.eos || workInputEos) ? C2FrameData::FLAG_END_OF_STREAM : static_cast<C2FrameData::flags_t>(0);
            work->worklets.front()->output.ordinal = work->input.ordinal;
            if (pkt.eos) mSawEos = true;
            emitWorkDone(std::move(work), C2_OK);
        }
    }

    void workerLoop() {
        while (true) {
            std::unique_ptr<C2Work> nextWork;
            bool doFlushWait = false;
            {
                std::unique_lock<std::mutex> lk(mStateMu);
                mInProcessLoop = false;
                if (mFlushing) mFlushDoneCv.notify_all();
                mCv.wait(lk, [&]() {
                    return mStopRequested || mFlushing || !mQueue.empty() || !mInFlight.empty() || mDrainRequested;
                });
                if (mStopRequested) break;
                if (mFlushing) {
                    doFlushWait = true;
                } else {
                    mInProcessLoop = true;
                    if (!mQueue.empty()) {
                        nextWork = std::move(mQueue.front());
                        mQueue.pop_front();
                    }
                    if (mDrainRequested && mQueue.empty() && mDrainWithEos && mSession.isOpen()) {
                        mSession.queueEos(mNextTagUs);
                        mDrainWithEos = false;
                        mDrainRequested = false;
                    } else if (mDrainRequested && mQueue.empty() && mInFlight.empty()) {
                        mDrainRequested = false;
                    }
                }
            }

            if (doFlushWait) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (nextWork) {
                if (!submitWorkInput(std::move(nextWork))) {
                    std::shared_ptr<Listener> l;
                    { std::lock_guard<std::mutex> lk(mStateMu); l = mListener; }
                    if (l) l->onError_nb(shared_from_this(), C2_CORRUPTED);
                }
            }

            if (mSession.isOpen()) {
                drainPackets(mInFlight.empty() ? 0 : 15);
            }
        }
    }

    std::shared_ptr<C2ComponentInterface> mIntf;
    std::shared_ptr<Sq2EncInterface> mEncIntf;
    std::shared_ptr<C2BlockPool> mOutputPool;
    Sq2V4l2EncSession mSession;
    Sq2EncParams mActiveParams{};

    std::mutex mStateMu;
    std::condition_variable mCv;
    std::condition_variable mFlushDoneCv;
    State mState = State::STOPPED;
    bool mStopRequested = false;
    bool mFlushing = false;
    bool mInProcessLoop = false;
    bool mDrainRequested = false;
    bool mDrainWithEos = false;
    bool mSentCsd = false;
    bool mSawEos = false;
    int64_t mNextTagUs = 1;
    std::shared_ptr<Listener> mListener;
    std::deque<std::unique_ptr<C2Work>> mQueue;
    std::map<int64_t, std::unique_ptr<C2Work>> mInFlight;
    std::thread mWorker;
};

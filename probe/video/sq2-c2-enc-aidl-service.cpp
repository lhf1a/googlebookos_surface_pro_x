#include "sq2-c2-enc-codecs.h"
#include "sq2-c2-enc-interface.h"
#include "sq2-c2-v4l2-encoder.h"

#include <codec2/aidl/ComponentStore.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <util/C2InterfaceHelper.h>
#include <C2Component.h>
#include <C2PlatformSupport.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Sq2EncStore final : public C2ComponentStore {
    class Params : public C2InterfaceHelper {
    public:
        explicit Params(const std::shared_ptr<C2ReflectorHelper> &r) : C2InterfaceHelper(r) {
            setDerivedInstance(this);
        }
    };

    std::shared_ptr<C2ReflectorHelper> mReflector = std::make_shared<C2ReflectorHelper>();
    Params mParams{mReflector};
    std::vector<Sq2EncCodecInfo> mCodecs;

    const Sq2EncCodecInfo *findCodec(const std::string &name) const {
        for (const auto &c : mCodecs) {
            if (name == c.c2Name) return &c;
        }
        return nullptr;
    }

public:
    Sq2EncStore() : mCodecs(sq2ProbeVenusEncoders()) {}

    const std::vector<Sq2EncCodecInfo> &codecs() const { return mCodecs; }

    C2String getName() const override { return "sq2.enc"; }

    c2_status_t createInterface(C2String name,
                                std::shared_ptr<C2ComponentInterface> *out) override {
        const Sq2EncCodecInfo *info = findCodec(name);
        if (!info) {
            out->reset();
            return C2_NOT_FOUND;
        }
        auto impl = std::make_shared<Sq2EncInterface>(mReflector, *info);
        *out = std::make_shared<android::SimpleC2Interface<Sq2EncInterface>>(info->c2Name, 0, impl);
        return C2_OK;
    }

    c2_status_t createComponent(C2String name,
                                std::shared_ptr<C2Component> *out) override {
        const Sq2EncCodecInfo *info = findCodec(name);
        if (!info) {
            out->reset();
            return C2_NOT_FOUND;
        }
        auto impl = std::make_shared<Sq2EncInterface>(mReflector, *info);
        auto intf = std::make_shared<android::SimpleC2Interface<Sq2EncInterface>>(info->c2Name, 0, impl);
        *out = std::make_shared<Sq2C2V4l2Encoder>(intf, impl);
        return C2_OK;
    }

    std::vector<std::shared_ptr<const C2Component::Traits>> listComponents() override {
        std::vector<std::shared_ptr<const C2Component::Traits>> result;
        for (const auto &c : mCodecs) {
            auto t = std::make_shared<C2Component::Traits>();
            t->name = c.c2Name;
            t->domain = C2Component::DOMAIN_VIDEO;
            t->kind = C2Component::KIND_ENCODER;
            t->rank = 100;
            t->mediaType = c.mime;
            t->owner = getName();
            result.push_back(t);
        }
        return result;
    }

    c2_status_t copyBuffer(std::shared_ptr<C2GraphicBuffer>,
                           std::shared_ptr<C2GraphicBuffer>) override {
        return C2_OMITTED;
    }

    c2_status_t query_sm(const std::vector<C2Param *> &stackParams,
                         const std::vector<C2Param::Index> &heapParamIndices,
                         std::vector<std::unique_ptr<C2Param>> *const heapParams) const override {
        return mParams.query(stackParams, heapParamIndices, C2_MAY_BLOCK, heapParams);
    }

    c2_status_t config_sm(const std::vector<C2Param *> &params,
                          std::vector<std::unique_ptr<C2SettingResult>> *const failures) override {
        std::vector<C2Param *> remaining;
        remaining.reserve(params.size());
        for (C2Param *p : params) {
            if (p && p->type() == C2StoreDmaBufUsageInfo::PARAM_TYPE) {
                auto *info = static_cast<C2StoreDmaBufUsageInfo *>(p);
                info->m.allocFlags = 0;
                size_t cap = info->flexCount();
                if (cap > 0) {
                    snprintf(info->m.heapName, cap, "%s", "system");
                }
            } else {
                remaining.push_back(p);
            }
        }
        if (remaining.empty()) return C2_OK;
        return mParams.config(remaining, C2_MAY_BLOCK, failures);
    }

    std::shared_ptr<C2ParamReflector> getParamReflector() const override {
        return mReflector;
    }

    c2_status_t querySupportedParams_nb(
            std::vector<std::shared_ptr<C2ParamDescriptor>> *const params) const override {
        return mParams.querySupportedParams(params);
    }

    c2_status_t querySupportedValues_sm(
            std::vector<C2FieldSupportedValuesQuery> &fields) const override {
        return mParams.querySupportedValues(fields, C2_MAY_BLOCK);
    }
};

static int printCapabilitiesXml(const Sq2EncStore &store) {
    printf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    printf("<MediaCodecs>\n");
    printf("    <Encoders>\n");
    for (const auto &c : store.codecs()) {
        uint32_t maxBlocks = (c.maxWidth / 16u) * (c.maxHeight / 16u);
        uint32_t minBlocks = std::max(1u, (c.minWidth / 16u) * (c.minHeight / 16u));
        uint64_t maxBps = static_cast<uint64_t>(1920 / 16) * (1080 / 16) * 120ULL;
        printf("        <MediaCodec name=\"%s\" type=\"%s\" rank=\"100\">\n", c.c2Name, c.mime);
        printf("            <Limit name=\"size\" min=\"%ux%u\" max=\"%ux%u\" />\n",
               c.minWidth, c.minHeight, c.maxWidth, c.maxHeight);
        printf("            <Limit name=\"alignment\" value=\"2x2\" />\n");
        printf("            <Limit name=\"block-size\" value=\"16x16\" />\n");
        printf("            <Limit name=\"block-count\" range=\"%u-%u\" />\n", minBlocks, maxBlocks);
        printf("            <Limit name=\"blocks-per-second\" range=\"1-%llu\" />\n",
               static_cast<unsigned long long>(maxBps));
        printf("            <Limit name=\"frame-rate\" range=\"1-%u\" />\n", c.maxFps);
        printf("            <Limit name=\"bitrate\" range=\"32000-%u\" />\n", c.maxBitrate);
        printf("            <Limit name=\"concurrent-instances\" max=\"16\" />\n");
        printf("            <Feature name=\"bitrate-modes\" value=\"CBR\" />\n");
        printf("        </MediaCodec>\n");
    }
    printf("    </Encoders>\n");
    printf("</MediaCodecs>\n");
    return 0;
}

static int runSelfTest() {
    auto store = std::make_shared<Sq2EncStore>();
    auto traits = store->listComponents();
    printf("PROBED_CODECS=%zu\n", traits.size());
    for (const auto &t : traits) {
        printf("CODEC name=%s mime=%s rank=%u kind=%d\n",
               t->name.c_str(), t->mediaType.c_str(), t->rank, static_cast<int>(t->kind));
    }
    if (traits.empty()) {
        fprintf(stderr, "NO_VENUS_ENCODER_FOUND\n");
        return 1;
    }

    std::shared_ptr<C2ComponentInterface> rejected;
    if (store->createInterface("c2.sq2.avc.encoder.secure", &rejected) != C2_NOT_FOUND ||
        store->createInterface("c2.sq2.avc.decoder", &rejected) != C2_NOT_FOUND) {
        fprintf(stderr, "REJECT_CHECK_FAILED\n");
        return 2;
    }

    std::shared_ptr<C2Component> comp;
    if (store->createComponent("c2.sq2.avc.encoder", &comp) != C2_OK || !comp) {
        fprintf(stderr, "CREATE_AVC_FAILED\n");
        return 3;
    }

    struct TestListener : public C2Component::Listener {
        std::mutex mu;
        std::condition_variable cv;
        int done = 0;
        bool sawCsd = false;
        bool sawKey = false;
        bool sawEos = false;
        size_t bytesOut = 0;

        void onWorkDone_nb(std::weak_ptr<C2Component>,
                           std::list<std::unique_ptr<C2Work>> workItems) override {
            std::lock_guard<std::mutex> lk(mu);
            for (auto &w : workItems) {
                if (!w || w->worklets.empty()) continue;
                const auto &out = w->worklets.front()->output;
                for (const auto &cfg : out.configUpdate) {
                    if (cfg && cfg->type() == C2StreamInitDataInfo::output::PARAM_TYPE) {
                        const auto *init = static_cast<const C2StreamInitDataInfo::output *>(cfg.get());
                        sawCsd = true;
                        printf("SELFTEST_CSD len=%zu\n", init->flexCount());
                    }
                }
                for (const auto &b : out.buffers) {
                    if (b && b->data().type() == C2BufferData::LINEAR) {
                        auto r = b->data().linearBlocks().front().map().get();
                        bytesOut += r.capacity();
                        if (b->hasInfo(C2StreamPictureTypeMaskInfo::output::PARAM_TYPE)) sawKey = true;
                        printf("SELFTEST_OUT ts=%llu cap=%u flags=0x%x\n",
                               static_cast<unsigned long long>(out.ordinal.timestamp.peeku()),
                               r.capacity(), out.flags);
                    }
                }
                if (out.flags & C2FrameData::FLAG_END_OF_STREAM) sawEos = true;
                done++;
            }
            cv.notify_all();
        }
        void onTripped_nb(std::weak_ptr<C2Component>,
                          std::vector<std::shared_ptr<C2SettingResult>>) override {}
        void onError_nb(std::weak_ptr<C2Component>, uint32_t err) override {
            fprintf(stderr, "SELFTEST_ERR=%u\n", err);
        }
    };

    auto listener = std::make_shared<TestListener>();
    if (comp->setListener_vb(listener, C2_MAY_BLOCK) != C2_OK) return 4;

    C2StreamPictureSizeInfo::input sz(0u, 1280, 720);
    C2StreamBitrateInfo::output br(0u, 4000000);
    C2StreamFrameRateInfo::output fr(0u, 30.f);
    std::vector<std::unique_ptr<C2SettingResult>> failures;
    comp->intf()->config_vb({&sz, &br, &fr}, C2_MAY_BLOCK, &failures);

    if (comp->start() != C2_OK) return 5;

    std::shared_ptr<C2BlockPool> grallocPool;
    if (android::GetCodec2BlockPool(C2BlockPool::BASIC_GRAPHIC, comp, &grallocPool) != C2_OK || !grallocPool) {
        fprintf(stderr, "NO_GRAPHIC_POOL\n");
        return 6;
    }

    for (int i = 0; i < 3; ++i) {
        std::shared_ptr<C2GraphicBlock> blk;
        if (grallocPool->fetchGraphicBlock(1280, 720, HAL_PIXEL_FORMAT_RGBA_8888,
                {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &blk) != C2_OK || !blk) {
            fprintf(stderr, "FETCH_GRAPHIC_FAILED\n");
            return 7;
        }
        {
            C2GraphicView v = blk->map().get();
            if (v.error() != C2_OK) return 8;
            uint8_t *p = v.data()[0];
            int32_t rowInc = v.layout().planes[0].rowInc;
            for (uint32_t y = 0; y < 720; ++y) {
                memset(p + y * rowInc, 0x30 + i * 30, 1280 * 4);
            }
        }
        auto w = std::make_unique<C2Work>();
        w->input.ordinal.frameIndex = i;
        w->input.ordinal.timestamp = i * 33333ULL;
        w->input.flags = (i == 2) ? C2FrameData::FLAG_END_OF_STREAM : static_cast<C2FrameData::flags_t>(0);
        w->input.buffers.push_back(C2Buffer::CreateGraphicBuffer(blk->share(C2Rect(1280, 720), C2Fence())));
        w->worklets.emplace_back(new C2Worklet);
        std::list<std::unique_ptr<C2Work>> q;
        q.push_back(std::move(w));
        if (comp->queue_nb(&q) != C2_OK) return 9;
    }

    {
        std::unique_lock<std::mutex> lk(listener->mu);
        listener->cv.wait_for(lk, std::chrono::seconds(5), [&]() { return listener->done >= 3; });
    }
    comp->stop();
    comp->release();
    printf("SELFTEST_DONE frames=%d csd=%d key=%d eos=%d bytes=%zu\n",
           listener->done, listener->sawCsd, listener->sawKey, listener->sawEos, listener->bytesOut);
    return (listener->done == 3 && listener->sawCsd && listener->sawKey && listener->sawEos && listener->bytesOut > 0) ? 0 : 10;
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *instance = "android.hardware.media.c2.IComponentStore/default";
    bool serve = true;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--print-capabilities-xml")) {
            Sq2EncStore store;
            return printCapabilitiesXml(store);
        }
        if (!strcmp(argv[i], "--self-test")) {
            return runSelfTest();
        }
        if (!strcmp(argv[i], "--instance") && i + 1 < argc) {
            instance = argv[++i];
        } else if (!strcmp(argv[i], "--serve")) {
            serve = true;
        }
    }
    if (!serve) return 0;

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    using Store = aidl::android::hardware::media::c2::utils::ComponentStore;
    auto c2Store = std::make_shared<Sq2EncStore>();
    printf("SQ2_C2_ENC_STORE codecs=%zu\n", c2Store->codecs().size());
    auto store = ndk::SharedRefBase::make<Store>(c2Store);
    if (store->status() != C2_OK) {
        fprintf(stderr, "STORE_INIT_ERR=%d\n", store->status());
        return 2;
    }
    auto binder = store->asBinder();
    binder_status_t rc = AServiceManager_addService(binder.get(), instance);
    printf("REGISTER instance=%s rc=%d\n", instance, rc);
    if (rc != STATUS_OK) return 3;
    ABinderProcess_joinThreadPool();
    return 0;
}

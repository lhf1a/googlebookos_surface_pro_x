/* The pinned mapper3 returns aligned pixel width as bytesPerStride for RGB.
 * Normalize only when public QTI metadata and the fixed format agree. */
namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
class NativeMapperCompat final : public qm::IQtiMapper {
    sp<qm::IQtiMapper> donor;
    using Void=android::hardware::Return<void>;
    using ErrorReturn=android::hardware::Return<am::Error>;
public:
    explicit NativeMapperCompat(const sp<qm::IQtiMapper> &value):donor(value){}
    Void createDescriptor(const BufferDescriptorInfo &d,createDescriptor_cb cb) override{return donor->createDescriptor(d,cb);}
    Void importBuffer(const android::hardware::hidl_handle &h,importBuffer_cb cb) override{return donor->importBuffer(h,cb);}
    ErrorReturn freeBuffer(void *b) override{return donor->freeBuffer(b);}
    ErrorReturn validateBufferSize(void *b,const BufferDescriptorInfo &d,uint32_t stride) override{return donor->validateBufferSize(b,d,stride);}
    Void getTransportSize(void *b,getTransportSize_cb cb) override{return donor->getTransportSize(b,cb);}
    Void lockYCbCr(void *b,uint64_t usage,const Rect &r,const android::hardware::hidl_handle &f,lockYCbCr_cb cb) override{return donor->lockYCbCr(b,usage,r,f,cb);}
    Void unlock(void *b,unlock_cb cb) override{return donor->unlock(b,cb);}
    Void isSupported(const BufferDescriptorInfo &d,isSupported_cb cb) override{return donor->isSupported(d,cb);}
    Void getMapperExtensions(getMapperExtensions_cb cb) override{return donor->getMapperExtensions(cb);}
    Void lock(void *buffer,uint64_t usage,const Rect &region,const android::hardware::hidl_handle &fence,lock_cb cb) override {
        sp<qe::IQtiMapperExtensions> extension;
        auto ext=donor->getMapperExtensions([&](am::Error error,const sp<qe::IQtiMapperExtensions> &v){
            if(error==am::Error::NONE)extension=v;
        });
        int32_t format=0,width=0,height=0,size=0;
        bool known=ext.isOk() && extension;
        if(known) {
            auto f=extension->getFormat(buffer,[&](qe::Error e,int32_t v){known &= e==qe::Error::NONE;format=v;});
            known &= f.isOk();
        }
        int32_t expected=quest2_rgb_bpp(format);
        if(known && expected) {
            auto w=extension->getWidth(buffer,[&](qe::Error e,int32_t v){known &= e==qe::Error::NONE;width=v;});
            auto h=extension->getHeight(buffer,[&](qe::Error e,int32_t v){known &= e==qe::Error::NONE;height=v;});
            auto s=extension->getSize(buffer,[&](qe::Error e,int32_t v){known &= e==qe::Error::NONE;size=v;});
            known &= w.isOk() && h.isOk() && s.isOk();
        }
        return donor->lock(buffer,usage,region,fence,[=](am::Error error,void *data,int32_t bpp,int32_t row){
            if(error==am::Error::NONE && known)
                row=quest2_rgb_byte_stride(format,width,height,size,bpp,row);
            cb(error,data,bpp,row);
        });
    }
};

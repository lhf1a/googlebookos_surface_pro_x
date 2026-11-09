#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <unistd.h>
#include "quest2-qti-mapper-extension.cpp"
struct Buffer {uint64_t usage=0,layers=1;std::shared_ptr<MetaData_t> metadata=std::make_shared<MetaData_t>();};
static std::map<buffer_handle_t,Buffer> buffers;
static AIMapper_Error load_error=AIMAPPER_ERROR_NONE,import_error=AIMAPPER_ERROR_NONE;
static bool metadata_failure=false,short_metadata=false,raw_metadata=false;
static int allocations=0,releases=0;
MetaData_t *fixtureMetadata(buffer_handle_t b) {
    auto it=buffers.find(b);return metadata_failure || it==buffers.end()?nullptr:it->second.metadata.get();
}
static buffer_handle_t create() {
    auto *b=static_cast<native_handle_t*>(std::calloc(1,sizeof(native_handle_t)+sizeof(int)));
    assert(b);b->version=sizeof(native_handle_t);b->numFds=1;b->data[0]=open("/dev/null",O_RDONLY);assert(b->data[0]>=0);
    buffers.emplace(b,Buffer{});allocations++;return b;
}
static AIMapper_Error release(buffer_handle_t b) {
    auto it=buffers.find(b);if(it==buffers.end())return AIMAPPER_ERROR_BAD_BUFFER;
    assert(close(b->data[0])==0);buffers.erase(it);std::free(const_cast<native_handle_t*>(b));releases++;
    return AIMAPPER_ERROR_NONE;
}
static AIMapper_Error transport(buffer_handle_t b,uint32_t *fds,uint32_t *ints) {
    if(!buffers.count(b))return AIMAPPER_ERROR_BAD_BUFFER;*fds=1;*ints=0;return AIMAPPER_ERROR_NONE;
}
static int32_t get(buffer_handle_t b,int64_t type,void *out,size_t size) {
    auto it=buffers.find(b);if(it==buffers.end())return -AIMAPPER_ERROR_BAD_BUFFER;
    if(short_metadata)return 4;
    if(raw_metadata){uint64_t raw=type==USAGE?it->second.usage:it->second.layers;if(out && size>=8)std::memcpy(out,&raw,8);return 8;}
    namespace mt=android::hardware::graphics::mapper;
    namespace gc=aidl::android::hardware::graphics::common;
    if(type==USAGE)return mt::StandardMetadata<gc::StandardMetadataType::USAGE>::value::encode(
        static_cast<gc::BufferUsage>(it->second.usage),out,size);
    return mt::StandardMetadata<gc::StandardMetadataType::LAYER_COUNT>::value::encode(it->second.layers,out,size);
}
static AIMapper_Error import(buffer_handle_t b,buffer_handle_t *out) {
    *out=nullptr;if(import_error!=AIMAPPER_ERROR_NONE)return import_error;
    auto it=buffers.find(b);if(it==buffers.end())return AIMAPPER_ERROR_BAD_BUFFER;
    auto *copy=static_cast<native_handle_t*>(std::malloc(sizeof(native_handle_t)+sizeof(int)));
    std::memcpy(copy,b,sizeof(native_handle_t));copy->data[0]=dup(b->data[0]);assert(copy->data[0]>=0);
    buffers.emplace(copy,it->second);allocations++;*out=copy;return AIMAPPER_ERROR_NONE;
}
extern "C" AIMapper_Error AIMapper_loadIMapper(AIMapper **out) {
    static AIMapper table=[] {AIMapper m{};m.version=AIMAPPER_VERSION_5;
        m.v5.getTransportSize=transport;m.v5.getStandardMetadata=get;
        m.v5.importBuffer=import;m.v5.freeBuffer=release;return m;}();
    *out=load_error==AIMAPPER_ERROR_NONE?&table:nullptr;return load_error;
}
int main() {
    IQtiMapperExt *ext=nullptr;
    assert(IQtiMapperExt_loadIMapperExt(nullptr)==IQTIMAPPEREXT_ERROR_BAD_VALUE);
    load_error=AIMAPPER_ERROR_NO_RESOURCES;assert(IQtiMapperExt_loadIMapperExt(&ext)==IQTIMAPPEREXT_ERROR_NO_RESOURCES && !ext);
    load_error=AIMAPPER_ERROR_NONE;assert(IQtiMapperExt_loadIMapperExt(&ext)==0 && ext && ext->version==2);
    assert(QTI_MAPPER_EXTENSION_VERSION==2);
    auto b=create();native_handle_t foreign{};uint32_t view=99;
    assert(ext->v2.getMultiViewInfo(&foreign,&view)==IQTIMAPPEREXT_ERROR_BAD_BUFFER && view==0);
    assert(ext->v2.getBaseView(nullptr,&view)==IQTIMAPPEREXT_ERROR_BAD_BUFFER);
    assert(ext->v2.getBaseView(b,nullptr)==IQTIMAPPEREXT_ERROR_BAD_VALUE);
    assert(ext->v2.getMultiViewInfo(b,&view)==0 && view==1);
    assert(ext->v2.getBaseView(b,&view)==0 && view==1);
    buffer_handle_t clone=reinterpret_cast<buffer_handle_t>(1);
    for(uint32_t mask:{0u,2u,3u,4u,8u,0x80000000u}) {
        assert(ext->v2.importViewBuffer(b,mask,&clone)==IQTIMAPPEREXT_ERROR_BAD_VALUE && !clone);
    }
    buffers.at(b).usage=1ULL<<58;
    assert(ext->v2.getMultiViewInfo(b,&view)==IQTIMAPPEREXT_ERROR_UNSUPPORTED && !view);
    assert(ext->v2.importViewBuffer(b,1,&clone)==IQTIMAPPEREXT_ERROR_UNSUPPORTED && !clone);
    buffers.at(b).usage=0;buffers.at(b).layers=2;
    assert(ext->v2.getBaseView(b,&view)==IQTIMAPPEREXT_ERROR_UNSUPPORTED);
    buffers.at(b).layers=1;short_metadata=true;
    assert(ext->v2.getBaseView(b,&view)==IQTIMAPPEREXT_ERROR_UNSUPPORTED);short_metadata=false;
    raw_metadata=true;assert(ext->v2.getBaseView(b,&view)==IQTIMAPPEREXT_ERROR_UNSUPPORTED);raw_metadata=false;
    import_error=AIMAPPER_ERROR_NO_RESOURCES;
    assert(ext->v2.importViewBuffer(b,1,&clone)==IQTIMAPPEREXT_ERROR_NO_RESOURCES && !clone);
    import_error=AIMAPPER_ERROR_NONE;
    for(int i=0;i<100;i++) {
        assert(ext->v2.importViewBuffer(b,1,&clone)==0 && clone!=b);
        assert(clone->data[0]!=b->data[0] && fcntl(clone->data[0],F_GETFD)>=0);
        bool set=true;
        assert(mapper::GetMetadataState(clone,DATASPACE,&set)==0 && !set);
        buffers.at(b).metadata->isStandardMetadataSet[DATASPACE]=true;
        assert(mapper::GetMetadataState(clone,DATASPACE,&set)==0 && set);
        buffers.at(b).metadata->isStandardMetadataSet[DATASPACE]=false;
        buffers.at(b).metadata->isVendorMetadataSet[UBWC_CR_STATS_INFO-VT_TIMESTAMP]=true;
        assert(mapper::GetMetadataState(clone,UBWC_CR_STATS_INFO,&set)==0 && set);
        int fd=clone->data[0];assert(release(clone)==0 && fcntl(fd,F_GETFD)<0 && errno==EBADF);
        assert(fcntl(b->data[0],F_GETFD)>=0);
    }
    bool set=true;
    assert(mapper::GetMetadataState(b,static_cast<vendor_qti_hardware_display_common_MetadataType>(10034),&set)==AIMAPPER_ERROR_UNSUPPORTED && !set);
    for(int type:{0,19,512,10001,10013,10049,10512}) {
        set=true;assert(mapper::GetMetadataState(b,static_cast<vendor_qti_hardware_display_common_MetadataType>(type),&set)==AIMAPPER_ERROR_UNSUPPORTED && !set);
    }
    metadata_failure=true;assert(mapper::GetMetadataState(b,DATASPACE,&set)==AIMAPPER_ERROR_BAD_BUFFER && !set);metadata_failure=false;
    assert(mapper::GetMetadataState(&foreign,DATASPACE,&set)==AIMAPPER_ERROR_BAD_BUFFER);
    assert(ext->v2.importViewBuffer(b,1,&clone)==0);int original_fd=b->data[0];assert(release(b)==0);
    assert(fcntl(original_fd,F_GETFD)<0 && fcntl(clone->data[0],F_GETFD)>=0);
    assert(ext->v2.getBaseView(clone,&view)==0 && view==1);assert(release(clone)==0);
    assert(buffers.empty() && allocations==releases);
    puts("QTI_EXTENSION_OWNERSHIP_AND_METADATA_STATE_PASS");
}

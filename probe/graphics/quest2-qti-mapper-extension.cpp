/* QTI stable-C extension companion for the native Quest2 mapper5.
 * Only imported single-view buffers are supported; this is not a second HAL.
 * Public ABI pins and the observed vendor binary layout are recorded in
 * configs/quest2-qti-mapper-extension-reference.json. */
#include <cstddef>
#include <cstdint>
#include <string>
#include <cstring>
#include <optional>
#include <vector>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
#include <android/hardware/graphics/mapper/IMapper.h>
#include "quest2-native-mapper-loader.h"
#include "qti-mapper-abi/IQtiMapperExt.h"
#include "qti-mapper-abi/QtiViews.h"
#include "qti-mapper-abi/MetadataType.h"
#include "quest2-gralloc-metadata.h"

static_assert(sizeof(IQtiMapperExt)==32 && alignof(IQtiMapperExt)==16);
static_assert(offsetof(IQtiMapperExt,v2)==8);
static_assert(offsetof(IQtiMapperExtV2,getBaseView)==8);
static_assert(offsetof(IQtiMapperExtV2,importViewBuffer)==16);
static_assert(sizeof(vendor_qti_hardware_display_common_MetadataType)==4);
static_assert(VT_TIMESTAMP==QTI_VT_TIMESTAMP && PP_PARAM_INTERLACED==QTI_PP_PARAM_INTERLACED);
static_assert(UBWC_CR_STATS_INFO==QTI_UBWC_CR_STATS_INFO && CVP_METADATA==QTI_CVP_METADATA);
static_assert(VIDEO_TRANSCODE_STATS==QTI_VIDEO_TRANSCODE_STATS);
static_assert(STANDARD_METADATA_STATUS==QTI_STANDARD_METADATA_STATUS);
static_assert(VENDOR_METADATA_STATUS==QTI_VENDOR_METADATA_STATUS);

namespace {
constexpr uint64_t multi_view_usage=1ULL<<58;
AIMapper_Error mapperForImported(buffer_handle_t buffer,AIMapper **out) {
    *out=nullptr;
    if(!buffer)return AIMAPPER_ERROR_BAD_BUFFER;
    auto error=quest2LoadNativeMapper(out);
    if(error!=AIMAPPER_ERROR_NONE)return error;
    if(!*out || (*out)->version<AIMAPPER_VERSION_5 || !(*out)->v5.getTransportSize ||
       !(*out)->v5.getStandardMetadata || !(*out)->v5.importBuffer)return AIMAPPER_ERROR_NO_RESOURCES;
    uint32_t fds=0,ints=0;
    error=(*out)->v5.getTransportSize(buffer,&fds,&ints);
    if(error!=AIMAPPER_ERROR_NONE)return error;
    // Stable-C scalar metadata includes the canonical name/type header. Use the
    // published encoder contract, including its size query and checked decoder.
    namespace mt=android::hardware::graphics::mapper;
    using Type=aidl::android::hardware::graphics::common::StandardMetadataType;
    auto decode=[&]<Type type>(auto *value) -> AIMapper_Error {
        int32_t size=(*out)->v5.getStandardMetadata(buffer,int64_t(type),nullptr,0);
        if(size<0)return static_cast<AIMapper_Error>(-size);
        if(size<=0 || size>4096)return AIMAPPER_ERROR_UNSUPPORTED;
        std::vector<uint8_t> encoded(size);
        int32_t written=(*out)->v5.getStandardMetadata(buffer,int64_t(type),encoded.data(),encoded.size());
        if(written<0)return static_cast<AIMapper_Error>(-written);
        if(written!=size)return AIMAPPER_ERROR_UNSUPPORTED;
        auto decoded=mt::StandardMetadata<type>::value::decode(encoded.data(),encoded.size());
        if(!decoded)return AIMAPPER_ERROR_UNSUPPORTED;
        *value=*decoded;return AIMAPPER_ERROR_NONE;
    };
    aidl::android::hardware::graphics::common::BufferUsage usage{};uint64_t layers=0;
    error=decode.template operator()<Type::USAGE>(&usage);
    if(error!=AIMAPPER_ERROR_NONE)return error;
    error=decode.template operator()<Type::LAYER_COUNT>(&layers);
    if(error!=AIMAPPER_ERROR_NONE)return error;
    if(layers!=1 || (uint64_t(usage)&multi_view_usage))return AIMAPPER_ERROR_UNSUPPORTED;
    return AIMAPPER_ERROR_NONE;
}
IQtiMapperExt_Error singleView(buffer_handle_t buffer,uint32_t *view) {
    if(!view)return IQTIMAPPEREXT_ERROR_BAD_VALUE;
    *view=0;AIMapper *mapper=nullptr;
    auto error=mapperForImported(buffer,&mapper);
    if(error!=AIMAPPER_ERROR_NONE)return static_cast<IQtiMapperExt_Error>(error);
    *view=PRIV_VIEW_MASK_PRIMARY;
    return IQTIMAPPEREXT_ERROR_NONE;
}
IQtiMapperExt_Error importView(buffer_handle_t buffer,uint32_t view,buffer_handle_t *out) {
    if(!out)return IQTIMAPPEREXT_ERROR_BAD_VALUE;
    *out=nullptr;AIMapper *mapper=nullptr;
    auto error=mapperForImported(buffer,&mapper);
    if(error!=AIMAPPER_ERROR_NONE)return static_cast<IQtiMapperExt_Error>(error);
    if(view!=PRIV_VIEW_MASK_PRIMARY)return IQTIMAPPEREXT_ERROR_BAD_VALUE;
    // The standard import clones/retains real storage. The caller owns a distinct
    // imported handle and frees it through the same mapper5, exactly like a normal import.
    return static_cast<IQtiMapperExt_Error>(mapper->v5.importBuffer(buffer,out));
}
IQtiMapperExt extension{IQTIMAPPEREXT_VERSION_2,{singleView,singleView,importView}};
bool supportedState(int32_t type) {
    if((type>=BUFFER_ID && type<=BLEND_MODE) || type==STRIDE)return true;
    // Exact common meanings shared with the pinned v77 QtiGrallocMetadata.h.
    // New Snap-only metadata (including color/HDR IDs 10034+) is not mapped onto old fields.
    switch(type) {
      case VT_TIMESTAMP:case PP_PARAM_INTERLACED:case VIDEO_PERF_MODE:
      case GRAPHICS_METADATA:case UBWC_CR_STATS_INFO:case REFRESH_RATE:
      case MAP_SECURE_BUFFER:case LINEAR_FORMAT:case SINGLE_BUFFER_MODE:
      case CVP_METADATA:case VIDEO_HISTOGRAM_STATS:case FD:
      case ALIGNED_WIDTH_IN_PIXELS:case ALIGNED_HEIGHT_IN_PIXELS:
      case STANDARD_METADATA_STATUS:case VENDOR_METADATA_STATUS:case BUFFER_TYPE:
      case VIDEO_TS_INFO:case CUSTOM_DIMENSIONS_STRIDE:case CUSTOM_DIMENSIONS_HEIGHT:
      case RGB_DATA_ADDRESS:case BUFFER_PERMISSION:case MEM_HANDLE:case TIMED_RENDERING:
      case CUSTOM_CONTENT_METADATA:case VIDEO_TRANSCODE_STATS:case EARLYNOTIFY_LINECOUNT:return true;
      default:return false;
    }
}
}
extern "C" {
uint32_t QTI_MAPPER_EXTENSION_VERSION=IQTIMAPPEREXT_VERSION_2;
IQtiMapperExt_Error IQtiMapperExt_loadIMapperExt(IQtiMapperExt **out) {
    if(!out)return IQTIMAPPEREXT_ERROR_BAD_VALUE;
    *out=nullptr;AIMapper *mapper=nullptr;
    auto error=quest2LoadNativeMapper(&mapper);
    if(error!=AIMAPPER_ERROR_NONE)return static_cast<IQtiMapperExt_Error>(error);
    if(!mapper || mapper->version<AIMAPPER_VERSION_5)return IQTIMAPPEREXT_ERROR_UNSUPPORTED;
    *out=&extension;return IQTIMAPPEREXT_ERROR_NONE;
}
}
namespace mapper {
AIMapper_Error GetMetadataState(buffer_handle_t buffer,
        vendor_qti_hardware_display_common_MetadataType type,bool *out) {
    if(!out)return AIMAPPER_ERROR_BAD_VALUE;
    *out=false;AIMapper *mapper=nullptr;
    auto error=mapperForImported(buffer,&mapper);
    if(error!=AIMAPPER_ERROR_NONE)return error;
    if(!supportedState(type))return AIMAPPER_ERROR_UNSUPPORTED;
    QuestMetadata metadata(buffer);
    auto *shared=metadata.get();
    if(!shared)return AIMAPPER_ERROR_BAD_BUFFER;
    // State is the real shared explicit-set bit, never inferred from a successful getter.
    *out=type>=VT_TIMESTAMP?shared->isVendorMetadataSet[type-VT_TIMESTAMP]:
                           shared->isStandardMetadataSet[type];
    return AIMAPPER_ERROR_NONE;
}
}

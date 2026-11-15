// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <aidl/android/hardware/camera/device/CameraMetadata.h>
#include <camera_metadata.h>
#include <stdexcept>
#include <vector>
#include <cstring>
namespace spx {
using Metadata = aidl::android::hardware::camera::device::CameraMetadata;
class Meta {
    camera_metadata_t* p;
public:
    Meta():p(allocate_camera_metadata(160,12000)) {if(!p) throw std::bad_alloc();}
    explicit Meta(const Metadata& m):Meta() {
        if(m.metadata.empty()) return;
        size_t n=m.metadata.size();
        auto* src=reinterpret_cast<const camera_metadata_t*>(m.metadata.data());
        if(validate_camera_metadata_structure(src,&n) || append_camera_metadata(p,src))
            throw std::runtime_error("invalid camera metadata");
    }
    ~Meta(){free_camera_metadata(p);}
    Meta(const Meta&)=delete;
    template<class T> void set(uint32_t tag,std::initializer_list<T> v){set(tag,v.begin(),v.size());}
    template<class T> void set(uint32_t tag,const T* v,size_t n) {
        int type=get_camera_metadata_tag_type(tag);
        if(type<0 || camera_metadata_type_size[type]!=sizeof(T)) throw std::runtime_error("metadata type");
        camera_metadata_entry_t e{};
        int r=find_camera_metadata_entry(p,tag,&e);
        r=r ? add_camera_metadata_entry(p,tag,v,n) : update_camera_metadata_entry(p,e.index,v,n,nullptr);
        if(r) throw std::runtime_error("metadata update");
    }
    int get(uint32_t tag,int fallback) const {
        camera_metadata_ro_entry_t e{};
        if(find_camera_metadata_ro_entry(p,tag,&e) || !e.count) return fallback;
        return e.type==TYPE_BYTE ? e.data.u8[0] : e.type==TYPE_INT32 ? e.data.i32[0] : fallback;
    }
    std::vector<int32_t> keys() const {
        std::vector<int32_t> r;
        for(size_t i=0;i<get_camera_metadata_entry_count(p);++i) {
            camera_metadata_ro_entry_t e{}; get_camera_metadata_ro_entry(p,i,&e);r.push_back(e.tag);
        } return r;
    }
    Metadata pack() {
        sort_camera_metadata(p);
        auto* compact=clone_camera_metadata(p); if(!compact) throw std::bad_alloc();
        Metadata m; auto* bytes=reinterpret_cast<const uint8_t*>(compact);
        m.metadata.assign(bytes,bytes+get_camera_metadata_size(compact));
        free_camera_metadata(compact); return m;
    }
};
Metadata defaults(bool front,int intent);
Metadata characteristics(bool front);
}

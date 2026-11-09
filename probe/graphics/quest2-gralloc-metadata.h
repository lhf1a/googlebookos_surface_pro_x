/* Matched QTI shared-metadata ABI, using the pinned vendor structure.
 * No private_handle fields are read. Validate fd[1]'s zero-offset mapping against
 * the original copy API before using it. The donor tail differs from the public
 * struct, so only the verified prefix through reservedSize is accessed. */
#pragma once
#include <QtiGrallocPriv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <vector>
#include <cstring>
#include <cstddef>
struct private_handle_t;
extern "C" unsigned long getMetaDataSize();
extern "C" int copyMetaDataHandleToVa(private_handle_t *,MetaData_t *);
static_assert(offsetof(MetaData_t,color)==0x28);
static_assert(offsetof(MetaData_t,blendMode)==0x60bc);
static_assert(offsetof(MetaData_t,name)==0x60c0);
static_assert(offsetof(MetaData_t,isStandardMetadataSet)==0x71c4);
static_assert(offsetof(MetaData_t,isVendorMetadataSet)==0x73c4);
static_assert(offsetof(MetaData_t,reservedSize)==0x75c8);
class QuestMetadata {
    void *mapping=MAP_FAILED;
    size_t size=0;
public:
    QuestMetadata(const QuestMetadata &)=delete;
    QuestMetadata &operator=(const QuestMetadata &)=delete;
    explicit QuestMetadata(buffer_handle_t b) {
        if(!b || b->numFds!=2 || b->numInts!=26)return;
        size=getMetaDataSize();
        constexpr size_t prefix=offsetof(MetaData_t,reservedSize)+sizeof(uint64_t);
        struct stat st{};struct statfs fs{};
        // This donor uses ashmem_create_region backed by coherent memfd/tmpfs, not a DMA-BUF.
        if(size<prefix || fstat(b->data[1],&st) || !S_ISREG(st.st_mode) ||
           fstatfs(b->data[1],&fs) || fs.f_type!=TMPFS_MAGIC ||
           st.st_size<0 || uint64_t(st.st_size)<size)return;
        mapping=mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_SHARED,b->data[1],0);
        if(mapping==MAP_FAILED)return;
        std::vector<uint64_t> snapshot((size+7)/8);
        auto *h=reinterpret_cast<private_handle_t *>(const_cast<native_handle_t *>(b));
        if(copyMetaDataHandleToVa(h,reinterpret_cast<MetaData_t *>(snapshot.data())) ||
           memcmp(mapping,snapshot.data(),prefix)) {munmap(mapping,size);mapping=MAP_FAILED;return;}
        // The current allocator never reserves a client region. Refuse foreign
        // reserved layouts until allocation and tail layout are supported together.
        if(get()->reservedSize) {munmap(mapping,size);mapping=MAP_FAILED;}
    }
    ~QuestMetadata(){if(mapping!=MAP_FAILED)munmap(mapping,size);}
    MetaData_t *get() const {return mapping==MAP_FAILED?nullptr:static_cast<MetaData_t *>(mapping);}
    bool initializeName(const std::string &name) {
        if(!get() || name.size()>=MAX_NAME_LEN || name.find('\0')!=std::string::npos)return false;
        memset(get()->name,0,sizeof(get()->name));memcpy(get()->name,name.data(),name.size());
        get()->isStandardMetadataSet[2]=true;return true;
    }
};

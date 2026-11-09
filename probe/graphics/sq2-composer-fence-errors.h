/* Controlled kernel sw_sync inputs. These are test fences, never GPU/VPU production fences. */
#pragma once
#include <thread>
#include <memory>
#include <fcntl.h>

static void fence_error_trace(const char *phase,unsigned frame) {
    int64_t stamp=now_ns();
    printf("ERROR_TRACE phase=%s frame=%u pid=%ld time_ns=%lld\n",
        phase,frame,(long)getpid(),(long long)stamp);
    const char *path=getenv("SQ2_COMPOSER_TRACE_MARKER");
    if(!path)return;
    int fd=open(path,O_WRONLY|O_CLOEXEC);assert(fd>=0);
    char line[192];
    int n=snprintf(line,sizeof(line),"SQ2_ACQUIRE_TEST phase=%s frame=%u pid=%ld time_ns=%lld\n",
        phase,frame,(long)getpid(),(long long)stamp);
    assert(n>0 && size_t(n)<sizeof(line) && write(fd,line,n)==n);close(fd);
}
class ControlledAcquire {
    struct Create { uint32_t value;char name[32];int32_t fence; };
    static_assert(sizeof(Create)==40);
    int timeline_=-1,fd_=-1,check_=-1;
    unsigned frame_;
    bool error_;
    std::thread producer_;
public:
    ControlledAcquire(const char *path,unsigned frame,bool error,bool pending):
        frame_(frame),error_(error) {
        timeline_=open(path,O_RDWR|O_CLOEXEC);assert(timeline_>=0);
        Create create{1,{},-1};snprintf(create.name,sizeof(create.name),"sq2-case-%u",frame);
        assert(!ioctl(timeline_,_IOWR('W',0,Create),&create)&&create.fence>=0);
        fd_=create.fence;check_=dup(fd_);assert(check_>=0);
        status("created",0);
        if(pending) {
            producer_=std::thread([this]{
                usleep(200000);
                fence_error_trace(error_?"producer-error":"producer-signal",frame_);
                signal();
                fence_error_trace("producer-complete",frame_);
            });
        } else {
            assert(error_);signal();
        }
    }
    void signal() {
        assert(timeline_>=0);
        if(!error_) {uint32_t increment=1;assert(!ioctl(timeline_,_IOW('W',1,uint32_t),&increment));}
        assert(!close(timeline_));timeline_=-1;
    }
    void status(const char *phase,int expected) const {
        struct sync_file_info info{};assert(!ioctl(check_,SYNC_IOC_FILE_INFO,&info));
        assert(info.num_fences==1);
        sync_fence_info point{};info.sync_fence_info=reinterpret_cast<uint64_t>(&point);
        assert(!ioctl(check_,SYNC_IOC_FILE_INFO,&info));
        printf("CONTROLLED_ACQUIRE frame=%u phase=%s fd=%d aggregate=%d driver=%s object=%s point_status=%d timestamp_ns=%llu time_ns=%lld\n",
            frame_,phase,check_,info.status,point.driver_name,point.obj_name,point.status,
            (unsigned long long)point.timestamp_ns,(long long)now_ns());
        assert(!strcmp(point.driver_name,"sw_sync") ||
               (expected && !strcmp(point.driver_name,"detached-driver")));
        assert(expected<0 ? info.status<0 && point.status==expected :
                            info.status==expected && point.status==expected);
        if(expected)assert(point.timestamp_ns);
    }
    int transfer() {assert(fd_>=0);int fd=fd_;fd_=-1;return fd;}
    void before() const {status("submitted",frame_==2?-ENOENT:0);}
    void finish() {
        if(producer_.joinable())producer_.join();
        status("completed",error_?-ENOENT:1);
    }
    ~ControlledAcquire() {
        if(producer_.joinable())producer_.join();
        if(timeline_>=0)close(timeline_);
        if(fd_>=0)close(fd_);
        if(check_>=0)close(check_);
    }
};
static void fence_error_snapshot(const char *phase) {
    const char *source=getenv("SQ2_COMPOSER_DRM_STATE"),*directory=getenv("SQ2_COMPOSER_EVIDENCE");
    if(!source || !directory)return;
    char path[1024];int n=snprintf(path,sizeof(path),"%s/%s-drm.txt",directory,phase);
    assert(n>0 && size_t(n)<sizeof(path));
    FILE *in=fopen(source,"r"),*out=fopen(path,"wx");assert(in && out);
    char bytes[4096];size_t length;
    while((length=fread(bytes,1,sizeof(bytes),in)))assert(fwrite(bytes,1,length,out)==length);
    assert(!ferror(in));assert(!fclose(in));assert(!fclose(out));
}
static void invalid_acquire(Client &client,uint64_t layer,unsigned slot,
                            const native_handle_t *handle,unsigned frame) {
    int invalid=open("/dev/null",O_RDONLY|O_CLOEXEC);assert(invalid>=0);
    struct sync_file_info info{};errno=0;
    assert(ioctl(invalid,SYNC_IOC_FILE_INFO,&info)==-1 && errno==ENOTTY);
    fence_error_snapshot("invalid-before");
    fence_error_trace("invalid-submit",frame);
    client.select(layer);client.writer.setLayerBuffer(slot,handle,invalid);
    auto status=client.execute();
    bool failed=status!=Error::NONE || client.reader.result.errors;
    if(!failed) {
        client.validate();client.writer.selectDisplay(client.display);
        client.writer.presentDisplay();status=client.execute();
        failed=status!=Error::NONE || client.reader.result.errors;
    }
    printf("INVALID_ACQUIRE_RESULT frame=%u execute=%d command_errors=%u presents=%u rejected=%d\n",
        frame,int(status),client.reader.result.errors,client.reader.result.presents,failed);
    fence_error_snapshot("invalid-after");
    assert(failed && !client.reader.result.presents && client.reader.result.present==-1 &&
           client.reader.result.releases.empty());
    fence_error_trace("invalid-rejected",frame);
}

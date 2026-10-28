// Reuse the independently tested system-EGL/fence harness. Only this file's
// main runs: no composition or diagnostic CPU-control mode is invoked.
#include <initializer_list>
#define main unused_composition_entry
#include "quest2-composition.cpp"
#undef main

static uint8_t component(unsigned x,unsigned y,unsigned channel,unsigned phase) {
    return static_cast<uint8_t>((x*(channel*13+7)+y*(channel*17+3)+phase*31+channel*59)&255);
}
static unsigned byte_index(unsigned format,unsigned channel) {
    return format==5&&channel<3?2-channel:channel;
}
struct ColorCycle {
    Harness &h;AHardwareBuffer *buffers[2]{};buffer_handle_t handles[2]{};
    sq2::BufferInfo layouts[2];EGLImageKHR images[2]={EGL_NO_IMAGE_KHR,EGL_NO_IMAGE_KHR};
    GLuint textures[2]{},fbo=0;bool complete=false;
    explicit ColorCycle(Harness &value):h(value){}
    ~ColorCycle(){cleanup();}
    void cleanup() {
        if(!h.current(0))return;
        if(!complete)glFinish(); // Failure cleanup only.
        glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
        if(fbo)glDeleteFramebuffers(1,&fbo);fbo=0;
        glDeleteTextures(2,textures);textures[0]=textures[1]=0;
        for(unsigned i=0;i<2;i++) {
            if(images[i]!=EGL_NO_IMAGE_KHR)h.destroy_image(h.display,images[i]);images[i]=EGL_NO_IMAGE_KHR;
            if(handles[i])sq2::mapper5()->v5.freeBuffer(handles[i]);handles[i]=nullptr;
            if(buffers[i])AHardwareBuffer_release(buffers[i]);buffers[i]=nullptr;
        }
    }
    bool run(unsigned w,unsigned height,unsigned phase,GLuint copy,unsigned source_format,unsigned target_format) {
        CHECK(h.current(0),"COLOR context");
        AIMapper *mapper=sq2::mapper5();CHECK(mapper,"COLOR mapper");
        ARect rect{0,0,static_cast<int32_t>(w),static_cast<int32_t>(height)};
        for(unsigned i=0;i<2;i++) {
            AHardwareBuffer_Desc desc{};desc.width=w;desc.height=height;desc.layers=1;desc.format=i?target_format:source_format;
            desc.usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|
                AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
            CHECK(!AHardwareBuffer_allocate(&desc,&buffers[i]),"allocate COLOR");
            auto native=AHardwareBuffer_getNativeHandle(buffers[i]);
            CHECK(sq2::color_info(native,layouts[i])&&sq2::linear_rgba_layout(layouts[i])&&layouts[i].format==desc.format,
                "verified COLOR component layout");
            CHECK(mapper->v5.importBuffer(native,&handles[i])==AIMAPPER_ERROR_NONE,"COLOR CPU import");
            void *data=nullptr;
            CHECK(mapper->v5.lock(handles[i],AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,rect,-1,&data)==AIMAPPER_ERROR_NONE,
                "COLOR CPU initialize");
            bool valid=data!=nullptr;
            if(valid)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++) {
                auto dst=static_cast<uint8_t*>(data)+y*layouts[i].stride+x*4;
                for(unsigned c=0;c<4;c++)dst[byte_index(desc.format,c)]=i?0x55:component(x,y,c,phase);
            }
            int released=-1;auto unlocked=mapper->v5.unlock(handles[i],&released);Fd fd;fd.reset(released);
            CHECK(valid&&unlocked==AIMAPPER_ERROR_NONE,"COLOR CPU initialize unlock");
            if(fd.value>=0)CHECK(h.server_wait(fd),"COLOR CPU producer fence");
            images[i]=h.create_image(h.display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,h.client(buffers[i]),nullptr);
            CHECK(images[i]!=EGL_NO_IMAGE_KHR,"import COLOR EGLImage");
            glGenTextures(1,&textures[i]);glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            h.image_target(GL_TEXTURE_2D,images[i]);CHECK(glGetError()==GL_NO_ERROR,"COLOR image texture");
            CHECK(h.destroy_image(h.display,images[i]),"early COLOR image destruction");images[i]=EGL_NO_IMAGE_KHR;
        }
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"COLOR output renderable");
        glViewport(0,0,w,height);glUseProgram(copy);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
        glUniform1i(glGetUniformLocation(copy,"src"),0);glDrawArrays(GL_TRIANGLES,0,3);
        CHECK(glGetError()==GL_NO_ERROR,"COLOR copy draw");
        Fd done;CHECK(h.export_fence(done,"color-copy"),"COLOR GPU release");
        void *data=nullptr;
        CHECK(mapper->v5.lock(handles[1],AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,rect,done.take(),&data)==AIMAPPER_ERROR_NONE,
            "COLOR GPU fence to CPU lock");
        size_t bad=0;bool valid=data!=nullptr;
        if(valid)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++) {
            auto actual=static_cast<const uint8_t*>(data)+y*layouts[1].stride+x*4;
            for(unsigned c=0;c<(target_format==2?3u:4u);c++) {
                uint8_t expected=source_format==2&&c==3?255:component(x,y,c,phase);
                uint8_t value=actual[byte_index(target_format,c)];
                if(value!=expected) {
                    if(bad<5)printf("COLOR_BAD source=%u target=%u x=%u y=%u c=%u actual=%u expected=%u\n",
                        source_format,target_format,x,y,c,value,expected);
                    ++bad;
                }
            }
        }
        int released=-1;auto unlocked=mapper->v5.unlock(handles[1],&released);Fd fd;fd.reset(released);
        CHECK(valid&&unlocked==AIMAPPER_ERROR_NONE,"COLOR read unlock");
        if(fd.value>=0){pollfd p{fd.value,POLLIN,0};CHECK(poll(&p,1,5000)==1&&(p.revents&POLLIN),"COLOR CPU release fence");}
        complete=true;
        printf("COLOR_RESULT phase=%u width=%u height=%u source=%u target=%u bad=%zu\n",phase,w,height,source_format,target_format,bad);
        return !bad;
    }
};
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    unsigned w=384,height=384,cycles=3;
    if(!environment()||!parameter("SQ2_COLOR_WIDTH",384,4096,&w)||!parameter("SQ2_COLOR_HEIGHT",384,4096,&height)||
       !parameter("SQ2_COLOR_CYCLES",3,150,&cycles))return 2;
    Harness h;if(!h.start()||!h.current(0))return 1;
    const char source[]=R"(#version 300 es
precision highp float;
precision highp sampler2D;
uniform sampler2D src;
layout(location=0) out vec4 color;
void main(){color=texelFetch(src,ivec2(gl_FragCoord.xy),0);}
)";
    GLuint copy=program(source);if(!copy)return 1;
    bool ok=true;int baseline=-1;
    for(unsigned phase=0;ok&&phase<cycles;phase++) {
        for(unsigned src:{1u,2u,5u})for(unsigned dst:{1u,2u,5u}) {
            if(!ok)break;
            ColorCycle test(h);ok=test.run(w,height,phase,copy,src,dst);test.cleanup();
        }
        int fds=fd_count();if(!phase)baseline=fds;
        printf("COLOR_FDS phase=%u current=%d baseline=%d\n",phase,fds,baseline);ok=ok&&fds>=0&&fds==baseline;
    }
    glDeleteProgram(copy);ok=h.cleanup()&&ok;
    printf("COLOR_%s cycles=%u size=%ux%u\n",ok?"PASS":"FAIL",cycles,w,height);return ok?0:1;
}

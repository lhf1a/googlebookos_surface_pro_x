// Reuse the independently tested system-EGL/fence harness. Only this file's
// main runs: no composition or diagnostic CPU-control mode is invoked.
#define main unused_composition_entry
#include "quest2-composition.cpp"
#undef main

static uint16_t component(unsigned x,unsigned y,unsigned channel,unsigned phase) {
    // Exactly representable binary16 values include negative and >1 RGB.
    static const uint16_t values[]={0,0x3000,0x3400,0x3800,0x3c00,0x4000,0xb800,0x3a00};
    return channel==3?0x3c00:values[(x*(channel+1)+y*(channel+3)+phase)&7];
}
struct HalfCycle {
    Harness &h;AHardwareBuffer *buffers[2]{};buffer_handle_t handles[2]{};
    sq2::BufferInfo layouts[2];EGLImageKHR images[2]={EGL_NO_IMAGE_KHR,EGL_NO_IMAGE_KHR};
    GLuint textures[2]{},fbo=0;bool complete=false;
    explicit HalfCycle(Harness &value):h(value){}
    ~HalfCycle(){cleanup();}
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
    bool run(unsigned w,unsigned height,unsigned phase,GLuint copy) {
        CHECK(h.current(0),"FP16 context");
        AIMapper *mapper=sq2::mapper5();CHECK(mapper,"FP16 mapper");
        ARect rect{0,0,static_cast<int32_t>(w),static_cast<int32_t>(height)};
        for(unsigned i=0;i<2;i++) {
            AHardwareBuffer_Desc desc{};desc.width=w;desc.height=height;desc.layers=1;desc.format=22;
            desc.usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|
                AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
            CHECK(!AHardwareBuffer_allocate(&desc,&buffers[i]),"allocate FP16");
            auto native=AHardwareBuffer_getNativeHandle(buffers[i]);
            CHECK(sq2::color_info(native,layouts[i])&&sq2::linear_rgba_layout(layouts[i])&&layouts[i].format==22,
                "verified FP16 component layout");
            CHECK(mapper->v5.importBuffer(native,&handles[i])==AIMAPPER_ERROR_NONE,"FP16 CPU import");
            void *data=nullptr;
            CHECK(mapper->v5.lock(handles[i],AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,rect,-1,&data)==AIMAPPER_ERROR_NONE,
                "FP16 CPU initialize");
            bool valid=data!=nullptr;
            if(valid)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++) {
                auto dst=reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(data)+y*layouts[i].stride+x*8);
                for(unsigned c=0;c<4;c++)dst[c]=i?0x3555:component(x,y,c,phase);
            }
            int released=-1;auto unlocked=mapper->v5.unlock(handles[i],&released);Fd fd;fd.reset(released);
            CHECK(valid&&unlocked==AIMAPPER_ERROR_NONE,"FP16 CPU initialize unlock");
            if(fd.value>=0)CHECK(h.server_wait(fd),"FP16 CPU producer fence");
            images[i]=h.create_image(h.display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,h.client(buffers[i]),nullptr);
            CHECK(images[i]!=EGL_NO_IMAGE_KHR,"import FP16 EGLImage");
            glGenTextures(1,&textures[i]);glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            h.image_target(GL_TEXTURE_2D,images[i]);CHECK(glGetError()==GL_NO_ERROR,"FP16 image texture");
            CHECK(h.destroy_image(h.display,images[i]),"early FP16 image destruction");images[i]=EGL_NO_IMAGE_KHR;
        }
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FP16 output renderable");
        glViewport(0,0,w,height);glUseProgram(copy);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
        glUniform1i(glGetUniformLocation(copy,"src"),0);glDrawArrays(GL_TRIANGLES,0,3);
        CHECK(glGetError()==GL_NO_ERROR,"FP16 copy draw");
        Fd done;CHECK(h.export_fence(done,"fp16-copy"),"FP16 GPU release");
        void *data=nullptr;
        CHECK(mapper->v5.lock(handles[1],AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,rect,done.take(),&data)==AIMAPPER_ERROR_NONE,
            "FP16 GPU fence to CPU lock");
        size_t bad=0;bool valid=data!=nullptr;
        if(valid)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++) {
            auto actual=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(data)+y*layouts[1].stride+x*8);
            for(unsigned c=0;c<4;c++)if(actual[c]!=component(x,y,c,phase)) {
                if(bad<5)printf("FP16_BAD x=%u y=%u c=%u actual=%04x expected=%04x\n",x,y,c,actual[c],component(x,y,c,phase));
                ++bad;
            }
        }
        int released=-1;auto unlocked=mapper->v5.unlock(handles[1],&released);Fd fd;fd.reset(released);
        CHECK(valid&&unlocked==AIMAPPER_ERROR_NONE,"FP16 read unlock");
        if(fd.value>=0){pollfd p{fd.value,POLLIN,0};CHECK(poll(&p,1,5000)==1&&(p.revents&POLLIN),"FP16 CPU release fence");}
        complete=true;
        printf("FP16_RESULT phase=%u width=%u height=%u checked_halfwords=%zu bad=%zu\n",phase,w,height,size_t(w)*height*4,bad);
        return !bad;
    }
};
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    unsigned w=384,height=384,cycles=3;
    if(!environment()||!parameter("SQ2_FP16_WIDTH",384,4096,&w)||!parameter("SQ2_FP16_HEIGHT",384,4096,&height)||
       !parameter("SQ2_FP16_CYCLES",3,150,&cycles))return 2;
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
        HalfCycle test(h);ok=test.run(w,height,phase,copy);test.cleanup();
        int fds=fd_count();if(!phase)baseline=fds;
        printf("FP16_FDS phase=%u current=%d baseline=%d\n",phase,fds,baseline);ok=ok&&fds>=0&&fds==baseline;
    }
    glDeleteProgram(copy);ok=h.cleanup()&&ok;
    printf("FP16_%s cycles=%u size=%ux%u\n",ok?"PASS":"FAIL",cycles,w,height);return ok?0:1;
}

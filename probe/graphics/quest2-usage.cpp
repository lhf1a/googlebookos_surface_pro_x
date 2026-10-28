// Test GPU-written source buffers with actual HWUI usage, without adding CPU
// flags that change the original QTI compression/allocation policy.
#define main unused_composition_entry
#include "quest2-composition.cpp"
#undef main

static uint8_t usage_component(unsigned x,unsigned y,unsigned c,unsigned phase) {
    return static_cast<uint8_t>((x*(c*13+7)+y*(c*17+3)+phase*31+c*59)&255);
}
struct UsageCycle {
    Harness &h;AHardwareBuffer *buffers[2]{};buffer_handle_t output_handle=nullptr;
    sq2::BufferInfo layouts[2];EGLImageKHR images[2]={EGL_NO_IMAGE_KHR,EGL_NO_IMAGE_KHR};
    GLuint textures[2]{},fbo=0;bool complete=false;
    explicit UsageCycle(Harness &value):h(value){}
    ~UsageCycle(){cleanup();}
    void cleanup() {
        if(!h.current(0))return;
        if(!complete)glFinish(); // Failure cleanup only.
        glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
        if(fbo)glDeleteFramebuffers(1,&fbo);fbo=0;
        glDeleteTextures(2,textures);textures[0]=textures[1]=0;
        for(unsigned i=0;i<2;i++) {
            if(images[i]!=EGL_NO_IMAGE_KHR)h.destroy_image(h.display,images[i]);images[i]=EGL_NO_IMAGE_KHR;
        }
        if(output_handle)sq2::mapper5()->v5.freeBuffer(output_handle);output_handle=nullptr;
        for(auto &buffer:buffers){if(buffer)AHardwareBuffer_release(buffer);buffer=nullptr;}
    }
    bool run(unsigned w,unsigned height,unsigned phase,uint64_t usage,GLuint pattern,GLuint copy,bool upload) {
        CHECK(h.current(0),"usage context");
        AIMapper *mapper=sq2::mapper5();CHECK(mapper,"usage mapper");
        ARect rect{0,0,static_cast<int32_t>(w),static_cast<int32_t>(height)};
        for(unsigned i=0;i<2;i++) {
            AHardwareBuffer_Desc desc{};desc.width=w;desc.height=height;desc.layers=1;desc.format=1;
            desc.usage=i?(AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|
                AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT):usage;
            CHECK(!AHardwareBuffer_allocate(&desc,&buffers[i]),"usage allocation");
            AHardwareBuffer_Desc actual{};AHardwareBuffer_describe(buffers[i],&actual);
            auto native=AHardwareBuffer_getNativeHandle(buffers[i]);
            CHECK(actual.usage==desc.usage&&actual.width==w&&actual.height==height&&actual.format==1&&actual.layers==1,
                "usage descriptor preserved");
            CHECK(sq2::color_info(native,layouts[i])&&sq2::linear_rgba_layout(layouts[i])&&layouts[i].format==1&&
                layouts[i].usage==desc.usage&&layouts[i].width==w&&layouts[i].height==height,
                "usage actual linear RGBA metadata");
            printf("USAGE_LAYOUT source=%u requested=0x%llx actual=0x%llx metadata=0x%llx "
                "width=%u height=%u stride=%llu plane_size=%llu modifier=%llu cpu_lock=%u\n",i==0,
                static_cast<unsigned long long>(desc.usage),static_cast<unsigned long long>(actual.usage),
                static_cast<unsigned long long>(layouts[i].usage),w,height,
                static_cast<unsigned long long>(layouts[i].stride),static_cast<unsigned long long>(layouts[i].plane_size),
                static_cast<unsigned long long>(layouts[i].modifier),i);
            if(i) {
                CHECK(mapper->v5.importBuffer(native,&output_handle)==AIMAPPER_ERROR_NONE,"usage output CPU import");
                void *data=nullptr;
                CHECK(mapper->v5.lock(output_handle,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,rect,-1,&data)==AIMAPPER_ERROR_NONE,
                    "usage output sentinel lock");
                bool valid=data!=nullptr;
                if(valid)for(unsigned y=0;y<height;y++)memset(static_cast<uint8_t*>(data)+y*layouts[1].stride,0x55,w*4);
                int released=-1;auto unlocked=mapper->v5.unlock(output_handle,&released);Fd fd;fd.reset(released);
                CHECK(valid&&unlocked==AIMAPPER_ERROR_NONE,"usage output sentinel unlock");
                if(fd.value>=0)CHECK(h.server_wait(fd),"usage output CPU release fence");
            }
            images[i]=h.create_image(h.display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,h.client(buffers[i]),nullptr);
            CHECK(images[i]!=EGL_NO_IMAGE_KHR,"usage import EGLImage");
            glGenTextures(1,&textures[i]);glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            h.image_target(GL_TEXTURE_2D,images[i]);CHECK(glGetError()==GL_NO_ERROR,"usage image texture");
            CHECK(h.destroy_image(h.display,images[i]),"usage early EGLImage destruction");images[i]=EGL_NO_IMAGE_KHR;
        }
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glViewport(0,0,w,height);
        if(upload) {
            // Imported hardware Bitmap-like source: initialize via the public
            // GL upload entry point, without CPU-locking the native buffer or
            // attaching the sample-only source as a render target.
            size_t size=size_t(w)*height*4;
            auto *pixels=static_cast<uint8_t*>(malloc(size));CHECK(pixels,"usage upload allocate");
            for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<4;c++)
                pixels[(size_t(y)*w+x)*4+c]=usage_component(x,y,c,phase);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
            glBindTexture(GL_TEXTURE_2D,textures[0]);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            GLenum error=glGetError();free(pixels);
            CHECK(error==GL_NO_ERROR,"usage imported source GL upload");
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"usage GPU-only source renderable");
            glBindTexture(GL_TEXTURE_2D,0);glUseProgram(pattern);
            GLint phase_location=glGetUniformLocation(pattern,"phase");CHECK(phase_location>=0,"usage pattern phase");
            glUniform1i(phase_location,phase);glDrawArrays(GL_TRIANGLES,0,3);
            CHECK(glGetError()==GL_NO_ERROR,"usage GPU source initialize");
        }
        // Source initialization -> sampling is ordered within this context;
        // neither mode CPU-locks the source or reads it with glReadPixels.
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"usage output renderable");
        glUseProgram(copy);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
        GLint source_location=glGetUniformLocation(copy,"src");CHECK(source_location>=0,"usage copy source");
        glUniform1i(source_location,0);glDrawArrays(GL_TRIANGLES,0,3);
        CHECK(glGetError()==GL_NO_ERROR,"usage source sample draw");
        Fd done;CHECK(h.export_fence(done,"usage-copy"),"usage GPU output release");
        void *data=nullptr;
        CHECK(mapper->v5.lock(output_handle,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,rect,done.take(),&data)==AIMAPPER_ERROR_NONE,
            "usage GPU fence to CPU output lock");
        size_t bad=0;bool valid=data!=nullptr;
        if(valid)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<w;x++) {
            auto actual=static_cast<const uint8_t*>(data)+y*layouts[1].stride+x*4;
            for(unsigned c=0;c<4;c++) {
                auto expected=usage_component(x,y,c,phase);
                if(actual[c]!=expected){
                    if(bad<8)printf("USAGE_BAD usage=0x%llx phase=%u x=%u y=%u c=%u actual=%u expected=%u\n",
                        static_cast<unsigned long long>(usage),phase,x,y,c,actual[c],expected);
                    ++bad;
                }
            }
        }
        int released=-1;auto unlocked=mapper->v5.unlock(output_handle,&released);Fd fd;fd.reset(released);
        CHECK(valid&&unlocked==AIMAPPER_ERROR_NONE,"usage CPU output read unlock");
        if(fd.value>=0){pollfd p{fd.value,POLLIN,0};CHECK(poll(&p,1,5000)==1&&(p.revents&POLLIN),"usage CPU output release fence");}
        complete=true;
        printf("USAGE_RESULT usage=0x%llx phase=%u width=%u height=%u checked_bytes=%zu bad=%zu source_cpu_lock=0 init=%s\n",
            static_cast<unsigned long long>(usage),phase,w,height,size_t(w)*height*4,bad,upload?"glTexSubImage2D":"GPU-draw");
        return !bad;
    }
};
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    unsigned w=72,height=72,cycles=3;uint64_t usage=0x900;
    if(!environment()||!parameter("SQ2_USAGE_WIDTH",72,4096,&w)||!parameter("SQ2_USAGE_HEIGHT",72,4096,&height)||
       !parameter("SQ2_USAGE_CYCLES",3,150,&cycles))return 2;
    const char *mode=getenv("SQ2_USAGE_UPLOAD");bool upload=mode&&!strcmp(mode,"1");
    if(mode&&!upload)return 2;
    const char *requested=getenv("SQ2_USAGE_SOURCE");
    if(requested){
        if(!strcmp(requested,"0x10000900"))usage=0x10000900;
        else if(!strcmp(requested,"0x100")&&upload)usage=0x100;
        else if(strcmp(requested,"0x900"))return 2;
    }
    Harness h;if(!h.start()||!h.current(0))return 1;
    const char pattern_text[]=R"(#version 300 es
precision highp float;
precision highp int;
uniform int phase;
layout(location=0) out vec4 color;
void main(){uvec2 p=uvec2(gl_FragCoord.xy);uvec4 c=uvec4(0u,1u,2u,3u);
uvec4 bytes=(p.x*(c*13u+7u)+p.y*(c*17u+3u)+uint(phase)*31u+c*59u)&255u;
color=vec4(bytes)/255.;}
)";
    const char copy_text[]=R"(#version 300 es
precision highp float;
precision highp sampler2D;
uniform sampler2D src;
layout(location=0) out vec4 color;
void main(){color=texelFetch(src,ivec2(gl_FragCoord.xy),0);}
)";
    GLuint pattern=program(pattern_text),copy=program(copy_text);
    if(!pattern||!copy){if(pattern)glDeleteProgram(pattern);if(copy)glDeleteProgram(copy);return 1;}
    bool ok=true;int baseline=-1;
    for(unsigned phase=0;ok&&phase<cycles;phase++) {
        UsageCycle test(h);ok=test.run(w,height,phase,usage,pattern,copy,upload);test.cleanup();
        int fds=fd_count();if(!phase)baseline=fds;
        printf("USAGE_FDS phase=%u current=%d baseline=%d\n",phase,fds,baseline);ok=ok&&fds>=0&&fds==baseline;
    }
    glDeleteProgram(pattern);glDeleteProgram(copy);ok=h.cleanup()&&ok;
    printf("USAGE_%s usage=0x%llx cycles=%u size=%ux%u init=%s\n",ok?"PASS":"FAIL",static_cast<unsigned long long>(usage),cycles,w,height,
        upload?"glTexSubImage2D":"GPU-draw");
    return ok?0:1;
}

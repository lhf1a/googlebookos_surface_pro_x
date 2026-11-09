/* Actual GPU-written AHB client targets and native acquire fences, through standard HWC. */
#pragma once
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <cstdlib>

static void gpu_trace(const char *phase,unsigned frame,unsigned slot,uint64_t id) {
    const char *path=getenv("SQ2_COMPOSER_TRACE_MARKER");
    if(!path)return;
    int fd=open(path,O_WRONLY|O_CLOEXEC);assert(fd>=0);
    char line[256];
    int n=snprintf(line,sizeof(line),"SQ2_GPU phase=%s frame=%u slot=%u id=%llu pid=%ld time_ns=%lld\n",
        phase,frame,slot,(unsigned long long)id,(long)getpid(),(long long)now_ns());
    assert(n>0 && size_t(n)<sizeof(line) && write(fd,line,n)==n);close(fd);
}
struct GpuTarget {
    AHardwareBuffer *ahb=nullptr;
    const native_handle_t *handle=nullptr;
    EGLImageKHR image=EGL_NO_IMAGE_KHR;
    GLuint texture=0,fbo=0;
    uint64_t id=0;
    unsigned last=0,uses=0;
    bool retired=true;
};
static void gpu_targets(Client &client,int w,int h) {
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    EGLint major=0,minor=0;assert(eglInitialize(display,&major,&minor));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;assert(eglChooseConfig(display,attr,&config,1,&count) && count==1);
    const EGLint context_attr[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    const EGLint surface_attr[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attr);assert(context!=EGL_NO_CONTEXT);
    EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attr);assert(surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(display,surface,surface,context));
    auto proc=[](const char *name){auto p=eglGetProcAddress(name);assert(p);return p;};
    auto create_image=reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(proc("eglCreateImageKHR"));
    auto destroy_image=reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(proc("eglDestroyImageKHR"));
    auto image_texture=reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(proc("glEGLImageTargetTexture2DOES"));
    auto create_sync=reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(proc("eglCreateSyncKHR"));
    auto destroy_sync=reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(proc("eglDestroySyncKHR"));
    auto export_fence=reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(proc("eglDupNativeFenceFDANDROID"));
    auto native=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(proc("eglGetNativeClientBufferANDROID"));
    printf("GPU_RENDERER renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    assert(strstr(reinterpret_cast<const char*>(glGetString(GL_RENDERER)),"Adreno"));
    auto shader=[](GLenum type,const char *source) {
        GLuint s=glCreateShader(type);assert(s);glShaderSource(s,1,&source,nullptr);glCompileShader(s);
        GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),nullptr,log);fprintf(stderr,"GPU_SHADER_ERROR %s\n",log);}
        assert(ok);return s;
    };
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;precision highp int;uniform uint frame;out vec4 color;void main(){uvec2 p=uvec2(gl_FragCoord.xy);uint t=((p.x>>4)+3u*(p.y>>4)+frame*7u)&255u;color=vec4(vec3(t,t^85u,t^170u)/255.,1);}");
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);
    glDeleteShader(vs);glDeleteShader(fs);glUseProgram(program);
    GLint frame_uniform=glGetUniformLocation(program,"frame");assert(frame_uniform>=0);
    glDisable(GL_DITHER);glDisable(GL_BLEND);glViewport(0,0,w,h);
    GpuTarget targets[3];
    for(unsigned slot=0;slot<3;++slot) {
        auto &target=targets[slot];
        AHardwareBuffer_Desc d{};d.width=w;d.height=h;d.layers=1;d.format=1;d.usage=0x10000b00;
        assert(!AHardwareBuffer_allocate(&d,&target.ahb) && target.ahb);
        assert(!AHardwareBuffer_getId(target.ahb,&target.id));
        auto *window=static_cast<ANativeWindowBuffer_t*>(native(target.ahb));assert(window && window->handle);
        target.handle=window->handle;
        const EGLint image_attr[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
        target.image=create_image(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,window,image_attr);
        assert(target.image!=EGL_NO_IMAGE_KHR);
        glGenTextures(1,&target.texture);glBindTexture(GL_TEXTURE_2D,target.texture);
        image_texture(GL_TEXTURE_2D,target.image);
        glGenFramebuffers(1,&target.fbo);glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.texture,0);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        printf("GPU_TARGET slot=%u id=%llu width=%d height=%d usage=0x10000b00\n",
            slot,(unsigned long long)target.id,w,h);
    }
    assert(targets[0].id!=targets[1].id && targets[0].id!=targets[2].id && targets[1].id!=targets[2].id);
    uint64_t layer=0;
    assert(client.hal->createLayer(client.display,3,[&](Error e,uint64_t l){assert(e==Error::NONE);layer=l;}).isOk());
    auto slots=client.hal->setClientTargetSlotCount(client.display,3);assert(slots.isOk() && slots==Error::NONE);
    client.select(layer);
    client.writer.setLayerCompositionType(c21::IComposerClient::Composition::CLIENT);
    client.writer.setLayerBlendMode(c21::IComposerClient::BlendMode::NONE);
    client.writer.setLayerPlaneAlpha(1.f);client.writer.setLayerZOrder(0);
    client.writer.setLayerDataspace(gc::Dataspace::SRGB);
    client.writer.setLayerSourceCrop({0,0,float(w),float(h)});
    client.writer.setLayerDisplayFrame({0,0,w,h});client.writer.setLayerVisibleRegion({{0,0,w,h}});
    client.check_execute();
    std::vector<uint8_t> pixels(size_t(w)*h*4);
    for(unsigned frame=0;frame<30;++frame) {
        unsigned slot=frame%3;auto &target=targets[slot];
        assert(target.retired);
        if(target.uses)printf("GPU_REUSE frame=%u slot=%u previous=%u id=%llu time_ns=%lld\n",
            frame,slot,target.last,(unsigned long long)target.id,(long long)now_ns());
        glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);glUniform1ui(frame_uniform,frame);
        gpu_trace("render_begin",frame,slot,target.id);
        // Multiple real draws keep this a bounded producer workload; no claimed performance metric.
        for(unsigned pass=0;pass<8;++pass)glDrawArrays(GL_TRIANGLES,0,3);
        assert(glGetError()==GL_NO_ERROR);
        const EGLint sync_attr[]={EGL_NONE};
        EGLSyncKHR sync=create_sync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,sync_attr);assert(sync!=EGL_NO_SYNC_KHR);
        glFlush();int acquire=export_fence(display,sync);assert(acquire>=0);
        assert(destroy_sync(display,sync));
        int check=dup(acquire);assert(check>=0);
        fence_info(check,"gpu_acquire",frame,slot,"exported",false);
        gpu_trace("render_exported",frame,slot,target.id);
        client.select(layer);
        client.writer.setLayerBuffer(slot,target.uses?nullptr:target.handle,-1);
        client.writer.selectDisplay(client.display);
        client.writer.setClientTarget(slot,target.uses?nullptr:target.handle,acquire,gc::Dataspace::SRGB,{});
        client.check_execute();client.validate(true);
        gpu_trace("present_begin",frame,slot,target.id);
        client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
        assert(client.reader.result.presents==1 && client.reader.result.present>=0);
        wait_fence(client.reader.result.present,"gpu_present",frame,slot);
        fence_info(check,"gpu_acquire",frame,slot,"completed",true);close(check);
        gpu_trace("present_complete",frame,slot,target.id);
        if(frame) {
            auto &previous=targets[(frame-1)%3];assert(!previous.retired);
            previous.retired=true;
            printf("GPU_TARGET_RETIRED frame=%u replaced_by=%u slot=%u id=%llu time_ns=%lld\n",
                frame-1,frame,(frame-1)%3,(unsigned long long)previous.id,(long long)now_ns());
        }
        target.last=frame;++target.uses;target.retired=false;
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());assert(glGetError()==GL_NO_ERROR);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            unsigned code=(unsigned(x/16)+3*unsigned(y/16)+frame*7)&255;
            const auto *actual=&pixels[(size_t(y)*w+x)*4];
            assert(actual[0]==code && actual[1]==(code^85) && actual[2]==(code^170) && actual[3]==255);
        }
        printf("GPU_FRAME frame=%u slot=%u id=%llu cached=%u pixels=%zu exact=1 time_ns=%lld\n",
            frame,slot,(unsigned long long)target.id,target.uses>1,size_t(w)*h,(long long)now_ns());
    }
    assert(client.service->dumpDebugInfo([](const auto &s){printf("GPU_HAL_DUMP_BEGIN\n%s\nGPU_HAL_DUMP_END\n",s.c_str());}).isOk());
    puts("GPU_TARGET_HOLD_READY");sleep(3);
    auto destroyed=client.hal->destroyLayer(client.display,layer);assert(destroyed.isOk() && destroyed==Error::NONE);
    client.validate();
    client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
    assert(client.reader.result.presents==1 && client.reader.result.present>=0);
    wait_fence(client.reader.result.present,"gpu_flush",30,0);
    targets[2].retired=true;
    printf("GPU_TARGET_RETIRED frame=29 replaced_by=30 slot=2 id=%llu time_ns=%lld\n",
        (unsigned long long)targets[2].id,(long long)now_ns());
    glFinish();assert(glGetError()==GL_NO_ERROR);
    for(auto &target:targets) {
        assert(target.retired && target.uses==10);
        glDeleteFramebuffers(1,&target.fbo);glDeleteTextures(1,&target.texture);
        assert(destroy_image(display,target.image));AHardwareBuffer_release(target.ahb);
    }
    glDeleteProgram(program);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroySurface(display,surface) && eglDestroyContext(display,context));
    assert(eglTerminate(display) && eglReleaseThread());
    client.reader.result.close_all();
    puts("GPU_TARGET_PASS frames=30 targets=3 reuses=27 native_acquires=30 gpu_pixels_exact=1 final_flush=1 full_stack_complete=0");
}

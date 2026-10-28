/* Private ARM64 Android native-buffer lifetime experiment. The measured
 * ANativeWindowBuffer ABI on this target is 168 bytes, inc/dec at 40/48.
 * Never apply to a different ABI or wrap the same buffer by mutating it. */
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
struct sq2_native_proxy {unsigned char bytes[168];void *original;atomic_int refs;void *handle;void (*release)(void*);};
static void sq2_native_dec(void *common);
static void sq2_native_inc(void *common){
    struct sq2_native_proxy *p=common;
    int count=atomic_fetch_add(&p->refs,1)+1;
    fprintf(stderr,"NATIVE_PROXY_INC refs=%d\n",count);
}
static void sq2_native_dec(void *common){
    struct sq2_native_proxy *p=common;
    int count=atomic_fetch_sub(&p->refs,1)-1;
    fprintf(stderr,"NATIVE_PROXY_DEC refs=%d\n",count);
    if(!count){
        p->release(p->handle);
        void (*dec)(void*)=NULL;memcpy(&dec,(char*)p->original+48,sizeof(dec));
        dec(p->original);free(p);
    }
}
static struct sq2_native_proxy *sq2_native_wrap(void *original){
    unsigned magic,version;memcpy(&magic,original,4);memcpy(&version,(char*)original+4,4);
    if(magic!=0x5f626672||version!=168)return NULL;
    void (*inc)(void*)=NULL;
    memcpy(&inc,(char*)original+40,sizeof(inc));
    void (*dec)(void*)=NULL;memcpy(&dec,(char*)original+48,sizeof(dec));
    if(!inc||!dec)return NULL;
    bool (*retain)(void*)=(void*)dlsym(RTLD_DEFAULT,"sq2_qti_mapper3_retain_buffer");
    void (*release)(void*)=(void*)dlsym(RTLD_DEFAULT,"sq2_qti_mapper3_release_buffer");
    void *handle=NULL;memcpy(&handle,(char*)original+96,sizeof(handle));
    if(!retain||!release||!handle)return NULL;
    struct sq2_native_proxy *p=calloc(1,sizeof(*p));if(!p)return NULL;
    if(!retain(handle)){free(p);return NULL;}
    p->handle=handle;p->release=release;
    memcpy(p->bytes,original,sizeof(p->bytes));p->original=original;atomic_init(&p->refs,1);
    void (*proxy_inc)(void*)=sq2_native_inc,(*proxy_dec)(void*)=sq2_native_dec;
    memcpy(p->bytes+40,&proxy_inc,sizeof(proxy_inc));memcpy(p->bytes+48,&proxy_dec,sizeof(proxy_dec));
    inc(original);
    return p;
}

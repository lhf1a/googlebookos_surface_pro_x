/* Target-specific ARM64 ANativeWindow proxy. Measured size 192, stable public
 * callback prefix. Buffer proxies are weakly indexed, with a dequeue lease and
 * actual donor references; queued buffers are never kept by an unbounded cache. */
#pragma once
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#ifndef __aarch64__
#error This native window ABI adapter is ARM64 only
#endif
struct sq2_window_slot {void *original;struct sq2_native_proxy *proxy;bool leased;};
struct sq2_window_proxy {
    unsigned char bytes[192];void *original;void *perform;
    atomic_int refs;pthread_mutex_t mutex;struct sq2_window_slot slots[64];
};
_Static_assert(offsetof(struct sq2_window_proxy,original)==192,"window ABI");
_Static_assert(offsetof(struct sq2_window_proxy,perform)==200,"perform trampoline ABI");
static void sq2_window_inc(void *common){
    struct sq2_window_proxy *w=common;
    fprintf(stderr,"WINDOW_PROXY_INC refs=%d\n",atomic_fetch_add(&w->refs,1)+1);
}
static void sq2_window_dec(void *common){
    struct sq2_window_proxy *w=common;int count=atomic_fetch_sub(&w->refs,1)-1;
    fprintf(stderr,"WINDOW_PROXY_DEC refs=%d\n",count);
    if(!count){
        void (*dec)(void*)=NULL;memcpy(&dec,(char*)w->original+48,sizeof(dec));
        dec(w->original);pthread_mutex_destroy(&w->mutex);free(w);
    }
}
static void sq2_window_buffer_zero(struct sq2_native_proxy *p,void *owner){
    struct sq2_window_proxy *w=owner;
    pthread_mutex_lock(&w->mutex);
    for(unsigned i=0;i<64;i++)if(w->slots[i].proxy==p){w->slots[i]=(struct sq2_window_slot){0};break;}
    pthread_mutex_unlock(&w->mutex);sq2_window_dec(w);
}
static struct sq2_native_proxy *sq2_window_buffer(struct sq2_window_proxy *w,void *original){
    struct sq2_native_proxy *p=NULL;struct sq2_window_slot *empty=NULL;
    pthread_mutex_lock(&w->mutex);
    for(unsigned i=0;i<64;i++){
        struct sq2_window_slot *slot=&w->slots[i];
        if(!slot->proxy){if(!empty)empty=slot;continue;}
        if(slot->original!=original||slot->leased)continue;
        int refs=atomic_load(&slot->proxy->refs);
        /* Do not resurrect a zero-count proxy whose final callback is waiting
         * for this mutex. That callback removes its own pointer, not its key. */
        while(refs>0)if(atomic_compare_exchange_weak(&slot->proxy->refs,&refs,refs+1)){
            slot->leased=true;p=slot->proxy;goto done;
        }
    }
    if(empty){
        p=sq2_native_wrap(original);
        if(p){
            p->owner=w;p->on_zero=sq2_window_buffer_zero;sq2_window_inc(w);
            *empty=(struct sq2_window_slot){original,p,true};
        }
    }
done:
    pthread_mutex_unlock(&w->mutex);return p;
}
static int sq2_window_dequeue_common(void *common,void **buffer,int *fence,bool old){
    struct sq2_window_proxy *w=common;void *original=NULL;int result;
    if(old){
        int (*fn)(void*,void**)=NULL;memcpy(&fn,(char*)w->original+120,sizeof(fn));
        result=fn(w->original,&original);
    }else{
        int (*fn)(void*,void**,int*)=NULL;memcpy(&fn,(char*)w->original+168,sizeof(fn));
        result=fn(w->original,&original,fence);
    }
    if(result)return result;
    struct sq2_native_proxy *p=sq2_window_buffer(w,original);
    if(!p){
        if(old){int (*cancel)(void*,void*)=NULL;memcpy(&cancel,(char*)w->original+160,sizeof(cancel));cancel(w->original,original);}
        else {int (*cancel)(void*,void*,int)=NULL;memcpy(&cancel,(char*)w->original+184,sizeof(cancel));cancel(w->original,original,*fence);*fence=-1;}
        *buffer=NULL;return -ENOMEM;
    }
    *buffer=p;return 0;
}
static int sq2_window_dequeue(void *w,void **b,int *f){return sq2_window_dequeue_common(w,b,f,false);}
static int sq2_window_dequeue_old(void *w,void **b){return sq2_window_dequeue_common(w,b,NULL,true);}
static int sq2_window_return(void *common,void *buffer,int fence,unsigned offset,bool old){
    struct sq2_window_proxy *w=common;struct sq2_native_proxy *p=NULL;
    pthread_mutex_lock(&w->mutex);
    for(unsigned i=0;i<64;i++)if(w->slots[i].proxy==buffer&&w->slots[i].leased){p=buffer;break;}
    pthread_mutex_unlock(&w->mutex);
    if(!p){if(fence>=0)close(fence);return -EINVAL;}
    int result;
    if(old){int (*fn)(void*,void*)=NULL;memcpy(&fn,(char*)w->original+offset,sizeof(fn));result=fn(w->original,p->original);}
    else {int (*fn)(void*,void*,int)=NULL;memcpy(&fn,(char*)w->original+offset,sizeof(fn));result=fn(w->original,p->original,fence);}
    if(!result){
        pthread_mutex_lock(&w->mutex);
        for(unsigned i=0;i<64;i++)if(w->slots[i].proxy==p){w->slots[i].leased=false;break;}
        pthread_mutex_unlock(&w->mutex);sq2_native_dec(p);
    }
    return result;
}
static int sq2_window_queue(void *w,void *b,int f){return sq2_window_return(w,b,f,176,false);}
static int sq2_window_cancel(void *w,void *b,int f){return sq2_window_return(w,b,f,184,false);}
static int sq2_window_queue_old(void *w,void *b){return sq2_window_return(w,b,-1,136,true);}
static int sq2_window_cancel_old(void *w,void *b){return sq2_window_return(w,b,-1,160,true);}
static int sq2_window_lock_old(void *common,void *buffer){
    struct sq2_window_proxy *w=common;void *original=NULL;
    pthread_mutex_lock(&w->mutex);
    for(unsigned i=0;i<64;i++)if(w->slots[i].proxy==buffer&&w->slots[i].leased){original=w->slots[i].original;break;}
    pthread_mutex_unlock(&w->mutex);if(!original)return -EINVAL;
    int (*fn)(void*,void*)=NULL;memcpy(&fn,(char*)w->original+128,sizeof(fn));return fn(w->original,original);
}
static int sq2_window_interval(void *common,int interval){
    struct sq2_window_proxy *w=common;int (*fn)(void*,int)=NULL;
    memcpy(&fn,(char*)w->original+112,sizeof(fn));return fn(w->original,interval);
}
static int sq2_window_query(const void *common,int query,int *value){
    const struct sq2_window_proxy *w=common;int (*fn)(const void*,int,int*)=NULL;
    memcpy(&fn,(char*)w->original+144,sizeof(fn));return fn(w->original,query,value);
}
/* Tail-call forwarding preserves all integer/FP registers and stack arguments
 * of perform's open-ended varargs ABI; only substitute the original 'this'. */
__attribute__((naked)) static int sq2_window_perform(void *w __attribute__((unused)),int op __attribute__((unused)),...){
    __asm__ volatile("ldr x16, [x0, #200]\nldr x0, [x0, #192]\nbr x16");
}
static struct sq2_window_proxy *sq2_window_wrap(void *original){
    if(!original)return NULL;
    unsigned header[2];memcpy(header,original,sizeof(header));
    if(header[0]!=0x5f776e64||header[1]!=192)return NULL;
    void (*existing_inc)(void*)=NULL;
    memcpy(&existing_inc,(char*)original+40,sizeof(existing_inc));
    if(existing_inc==sq2_window_inc){sq2_window_inc(original);return original;}
    const unsigned offsets[]={40,48,112,120,128,136,144,152,160,168,176,184};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        void *fn=NULL;memcpy(&fn,(char*)original+offsets[i],sizeof(fn));if(!fn)return NULL;
    }
    struct sq2_window_proxy *w=calloc(1,sizeof(*w));if(!w)return NULL;
    memcpy(w->bytes,original,sizeof(w->bytes));w->original=original;
    memcpy(&w->perform,(char*)original+152,sizeof(w->perform));
    atomic_init(&w->refs,1);if(pthread_mutex_init(&w->mutex,NULL)){free(w);return NULL;}
    void (*inc)(void*)=NULL;memcpy(&inc,(char*)original+40,sizeof(inc));inc(original);
    void *callbacks[]={(void*)sq2_window_inc,(void*)sq2_window_dec,(void*)sq2_window_interval,
        (void*)sq2_window_dequeue_old,(void*)sq2_window_lock_old,(void*)sq2_window_queue_old,
        (void*)sq2_window_query,(void*)sq2_window_perform,(void*)sq2_window_cancel_old,
        (void*)sq2_window_dequeue,(void*)sq2_window_queue,(void*)sq2_window_cancel};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)memcpy(w->bytes+offsets[i],&callbacks[i],sizeof(void*));
    return w;
}

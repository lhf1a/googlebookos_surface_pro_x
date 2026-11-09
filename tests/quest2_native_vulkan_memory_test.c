#include "quest2-native-vulkan-memory.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
static _Thread_local uint64_t expected_size, expected_flags;
static _Thread_local void *expected_out;
static _Thread_local int expected_result;
int gsl_memory_alloc_pure_64(uint64_t size,uint64_t flags,void *out) {
    assert(size==expected_size && flags==expected_flags && out==expected_out);
    return expected_result;
}
static void invoke(unsigned cached,uint64_t flags,uint64_t size) {
    int allocation=7;
    expected_out=&allocation;expected_size=size;
    expected_flags=flags;
    if(cached && !(flags&UINT64_C(0x0c000000)))expected_flags|=UINT64_C(0x0c000000);
    expected_result=-7;
    assert(sq2_memory_alloc_pure_64(size,flags,&allocation)==-7);
    assert(allocation==7);
}
static void *thread(void *arg) {
    unsigned enabled=(uintptr_t)arg;
    struct Sq2VulkanMemoryScope value={enabled,4,15,1000+enabled};
    struct Sq2VulkanMemoryScope old=sq2_native_vulkan_memory_scope(value);
    assert(!old.enabled && !old.serial);
    for(unsigned i=0;i<1000;i++)invoke(enabled,UINT64_C(0x000c2600),4096+i);
    struct Sq2VulkanMemoryScope final=sq2_native_vulkan_memory_scope(old);
    assert(final.enabled==enabled && final.serial==1000+enabled);
    return NULL;
}
int main(void) {
    unsigned cases=0;
    for(unsigned cached=0;cached<2;cached++)for(unsigned mask=0;mask<4;mask++)
    for(unsigned flags=0;flags<16;flags++)for(unsigned size=0;size<4;size++) {
        struct Sq2VulkanMemoryScope value={cached,2+cached,11+4*cached,cases};
        struct Sq2VulkanMemoryScope old=sq2_native_vulkan_memory_scope(value);
        invoke(cached,((uint64_t)mask<<26)|((uint64_t)flags<<40)|UINT64_C(0xc2600),UINT64_C(1)<<(12+size));
        struct Sq2VulkanMemoryScope restored=sq2_native_vulkan_memory_scope(old);
        assert(restored.enabled==value.enabled && restored.memory_type==value.memory_type &&
               restored.memory_flags==value.memory_flags && restored.serial==value.serial);
        cases++;
    }
    struct Sq2VulkanMemoryScope parent={1,4,15,321},nested={0,0,1,654};
    struct Sq2VulkanMemoryScope old=sq2_native_vulkan_memory_scope(parent);
    struct Sq2VulkanMemoryScope saved=sq2_native_vulkan_memory_scope(nested);
    invoke(0,UINT64_C(0xc2600),4096);
    sq2_native_vulkan_memory_scope(saved);
    invoke(1,UINT64_C(0xc2600),4096);
    sq2_native_vulkan_memory_scope(old);
    pthread_t workers[2];
    assert(!pthread_create(&workers[0],NULL,thread,(void *)(uintptr_t)0));
    assert(!pthread_create(&workers[1],NULL,thread,(void *)(uintptr_t)1));
    assert(!pthread_join(workers[0],NULL) && !pthread_join(workers[1],NULL));
    printf("NATIVE_VULKAN_MEMORY_POLICY_PASS matrix=%u nested=2 threaded=2000\n",cases);
    return 0;
}

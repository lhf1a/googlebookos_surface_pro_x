// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#define __init
#define SCHED_FIFO 1
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PMD_SIZE (1UL << 21)
#define IS_ALIGNED(x, a) (!((x) & ((a) - 1)))
#define IS_ENABLED(x) 0
#define offset_in_page(x) ((x) & (PAGE_SIZE - 1))
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((long)(intptr_t)(x))
#define IS_ERR_VALUE(x) ((unsigned long)(x) >= (unsigned long)-4095)
#define IS_ERR(x) IS_ERR_VALUE((uintptr_t)(x))
#define KGSL_MEMSTORE_TOKEN_ADDRESS 0x100000UL
#define dev_err_ratelimited(dev, ...) ((void)(dev))
typedef unsigned long vm_flags_t;
struct kthread_worker { int initialized; };
struct task_struct { bool alive, policy, running, flushed; };
struct sched_attr { unsigned int size, sched_policy, sched_priority; };
static struct { struct kthread_worker worker; struct task_struct *worker_thread; } kgsl_driver;
static struct task_struct task;
static int create_error, scheduler_error, creates, schedules, wakes, stops, flushes;
static unsigned int cases;
static void kthread_init_worker(struct kthread_worker *w) { assert(w==&kgsl_driver.worker); w->initialized=1; }
static int kthread_worker_fn(void *ptr) { assert(!"Model must not execute a fake worker"); return 0; }
static struct task_struct *kthread_create(int (*fn)(void *), void *ptr, const char *name)
{
 assert(fn==kthread_worker_fn && ptr==&kgsl_driver.worker && kgsl_driver.worker.initialized);
 assert(!strcmp(name,"kgsl_worker_thread") && !task.alive && !kgsl_driver.worker_thread);
 creates++; if(create_error) return ERR_PTR(create_error); task.alive=true; return &task;
}
static int sched_setattr_nocheck(struct task_struct *t, const struct sched_attr *a)
{
 assert(t==&task && t->alive && !t->running && !kgsl_driver.worker_thread);
 assert(a->size==sizeof(*a) && a->sched_policy==SCHED_FIFO && a->sched_priority==2);
 schedules++; if(scheduler_error) return scheduler_error; t->policy=true; return 0;
}
static int wake_up_process(struct task_struct *t)
{ assert(t==&task && t->alive && t->policy && !t->running && kgsl_driver.worker_thread==t); t->running=true; wakes++; return 1; }
static void kthread_flush_worker(struct kthread_worker *w)
{ assert(w==&kgsl_driver.worker && task.alive && task.running); task.flushed=true; flushes++; }
static int kthread_stop(struct task_struct *t)
{ assert(t==&task && t->alive); if(t->running) assert(t->flushed); t->alive=false; stops++; return 0; }
struct mm_struct { int marker; };
static struct mm_struct mm;
static struct { struct mm_struct *mm; } current_task={&mm};
#define current (&current_task)
static unsigned long task_size;
#define TASK_SIZE task_size
struct file;
struct file_operations { unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long); };
struct file { void *private_data; const struct file_operations *f_op; };
struct kgsl_process_private { int pid; };
struct kgsl_device { void *dev; };
struct kgsl_device_private { struct kgsl_process_private *process_priv; struct kgsl_device *device; };
struct kgsl_memdesc { bool secured, cpu_map; };
struct kgsl_mem_entry { struct kgsl_memdesc memdesc; };
static struct kgsl_process_private process;
static struct kgsl_device device;
static struct kgsl_device_private priv={&process,&device};
static struct kgsl_mem_entry entry;
static struct file file;
static unsigned long selected, expected_addr, expected_len, expected_pgoff, expected_flags;
static int arch_error, lsm_error, lookup_error, lookups, entry_puts, areas, securities, svm_calls;
static unsigned long svm_result;
static int arch_mmap_check(unsigned long a, unsigned long n, unsigned long f) { return arch_error; }
static int security_mmap_addr(unsigned long a) { assert(a==selected || a==svm_result); securities++; return lsm_error; }
static unsigned long mm_get_unmapped_area(struct mm_struct *m, struct file *f, unsigned long a, unsigned long n, unsigned long off, unsigned long flags)
{ assert(m==&mm && f==&file); assert(a==expected_addr && n==expected_len && off==expected_pgoff && flags==expected_flags); areas++; return selected; }
static unsigned long mm_get_unmapped_area_vmflags(struct mm_struct *m, struct file *f, unsigned long a, unsigned long n, unsigned long off, unsigned long flags, vm_flags_t vf)
{ assert(!"KGSL file callback must be used"); return -EIO; }
static unsigned long shmem_get_unmapped_area(struct file *f, unsigned long a, unsigned long n, unsigned long off, unsigned long flags)
{ assert(!"KGSL memstore is not anonymous shmem"); return -EIO; }
static unsigned long thp_get_unmapped_area_vmflags(struct file *f, unsigned long a, unsigned long n, unsigned long off, unsigned long flags, vm_flags_t vf)
{ assert(!"KGSL memstore is not anonymous THP"); return -EIO; }
static unsigned long get_mmap_entry(struct kgsl_process_private *p, struct kgsl_mem_entry **e, unsigned long off, unsigned long n)
{ assert(p==&process); lookups++; if(lookup_error) return lookup_error; *e=&entry; return 0; }
static bool kgsl_memdesc_is_secured(struct kgsl_memdesc *m) { return m->secured; }
static bool kgsl_memdesc_use_cpu_map(struct kgsl_memdesc *m) { return m->cpu_map; }
static void kgsl_mem_entry_put(struct kgsl_mem_entry *e) { assert(e==&entry); entry_puts++; }
static unsigned long get_svm_unmapped_area(struct file *f, struct kgsl_mem_entry *e, unsigned long a, unsigned long n, unsigned long flags)
{ assert(f==&file && e==&entry); svm_calls++; return svm_result; }
#include "kgsl-module-api-functions.h"
static const struct file_operations fops={kgsl_get_unmapped_area};
static void reset(void)
{
 assert(!task.alive && !kgsl_driver.worker_thread);
 kgsl_driver.worker.initialized=0; task=(struct task_struct){0};
 create_error=scheduler_error=creates=schedules=wakes=stops=flushes=0;
 file=(struct file){&priv,&fops}; entry=(struct kgsl_mem_entry){0}; task_size=1UL<<48;
 selected=0x10000000; expected_addr=0; expected_len=PAGE_SIZE;
 expected_pgoff=KGSL_MEMSTORE_TOKEN_ADDRESS>>PAGE_SHIFT; expected_flags=MAP_SHARED;
 arch_error=lsm_error=lookup_error=lookups=entry_puts=areas=securities=svm_calls=0; svm_result=selected;
 cases++;
}
static unsigned long map(void) { return __get_unmapped_area(&file,expected_addr,expected_len,expected_pgoff,expected_flags,0); }
int main(void)
{
 reset(); assert(kgsl_worker_init()==0 && task.alive && task.running && schedules==1 && wakes==1); kgsl_worker_close(); assert(stops==1 && flushes==1); kgsl_worker_close(); assert(stops==1);
 reset(); kgsl_worker_close(); assert(!stops && !flushes);
 for(int err=-ENOMEM;err>=-ENOMEM-2;err--) { reset(); create_error=err; assert(kgsl_worker_init()==err && !schedules && !wakes && !stops && !kgsl_driver.worker_thread); kgsl_worker_close(); }
 for(int err=-EINVAL;err>=-EINVAL-2;err--) { reset(); scheduler_error=err; assert(kgsl_worker_init()==err && !wakes && stops==1 && !kgsl_driver.worker_thread && !task.alive); kgsl_worker_close(); assert(stops==1); }
 reset(); assert(map()==selected && areas==1 && securities==1 && !lookups && !entry_puts);
 reset(); expected_flags=MAP_PRIVATE; expected_addr=selected; assert(map()==selected && areas==1 && securities==1);
 reset(); expected_flags=MAP_SHARED|MAP_FIXED; expected_addr=selected; assert(map()==selected && areas==1 && securities==1);
 reset(); selected=(unsigned long)-ENOMEM; assert((long)map()==-ENOMEM && !securities);
 reset(); arch_error=-EINVAL; assert((long)map()==-EINVAL && !areas && !securities);
 reset(); expected_len=TASK_SIZE+PAGE_SIZE; assert((long)map()==-ENOMEM && !areas);
 reset(); selected=TASK_SIZE; assert((long)map()==-ENOMEM && areas==1 && !securities);
 reset(); selected++; assert((long)map()==-EINVAL && !securities);
 reset(); lsm_error=-EACCES; assert((long)map()==-EACCES && securities==1);
 reset(); task_size=1UL<<32; selected=TASK_SIZE-PAGE_SIZE; assert(map()==selected && securities==1);
 reset(); task_size=1UL<<32; selected=TASK_SIZE; assert((long)map()==-ENOMEM && !securities);
 reset(); expected_pgoff=0; assert(map()==selected && lookups==1 && entry_puts==1 && areas==1);
 reset(); expected_pgoff=0; lookup_error=-ENOENT; assert((long)map()==-ENOENT && !entry_puts && !areas);
 reset(); expected_pgoff=0; entry.memdesc.secured=true; assert((long)map()==-EPERM && entry_puts==1 && !areas);
 reset(); expected_pgoff=0; selected=-ENOMEM; assert((long)map()==-ENOMEM && entry_puts==1 && !securities);
 reset(); expected_pgoff=0; entry.memdesc.cpu_map=true; assert(map()==svm_result && svm_calls==1 && entry_puts==1 && !areas);
 reset(); expected_pgoff=0; entry.memdesc.cpu_map=true; svm_result=-EBUSY; assert((long)map()==-EBUSY && svm_calls==1 && entry_puts==1 && !securities);
 printf("%u module API scenarios passed (scheduler/thread/VMA/LSM leaves modeled)\n",cases);
 return 0;
}

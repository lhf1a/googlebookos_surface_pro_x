// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
typedef uint64_t u64;
#define smp_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)

typedef unsigned long vm_flags_t;
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL<<PAGE_SHIFT)
#define PMD_SIZE (1UL<<21)
#define SZ_4K PAGE_SIZE
#define SZ_64K (1UL<<16)
#define SZ_1M (1UL<<20)
#define SZ_2M PMD_SIZE
#define ilog2(x) (__builtin_ctzl(x))
#define IS_ALIGNED(x,a) (!((x)&((a)-1)))
#define IS_ERR_VALUE(x) ((unsigned long)(x)>=(unsigned long)-4095)
#define IS_ENABLED(x) 0
#define offset_in_page(x) ((x)&(PAGE_SIZE-1))
#define READ_ONCE(x) (x)
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define likely(x) (x)
#define KGSL_MEMSTORE_TOKEN_ADDRESS 0x100000UL
#define KGSL_CACHEMODE_WRITETHROUGH 1
#define KGSL_CACHEMODE_WRITEBACK 2
#define KGSL_CACHEMODE_UNCACHED 3
#define KGSL_CACHEMODE_WRITECOMBINE 4
#define VM_MAYWRITE 2
#define dev_err_ratelimited(dev,...) ((void)(dev))
#define WARN_ONCE(x,...) ((void)(x))
#define WARN_ON_ONCE(x) assert(!(x))
#define VM_WARN_ON_ONCE(x) assert(!(x))
static unsigned long task_size;
#define TASK_SIZE task_size
struct mm_struct { int marker; };
static struct mm_struct mm;
static struct { struct mm_struct *mm; } cur={&mm};
#define current (&cur)
struct file; struct vm_area_struct;
struct file_operations { unsigned long (*get_unmapped_area)(struct file *,unsigned long,unsigned long,unsigned long,unsigned long); int (*mmap)(struct file *,struct vm_area_struct *); };
struct file { void *private_data; const struct file_operations *f_op; int refs; };
struct vm_operations_struct { int marker; };
static const struct vm_operations_struct kgsl_gpumem_vm_ops={1},vma_dummy_vm_ops={2};
struct vm_area_struct { unsigned long vm_start,vm_end,vm_pgoff,vm_flags,vm_page_prot; void *vm_private_data; const struct vm_operations_struct *vm_ops; struct file *vm_file; };
struct vma_iterator { int mas; };
struct mmap_state { struct vma_iterator *vmi; struct file *file; struct vm_area_struct *prev,*next; unsigned long addr,vm_flags; };
struct page { int marker; };
struct kgsl_pagetable { int marker; };
static struct kgsl_pagetable pt;
struct memops { unsigned long vmflags; };
static const struct memops ops={0x100};
struct kgsl_memdesc { unsigned long gpuaddr,size,flags; int lock,align,cache; bool cpu_map,secured,mapped,uncertain; struct kgsl_pagetable *pagetable; const struct memops *ops; unsigned int page_count; struct page **pages; };
struct kgsl_process_private { struct kgsl_pagetable *pagetable; int pid; long gpumem_mapped; };
struct kgsl_device { void *dev; };
struct kgsl_device_private { struct kgsl_process_private *process_priv; struct kgsl_device *device; };
struct kgsl_mem_entry {u64 published_gpuaddr; struct kgsl_memdesc memdesc; struct kgsl_process_private *priv; int release_error,map_count,refs; bool id_visible,retained; };
static struct kgsl_process_private process;
static struct kgsl_device device;
static struct kgsl_device_private priv;
static struct kgsl_mem_entry entry;
static struct page pages[4]; static struct page *page_array[4]={pages,pages+1,pages+2,pages+3};
static struct file file;
static struct vm_area_struct collision,vma;
static unsigned long lo,hi,cpu_relocate,expected_flags,last_search_end,last_intersection_end,forced_gpu_result;
static unsigned long occupied_start,occupied_end;
static int range_error,search_error,mm_error,lsm_error,lookup_error,reserve_error,map_error,rollback_error,insert_error,fail_insert_at;
static int mm_calls,search_calls,reserves,maps,rollbacks,retains,inserts,ptes,unmaps,entry_puts,traces,securities;
static bool have_collision,force_gpu_result,skip_cpu_check;
static unsigned int cases;
static void spin_lock(int *l) { assert(!*l); *l=1; }
static void spin_unlock(int *l) { assert(*l); *l=0; }
static int kgsl_memdesc_get_align(struct kgsl_memdesc *m) { return m->align; }
static int kgsl_memdesc_get_cachemode(struct kgsl_memdesc *m) { return m->cache; }
static bool kgsl_memdesc_use_cpu_map(struct kgsl_memdesc *m) { return m->cpu_map; }
static bool kgsl_memdesc_is_secured(struct kgsl_memdesc *m) { return m->secured; }
static int kgsl_mmu_svm_range(struct kgsl_pagetable *p,u64 *start,u64 *end,u64 flags)
{ assert(p==&pt); if(range_error) return range_error; *start=lo; *end=hi; return 0; }
static u64 kgsl_mmu_find_svm_region(struct kgsl_pagetable *p,u64 start,u64 end,u64 len,u64 align)
{
 assert(p==&pt && start<end && len && len<=end-start); search_calls++; assert(search_calls<50); last_search_end=end;
 if(search_error) return search_error;
 if(force_gpu_result) return forced_gpu_result;
 u64 a=(end-len)&~(align-1);
 if(occupied_start<occupied_end && a<occupied_end && a+len>occupied_start) {
  if(occupied_start<start+len) return (u64)-ENOMEM;
  a=(occupied_start-len)&~(align-1);
 }
 return a>=start?a:(u64)-ENOMEM;
}
static int kgsl_mmu_set_svm_region(struct kgsl_pagetable *p,u64 addr,u64 size)
{
 assert(p==&pt && entry.memdesc.lock && !entry.memdesc.gpuaddr); reserves++;
 if(reserve_error) return reserve_error;
 if(occupied_start<occupied_end && addr<occupied_end && addr+size>occupied_start) return -ENOMEM;
 assert(addr>=lo && addr+size<=hi); return 0;
}
static int kgsl_mmu_map(struct kgsl_pagetable *p,struct kgsl_memdesc *m)
{ assert(p==&pt && m==&entry.memdesc && m->pagetable==p && !m->lock && m->gpuaddr); maps++; if(map_error) {m->uncertain=rollback_error!=0; return map_error;} m->mapped=true; return 0; }
static int kgsl_mmu_put_gpuaddr(struct kgsl_memdesc *m)
{ assert(m==&entry.memdesc); rollbacks++; if(rollback_error) return rollback_error; m->gpuaddr=0; m->pagetable=NULL; m->mapped=false; return 0; }
static int kgsl_mem_entry_detach_process(struct kgsl_mem_entry *e)
{ assert(e==&entry && rollback_error && e->memdesc.uncertain); e->id_visible=false; e->retained=true; e->release_error=rollback_error; retains++; return rollback_error; }
static void kgsl_memfree_purge(struct kgsl_pagetable *p,u64 addr,u64 size) { assert(p==&pt && addr==entry.memdesc.gpuaddr); }
static struct vm_area_struct *find_vma_intersection(struct mm_struct *m,unsigned long a,unsigned long b)
{ assert(m==&mm && a<b); last_intersection_end=b; return have_collision && a<collision.vm_end && b>collision.vm_start?&collision:NULL; }
static unsigned long mm_get_unmapped_area(struct mm_struct *m,struct file *f,unsigned long a,unsigned long len,unsigned long off,unsigned long flags)
{ assert(m==&mm && f==&file && flags==expected_flags); mm_calls++; if(mm_error) return mm_error; if(cpu_relocate==a) return a+PAGE_SIZE; if(!(flags&MAP_FIXED) && have_collision && a<collision.vm_end && a+len>collision.vm_start) return hi; return a; }
static int arch_mmap_check(unsigned long a,unsigned long n,unsigned long f) { return 0; }
static int security_mmap_addr(unsigned long a) { securities++; assert(!reserves && !maps); return lsm_error; }
static unsigned long shmem_get_unmapped_area(struct file *f,unsigned long a,unsigned long l,unsigned long p,unsigned long flags) { assert(0); return 0; }
static unsigned long thp_get_unmapped_area_vmflags(struct file *f,unsigned long a,unsigned long l,unsigned long p,unsigned long flags,vm_flags_t v) { assert(0); return 0; }
static unsigned long mm_get_unmapped_area_vmflags(struct mm_struct *m,struct file *f,unsigned long a,unsigned long l,unsigned long p,unsigned long flags,vm_flags_t v) { assert(0); return 0; }
static int get_mmap_entry(struct kgsl_process_private *p,struct kgsl_mem_entry **e,unsigned long off,unsigned long len)
{ assert(p==&process && off==7); if(lookup_error) return lookup_error; if(!entry.id_visible) return -ENOENT; entry.refs++; *e=&entry; return 0; }
static void kgsl_mem_entry_put(struct kgsl_mem_entry *e) { assert(e==&entry && e->refs>1); e->refs--; entry_puts++; }
static void vm_flags_set(struct vm_area_struct *v,unsigned long flags) { v->vm_flags|=flags; }
static unsigned long pgprot_writebackcache(unsigned long p) { return 2; }
static unsigned long pgprot_writethroughcache(unsigned long p) { return 2; }
static unsigned long pgprot_writecombine(unsigned long p) { return 4; }
#define pgprot_val(p) (p)
static int vm_insert_page(struct vm_area_struct *v,unsigned long addr,struct page *page)
{ assert(!maps && !reserves && !v->vm_private_data && !v->vm_ops); assert(addr>=v->vm_start && addr<v->vm_end && page); inserts++; if(inserts==fail_insert_at) return insert_error; ptes++; return 0; }
static int atomic_inc_return(int *v) { return ++*v; }
static void atomic64_add(long n,long *v) { *v+=n; }
static void trace_kgsl_mem_mmap(struct kgsl_mem_entry *e,unsigned long addr) { traces++; }
static int kgsl_mmap_memstore(struct file *f,struct kgsl_device *d,struct vm_area_struct *v) { assert(0); return -EINVAL; }
static struct file *get_file(struct file *f) { f->refs++; return f; }
static void fput(struct file *f) { assert(f->refs>1); f->refs--; }
static int vfs_mmap(struct file *f,struct vm_area_struct *v) { return f->f_op->mmap(f,v); }
static void vma_iter_set(struct vma_iterator *v,unsigned long addr) { (void)v; }
static void unmap_region(int *mas,struct vm_area_struct *v,struct vm_area_struct *prev,struct vm_area_struct *next)
{ assert(v->vm_ops==&vma_dummy_vm_ops && !v->vm_file); ptes=0; unmaps++; }
#include "kgsl-svm-functions.h"
static const struct file_operations fops={kgsl_get_unmapped_area,kgsl_mmap};
static void reset(void)
{
 memset(&entry,0,sizeof(entry));memset(&vma,0,sizeof(vma));process=(struct kgsl_process_private){.pagetable=&pt};priv=(struct kgsl_device_private){&process,&device};file=(struct file){&priv,&fops,1};
 entry=(struct kgsl_mem_entry){.priv=&process,.refs=1,.id_visible=true,.memdesc={.size=2*PAGE_SIZE,.align=12,.cache=KGSL_CACHEMODE_WRITEBACK,.cpu_map=true,.ops=&ops,.pages=page_array,.page_count=2}};
 lo=0x10000;hi=0x20000;task_size=1UL<<48;cpu_relocate=0;expected_flags=MAP_SHARED;last_search_end=last_intersection_end=forced_gpu_result=occupied_start=occupied_end=0;
 range_error=search_error=mm_error=lsm_error=lookup_error=reserve_error=map_error=rollback_error=insert_error=fail_insert_at=0;
 mm_calls=search_calls=reserves=maps=rollbacks=retains=inserts=ptes=unmaps=entry_puts=traces=securities=0;have_collision=force_gpu_result=skip_cpu_check=false;cases++;
}
static unsigned long choose(unsigned long hint,unsigned long len)
{ unsigned long r=__get_unmapped_area(&file,hint,len,7,expected_flags,0); assert(!maps && !reserves && !entry.memdesc.gpuaddr && entry.refs==1); return r; }
static int bind_at(unsigned long addr,unsigned long len)
{
 struct vma_iterator iter={0};struct mmap_state map={.vmi=&iter,.file=&file,.addr=addr,.vm_flags=VM_MAYWRITE};
 vma=(struct vm_area_struct){.vm_start=addr,.vm_end=addr+len,.vm_pgoff=7,.vm_flags=VM_MAYWRITE};
 return __mmap_new_file_vma(&map,&vma);
}
static void clean_error(int ret)
{ assert(ret<0 && !entry.map_count && !process.gpumem_mapped && !traces && !vma.vm_private_data && entry.refs==1 && file.refs==1 && !ptes && unmaps==1); }
int main(void)
{
 unsigned long addr;
 reset();addr=choose(0,2*PAGE_SIZE);assert(addr==hi-2*PAGE_SIZE);assert(bind_at(addr,2*PAGE_SIZE)==0 && maps==1 && reserves==1 && ptes==2 && entry.memdesc.gpuaddr==addr && entry.map_count==1 && process.gpumem_mapped==2*PAGE_SIZE && entry.refs==2 && vma.vm_ops==&kgsl_gpumem_vm_ops);
 reset();assert(choose(lo,2*PAGE_SIZE)==lo);
 reset();cpu_relocate=lo;assert(choose(lo,2*PAGE_SIZE)==hi-2*PAGE_SIZE);
 reset();occupied_start=lo;occupied_end=lo+2*PAGE_SIZE;assert(choose(lo,2*PAGE_SIZE)==hi-2*PAGE_SIZE);
 reset();expected_flags|=MAP_FIXED;assert(choose(lo,2*PAGE_SIZE)==lo);
 reset();expected_flags|=MAP_FIXED;assert((long)choose(lo+1,2*PAGE_SIZE)==-EINVAL);
 reset();expected_flags|=MAP_FIXED;assert((long)choose(0,2*PAGE_SIZE)==-EINVAL);
 reset();expected_flags|=MAP_FIXED;assert((long)choose(lo-PAGE_SIZE,2*PAGE_SIZE)==-ENOMEM);
 reset();expected_flags|=MAP_FIXED;assert((long)choose(hi-PAGE_SIZE,2*PAGE_SIZE)==-ENOMEM);
 reset();expected_flags|=MAP_FIXED;assert((long)choose(lo,0)==-EINVAL);
 reset();expected_flags|=MAP_FIXED;assert((long)choose(lo,PAGE_SIZE+1)==-EINVAL);
 reset();expected_flags|=MAP_FIXED;cpu_relocate=lo;assert((long)choose(lo,2*PAGE_SIZE)==-ENOMEM);
 reset();lsm_error=-EACCES;assert((long)choose(0,2*PAGE_SIZE)==-EACCES && securities==1);
 reset();range_error=-ENODEV;assert((long)choose(0,2*PAGE_SIZE)==-ENODEV);
 reset();search_error=-EIO;assert((long)choose(0,2*PAGE_SIZE)==-EIO);
 reset();mm_error=-EIO;assert((long)choose(0,2*PAGE_SIZE)==-EIO);
 reset();have_collision=true;collision=(struct vm_area_struct){.vm_start=hi-PAGE_SIZE,.vm_end=hi};addr=choose(0,2*PAGE_SIZE);assert(addr==hi-3*PAGE_SIZE && last_intersection_end==hi-PAGE_SIZE);
 reset();have_collision=true;collision=(struct vm_area_struct){.vm_start=lo-PAGE_SIZE,.vm_end=hi};assert((long)choose(0,2*PAGE_SIZE)==-ENOMEM);
 reset();cpu_relocate=hi-2*PAGE_SIZE;assert(choose(0,2*PAGE_SIZE)==hi-4*PAGE_SIZE);
 reset();task_size=hi-PAGE_SIZE;assert(choose(0,2*PAGE_SIZE)==TASK_SIZE-2*PAGE_SIZE);
 reset();task_size=1UL<<32;lo=TASK_SIZE-2*PAGE_SIZE;hi=TASK_SIZE+PAGE_SIZE;assert(choose(0,2*PAGE_SIZE)==lo);
 reset();force_gpu_result=true;forced_gpu_result=hi;assert((long)choose(0,2*PAGE_SIZE)==-EINVAL);
 reset();force_gpu_result=true;forced_gpu_result=lo+1;assert((long)choose(0,2*PAGE_SIZE)==-EINVAL);
 reset();entry.release_error=-EUCLEAN;assert((long)choose(0,2*PAGE_SIZE)==-EUCLEAN);
 reset();entry.memdesc.gpuaddr=lo;assert((long)get_svm_unmapped_area(&file,&entry,0,2*PAGE_SIZE,expected_flags)==-EBUSY);
 for(int i=1;i<=2;i++){reset();addr=choose(0,2*PAGE_SIZE);fail_insert_at=i;insert_error=-ENOMEM;int r=bind_at(addr,2*PAGE_SIZE);assert(r==-ENOMEM && !maps && !reserves);clean_error(r);}
 reset();addr=choose(0,2*PAGE_SIZE);entry.memdesc.pages=NULL;int r=bind_at(addr,2*PAGE_SIZE);assert(r==-EINVAL && !inserts && !maps);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);entry.memdesc.page_count=3;r=bind_at(addr,2*PAGE_SIZE);assert(r==-EINVAL && !inserts && !maps);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);reserve_error=-ENOMEM;r=bind_at(addr,2*PAGE_SIZE);assert(r==-ENOMEM && reserves==1 && !maps && !rollbacks);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);map_error=-EIO;r=bind_at(addr,2*PAGE_SIZE);assert(r==-EIO && maps==1 && rollbacks==1 && !entry.memdesc.gpuaddr && !entry.retained);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);map_error=-EIO;rollback_error=-EUCLEAN;r=bind_at(addr,2*PAGE_SIZE);assert(r==-EIO && entry.retained && retains==1 && !entry.id_visible && entry.memdesc.gpuaddr==addr && entry.memdesc.pagetable==&pt && entry.memdesc.uncertain);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);occupied_start=addr;occupied_end=addr+2*PAGE_SIZE;r=bind_at(addr,2*PAGE_SIZE);assert(r==-ENOMEM && !maps);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);lookup_error=-ENOENT;r=bind_at(addr,2*PAGE_SIZE);assert(r==-ENOENT && !maps && !inserts);clean_error(r);
 reset();entry.memdesc.cache=KGSL_CACHEMODE_WRITECOMBINE;addr=choose(0,2*PAGE_SIZE);assert(bind_at(addr,2*PAGE_SIZE)==0 && !inserts && maps==1);
 reset();entry.memdesc.cpu_map=false;assert(bind_at(lo,2*PAGE_SIZE)==0 && !maps && entry.map_count==1 && process.gpumem_mapped==2*PAGE_SIZE);assert(bind_at(lo+2*PAGE_SIZE,2*PAGE_SIZE)==0 && !maps && entry.map_count==2 && process.gpumem_mapped==2*PAGE_SIZE);
 reset();addr=choose(0,2*PAGE_SIZE);entry.memdesc.secured=true;r=bind_at(addr,2*PAGE_SIZE);assert(r==-EPERM && !maps && !inserts);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);entry.memdesc.align=16;r=bind_at(addr,2*PAGE_SIZE);assert(r==-EINVAL && !maps && !inserts);clean_error(r);
 reset();addr=choose(0,2*PAGE_SIZE);entry.release_error=-EUCLEAN;r=bind_at(addr,2*PAGE_SIZE);assert(r==-EUCLEAN && !maps && !inserts);clean_error(r);
 printf("%u SVM/mmap scenarios passed (MMU/VMA/PTE/LSM leaves modeled)\n",cases);return 0;
}

"""Compile actual generated helpers with mock HFI/DMA; no device access.

Optional helpers.c argument observes a baseline/rollback without asserting the
new policy. Otherwise generate a fresh candidate and require the policy.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct list_head { struct list_head *next, *prev; };
#define entry(p) ((struct intbuf *)((char *)(p)-offsetof(struct intbuf,list)))
#define list_for_each_entry_safe(b,n,h,m) \
 for (b=entry((h)->next), n=entry(b->m.next); &b->m!=(h); b=n,n=entry(n->m.next))
static void list_del_init(struct list_head *p) {
 p->prev->next=p->next; p->next->prev=p->prev; p->next=p->prev=p;
}
enum hfi_version { HFI_VERSION_4XX=4 };
struct resources { enum hfi_version hfi_version; };
struct core { bool sq2; void *dev; struct resources *res; };
struct venus_inst { struct core *core; struct list_head internalbufs; };
struct intbuf { struct list_head list; unsigned type; size_t size;
 void *va; unsigned long da,attrs; };
struct hfi_buffer_desc { size_t buffer_size; unsigned buffer_type,num_buffers;
 unsigned long device_addr; bool response_required; };
#define IS_SQ2_HFI(c) ((c)->sq2)
#define HFI_BUFFER_INTERNAL_PERSIST 10
#define HFI_BUFFER_INTERNAL_PERSIST_1 11
#define HFI_BUFFER_INTERNAL_SCRATCH(v) ((v)+20)
#define HFI_BUFFER_INTERNAL_SCRATCH_1(v) ((v)+21)
#define HFI_BUFFER_INTERNAL_SCRATCH_2(v) ((v)+22)
static int calls, frees, sets, fail_at, failure, retired;
static int hfi_session_unset_buffers(struct venus_inst *i,struct hfi_buffer_desc *b) {
 (void)i; assert(b->response_required); return ++calls==fail_at ? failure : 0;
}
static void dma_free_attrs(void *d,size_t s,void *v,unsigned long a,unsigned long f) {
 (void)d;(void)s;(void)v;(void)a;(void)f; ++frees;
}
#define kfree free
static void sq2_intbuf_dma_free(struct venus_inst *i,struct intbuf *b)
{dma_free_attrs(i->core->dev,b->size,b->va,b->da,b->attrs);}
static int intbufs_set_buffer(struct venus_inst *i,unsigned t) {
 (void)i;(void)t;++sets;return 0;
}
static void sq2_intbuf_retire(struct venus_inst *i,struct intbuf *b)
{(void)i;list_del_init(&b->list);free(b);retired++;} /* mock transfer, not DMA free */
/* ACTUAL_FUNCTIONS */
int main(int argc,char **argv) {
 bool strict=argc>1 && !strcmp(argv[1],"strict");
 struct resources res={HFI_VERSION_4XX};
 for(int sq2=0;sq2<2;++sq2) for(int reallocating=0;reallocating<2;++reallocating)
 for(int fault=0;fault<4;++fault) for(int err=0;err<2;++err) {
  struct core core={sq2,NULL,&res}; struct venus_inst inst={.core=&core};
  struct list_head *h=&inst.internalbufs; h->next=h->prev=h;
  calls=frees=sets=retired=0;fail_at=fault;failure=err ? -5 : -110;
  for(int j=0;j<3;++j) {
   struct intbuf *b=calloc(1,sizeof(*b));assert(b);
   b->size=4096;b->da=0x30000000ul+j*4096;b->type=1;
   b->list.prev=h->prev;b->list.next=h;h->prev->next=&b->list;h->prev=&b->list;
  }
  int ret=reallocating ? venus_helper_intbufs_realloc(&inst) : intbufs_unset_buffers(&inst);
  int remain=0;for(struct list_head *p=h->next;p!=h;p=p->next) ++remain;
  if(sq2 && fault && strict) {
   assert(ret==failure && calls==fault && frees==0 && retired==fault-1 && remain==4-fault && sets==0);
  } else if(!sq2 || !fault) {
   assert(calls==3 && frees==(sq2?0:3) && retired==(sq2?3:0) && remain==0);
   assert(sets==(reallocating?3:0));
   assert(ret==(!reallocating && fault==3 ? failure : 0));
  }
  if(sq2 && fault==1 && !err)
   printf("%s fail=-110 ret=%d release_calls=%d dma_frees=%d retained=%d new_allocations=%d\n",
          reallocating?"realloc":"unset",ret,calls,frees,remain,sets);
  /* Harness teardown, not a claim that production may free retained DMA. */
  while(h->next!=h) {struct intbuf *b=entry(h->next);list_del_init(&b->list);free(b);}
 }
 puts("32 mock cases completed; not a PAS/concurrency/VB2 proof");
 return 0;
}
'''


def run(source, directory, strict):
    text = Path(source).read_text()
    functions = []
    for start, end in (
        ('static int intbufs_unset_buffers(', 'static const unsigned int intbuf_types_1xx'),
        ('int venus_helper_intbufs_realloc(', 'EXPORT_SYMBOL_GPL(venus_helper_intbufs_realloc);'),
    ):
        functions.append(text[text.index(start):text.index(end)])
    c = directory / 'release.c'
    c.write_text(HARNESS.replace('/* ACTUAL_FUNCTIONS */', '\n'.join(functions)))
    exe = directory / 'release'
    subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-g', '-fno-pie', '-no-pie', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe), 'strict' if strict else 'observe'], check=True)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        if len(sys.argv) >= 2:
            run(sys.argv[1], directory, '--strict' in sys.argv[2:])
        else:
            output = directory / 'venus'
            subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                            '--output', str(output)], check=True)
            run(output / 'helpers.c', directory, True)

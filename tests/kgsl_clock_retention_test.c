/* Execute actual provider additions; CCF, devres, regmap and PM mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
#define fallthrough __attribute__((fallthrough))
#define EXPORT_SYMBOL_GPL(n)
#define GFP_KERNEL 0
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head { struct list_head *next,*prev; };
#define LIST_HEAD(n) struct list_head n={&n,&n}
static void list_add_tail(struct list_head *n,struct list_head *h)
{ n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n; }
static void list_del(struct list_head *n) { n->prev->next=n->next;n->next->prev=n->prev; }
#define list_for_each_entry(pos,head,member) \
    for (struct list_head *cursor=(head)->next; cursor!=(head) && ((pos)=container_of(cursor,__typeof__(*(pos)),member),1);cursor=cursor->next)
struct mutex { pthread_mutex_t raw; int held; };
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER,0}
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->raw));assert(!m->held);m->held=1; }
static void mutex_unlock(struct mutex *m) { assert(m->held);m->held=0;assert(!pthread_mutex_unlock(&m->raw)); }
struct regmap { u32 bits; };
struct device { struct device *parent; struct regmap *regmap; bool rpm; };
struct clk_ops { int tag; };
static const struct clk_ops clk_branch2_ops={1},clk_branch2_aon_ops={2},clk_branch2_mem_ops={3},clk_branch2_prepare_ops={4},other_ops={5};
struct clk_init_data { const struct clk_ops *ops; };
struct clk_hw { const struct clk_init_data *init; bool live; };
struct clk { struct clk_hw *hw; };
struct clk_regmap { struct clk_hw hw; struct regmap *regmap; };
struct clk_branch { u32 halt_reg;struct clk_regmap clkr; };
#define to_clk_branch(_hw) container_of(container_of(_hw,struct clk_regmap,hw),struct clk_branch,clkr)
#include "retention-types.h"
static int alloc_error,action_error,register_error,get_error,put_error,write_error;
static int allocations,registrations,get_calls,put_calls,writes,barriers,delays,refs,scenarios;
static u32 actual_reg,actual_mask,actual_value;
static void (*actions[16])(void *);
static void *action_data[16];
static int action_count;
static struct regmap map;
static struct device dev,parent;
static struct clk_branch branch;
static struct clk_init_data init;
static struct clk test_clk;
static void require_lock(void);
static struct clk_hw *__clk_get_hw(struct clk *c) { return c->hw; }
static struct regmap *dev_get_regmap(struct device *d,void *unused) { return d->regmap; }
static bool pm_runtime_enabled(struct device *d) { return d->rpm; }
static int pm_runtime_resume_and_get(struct device *d)
{ require_lock();get_calls++;if(get_error)return get_error;refs++;return 0; }
static int pm_runtime_put(struct device *d)
{ require_lock();put_calls++;assert(refs==1);refs--;return put_error; }
static int regmap_update_bits(struct regmap *m,u32 reg,u32 mask,u32 value)
{ require_lock();assert(!dev.rpm || refs==1);writes++;actual_reg=reg;actual_mask=mask;actual_value=value;if(write_error)return write_error;m->bits=(m->bits&~mask)|(value&mask);return 0; }
static void mb(void) { require_lock();barriers++; }
static void udelay(int n) { require_lock();assert(n==1);delays++; }
static void push_action(void (*fn)(void *),void *p)
{ assert(action_count<16);actions[action_count]=fn;action_data[action_count++]=p; }
static void *devm_kzalloc(struct device *d,size_t n,int flags)
{ allocations++;if(alloc_error)return NULL;void *p=calloc(1,n);assert(p);push_action(free,p);return p; }
static int devm_add_action_or_reset(struct device *d,void (*fn)(void *),void *p)
{ if(action_error){fn(p);return action_error;}push_action(fn,p);return 0; }
static void unregister_clock(void *data);
static int devm_clk_hw_register(struct device *d,struct clk_hw *hw)
{ registrations++;if(register_error)return register_error;assert(hw->init);hw->init=NULL;hw->live=true;push_action(unregister_clock,hw);return 0; }
#include "retention-functions.h"
static void require_lock(void) { assert(qcom_branch_retention_lock.held); }
static void unregister_clock(void *data)
{
    struct clk_hw *hw=data;struct qcom_branch_retention *e;
    list_for_each_entry(e,&qcom_branch_retention_list,node) assert(e->hw!=hw);
    hw->live=false;
}
static void cleanup(void)
{
    while(action_count){action_count--;actions[action_count](action_data[action_count]);}
    assert(qcom_branch_retention_list.next==&qcom_branch_retention_list);
    assert(!qcom_branch_retention_lock.held && !refs);
}
static void reset(const struct clk_ops *ops)
{
    cleanup();memset(&branch,0,sizeof(branch));memset(&dev,0,sizeof(dev));memset(&parent,0,sizeof(parent));
    init.ops=ops;branch.clkr.hw.init=&init;branch.halt_reg=0x1234;map.bits=0xa5a585a5;
    dev.regmap=&map;dev.rpm=true;test_clk.hw=&branch.clkr.hw;
    alloc_error=action_error=register_error=get_error=put_error=write_error=0;
    allocations=registrations=get_calls=put_calls=writes=barriers=delays=0;
    actual_reg=actual_mask=actual_value=0;
}
int main(void)
{
    const struct clk_ops *supported[]={&clk_branch2_ops,&clk_branch2_aon_ops,&clk_branch2_mem_ops,&clk_branch2_prepare_ops};
    for(unsigned i=0;i<4;i++) {
        reset(supported[i]);assert(!devm_clk_register_regmap(&dev,&branch.clkr));
        assert(!qcom_clk_set_flags(&test_clk,CLKFLAG_RETAIN_MEM));
        assert(registrations==1 && allocations==1 && writes==1 && get_calls==1 && put_calls==1 && !refs);
        assert(actual_reg==0x1234 && actual_mask==BIT(14) && actual_value==BIT(14));scenarios++;
    }
    const u32 masks[]={BIT(13),BIT(13),BIT(14),BIT(14),BIT(12),BIT(12)};
    for(unsigned i=0;i<6;i++) {
        reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));
        map.bits=(i%2)?0xffffffff:0xa5a585a5;u32 old=map.bits;
        assert(!qcom_clk_set_flags(&test_clk,i));
        assert(actual_mask==masks[i] && actual_value==((i%2)?0:masks[i]));
        assert(map.bits==((old&~masks[i])|actual_value));assert(barriers==1 && delays==1);scenarios++;
    }
    reset(&other_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));
    assert(!allocations && qcom_clk_set_flags(&test_clk,0)==-EOPNOTSUPP && !writes && !get_calls);scenarios++;
    reset(&clk_branch2_ops);register_error=-517;
    assert(devm_clk_register_regmap(&dev,&branch.clkr)==-517 && !allocations && !action_count);scenarios++;
    reset(&clk_branch2_ops);alloc_error=1;
    assert(devm_clk_register_regmap(&dev,&branch.clkr)==-ENOMEM && qcom_clk_set_flags(&test_clk,0)==-EOPNOTSUPP);scenarios++;
    reset(&clk_branch2_ops);action_error=-ENOMEM;
    assert(devm_clk_register_regmap(&dev,&branch.clkr)==-ENOMEM && qcom_clk_set_flags(&test_clk,0)==-EOPNOTSUPP);scenarios++;
    reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));get_error=-EIO;
    assert(qcom_clk_set_flags(&test_clk,0)==-EIO && !writes && !put_calls && !refs);scenarios++;
    reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));write_error=-ETIMEDOUT;
    assert(qcom_clk_set_flags(&test_clk,0)==-ETIMEDOUT && writes==1 && put_calls==1 && !refs);scenarios++;
    reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));put_error=-EIO;
    assert(qcom_clk_set_flags(&test_clk,0)==-EIO && writes==1 && put_calls==1 && !refs);scenarios++;
    reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));write_error=-ETIMEDOUT;put_error=-EIO;
    assert(qcom_clk_set_flags(&test_clk,0)==-ETIMEDOUT && put_calls==1);scenarios++;
    reset(&clk_branch2_ops);dev.rpm=false;assert(!devm_clk_register_regmap(&dev,&branch.clkr));
    assert(!qcom_clk_set_flags(&test_clk,0) && writes==1 && !get_calls && !put_calls);scenarios++;
    reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));
    assert(qcom_clk_set_flags(&test_clk,6)==-EINVAL && !get_calls && !writes);scenarios++;
    assert(qcom_clk_set_flags(NULL,0)==-EINVAL);scenarios++;
    assert(qcom_clk_set_flags((void *)(intptr_t)-517,0)==-517);scenarios++;
    test_clk.hw=NULL;assert(qcom_clk_set_flags(&test_clk,0)==-ENODEV);scenarios++;
    struct clk_hw stranger={0};test_clk.hw=&stranger;
    assert(qcom_clk_set_flags(&test_clk,0)==-EOPNOTSUPP && !get_calls && !writes);scenarios++;
    reset(&clk_branch2_ops);dev.regmap=NULL;
    assert(devm_clk_register_regmap(&dev,&branch.clkr)==-EINVAL && !allocations);scenarios++;
    reset(&clk_branch2_ops);dev.regmap=NULL;dev.parent=&parent;parent.regmap=&map;
    assert(!devm_clk_register_regmap(&dev,&branch.clkr) && branch.clkr.regmap==&map);scenarios++;
    reset(&clk_branch2_ops);assert(!devm_clk_register_regmap(&dev,&branch.clkr));cleanup();
    assert(qcom_clk_set_flags(&test_clk,0)==-EOPNOTSUPP && !writes);scenarios++;
    cleanup();printf("%d provider scenarios passed; CCF/devres/PM/registers mocked\n",scenarios);
    return 0;
}

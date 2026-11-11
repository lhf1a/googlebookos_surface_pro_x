/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual API/initializer bodies; filesystem and enclosing driver types modeled. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
typedef uint32_t u32;typedef unsigned short umode_t;
#define ERR_PTR(x) ((void*)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x)||IS_ERR(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*(x)))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define S_IWUGO 0222
#define S_IRUGO 0444
static int scenarios,cfg_debugfs=1,backend_available=1,creates,files,scalars,removed,dir_fail,file_fail,scalar_fail,fail_code=-EACCES,parser_calls;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL scenario=%d line=%d: %s\n",scenarios,__LINE__,#x);exit(1);}}while(0)
#define IS_ENABLED(x) cfg_debugfs
#define DP_ERR(...) ((void)0)
#define DP_WARN(...) ((void)0)
#define DRM_ERROR(...) ((void)0)
#define DSI_ERR(...) ((void)0)
#define DSI_CTRL_ERR(...) ((void)0)
#define SDE_ERROR(...) ((void)0)
#define MAX_NAME_SIZE 128
struct file_operations{int id;};
struct dentry{int id;struct dentry *parent;bool removed,is_dir;};
static struct dentry pool[256];static int allocated;
struct event{char name[160];umode_t mode;struct dentry *parent;void *data;const struct file_operations *fops;int kind;};
static struct event events[256];static int event_count;
static const struct file_operations fops_u32={1},fops_u32_ro={2},fops_u32_wo={3},fops_bool={4},fops_bool_ro={5},fops_bool_wo={6},fops_ulong={7},fops_ulong_ro={8},fops_ulong_wo={9};
#include "actual-fops-names.h"
#include "actual-definitions.h"
static struct dentry *new_node(struct dentry *parent,bool dir){CHECK(allocated<256);struct dentry *p=&pool[allocated++];*p=(struct dentry){allocated,parent,false,dir};return p;}
static void record(const char *name,umode_t mode,struct dentry *parent,void *data,const struct file_operations *fops,int kind){CHECK(event_count<256);struct event *e=&events[event_count++];snprintf(e->name,sizeof(e->name),"%s",name);e->mode=mode;e->parent=parent;e->data=data;e->fops=fops;e->kind=kind;}
static struct dentry *debugfs_create_file(const char *name,umode_t mode,struct dentry *parent,void *data,const struct file_operations *fops){creates++;files++;record(name,mode,parent,data,fops,0);if(!backend_available)return ERR_PTR(-ENODEV);if(IS_ERR(parent))return parent;if(file_fail==files)return ERR_PTR(fail_code);return new_node(parent,false);}
static struct dentry *debugfs_create_file_unsafe(const char *name,umode_t mode,struct dentry *parent,void *value,const struct file_operations *fops){creates++;scalars++;record(name,mode,parent,value,fops,1);if(!backend_available)return ERR_PTR(-ENODEV);if(IS_ERR(parent))return parent;if(scalar_fail==-1||scalar_fail==scalars)return ERR_PTR(-ENOMEM);return new_node(parent,false);}
static struct dentry *debugfs_create_dir(const char *name,struct dentry *parent){creates++;if(!backend_available)return ERR_PTR(-ENODEV);if(IS_ERR(parent))return parent;if(dir_fail)return ERR_PTR(fail_code);return new_node(parent,true);}
static void debugfs_remove(struct dentry *p){if(IS_ERR_OR_NULL(p))return;removed++;CHECK(!p->removed);for(int i=0;i<allocated;i++){struct dentry *n=&pool[i];for(struct dentry *q=n;q;q=q->parent)if(q==p){n->removed=true;break;}}}
#define debugfs_remove_recursive debugfs_remove
#include "actual-debugfs-helpers.h"
struct device{void *of_node;};
static const void *of_get_property(void *node,const char *name,int *len){return NULL;}
struct dp_parser{u32 max_lclk_khz;bool dsc_feature_enable,fec_feature_enable,has_widebus;};
struct dp_panel{u32 lane_count,link_bw_code;};struct dp_hpd{bool force_multi_func;};struct dp_pll{bool ssc_en;};
struct dp_debug{bool hdcp_wait_sink_sync,force_encryption,skip_uevent;unsigned long connect_notification_delay_ms;u32 disconnect_delay_ms;};
struct dp_debug_private{struct dp_debug dp_debug;const char *name;struct device *dev;struct dentry *root;struct dp_parser *parser;struct dp_panel *panel;struct dp_hpd *hpd;struct dp_pll *pll;};
struct dsi_ctrl{const char *name;bool enable_cmd_dma_stats;struct dentry *debugfs_root;};
struct msm_dsi_phy{const char *name;bool allow_phy_power_off;u32 regulator_min_datarate_bps;};
struct panel{bool ulps_feature_enabled,ulps_suspend_enabled;};
struct display_ctrl{struct msm_dsi_phy *phy;};
struct dsi_display{const char *name,*display_type;int ctrl_count;struct display_ctrl ctrl[2];struct panel *panel;bool ulps_enabled;u32 clk_gating_config;void *parser;struct dentry *root;};
#define display_for_each_ctrl(i,d) for(i=0;i<(d)->ctrl_count;i++)
static void dsi_parser_dbg_init(void *p,struct dentry *d){CHECK(d&&!IS_ERR(d)&&!d->removed);parser_calls++;}
/* Host leaves for already migrated string calls; this test is not their ABI gate. */
static ssize_t model_strscpy(char *d,const char *s,size_t n){size_t len=strlen(s);if(!n)return-E2BIG;size_t c=len<n?len:n-1;memcpy(d,s,c);d[c]=0;return len<n?(ssize_t)c:-E2BIG;}
static size_t model_strlcat(char *d,const char *s,size_t n){size_t len=strlen(d),slen=strlen(s);if(len<n)model_strscpy(d+len,s,n-len);return len+slen;}
#define strscpy model_strscpy
#define strlcat model_strlcat
struct sde_encoder_phys{int unused;};struct sde_encoder_phys_wb{struct sde_encoder_phys base;void *hw_wb;u32 wbdone_timeout;};
#define to_sde_encoder_phys_wb(p) container_of(p,struct sde_encoder_phys_wb,base)
#include "actual-driver-functions.h"
static void reset(void){scenarios++;creates=files=scalars=removed=allocated=event_count=parser_calls=0;dir_fail=file_fail=scalar_fail=0;backend_available=cfg_debugfs=1;memset(pool,0,sizeof(pool));}
static void expect_scalar(const char *name,umode_t mode,void *data,struct dentry *parent){int found=0;for(int i=0;i<event_count;i++)if(events[i].kind&& !strcmp(events[i].name,name)){CHECK(events[i].mode==mode&&events[i].data==data&&events[i].parent==parent);found++;}CHECK(found==1);}
static void dp_tests(void){struct device dev={0};struct dp_parser parser={0};struct dp_panel panel={0};struct dp_hpd hpd={0};struct dp_pll pll={0};struct dp_debug_private d={.dev=&dev,.parser=&parser,.panel=&panel,.hpd=&hpd,.pll=&pll};
 reset();CHECK(dp_debug_init(&d.dp_debug)==0);CHECK(d.root&&!d.root->removed&&scalars==13&&!removed);int total_files=files;
 expect_scalar("max_lclk_khz",0644,&parser.max_lclk_khz,d.root);expect_scalar("lane_count",0644,&panel.lane_count,d.root);expect_scalar("link_bw_code",0644,&panel.link_bw_code,d.root);expect_scalar("hdcp_wait_sink_sync",0644,&d.dp_debug.hdcp_wait_sink_sync,d.root);expect_scalar("force_encryption",0644,&d.dp_debug.force_encryption,d.root);expect_scalar("skip_uevent",0644,&d.dp_debug.skip_uevent,d.root);expect_scalar("force_multi_func",0644,&hpd.force_multi_func,d.root);expect_scalar("dsc_feature_enable",0644,&parser.dsc_feature_enable,d.root);expect_scalar("fec_feature_enable",0644,&parser.fec_feature_enable,d.root);expect_scalar("ssc_enable",0644,&pll.ssc_en,d.root);expect_scalar("widebus_mode",0644,&parser.has_widebus,d.root);expect_scalar("connect_notification_delay_ms",0644,&d.dp_debug.connect_notification_delay_ms,d.root);expect_scalar("disconnect_delay_ms",0644,&d.dp_debug.disconnect_delay_ms,d.root);CHECK(d.dp_debug.connect_notification_delay_ms==DEFAULT_CONNECT_NOTIFICATION_DELAY_MS&&d.dp_debug.disconnect_delay_ms==DEFAULT_DISCONNECT_DELAY_MS);
 for(int failure=-1;failure<=13;failure++){reset();scalar_fail=failure;d.root=NULL;CHECK(dp_debug_init(&d.dp_debug)==0);CHECK(files==total_files&&scalars==13&&!removed&&d.root&&!d.root->removed);}
 for(int failure=1;failure<=total_files;failure++){reset();file_fail=failure;CHECK(dp_debug_init(&d.dp_debug)==-EACCES);CHECK(removed==1&&files==failure);}
 reset();dir_fail=1;CHECK(dp_debug_init(&d.dp_debug)==-EACCES&&removed==0&&!files&&!scalars);
 reset();backend_available=0;CHECK(dp_debug_init(&d.dp_debug)==-ENODEV&&!removed&&!files&&!scalars);
 reset();cfg_debugfs=0;d.root=(void*)1;CHECK(!dp_debug_init(&d.dp_debug)&&!d.root&&!creates);
}
static void ctrl_tests(void){struct dentry parent={.id=999,.is_dir=true};struct dsi_ctrl d={.name="ctrl"};reset();CHECK(!dsi_ctrl_debugfs_init(&d,&parent));CHECK(scalars==1&&files==3&&d.debugfs_root);expect_scalar("enable_cmd_dma_stats",0600,&d.enable_cmd_dma_stats,d.debugfs_root);
#ifdef REQUIRE_CONTROLLER_ROOT
 CHECK(!removed&&!d.debugfs_root->removed);
#else
 CHECK(removed==1&&d.debugfs_root->removed);puts("KNOWN_INHERITED_DEFECT controller success falls through to remove its directory; API-only patch preserves it");
#endif
 int inherited_remove=removed;reset();scalar_fail=-1;CHECK(!dsi_ctrl_debugfs_init(&d,&parent));CHECK(scalars==1&&files==3&&removed==inherited_remove);
 for(int i=1;i<=3;i++){reset();file_fail=i;d.debugfs_root=NULL;CHECK(dsi_ctrl_debugfs_init(&d,&parent)==-EACCES);CHECK(removed==1&&!d.debugfs_root);}
 reset();dir_fail=1;CHECK(dsi_ctrl_debugfs_init(&d,&parent)==-EACCES&&!removed);reset();CHECK(dsi_ctrl_debugfs_init(&d,NULL)==-EINVAL&&!creates);
}
static void display_tests(void){struct panel panel={0};struct msm_dsi_phy p0={.name="phy0"},p1={.name="phy1"};struct dsi_display d={.name="display",.display_type="secondary",.ctrl_count=2,.ctrl={{&p0},{&p1}},.panel=&panel};reset();CHECK(!dsi_display_debugfs_init(&d));CHECK(d.root&&!d.root->removed&&scalars==8&&files==5&&parser_calls==1&&!removed);expect_scalar("phy0_allow_phy_power_off",0600,&p0.allow_phy_power_off,d.root);expect_scalar("phy0_regulator_min_datarate_bps",0600,&p0.regulator_min_datarate_bps,d.root);expect_scalar("phy1_allow_phy_power_off",0600,&p1.allow_phy_power_off,d.root);expect_scalar("phy1_regulator_min_datarate_bps",0600,&p1.regulator_min_datarate_bps,d.root);expect_scalar("ulps_feature_enable",0600,&panel.ulps_feature_enabled,d.root);expect_scalar("ulps_suspend_feature_enable",0600,&panel.ulps_suspend_enabled,d.root);expect_scalar("ulps_status",0400,&d.ulps_enabled,d.root);expect_scalar("clk_gating_config",0600,&d.clk_gating_config,d.root);
 for(int failure=-1;failure<=8;failure++){reset();scalar_fail=failure;d.root=NULL;CHECK(!dsi_display_debugfs_init(&d));CHECK(scalars==8&&files==5&&parser_calls==1&&!removed&&d.root&&!d.root->removed);}
 for(int failure=1;failure<=5;failure++){reset();file_fail=failure;d.root=NULL;CHECK(dsi_display_debugfs_init(&d)==-EACCES);CHECK(removed==1&&!d.root&&!parser_calls);}
 reset();dir_fail=1;CHECK(dsi_display_debugfs_init(&d)==-EACCES&&!removed);reset();d.ctrl[1].phy=NULL;CHECK(!dsi_display_debugfs_init(&d)&&scalars==6);reset();d.ctrl[1].phy=&p1;p1.name=NULL;CHECK(!dsi_display_debugfs_init(&d)&&scalars==6);
}
static void wb_tests(void){struct dentry parent={.id=999,.is_dir=true};struct sde_encoder_phys_wb wb={.hw_wb=&parent,.wbdone_timeout=123};reset();CHECK(!sde_encoder_phys_wb_init_debugfs(&wb.base,&parent));expect_scalar("wbdone_timeout",0600,&wb.wbdone_timeout,&parent);CHECK(wb.wbdone_timeout==123&&!removed);reset();scalar_fail=-1;CHECK(!sde_encoder_phys_wb_init_debugfs(&wb.base,&parent)&&scalars==1&&!removed);reset();CHECK(sde_encoder_phys_wb_init_debugfs(&wb.base,NULL)==-EINVAL&&!creates);wb.hw_wb=NULL;reset();CHECK(sde_encoder_phys_wb_init_debugfs(&wb.base,&parent)==-EINVAL&&!creates);}
int main(void){dp_tests();ctrl_tests();display_tests();wb_tests();printf("PASS %d actual debugfs initializer scenarios; scalar attempts are best effort, not creation guarantees\n",scenarios);return 0;}

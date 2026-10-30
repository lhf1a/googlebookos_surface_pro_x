// SPDX-License-Identifier: GPL-2.0-only
/* Explicit power owner, ported from the verified private Iris1 sequence.
 * No devm and no firmware calls. Caller must prove PAS stopped before off.
 */
#ifndef SQ2_VENUS_POWER_HOST_TEST
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#endif
#include "sq2-venus-power.h"
struct sq2_venus_power {
 struct device *dev, *pd[3];
 struct clk *clk[7];
 struct reset_control *rst[4];
 struct icc_bulk_data icc[3];
 int npd, nclk, nrst, powered, enabled, states[4];
 bool voted, resets_changed, rate_changed, config_saved, ready;
 unsigned long rate;
 void __iomem *base;
 u32 cgc, config, version;
};
static const char * const clocks[] = {"axic", "axi0", "axi1", "ahb", "core", "vcodec", "cvp"};
static const char * const resets[] = {"axi", "core", "axi0", "axi1"};
#ifndef SQ2_VENUS_POWER_HOST_TEST
static int reset_status(struct device *dev, int index, struct reset_control *rst)
{
 static const u32 ids[] = {41, 0, 42, 43};
 static const u32 regs[] = {0xb02c, 0x850, 0xb024, 0xb028};
 struct of_phandle_args args;
 struct platform_device *provider;
 struct regmap *map;
 struct resource res;
 unsigned int value;
 int ret = reset_control_status(rst);
 if (ret != -ENOTSUPP)
  return ret;
 ret = of_parse_phandle_with_args(dev->of_node, "resets", "#reset-cells", index, &args);
 if (ret)
  return ret;
 if (args.args_count != 1 || args.args[0] != ids[index] ||
     !of_device_is_compatible(args.np, index == 1 ? "qcom,sm8150-videocc" : "qcom,gcc-sc8180x") ||
     of_address_to_resource(args.np, 0, &res) ||
     res.start != (index == 1 ? 0xab00000 : 0x100000) ||
     resource_size(&res) < regs[index] + 4) {
  of_node_put(args.np);
  return -EINVAL;
 }
 provider = of_find_device_by_node(args.np);
 of_node_put(args.np);
 if (!provider)
  return -EPROBE_DEFER;
 map = dev_get_regmap(&provider->dev, NULL);
 if (!provider->dev.driver || !map)
  ret = -ENODEV;
 else {
  ret = regmap_read(map, regs[index], &value);
  if (!ret) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_status_regmap index=%d reg=%x raw=%08x\n",
           index, regs[index], value);
   ret = !!(value & BIT(2));
  }
 }
 put_device(&provider->dev);
 return ret;
}

#endif
static int pulse(struct sq2_venus_power *p)
{
 int i, ret;
 for (i=0;i<4;i++) {
  ret=reset_control_assert(p->rst[i]);
  if(ret) return ret;
 }
 usleep_range(150,250);
 for(i=0;i<4;i++) {
  ret=reset_control_deassert(p->rst[i]);
  if(ret) return ret;
 }
 return 0;
}
bool sq2_venus_power_ready(struct sq2_venus_power *p)
{
 return p && p->ready;
}
int sq2_venus_power_on(struct device *dev, void __iomem *base, struct sq2_venus_power **out)
{
 struct sq2_venus_power *p;
 int i,ret;
 if (!out || *out || !base) return -EINVAL;
 p=kzalloc(sizeof(*p),GFP_KERNEL);
 if(!p) return -ENOMEM;
 *out=p; p->dev=dev; p->base=base;
 p->icc[0]=(struct icc_bulk_data){.name="cpu-cfg",.avg_bw=1000,.peak_bw=1000};
 p->icc[1]=(struct icc_bulk_data){.name="video-mem",.peak_bw=304000};
 p->icc[2]=(struct icc_bulk_data){.name="firmware-mem",.peak_bw=304000};
 ret=of_icc_bulk_get(dev,3,p->icc);
 if(ret) return ret;
 p->voted=true;
 for(i=0;i<3;i++) if(!p->icc[i].path) return -ENODEV;
 ret=icc_bulk_set_bw(3,p->icc);
 if(ret) return ret;
 for(i=0;i<3;i++) {
  p->pd[i]=dev_pm_domain_attach_by_id(dev,i);
  if(IS_ERR_OR_NULL(p->pd[i])) return p->pd[i] ? PTR_ERR(p->pd[i]) : -ENODEV;
  p->npd++;
 }
 for(i=0;i<7;i++) {
  p->clk[i]=clk_get(dev,clocks[i]);
  if(IS_ERR(p->clk[i])) return PTR_ERR(p->clk[i]);
  p->nclk++;
 }
 for(i=0;i<4;i++) {
  p->rst[i]=reset_control_get_exclusive(dev,resets[i]);
  if(IS_ERR(p->rst[i])) return PTR_ERR(p->rst[i]);
  p->nrst++;
  ret=reset_status(dev,i,p->rst[i]);
  if(ret<0) return ret;
  p->states[i]=ret;
 }
 for(i=0;i<3;i++) {
  ret=pm_runtime_resume_and_get(p->pd[i]);
  if(ret<0) return ret;
  p->powered++;
 }
 p->resets_changed=true;
 ret=pulse(p);
 if(ret) return ret;
 for(i=0;i<7;i++) {
  ret=clk_prepare_enable(p->clk[i]);
  if(ret) return ret;
  p->enabled++;
 }
 p->rate=clk_get_rate(p->clk[4]);
 p->rate_changed=true;
 ret=clk_set_rate(p->clk[4],200000000);
 if(ret) return ret;
 if(clk_get_rate(p->clk[4])<199999000 || clk_get_rate(p->clk[4])>200001000) return -ERANGE;
 p->version=readl(base+0xe0000);
 if(p->version!=0x5010002f) return -ENODEV;
 p->cgc=readl(base+0xe2010); p->config=readl(base+0xe2000);
 p->config_saved=true;
 p->ready=true;
 return 0;
}
int sq2_venus_power_off(struct sq2_venus_power **owner, bool firmware_attempted)
{
 struct sq2_venus_power *p;
 int i,ret,first=0;
 if(!owner) return -EINVAL;
 p=*owner;
 if(!p) return 0;
 p->ready=false;
 if(p->config_saved) {
  writel(p->config,p->base+0xe2000); writel(p->cgc,p->base+0xe2010);
  p->config_saved=false;
 }
 if(p->rate_changed) {
  ret=clk_set_rate(p->clk[4],p->rate);
  if(ret) return ret; /* keep powered references; do not free partial state */
  p->rate_changed=false;
 }
 while(p->enabled) clk_disable_unprepare(p->clk[--p->enabled]);
 if(firmware_attempted && p->resets_changed && (p->version & ~0xffffU)==0x50100000) {
  ret=pulse(p);
  if(ret) return ret;
 }
 if(p->resets_changed) {
  for(i=0;i<4;i++) {
   ret=p->states[i] ? reset_control_assert(p->rst[i]) : reset_control_deassert(p->rst[i]);
   if(ret) return ret;
  }
  p->resets_changed=false;
 }
 while(p->powered) {
  ret=pm_runtime_put_sync(p->pd[--p->powered]);
  if(ret<0 && !first) first=ret;
 }
 while(p->nclk) clk_put(p->clk[--p->nclk]);
 while(p->nrst) reset_control_put(p->rst[--p->nrst]);
 while(p->npd) dev_pm_domain_detach(p->pd[--p->npd],false);
 if(p->voted) {
  for(i=2;i>=0;i--) {
   ret=icc_set_bw(p->icc[i].path,0,0);
   if(ret && !first) first=ret;
  }
  icc_bulk_put(3,p->icc);
 }
 kfree(p); *owner=NULL;
 return first;
}

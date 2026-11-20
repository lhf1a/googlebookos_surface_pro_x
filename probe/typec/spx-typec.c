// SPDX-License-Identifier: GPL-2.0-only
/* Surface Pro X USC -> QMP mux / DRM HPD. No PMIC GLINK or PD policy owner.
 * Event layout: linux-surface/surface-pro-x#35. PHY/bridge API follows
 * drivers/soc/qcom/pmic_glink_altmode.c (Linaro, GPL-2.0-only).
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/auxiliary_bus.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>
#include <linux/surface_aggregator/controller.h>
#include <linux/usb/typec.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/role.h>
#include <drm/bridge/aux-bridge.h>
/* Runtime connector mask supplied by initramfs from the selected boot disk.
 * Read-only for this boot: a mounted system/swap device must not be reset by
 * USC replay or a later role request. Other connectors remain unrestricted.
 */
static unsigned int boot_storage_ports;
module_param(boot_storage_ports, uint, 0444);
MODULE_PARM_DESC(boot_storage_ports, "Connectors carrying the mounted boot disk");
struct spx_port {
 struct typec_switch *sw;
 struct typec_mux *mux;
 struct auxiliary_device *bridge;
 struct typec_altmode alt;
 bool present;
 struct usb_role_switch *role_sw;
 enum usb_role requested_role;
 bool dp_active;
 bool hpd_asserted;
 bool state_valid;
 u8 state[11];
};
struct spx_typec {
 struct device *dev;
 struct ssam_controller *ctrl;
 struct ssam_event_notifier notifier;
 struct workqueue_struct *queue;
 struct mutex event_lock;
 bool events_enabled;
 bool storage_ports_set;
 bool routing_ready;
 bool suspended;
 struct spx_port ports[2];
};
struct spx_event {struct work_struct work;struct spx_typec *ctx;u8 bytes[11];};
static void put_switch(void *p){typec_switch_put(p);}
static void put_mux(void *p){typec_mux_put(p);}
static void put_role(void *p){usb_role_switch_put(p);}
static int spx_apply(struct spx_typec *ctx,const u8 *b,bool force_hpd)
{
 struct spx_port *p=&ctx->ports[b[4]];
 struct typec_mux_state state={};
 struct typec_displayport_data dp={};
 enum typec_orientation orientation;
 int pin,ret;
 bool hpd,irq_hpd;
 if(!p->present)return -ENODEV;
 if(b[5]>2 || b[6]>3 || b[9]>1 || b[10]>1)return -EPROTO;
 /* Intentionally retain the boot connector: no mux/role/HPD writes. */
 if(boot_storage_ports & BIT(b[4]))return 0;
 orientation=b[5]==0?TYPEC_ORIENTATION_NORMAL:b[5]==1?TYPEC_ORIENTATION_REVERSE:TYPEC_ORIENTATION_NONE;
 if(b[6]==2 || b[6]==3) {
  if(p->role_sw && p->requested_role!=USB_ROLE_HOST)return -EBUSY;
  /* SAM pin encodes C=3/D=4, with +6 for reversed cable. */
  pin=b[6]==2?DP_PIN_ASSIGN_C:DP_PIN_ASSIGN_D;
  if(b[5]==2 || b[8]!=(pin+1+(b[5]==1?6:0)))return -EPROTO;
  dp.status=DP_STATUS_ENABLED|(b[9]?DP_STATUS_HPD_STATE:0)|(b[10]?DP_STATUS_IRQ_HPD:0);
  dp.conf=DP_CONF_SET_PIN_ASSIGN(BIT(pin));
  state.alt=&p->alt;state.mode=TYPEC_MODAL_STATE(pin);state.data=&dp;
 } else {
  /* Withdraw HPD before disconnect/USB routing changes. */
  drm_aux_hpd_bridge_notify(&p->bridge->dev,connector_status_disconnected);
  p->hpd_asserted=false;
  state.mode=TYPEC_STATE_USB;
 }
 ret=typec_switch_set(p->sw,orientation);if(ret)return ret;
 ret=typec_mux_set(p->mux,&state);if(ret)return ret;
 if(p->role_sw && usb_role_switch_get_role(p->role_sw)!=p->requested_role) {
  ret=usb_role_switch_set_role(p->role_sw,p->requested_role);if(ret)return ret;
 }
 p->dp_active=b[6]==2 || b[6]==3;
 if(p->dp_active) {
  hpd=!!b[9];
  irq_hpd=!!b[10];
  if(hpd!=p->hpd_asserted || irq_hpd || force_hpd) {
   if(irq_hpd)
    dev_dbg(ctx->dev,"USC port=%u forwarding IRQ_HPD (hpd=%u)\n",b[4],hpd);
   drm_aux_hpd_bridge_notify(&p->bridge->dev,hpd?connector_status_connected:connector_status_disconnected);
   p->hpd_asserted=hpd;
  }
 }
 return 0;
}
/* SAM event registration is a firmware operation, not a passive listener.
 * Initramfs enables it before discovering/mounting storage. Cache/ACK state
 * until the boot connector mask and DRM consumers are ready. Never re-enable
 * the firmware event source merely to replay a state for a late consumer.
 */
static int spx_handle_state(struct spx_typec *ctx,const u8 *b)
{
 struct spx_port *p;
 int ret;
 if(b[4]>=ARRAY_SIZE(ctx->ports))return -EPROTO;
 p=&ctx->ports[b[4]];
 if(!p->present)return -ENODEV;
 if(b[5]>2 || b[6]>3 || b[9]>1 || b[10]>1)return -EPROTO;
 memcpy(p->state,b,sizeof(p->state));p->state_valid=true;
 if(!ctx->routing_ready || ctx->suspended)return 0;
 ret=spx_apply(ctx,b,false);
 if(!ret)p->state[10]=0;
 return ret;
}
static int spx_set_storage_ports(struct spx_typec *ctx,unsigned int mask)
{
 if(mask & ~GENMASK(1,0))return -EINVAL;
 if(ctx->storage_ports_set)return mask==boot_storage_ports?0:-EBUSY;
 if(ctx->routing_ready)return -EBUSY;
 boot_storage_ports=mask;ctx->storage_ports_set=true;
 return 0;
}
static int spx_activate_routing(struct spx_typec *ctx)
{
 int i,ret;
 if(!ctx->events_enabled || !ctx->storage_ports_set)return -EAGAIN;
 if(ctx->routing_ready)return 0;
 for(i=0;i<ARRAY_SIZE(ctx->ports);i++) {
  if(!ctx->ports[i].state_valid)continue;
  ret=spx_apply(ctx,ctx->ports[i].state,true);
  if(ret)return ret;
  ctx->ports[i].state[10]=0;
 }
 ctx->routing_ready=true;
 return 0;
}
static void spx_process(struct work_struct *work)
{
 struct spx_event *e=container_of(work,struct spx_event,work);
 struct spx_typec *ctx=e->ctx;
 __le32 ack[3]={cpu_to_le32(get_unaligned_le32(e->bytes)),cpu_to_le32(e->bytes[4]),0};
 struct ssam_request req={.target_category=0x1b,.target_id=1,.instance_id=1,.command_id=3,.flags=0,.length=sizeof(ack),.payload=(u8*)ack};
 int result,ret;
 mutex_lock(&ctx->event_lock);result=spx_handle_state(ctx,e->bytes);mutex_unlock(&ctx->event_lock);
 ack[2]=cpu_to_le32(result==0);
 ret=ssam_request_do_sync(ctx->ctrl,&req,NULL);
 /* Candidate-only state evidence, not a per-frame graphics logger. */
 dev_dbg(ctx->dev,"USC seq=%u port=%u orientation=%u mode=%u pin=%u hpd=%u irq=%u apply=%d ack=%d\n",get_unaligned_le32(e->bytes),e->bytes[4],e->bytes[5],e->bytes[6],e->bytes[8],e->bytes[9],e->bytes[10],result,ret);
 if(ret)dev_err(ctx->dev,"USC acknowledgement failed: %d\n",ret);
 if(result && result!=-ENODEV)dev_err(ctx->dev,"USC routing failed: %d\n",result);
 kfree(e);
}
static u32 spx_notify(struct ssam_event_notifier *nf,const struct ssam_event *ev)
{
 struct spx_typec *ctx=container_of(nf,struct spx_typec,notifier);
 struct spx_event *e;
 if(ev->target_category!=0x1b || ev->target_id!=1 || ev->command_id!=2 || ev->instance_id!=0)return 0;
 if(ev->length!=11 || ev->data[4]>=ARRAY_SIZE(ctx->ports)) {
  dev_err(ctx->dev,"malformed USC event length=%u\n",ev->length);return SSAM_NOTIF_HANDLED;
 }
 e=kzalloc(sizeof(*e),GFP_ATOMIC);
 if(!e){dev_err(ctx->dev,"USC allocation failed, event not ACKed\n");return SSAM_NOTIF_HANDLED;}
 e->ctx=ctx;memcpy(e->bytes,ev->data,11);INIT_WORK(&e->work,spx_process);queue_work(ctx->queue,&e->work);
 return SSAM_NOTIF_HANDLED;
}
/* Firmware notifications start before storage discovery. Routing starts
 * separately after the boot connector is pinned and MSM attaches HPD. */
static ssize_t events_enabled_show(struct device *dev,struct device_attribute *attr,char *buf)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);return sysfs_emit(buf,"%u\n",ctx->events_enabled);
}
static ssize_t events_enabled_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);bool enable;int ret=kstrtobool(buf,&enable);
 if(ret)return ret;
 mutex_lock(&ctx->event_lock);
 if(!enable && ctx->storage_ports_set) {
  mutex_unlock(&ctx->event_lock);return -EBUSY;
 }
 if(enable!=ctx->events_enabled) {
  if(enable)ret=ssam_notifier_register(ctx->ctrl,&ctx->notifier);
  else ret=ssam_notifier_unregister(ctx->ctrl,&ctx->notifier);
  if(!ret)ctx->events_enabled=enable;
 }
 mutex_unlock(&ctx->event_lock);
 return ret?ret:count;
}
static DEVICE_ATTR_RW(events_enabled);

static ssize_t storage_ports_show(struct device *dev,struct device_attribute *attr,char *buf)
{
 return sysfs_emit(buf,"%u\n",boot_storage_ports);
}
static ssize_t storage_ports_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);unsigned int mask;int ret=kstrtouint(buf,0,&mask);
 if(ret)return ret;
 mutex_lock(&ctx->event_lock);ret=spx_set_storage_ports(ctx,mask);mutex_unlock(&ctx->event_lock);
 return ret?ret:count;
}
static DEVICE_ATTR_RW(storage_ports);
static ssize_t routing_ready_show(struct device *dev,struct device_attribute *attr,char *buf)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);return sysfs_emit(buf,"%u\n",ctx->routing_ready);
}
static ssize_t routing_ready_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);bool enable;int ret=kstrtobool(buf,&enable);
 if(ret)return ret;
 if(!enable)return -EINVAL;
 mutex_lock(&ctx->event_lock);ret=spx_activate_routing(ctx);mutex_unlock(&ctx->event_lock);
 return ret?ret:count;
}
static DEVICE_ATTR_RW(routing_ready);

static ssize_t lower_data_role_show(struct device *dev,struct device_attribute *attr,char *buf)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);struct spx_port *p=&ctx->ports[0];
 if(!p->role_sw)return -ENODEV;
 return sysfs_emit(buf,"%s\n",usb_role_string(usb_role_switch_get_role(p->role_sw)));
}
static ssize_t lower_data_role_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t count)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);struct spx_port *p=&ctx->ports[0];enum usb_role role;int ret;
 if(sysfs_streq(buf,"host"))role=USB_ROLE_HOST;
 else if(sysfs_streq(buf,"device"))role=USB_ROLE_DEVICE;
 else return -EINVAL;
 if(!p->role_sw)return -ENODEV;
 mutex_lock(&ctx->event_lock);
 if(boot_storage_ports & BIT(0))
  ret=usb_role_switch_get_role(p->role_sw)==role?0:-EBUSY;
 else if(p->dp_active && role!=USB_ROLE_HOST)ret=-EBUSY;
 else {ret=usb_role_switch_set_role(p->role_sw,role);if(!ret)p->requested_role=role;}
 mutex_unlock(&ctx->event_lock);return ret?ret:count;
}
static DEVICE_ATTR_RW(lower_data_role);
static struct attribute *spx_attrs[]={&dev_attr_events_enabled.attr,&dev_attr_storage_ports.attr,&dev_attr_routing_ready.attr,&dev_attr_lower_data_role.attr,NULL};
static const struct attribute_group spx_group={.attrs=spx_attrs};
static int spx_probe(struct platform_device *pdev)
{
 struct device *dev=&pdev->dev;
 struct spx_typec *ctx;
 int ret;
 ctx=devm_kzalloc(dev,sizeof(*ctx),GFP_KERNEL);if(!ctx)return -ENOMEM;
 ctx->dev=dev;ctx->ctrl=ssam_client_bind(dev);if(IS_ERR(ctx->ctrl))return PTR_ERR(ctx->ctrl);
 device_for_each_child_node_scoped(dev,node) {
  struct spx_port *p;u32 id;
  ret=fwnode_property_read_u32(node,"reg",&id);if(ret)return ret;
  if(id>=ARRAY_SIZE(ctx->ports) || ctx->ports[id].present)return -EINVAL;
  p=&ctx->ports[id];
  p->requested_role=USB_ROLE_HOST;
  if(id==0) {
   p->role_sw=fwnode_usb_role_switch_get(node);
   if(IS_ERR(p->role_sw))return PTR_ERR(p->role_sw);
   if(!p->role_sw)return -ENODEV;
   ret=devm_add_action_or_reset(dev,put_role,p->role_sw);if(ret)return ret;
  }
  p->sw=fwnode_typec_switch_get(node);if(IS_ERR(p->sw))return PTR_ERR(p->sw);if(!p->sw)return -ENODEV;
  ret=devm_add_action_or_reset(dev,put_switch,p->sw);if(ret)return ret;
  p->mux=fwnode_typec_mux_get(node);if(IS_ERR(p->mux))return PTR_ERR(p->mux);if(!p->mux)return -ENODEV;
  ret=devm_add_action_or_reset(dev,put_mux,p->mux);if(ret)return ret;
  p->bridge=devm_drm_dp_hpd_bridge_alloc(dev,to_of_node(node));if(IS_ERR(p->bridge))return PTR_ERR(p->bridge);
  ret=devm_drm_dp_hpd_bridge_add(dev,p->bridge);if(ret)return ret;
  p->alt.svid=USB_TYPEC_DP_SID;p->alt.mode=USB_TYPEC_DP_MODE;p->alt.active=1;p->present=true;
 }
 /* Non-freezable so SAM USC events are still cached and ACKed across s2idle. */
 ctx->queue=alloc_ordered_workqueue("spx-typec",WQ_MEM_RECLAIM);if(!ctx->queue)return -ENOMEM;
 ctx->notifier.base.fn=spx_notify;ctx->notifier.base.priority=1;
 ctx->notifier.event.reg=SSAM_EVENT_REGISTRY_SAM;
 ctx->notifier.event.id.target_category=0x1b;ctx->notifier.event.id.instance=1;
 ctx->notifier.event.mask=SSAM_EVENT_MASK_NONE;ctx->notifier.event.flags=SSAM_EVENT_SEQUENCED;
 mutex_init(&ctx->event_lock);platform_set_drvdata(pdev,ctx);
 ret=devm_device_add_group(dev,&spx_group);
 if(ret){destroy_workqueue(ctx->queue);return ret;}
 return 0;
}
static void spx_remove(struct platform_device *pdev)
{
 struct spx_typec *ctx=platform_get_drvdata(pdev);
 if(ctx->events_enabled)ssam_notifier_unregister(ctx->ctrl,&ctx->notifier);
 destroy_workqueue(ctx->queue);
 for(int i=0;i<ARRAY_SIZE(ctx->ports);i++)if(ctx->ports[i].present)
  drm_aux_hpd_bridge_notify(&ctx->ports[i].bridge->dev,connector_status_disconnected);
}
static int spx_suspend(struct device *dev)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);
 mutex_lock(&ctx->event_lock);
 ctx->suspended=true;
 mutex_unlock(&ctx->event_lock);
 flush_workqueue(ctx->queue);
 return 0;
}
static int spx_resume(struct device *dev)
{
 /* Keep hardware applies deferred until spx_complete() after SSAM display_on
  * and DRM resume; spx_process() still caches and ACKs SAM USC events now. */
 return 0;
}
static void spx_complete(struct device *dev)
{
 struct spx_typec *ctx=dev_get_drvdata(dev);
 int i,ret=0;
 mutex_lock(&ctx->event_lock);
 ctx->suspended=false;
 if(ctx->routing_ready) {
  for(i=0;i<ARRAY_SIZE(ctx->ports);i++) {
   int err;
   if(!ctx->ports[i].present || !ctx->ports[i].state_valid)continue;
   err=spx_apply(ctx,ctx->ports[i].state,true);
   if(!err)ctx->ports[i].state[10]=0;
   else if(!ret)ret=err;
  }
 }
 mutex_unlock(&ctx->event_lock);
 if(ret)dev_err(ctx->dev,"USC resume replay failed: %d\n",ret);
}
static const struct dev_pm_ops spx_pm_ops={
 SYSTEM_SLEEP_PM_OPS(spx_suspend,spx_resume)
 .complete=pm_sleep_ptr(spx_complete),
};
static const struct of_device_id spx_match[]={ {.compatible="microsoft,surface-pro-x-typec"},{} };
MODULE_DEVICE_TABLE(of,spx_match);
static struct platform_driver spx_driver={.probe=spx_probe,.remove=spx_remove,.driver={.name="spx-typec",.of_match_table=spx_match,.pm=pm_sleep_ptr(&spx_pm_ops)}};
module_platform_driver(spx_driver);
MODULE_LICENSE("GPL");MODULE_DESCRIPTION("Surface Pro X SAM USC to native QMP mux and DisplayPort HPD");

MODULE_INFO(spx_boot_handoff,"1");

// SPDX-License-Identifier: GPL-2.0-only
/* Read only the three ACPI-declared chip IDs. No sensor mode table or streaming.
 * Probe each documented CCI0/1 master once to establish physical routing.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

struct sensor {
 const char *name;
 u16 address;
 u32 id;
 unsigned int bytes, supplies, index;
};
static const struct sensor front = {"ov5693", 0x36, 0x5690, 2, 1, 0};
static const struct sensor rear = {"ov13858", 0x10, 0x00d855, 3, 3, 1};
static const struct sensor ir = {"ov7251", 0x60, 0x7750, 2, 1, 2};
static bool allow;
module_param(allow, bool, 0400);
static int results[3] = {-EINPROGRESS, -EINPROGRESS, -EINPROGRESS};
module_param_array(results, int, NULL, 0444);

static int identify(struct platform_device *pdev)
{
 struct device *dev = &pdev->dev;
 const struct sensor *s = device_get_match_data(dev);
 static const struct regulator_bulk_data supply_config[3] = {
  {.supply = "iovdd"}, {.supply = "avdd"}, {.supply = "dvdd"}};
 struct regulator_bulk_data *supplies;
 struct gpio_desc *reset;
 struct clk *clk;
 struct i2c_adapter *adapters[4] = {};
 unsigned long old_rate;
 int ret, i, mask = 0, off, enabled = 0;
 bool clocked = false;
 if (!s)
  return -EINVAL;
 /* Resolve all buses before powering a sensor; late providers may defer. */
 for (i = 0; i < 4; i++) {
  struct device_node *np;
  char path[80];
  snprintf(path, sizeof(path), "/soc@0/cci@%x/i2c-bus@%d",
           0xac4a000 + (i / 2) * 0x1000, i % 2);
  np = of_find_node_by_path(path);
  if (!np) { ret = -ENODEV; goto put; }
  adapters[i] = of_find_i2c_adapter_by_node(np);
  of_node_put(np);
  if (!adapters[i]) { ret = -EPROBE_DEFER; goto put; }
 }
 reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
 if (IS_ERR(reset)) { ret = PTR_ERR(reset); goto put; }
 clk = devm_clk_get(dev, "xclk");
 if (IS_ERR(clk)) { ret = PTR_ERR(clk); goto put; }
 ret = devm_regulator_bulk_get_const(dev, s->supplies, supply_config, &supplies);
 if (ret) goto put;
 old_rate = clk_get_rate(clk);
 ret = clk_set_rate(clk, 19200000);
 if (ret) goto put;
 ret = pm_runtime_set_active(dev);
 if (ret) goto restore;
 pm_runtime_get_noresume(dev);
 pm_runtime_enable(dev);
 for (i = 0; i < s->supplies; i++) {
  ret = regulator_enable(supplies[i].consumer);
  if (ret) goto poweroff;
  enabled++;
 }
 usleep_range(1000, 1500);
 ret = clk_prepare_enable(clk);
 if (ret) goto poweroff;
 clocked = true;
 usleep_range(1000, 1500);
 gpiod_set_value_cansleep(reset, 0);
 msleep(10);
 dev_info(dev, "SPX_CAMERA_ID sensor=%s power_on xclk=%lu\n", s->name, clk_get_rate(clk));
 for (i = 0; i < 4; i++) {
  u8 reg[] = {0x30, 0x0a}, value[3] = {};
  u32 id = 0;
  unsigned int j;
  struct i2c_msg msgs[] = {
   {.addr = s->address, .len = sizeof(reg), .buf = reg},
   {.addr = s->address, .flags = I2C_M_RD, .len = s->bytes, .buf = value}};
  ret = i2c_transfer(adapters[i], msgs, ARRAY_SIZE(msgs));
  for (j = 0; j < s->bytes; j++) id = (id << 8) | value[j];
  dev_info(dev, "SPX_CAMERA_ID sensor=%s cci=%d master=%d addr=0x%02x ret=%d id=0x%06x expected=0x%06x\n",
           s->name, i/2, i%2, s->address, ret, id, s->id);
  if (ret == ARRAY_SIZE(msgs) && id == s->id) mask |= BIT(i);
 }
 results[s->index] = mask ? mask : -ENODEV;
 ret = 0;
poweroff:
 gpiod_set_value_cansleep(reset, 1);
 usleep_range(1000, 1500);
 if (clocked) clk_disable_unprepare(clk);
 while (enabled) {
  off = regulator_disable(supplies[--enabled].consumer);
  if (off) dev_err(dev, "supply disable failed: %d\n", off);
 }
 off = pm_runtime_put_sync_suspend(dev);
 if (off < 0) dev_err(dev, "power-domain suspend failed: %d\n", off);
 if (ret) pm_runtime_disable(dev);
 dev_info(dev, "SPX_CAMERA_ID sensor=%s power_off result=%d ret=%d\n",
          s->name, results[s->index], ret);
restore:
 if (old_rate && old_rate != 19200000) {
  off = clk_set_rate(clk, old_rate);
  if (off) dev_warn(dev, "xclk restore failed: %d\n", off);
 }
put:
 for (i = 0; i < ARRAY_SIZE(adapters); i++)
  if (adapters[i]) i2c_put_adapter(adapters[i]);
 return ret ? dev_err_probe(dev, ret, "identification setup failed\n") : 0;
}

static void remove_probe(struct platform_device *pdev)
{
 pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id matches[] = {
 {.compatible = "microsoft,spx-camera-id-front", .data = &front},
 {.compatible = "microsoft,spx-camera-id-rear", .data = &rear},
 {.compatible = "microsoft,spx-camera-id-ir", .data = &ir},
 {}
};
MODULE_DEVICE_TABLE(of, matches);
static struct platform_driver driver = {
 .probe = identify, .remove = remove_probe,
 .driver = {.name = "spx-camera-id", .of_match_table = matches}
};
static int __init start(void)
{
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 return platform_driver_register(&driver);
}
static void __exit stop(void) { platform_driver_unregister(&driver); }
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPX front/rear/IR one-shot sensor chip ID test");

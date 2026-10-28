// SPDX-License-Identifier: GPL-2.0-only
/* Read-only Android healthd compatibility for Surface BAT1 energy units.
 * Charge values are estimates at design voltage, not measured coulomb counts.
 * Keep the original power_supply, events, charging control and driver intact.
 */
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/math64.h>
#include <linux/sysfs.h>

static struct power_supply *battery;

static int read_positive(enum power_supply_property p, int *value)
{
    union power_supply_propval v;
    int ret = power_supply_get_property(battery, p, &v);
    if (ret) return ret;
    if (v.intval < 0) return -ENODATA;
    *value = v.intval;
    return 0;
}

static ssize_t charge_value(char *buf, enum power_supply_property p)
{
    int energy, voltage, ret;
    ret = read_positive(p, &energy);
    if (ret) return ret;
    ret = read_positive(POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN, &voltage);
    if (ret) return ret;
    if (!voltage) return -ENODATA;
    return sysfs_emit(buf, "%lld\n", div_s64((s64)energy * 1000000, voltage));
}

static ssize_t charge_full_show(struct device *dev, struct device_attribute *attr, char *buf)
{ return charge_value(buf, POWER_SUPPLY_PROP_ENERGY_FULL); }
static ssize_t charge_full_design_show(struct device *dev, struct device_attribute *attr, char *buf)
{ return charge_value(buf, POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN); }
static ssize_t charge_counter_show(struct device *dev, struct device_attribute *attr, char *buf)
{ return charge_value(buf, POWER_SUPPLY_PROP_ENERGY_NOW); }
static ssize_t charge_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{ return charge_value(buf, POWER_SUPPLY_PROP_ENERGY_NOW); }

static ssize_t current_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int power, voltage, status, ret;
    s64 microamps;
    ret = read_positive(POWER_SUPPLY_PROP_POWER_NOW, &power);
    if (ret) return ret;
    ret = read_positive(POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
    if (ret) return ret;
    ret = read_positive(POWER_SUPPLY_PROP_STATUS, &status);
    if (ret) return ret;
    if (!voltage) return -ENODATA;
    microamps = div_s64((s64)power * 1000000, voltage);
    if (status == POWER_SUPPLY_STATUS_DISCHARGING) microamps = -microamps;
    else if (status != POWER_SUPPLY_STATUS_CHARGING && microamps) return -ENODATA;
    return sysfs_emit(buf, "%lld\n", microamps);
}

static ssize_t state_of_health_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int full, design, ret;
    s64 percent;
    ret = read_positive(POWER_SUPPLY_PROP_ENERGY_FULL, &full);
    if (ret) return ret;
    ret = read_positive(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, &design);
    if (ret) return ret;
    if (!design) return -ENODATA;
    percent = div_s64((s64)full * 100, design);
    return sysfs_emit(buf, "%lld\n", min_t(s64, percent, 100));
}

static DEVICE_ATTR_RO(charge_full);
static DEVICE_ATTR_RO(charge_full_design);
static DEVICE_ATTR_RO(charge_counter);
static DEVICE_ATTR_RO(charge_now);
static DEVICE_ATTR_RO(current_now);
static DEVICE_ATTR_RO(state_of_health);
static struct device_attribute *attrs[] = {
    &dev_attr_charge_full, &dev_attr_charge_full_design, &dev_attr_charge_counter,
    &dev_attr_charge_now, &dev_attr_current_now, &dev_attr_state_of_health,
};

static int __init sq2_battery_compat_init(void)
{
    int i, ret, value;
    battery = power_supply_get_by_name("BAT1");
    if (!battery) return -ENODEV;
    /* Reject supplies without the energy/design-voltage interface. */
    ret = read_positive(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, &value);
    if (ret || !value) { ret = ret ?: -ENODATA; goto fail; }
    ret = read_positive(POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN, &value);
    if (ret || !value) { ret = ret ?: -ENODATA; goto fail; }
    for (i = 0; i < ARRAY_SIZE(attrs); i++) {
        ret = device_create_file(&battery->dev, attrs[i]);
        if (ret) {
            while (i--) device_remove_file(&battery->dev, attrs[i]);
            goto fail;
        }
    }
    pr_info("sq2_battery_compat: read-only energy-to-charge attributes ready\n");
    return 0;
fail:
    power_supply_put(battery);
    return ret;
}

static void __exit sq2_battery_compat_exit(void)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(attrs); i++) device_remove_file(&battery->dev, attrs[i]);
    power_supply_put(battery);
}
module_init(sq2_battery_compat_init);
module_exit(sq2_battery_compat_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 read-only Android battery unit compatibility");

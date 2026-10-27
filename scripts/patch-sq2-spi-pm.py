#!/usr/bin/env python3
"""Add system-sleep quiesce/reset to the older SQ2 SPI-HID transport."""
from pathlib import Path
import sys

base = Path(sys.argv[1])
c = base / 'spi-hid-core.c'
h = base / 'spi-hid-core.h'
s = c.read_text()
if 'SQ2_SPI_PM_V1' in s:
    raise SystemExit(0)
hs = h.read_text()
assert '\tbool irq_enabled;' in hs
hs = hs.replace('\tbool irq_enabled;', '\tbool irq_enabled;\n\tbool pm_suspended;\n\tbool pm_restore;\n\tu32 pm_resumes;')
assert s.count('if (!shid->powered)\n\t\tgoto out;') == 2
s = s.replace('if (!shid->powered)\n\t\tgoto out;', 'if (!shid->powered || READ_ONCE(shid->pm_suspended))\n\t\tgoto out;')
s = s.replace('\ttrace_spi_hid_dev_irq(shid, irq);', '\tif (READ_ONCE(shid->pm_suspended)) {\n\t\tspin_unlock(&shid->input_lock);\n\t\treturn IRQ_HANDLED;\n\t}\n\ttrace_spi_hid_dev_irq(shid, irq);')
s = s.replace('\tret = spi_hid_error_handler(shid);\n\tif (ret)\n\t\tdev_err(dev, "%s: error handler failed\\n", __func__);\n}', '\tif (READ_ONCE(shid->pm_suspended))\n\t\treturn;\n\tret = spi_hid_error_handler(shid);\n\tif (ret)\n\t\tdev_err(dev, "%s: error handler failed\\n", __func__);\n}', 1)
s = s.replace('\ttrace_spi_hid_reset_work(shid);', '\tif (READ_ONCE(shid->pm_suspended))\n\t\treturn;\n\ttrace_spi_hid_reset_work(shid);', 1)
pm = '''
/* SQ2_SPI_PM_V1: stop the producer before its SPI parent suspends.
 * Keep the HID object and its open handles; reset the hardware on resume.
 * SQ2 wakeup is provided by the power key and SSAM, not this touchscreen.
 */
static int spi_hid_pm_suspend(struct device *dev)
{
	struct spi_hid *shid = dev_get_drvdata(dev);
	unsigned long flags;
	int ret = 0;

	shid->pm_restore = shid->hid && shid->power_state != SPI_HID_POWER_MODE_OFF;
	spin_lock_irqsave(&shid->input_lock, flags);
	WRITE_ONCE(shid->pm_suspended, true);
	spin_unlock_irqrestore(&shid->input_lock, flags);
	if (shid->irq_enabled) {
		disable_irq(shid->spi->irq);
		shid->irq_enabled = false;
	}
	cancel_work_sync(&shid->error_work);
	cancel_work_sync(&shid->reset_work);
	cancel_work_sync(&shid->create_device_work);
	cancel_work_sync(&shid->refresh_device_work);
	mutex_lock(&shid->lock);
	if (shid->pm_restore) {
		shid->ready = false;
		sysfs_notify(&dev->kobj, NULL, "ready");
		ret = spi_hid_power_down(shid);
		shid->power_state = SPI_HID_POWER_MODE_OFF;
	}
	mutex_unlock(&shid->lock);
	if (ret) {
		WRITE_ONCE(shid->pm_suspended, false);
		if (shid->pm_restore)
			spi_hid_ll_open(shid->hid);
	}
	return ret;
}

static int spi_hid_pm_resume(struct device *dev)
{
	struct spi_hid *shid = dev_get_drvdata(dev);
	int ret = 0;

	shid->input_transfer_pending = 0;
	shid->input_stage = SPI_HID_INPUT_STAGE_IDLE;
	shid->attempts = 0;
	WRITE_ONCE(shid->pm_suspended, false);
	if (shid->pm_restore && shid->hid) {
		ret = spi_hid_ll_open(shid->hid);
		if (!ret)
			shid->pm_resumes++;
	}
	return ret;
}
static SIMPLE_DEV_PM_OPS(spi_hid_pm_ops, spi_hid_pm_suspend, spi_hid_pm_resume);

static ssize_t resume_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\\n", shid->pm_resumes);
}
static DEVICE_ATTR_RO(resume_count);

'''
marker = 'static ssize_t ready_show('
assert s.count(marker) == 1
s = s.replace(marker, pm + marker)
s = s.replace('\t&dev_attr_ready.attr,', '\t&dev_attr_ready.attr,\n\t&dev_attr_resume_count.attr,')
s = s.replace('\t\t.name\t= "spi_hid",', '\t\t.name\t= "spi_hid",\n\t\t.pm\t= &spi_hid_pm_ops,')
h.write_text(hs)
c.write_text(s)
print('SPI-HID system sleep callbacks applied')

#!/usr/bin/env python3
"""Add system-sleep quiesce/reset to the older SQ2 SPI-HID transport."""
from pathlib import Path
import sys

def repair_pending_irq(source):
    # Clear stale latched falling edge while touch IC is still held in reset
    # (after power-up + INT pull-up settle, before reset deassert) on both
    # resume open and initial probe so enable_irq never reads 00 00 00 00.
    marker = 'SQ2_SPI_PM_CLEAR_PENDING_IRQ_V3'
    if marker in source:
        return source
    start = source.index('static int spi_hid_ll_open(')
    end = source.index('static void spi_hid_ll_close(', start)
    body = source[start:end]
    power_anchor = """	ret = spi_hid_power_up(shid);
	if (ret) {
		dev_err(dev, "%s: could not power up\\n", __func__);
		goto err1;
	}
"""
    assert body.count(power_anchor) == 1, 'SPI open power-up sequence changed'
    clear_call = (
        '\n\t/* ' + marker + ': discard stale edge latched during power-down/reset/power-up\n'
        '\t * while reset is still asserted and the INT line pull-up is active.\n'
        '\t */\n'
        '\tirq_set_irqchip_state(spi->irq, IRQCHIP_STATE_PENDING, false);\n'
    )
    body = body.replace(power_anchor, power_anchor + clear_call, 1)
    source = source[:start] + body + source[end:]

    pstart = source.index('static int spi_hid_probe(')
    pend = source.index('static void spi_hid_remove(', pstart)
    pbody = source[pstart:pend]
    old_probe = """	irqflags = irq_get_trigger_type(spi->irq) | IRQF_ONESHOT;
	ret = request_irq(spi->irq, spi_hid_dev_irq, irqflags,
			dev_name(&spi->dev), shid);
	if (ret)
		goto err1;
	else
		shid->irq_enabled = true;

	ret = spi_hid_assert_reset(shid);
	if (ret) {
		dev_err(dev, "%s: failed to assert reset\\n", __func__);
		goto err1;
	}

	ret = spi_hid_power_up(shid);
	if (ret) {
		dev_err(dev, "%s: could not power up\\n", __func__);
		goto err1;
	}

	ret = spi_hid_deassert_reset(shid);
	if (ret) {
		dev_err(dev, "%s: failed to deassert reset\\n", __func__);
		goto err1;
	}

	dev_err(dev, "%s: d3 -> %s\\n", __func__,
			spi_hid_power_mode_string(shid->power_state));

	return 0;

err1:
"""
    new_probe = """	irqflags = irq_get_trigger_type(spi->irq) | IRQF_ONESHOT | IRQF_NO_AUTOEN;
	ret = request_irq(spi->irq, spi_hid_dev_irq, irqflags,
			dev_name(&spi->dev), shid);
	if (ret)
		goto err1;

	ret = spi_hid_assert_reset(shid);
	if (ret) {
		dev_err(dev, "%s: failed to assert reset\\n", __func__);
		goto err_irq;
	}

	ret = spi_hid_power_up(shid);
	if (ret) {
		dev_err(dev, "%s: could not power up\\n", __func__);
		goto err_irq;
	}

	irq_set_irqchip_state(spi->irq, IRQCHIP_STATE_PENDING, false);

	ret = spi_hid_deassert_reset(shid);
	if (ret) {
		dev_err(dev, "%s: failed to deassert reset\\n", __func__);
		goto err_power;
	}

	enable_irq(spi->irq);
	shid->irq_enabled = true;

	dev_err(dev, "%s: d3 -> %s\\n", __func__,
			spi_hid_power_mode_string(shid->power_state));

	return 0;

err_power:
	spi_hid_power_down(shid);

err_irq:
	free_irq(spi->irq, shid);

err1:
"""
    assert pbody.count(old_probe) == 1, 'SPI probe IRQ/reset sequence changed'
    pbody = pbody.replace(old_probe, new_probe, 1)
    return source[:pstart] + pbody + source[pend:]


def repair_resume_input(source):
    # Opening the IRQ while reset is asserted can read an all-zero header.
    marker = 'SQ2_SPI_PM_INPUT_RECOVERY_V2'
    if marker in source:
        return repair_pending_irq(source)
    irq = """	if (!shid->irq_enabled) {
		enable_irq(spi->irq);
		shid->irq_enabled = true;
	} else {
		dev_err(dev, "%s called with interrupt already enabled\\n",
								__func__);
		shid->logic_error_count++;
		shid->logic_last_error = -EEXIST;
	}
"""
    # Work only inside open; similarly shaped IRQ handling elsewhere is unrelated.
    start = source.index('static int spi_hid_ll_open(')
    end = source.index('static void spi_hid_ll_close(', start)
    body = source[start:end]
    assert body.count(irq) == 1, 'SPI open IRQ sequence changed'
    body = body.replace(irq, '', 1)
    anchor = """	ret = spi_hid_deassert_reset(shid);
	if (ret) {
		dev_err(dev, "%s: failed to deassert reset\\n", __func__);
		goto err2;
	}
"""
    assert body.count(anchor) == 1, 'SPI open reset sequence changed'
    body = body.replace(anchor, anchor + '\n	/* ' + marker + ': receive only after power/reset settle. */\n' + irq, 1)
    source = source[:start] + body + source[end:]
    invalid = """		shid->bus_error_count++;
		shid->bus_last_error = ret;
		goto out;
"""
    start = source.index('static void spi_hid_input_header_complete(void *_shid)\n{')
    end = source.index('static int spi_hid_bus_input_report(', start)
    body = source[start:end]
    assert body.count(invalid) == 1, 'SPI invalid-header recovery changed'
    body = body.replace(invalid, invalid.replace('\t\tgoto out;', '\t\tschedule_work(&shid->error_work);\n\t\tgoto out;'), 1)
    return repair_pending_irq(source[:start] + body + source[end:])


def repair_ready_watchdog(source, header):
    # Recover if a short s2idle cycle resumes SPI-HID to D0 without ever
    # reaching FW ready state, and clean up the delayed work across close/PM.
    marker = 'SQ2_SPI_PM_READY_WATCHDOG_V4'
    if marker in source and 'ready_work;' in header:
        return source, header

    if 'ready_work;' not in header:
        h_anchor = '\tstruct work_struct error_work;\n'
        assert header.count(h_anchor) == 1, 'spi-hid-core.h work_struct anchor changed'
        header = header.replace(
            h_anchor,
            h_anchor + '\tu8 ready_retries;\n\tstruct delayed_work ready_work;\n',
            1,
        )

    if marker not in source:
        watchdog_fn = (
            '\n/* ' + marker + ': recover if D0 resume never reaches ready. */\n'
            'static void spi_hid_ready_watchdog_work(struct work_struct *work)\n'
            '{\n'
            '\tstruct spi_hid *shid = container_of(work, struct spi_hid,\n'
            '\t\t\t\t\t    ready_work.work);\n'
            '\tstruct device *dev = &shid->spi->dev;\n'
            '\tunsigned long flags;\n'
            '\tint ret;\n'
            '\n'
            '\tif (READ_ONCE(shid->pm_suspended) || shid->ready ||\n'
            '\t    shid->refresh_in_progress || !shid->powered ||\n'
            '\t    shid->power_state != SPI_HID_POWER_MODE_ACTIVE)\n'
            '\t\treturn;\n'
            '\n'
            '\tif (shid->ready_retries >= 3) {\n'
            '\t\tdev_warn(dev, "ready watchdog: exceeded retries without ready\\n");\n'
            '\t\treturn;\n'
            '\t}\n'
            '\n'
            '\tshid->ready_retries++;\n'
            '\tdev_warn(dev, "ready watchdog: retry %u/3\\n", shid->ready_retries);\n'
            '\n'
            '\tmutex_lock(&shid->lock);\n'
            '\tif (READ_ONCE(shid->pm_suspended) || shid->ready ||\n'
            '\t    shid->refresh_in_progress || !shid->powered ||\n'
            '\t    shid->power_state != SPI_HID_POWER_MODE_ACTIVE) {\n'
            '\t\tmutex_unlock(&shid->lock);\n'
            '\t\treturn;\n'
            '\t}\n'
            '\n'
            '\tif (shid->irq_enabled) {\n'
            '\t\tdisable_irq(shid->spi->irq);\n'
            '\t\tshid->irq_enabled = false;\n'
            '\t}\n'
            '\n'
            '\tshid->input_stage = SPI_HID_INPUT_STAGE_IDLE;\n'
            '\tshid->input_transfer_pending = 0;\n'
            '\n'
            '\tret = spi_hid_power_down(shid);\n'
            '\tif (ret) {\n'
            '\t\tdev_err(dev, "ready watchdog: power down failed: %d\\n", ret);\n'
            '\t\tgoto err_off;\n'
            '\t}\n'
            '\n'
            '\tmsleep(100);\n'
            '\tif (READ_ONCE(shid->pm_suspended))\n'
            '\t\tgoto err_off;\n'
            '\n'
            '\tret = spi_hid_assert_reset(shid);\n'
            '\tif (ret) {\n'
            '\t\tdev_err(dev, "ready watchdog: assert reset failed: %d\\n", ret);\n'
            '\t\tgoto err_off;\n'
            '\t}\n'
            '\n'
            '\tret = spi_hid_power_up(shid);\n'
            '\tif (ret) {\n'
            '\t\tdev_err(dev, "ready watchdog: power up failed: %d\\n", ret);\n'
            '\t\tgoto err_sleep;\n'
            '\t}\n'
            '\n'
            '\tif (READ_ONCE(shid->pm_suspended)) {\n'
            '\t\tspi_hid_power_down(shid);\n'
            '\t\tgoto err_off;\n'
            '\t}\n'
            '\n'
            '\tirq_set_irqchip_state(shid->spi->irq, IRQCHIP_STATE_PENDING, false);\n'
            '\n'
            '\tret = spi_hid_deassert_reset(shid);\n'
            '\tif (ret) {\n'
            '\t\tdev_err(dev, "ready watchdog: deassert reset failed: %d\\n", ret);\n'
            '\t\tspi_hid_power_down(shid);\n'
            '\t\tgoto err_off;\n'
            '\t}\n'
            '\n'
            '\tenable_irq(shid->spi->irq);\n'
            '\tshid->irq_enabled = true;\n'
            '\tmutex_unlock(&shid->lock);\n'
            '\n'
            '\tspin_lock_irqsave(&shid->input_lock, flags);\n'
            '\tif (!shid->pm_suspended && !shid->ready &&\n'
            '\t    shid->power_state == SPI_HID_POWER_MODE_ACTIVE)\n'
            '\t\tmod_delayed_work(system_wq, &shid->ready_work,\n'
            '\t\t\t\t msecs_to_jiffies(1500));\n'
            '\tspin_unlock_irqrestore(&shid->input_lock, flags);\n'
            '\treturn;\n'
            '\n'
            'err_sleep:\n'
            '\tpinctrl_select_state(shid->pinctrl, shid->pinctrl_sleep);\n'
            'err_off:\n'
            '\tspin_lock_irqsave(&shid->input_lock, flags);\n'
            '\tshid->power_state = SPI_HID_POWER_MODE_OFF;\n'
            '\tspin_unlock_irqrestore(&shid->input_lock, flags);\n'
            '\tmutex_unlock(&shid->lock);\n'
            '}\n\n'
        )
        pm_anchor = 'static int spi_hid_pm_suspend(struct device *dev)\n'
        assert source.count(pm_anchor) == 1, 'SPI pm_suspend anchor changed'
        source = source.replace(pm_anchor, watchdog_fn + pm_anchor, 1)

        r_start = source.index('static void spi_hid_refresh_device_work(')
        r_end = source.index('static void spi_hid_input_header_complete(', r_start)
        r_body = source[r_start:r_end]
        r_a = '\t\tshid->ready = true;\n\t\tsysfs_notify(&dev->kobj, NULL, "ready");\n\t\treturn;\n'
        r_b = '\tshid->refresh_in_progress = false;\n\tshid->ready = true;\n\tsysfs_notify(&dev->kobj, NULL, "ready");\n'
        assert r_body.count(r_a) == 1 and r_body.count(r_b) == 1, 'SPI refresh ready paths changed'
        r_body = r_body.replace(
            r_a,
            '\t\tshid->ready = true;\n\t\tshid->ready_retries = 0;\n\t\tcancel_delayed_work(&shid->ready_work);\n\t\tsysfs_notify(&dev->kobj, NULL, "ready");\n\t\treturn;\n',
            1,
        )
        r_body = r_body.replace(
            r_b,
            '\tshid->refresh_in_progress = false;\n\tshid->ready = true;\n\tshid->ready_retries = 0;\n\tcancel_delayed_work(&shid->ready_work);\n\tsysfs_notify(&dev->kobj, NULL, "ready");\n',
            1,
        )
        source = source[:r_start] + r_body + source[r_end:]

        o_start = source.index('static int spi_hid_ll_open(')
        o_end = source.index('static void spi_hid_ll_close(', o_start)
        o_body = source[o_start:o_end]
        o_anchor = '\tdev_err(dev, "%s: %s -> %s\\n", __func__,\n\t\t\tspi_hid_power_mode_string(prev_state),\n\t\t\tspi_hid_power_mode_string(shid->power_state));\n\n\treturn 0;\n'
        assert o_body.count(o_anchor) == 1, 'SPI ll_open return anchor changed'
        o_body = o_body.replace(
            o_anchor,
            '\tshid->ready_retries = 0;\n\tmod_delayed_work(system_wq, &shid->ready_work,\n\t\t\t msecs_to_jiffies(1500));\n\n' + o_anchor,
            1,
        )
        source = source[:o_start] + o_body + source[o_end:]

        c_start = source.index('static void spi_hid_ll_close(')
        c_end = source.index('static int spi_hid_ll_power(', c_start)
        c_body = source[c_start:c_end]
        c_var = '\tint ret;\n\n\tif (shid->refresh_in_progress'
        assert c_body.count(c_var) == 1, 'SPI ll_close locals changed'
        c_body = c_body.replace(c_var, '\tunsigned long flags;\n' + c_var, 1)
        c_anchor = '\tif (shid->refresh_in_progress || prev_state == SPI_HID_POWER_MODE_OFF)\n\t\treturn;\n'
        assert c_body.count(c_anchor) == 1, 'SPI ll_close anchor changed'
        c_body = c_body.replace(
            c_anchor,
            c_anchor
            + '\n\tspin_lock_irqsave(&shid->input_lock, flags);\n'
            + '\tshid->power_state = SPI_HID_POWER_MODE_OFF;\n'
            + '\tspin_unlock_irqrestore(&shid->input_lock, flags);\n'
            + '\tshid->ready_retries = 0;\n'
            + '\tcancel_delayed_work_sync(&shid->ready_work);\n',
            1,
        )
        source = source[:c_start] + c_body + source[c_end:]

        s_start = source.index('static int spi_hid_pm_suspend(')
        s_end = source.index('static int spi_hid_pm_resume(', s_start)
        s_body = source[s_start:s_end]
        s_anchor = '\tspin_unlock_irqrestore(&shid->input_lock, flags);\n\tif (shid->irq_enabled) {'
        assert s_body.count(s_anchor) == 1, 'SPI pm_suspend lock/irq anchor changed'
        s_body = s_body.replace(
            s_anchor,
            '\tspin_unlock_irqrestore(&shid->input_lock, flags);\n\tcancel_delayed_work_sync(&shid->ready_work);\n\tif (shid->irq_enabled) {',
            1,
        )
        source = source[:s_start] + s_body + source[s_end:]

        res_start = source.index('static int spi_hid_pm_resume(')
        res_end = source.index('static SIMPLE_DEV_PM_OPS(', res_start)
        res_body = source[res_start:res_end]
        res_anchor = '\tshid->attempts = 0;\n\tWRITE_ONCE(shid->pm_suspended, false);\n'
        assert res_body.count(res_anchor) == 1, 'SPI pm_resume anchor changed'
        res_body = res_body.replace(
            res_anchor,
            '\tshid->attempts = 0;\n\tshid->ready_retries = 0;\n\tWRITE_ONCE(shid->pm_suspended, false);\n',
            1,
        )
        source = source[:res_start] + res_body + source[res_end:]

        p_start = source.index('static int spi_hid_probe(')
        p_end = source.index('static void spi_hid_remove(', p_start)
        p_body = source[p_start:p_end]
        p_anchor = '\tINIT_WORK(&shid->error_work, spi_hid_error_work);\n'
        assert p_body.count(p_anchor) == 1, 'SPI probe INIT_WORK anchor changed'
        p_body = p_body.replace(
            p_anchor,
            p_anchor + '\tINIT_DELAYED_WORK(&shid->ready_work, spi_hid_ready_watchdog_work);\n',
            1,
        )
        source = source[:p_start] + p_body + source[p_end:]

        rm_start = source.index('static void spi_hid_remove(')
        rm_end = source.index('static const struct spi_device_id spi_hid_id_table[]', rm_start)
        rm_body = source[rm_start:rm_end]
        rm_anchor = '\tdev_info(dev, "%s\\n", __func__);\n\n\tspi_hid_power_down(shid);'
        assert rm_body.count(rm_anchor) == 1, 'SPI remove anchor changed'
        rm_body = rm_body.replace(
            rm_anchor,
            '\tdev_info(dev, "%s\\n", __func__);\n\n'
            '\tWRITE_ONCE(shid->pm_suspended, true);\n'
            '\tshid->power_state = SPI_HID_POWER_MODE_OFF;\n'
            '\tcancel_delayed_work_sync(&shid->ready_work);\n'
            '\tspi_hid_power_down(shid);',
            1,
        )
        source = source[:rm_start] + rm_body + source[rm_end:]

    return source, header


base = Path(sys.argv[1])
c = base / 'spi-hid-core.c'
h = base / 'spi-hid-core.h'
s = c.read_text()
hs = h.read_text()
if 'SQ2_SPI_PM_V1' in s:
    s = repair_resume_input(s)
    s, hs = repair_ready_watchdog(s, hs)
    h.write_text(hs)
    c.write_text(s)
    raise SystemExit(0)
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
s = repair_resume_input(s)
s, hs = repair_ready_watchdog(s, hs)
h.write_text(hs)
c.write_text(s)
print('SPI-HID system sleep callbacks applied')

// SPDX-License-Identifier: GPL-2.0
/* One bounded, read-only TLMM GPIO10 sample on the identified SQ2 lab boot.
 * Does not request the GPIO, change mux/direction, or write its value.
 * GPIO numbering is deliberately fixed and checked; fail on another layout.
 * Sampling is not an oscilloscope and cannot exclude pulses between reads.
 */
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/of.h>

static char result[4096];
module_param_string(result, result, sizeof(result), 0444);

static int __init sq2_hpd_sample_init(void)
{
	struct gpio_desc *desc;
	struct gpio_chip *chip;
	u64 start, now, previous, changed, maxgap = 0, longest[2] = {0};
	u64 edge_us[48];
	unsigned int count[2] = {0}, edges = 0, i, kept = 0;
	int value, last, pos;

	if (!of_machine_is_compatible("microsoft,surface-pro-x"))
		return -ENODEV;
	desc = gpio_to_desc(554);
	if (!desc || gpiod_cansleep(desc))
		return -ENODEV;
	chip = gpiod_to_chip(desc);
	if (!chip || chip->base != 544 || chip->ngpio != 191 ||
	    !chip->label || strcmp(chip->label, "3100000.pinctrl"))
		return -ENODEV;
	last = gpiod_get_raw_value(desc);
	if (last < 0)
		return last;
	start = previous = changed = ktime_get_mono_fast_ns();
	for (i = 0; i < 20000; i++) {
		value = gpiod_get_raw_value(desc);
		now = ktime_get_mono_fast_ns();
		if (value < 0)
			return value;
		count[!!value]++;
		maxgap = max(maxgap, now - previous);
		previous = now;
		if (value != last) {
			longest[!!last] = max(longest[!!last], now - changed);
			changed = now;
			if (kept < ARRAY_SIZE(edge_us))
				edge_us[kept++] = (now - start) / 1000;
			edges++;
			last = value;
		}
		udelay(50);
		if (!(i % 64))
			cond_resched();
	}
	longest[!!last] = max(longest[!!last], now - changed);
	pos = scnprintf(result, sizeof(result),
		"gpio=554 chip=3100000.pinctrl offset=10 samples=%u high=%u low=%u transitions=%u final=%d elapsed_us=%llu max_gap_us=%llu longest_high_us=%llu longest_low_us=%llu edges_us=",
		i, count[1], count[0], edges, last, (now - start) / 1000,
		maxgap / 1000, longest[1] / 1000, longest[0] / 1000);
	for (i = 0; i < kept; i++)
		pos += scnprintf(result + pos, sizeof(result) - pos, "%s%llu",
				 i ? "," : "", edge_us[i]);
	pr_info("SQ2_HPD_SAMPLE %s\n", result);
	return 0;
}

static void __exit sq2_hpd_sample_exit(void) { }
module_init(sq2_hpd_sample_init);
module_exit(sq2_hpd_sample_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded read-only SQ2 lab HPD input sampler");

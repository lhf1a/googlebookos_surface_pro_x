/* SPDX-License-Identifier: GPL-2.0-only */
/* Fixed SC8180X candidate from QcSkExt8180 a090/b4f8/b930. Call only with
 * power held and the checked firmware mapped at IOVA 0. Never read TZ ranges.
 */
#ifndef SQ2_VENUS_NO_TZ_H
#define SQ2_VENUS_NO_TZ_H
static int sq2_no_tz_reset_held(void __iomem *base)
{
 u32 value = readl(base + 0xe3000);
 return value != ~0U && (value & BIT(4)) ? 0 : -EBUSY;
}

static int sq2_no_tz_stop_cpu(void __iomem *base)
{
 u32 value = readl(base + 0xe3000);
 if (value == ~0U) return -EIO;
 writel(value | BIT(4), base + 0xe3000);
 udelay(1);
 return sq2_no_tz_reset_held(base);
}

static int sq2_no_tz_start_cpu(void __iomem *base, u32 span)
{
 u32 value;
 int ret;
 if (span != 0x500000) return -EINVAL;
 ret = sq2_no_tz_reset_held(base);
 if (ret) return ret;
 udelay(1);
 /* Unlike the intermediate Windows PIL state, Linux no-TZ must give the
  * firmware a usable final range before CTRL_INIT builds its own MMU table. */
 writel(0, base + 0xe1028);
 writel(span, base + 0xe102c);
 writel(0, base + 0xe1020);
 writel(span, base + 0xe1024);
 writel(span, base + 0xe1030);
 writel(span, base + 0xe1034);
 writel(1, base + 0xe1084);
 writel(3, base + 0xe3084);
 writel(0x540, base + 0xe3050);
 writel(0x10e, base + 0xe3054);
 writel(0x2e, base + 0xe3058);
 writel(0xed8, base + 0xe306c);
 writel(0x3138, base + 0xe3070);
 writel(0x640, base + 0xe3078);
 writel(readl(base + 0xe3088) & ~1U, base + 0xe3088);
 writel(readl(base + 0xe308c) & ~1U, base + 0xe308c);
 writel(readl(base + 0xe304c) | 1U, base + 0xe304c);
 writel(readl(base + 0xe2010) & ~BIT(4), base + 0xe2010);
 writel(readl(base + 0xe2000) & ~BIT(4), base + 0xe2000);
 value = readl(base + 0xe3000);
 if (value == ~0U || !(value & BIT(4))) return -EIO;
 writel(value & ~BIT(4), base + 0xe3000);
 value = readl(base + 0xe3000);
 return value == ~0U || (value & BIT(4)) ? -EIO : 0;
}
#endif

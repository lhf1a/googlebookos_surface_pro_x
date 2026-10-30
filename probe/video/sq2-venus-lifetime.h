/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_LIFETIME_H
#define SQ2_VENUS_LIFETIME_H
/* Shared policy used by the real resource owner and host fault tests. */
struct sq2_venus_lifetime {
 bool secure_touched, quarantined;
 int stop_error;
};
static int sq2_venus_lifetime_begin(struct sq2_venus_lifetime *s)
{
 if (s->quarantined || s->secure_touched)
  return -EBUSY;
 s->secure_touched = true;
 return 0;
}
static int sq2_venus_lifetime_end(struct sq2_venus_lifetime *s, int ret)
{
 if (s->quarantined)
  return s->stop_error;
 if (!s->secure_touched)
  return -EINVAL;
 if (ret) {
  s->quarantined = true;
  s->stop_error = ret;
  return ret;
 }
 s->secure_touched = false;
 return 0;
}
static bool sq2_venus_lifetime_releasable(const struct sq2_venus_lifetime *s)
{
 return !s->secure_touched && !s->quarantined;
}
#endif

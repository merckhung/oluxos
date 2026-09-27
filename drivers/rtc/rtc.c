/*
 * RTC core. The first registered clock sets the system time ("hctosys")
 * and is written back whenever the system time is set, so the time
 * survives power cycles. /dev/rtc0 speaks Linux's RTC ioctls (RTC_RD_TIME,
 * RTC_SET_TIME), so BusyBox hwclock works. Clocks run in UTC.
 */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/process.h>
#include <olux/rtc.h>
#include <olux/time.h>
#include <olux/uaccess.h>

#define RTC_MAJOR 253
#define RTC_RD_TIME 0x80247009u
#define RTC_SET_TIME 0x4024700au

struct rtc_time { /* struct tm layout */
  int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};

static const struct rtc_ops *rtc;
static void *rtc_priv;

/* days since 1970-01-01 <-> civil date (proleptic Gregorian), after H. Hinnant */
static s64 days_from_civil(s64 y, int m, int d) {
  y -= m <= 2;
  s64 era = (y >= 0 ? y : y - 399) / 400;
  s64 yoe = y - era * 400;
  s64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  s64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(s64 z, s64 *y, int *m, int *d) {
  z += 719468;
  s64 era = (z >= 0 ? z : z - 146096) / 146097;
  s64 doe = z - era * 146097;
  s64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  s64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  s64 mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = yoe + era * 400 + (*m <= 2);
}

static void secs_to_tm(u64 secs, struct rtc_time *t) {
  s64 days = (s64)(secs / 86400), rem = (s64)(secs % 86400), y;
  int m, d;
  civil_from_days(days, &y, &m, &d);
  t->tm_sec = (int)(rem % 60);
  t->tm_min = (int)(rem / 60 % 60);
  t->tm_hour = (int)(rem / 3600);
  t->tm_mday = d;
  t->tm_mon = m - 1;
  t->tm_year = (int)(y - 1900);
  t->tm_wday = (int)((days + 4) % 7); /* 1970-01-01 was a Thursday */
  t->tm_yday = (int)(days - days_from_civil(y, 1, 1));
  t->tm_isdst = 0;
}

static int tm_to_secs(const struct rtc_time *t, u64 *secs) {
  if (t->tm_sec < 0 || t->tm_sec > 60 || t->tm_min < 0 || t->tm_min > 59 || t->tm_hour < 0 || t->tm_hour > 23 ||
      t->tm_mday < 1 || t->tm_mday > 31 || t->tm_mon < 0 || t->tm_mon > 11 || t->tm_year < 70)
    return -EINVAL;
  s64 days = days_from_civil(t->tm_year + 1900LL, t->tm_mon + 1, t->tm_mday);
  *secs = (u64)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
  return 0;
}

static long rtc_ioctl(struct file *f, unsigned cmd, u64 arg) {
  (void)f;
  struct rtc_time t;
  u64 secs;
  int r;
  switch (cmd) {
    case RTC_RD_TIME:
      if ((r = rtc->read(rtc_priv, &secs))) return r;
      secs_to_tm(secs, &t);
      return copy_to_user(arg, &t, sizeof(t)) ? -EFAULT : 0;
    case RTC_SET_TIME:
      if (!capable_root()) return -EACCES;
      if (copy_from_user(&t, arg, sizeof(t))) return -EFAULT;
      if ((r = tm_to_secs(&t, &secs))) return r;
      return rtc->set ? rtc->set(rtc_priv, secs) : -EOPNOTSUPP;
    default:
      return -ENOTTY;
  }
}

static const struct file_operations rtc_fops = {.ioctl = rtc_ioctl};

int rtc_register(const struct rtc_ops *ops, void *priv) {
  if (rtc) return -EBUSY; /* the first clock wins */
  rtc = ops;
  rtc_priv = priv;
  register_chrdev(MKDEV(RTC_MAJOR, 0), "rtc0", &rtc_fops, NULL);
  devfs_create("rtc0", S_IFCHR | 0644, MKDEV(RTC_MAJOR, 0));
  devfs_create("rtc", S_IFCHR | 0644, MKDEV(RTC_MAJOR, 0));
  u64 secs;
  if (ops->read(priv, &secs) == 0 && secs > 946684800ULL) { /* after 2000: plausibly set */
    set_realtime_ns(secs * NSEC_PER_SEC);
    struct rtc_time t;
    secs_to_tm(secs, &t);
    pr_info("rtc0: %s: system time set to %04d-%02d-%02d %02d:%02d:%02d UTC\n", ops->name, t.tm_year + 1900,
            t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    pr_info("rtc0: %s: clock not set\n", ops->name);
  }
  return 0;
}

void rtc_set_time(u64 ns) {
  if (rtc && rtc->set) rtc->set(rtc_priv, ns / NSEC_PER_SEC);
}

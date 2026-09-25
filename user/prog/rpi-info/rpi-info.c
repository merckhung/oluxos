/* rpi-info - query the Raspberry Pi firmware through /dev/vcio.
 *
 *   rpi-info            board, memory, clocks, temperature, throttling
 *   rpi-info temp       SoC temperature (e.g. "temp=48.2'C")
 *   rpi-info throttled  under-voltage / throttling flags (hex)
 *   rpi-info clock NAME current rate of arm|core|uart|emmc|emmc2
 *   rpi-info tryboot    boot the tryboot partition (autoboot.txt) once, at the
 *                       next reboot (the A/B update trial boot)
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define IOCTL_MBOX_PROPERTY 0xC0086400u

static int fd;

/* One-tag request; returns the response length or -1. */
static int prop(uint32_t tag, uint32_t *val, uint32_t req_words, uint32_t buf_words) {
  uint32_t m[32] = {0};
  uint32_t n = buf_words > req_words ? buf_words : req_words;
  m[0] = (6 + n) * 4;
  m[2] = tag;
  m[3] = n * 4;
  m[4] = req_words * 4;
  memcpy(&m[5], val, req_words * 4);
  if (ioctl(fd, IOCTL_MBOX_PROPERTY, m) < 0 || m[1] != 0x80000000u || !(m[4] & 0x80000000u)) return -1;
  memcpy(val, &m[5], n * 4);
  return (int)(m[4] & 0x7fffffff);
}

static const struct {
  const char *name;
  uint32_t id;
} clocks[] = {{"emmc", 1}, {"uart", 2}, {"arm", 3}, {"core", 4}, {"emmc2", 12}};

static long clock_rate(uint32_t id) {
  uint32_t v[2] = {id, 0};
  return prop(0x00030002, v, 1, 2) < 0 ? -1 : (long)v[1];
}

static int temp(void) {
  uint32_t v[2] = {0, 0};
  if (prop(0x00030006, v, 1, 2) < 0) return -1;
  printf("temp=%u.%u'C\n", v[1] / 1000, v[1] % 1000 / 100);
  return 0;
}

int main(int argc, char **argv) {
  fd = open("/dev/vcio", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    perror("rpi-info: /dev/vcio");
    return 1;
  }
  if (argc > 1 && !strcmp(argv[1], "temp")) return temp() ? 1 : 0;
  if (argc > 1 && !strcmp(argv[1], "tryboot")) {
    uint32_t v[1] = {1}; /* SET_REBOOT_FLAGS: tryboot */
    if (prop(0x00038064, v, 1, 1) < 0) {
      fprintf(stderr, "rpi-info: the firmware refused the tryboot flag\n");
      return 1;
    }
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "throttled")) {
    uint32_t v[1] = {0};
    if (prop(0x00030046, v, 0, 1) < 0) return 1;
    printf("throttled=0x%x\n", v[0]);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "clock")) {
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++)
      if (!strcmp(argv[2], clocks[i].name)) {
        long hz = clock_rate(clocks[i].id);
        if (hz < 0) return 1;
        printf("frequency(%u)=%ld\n", clocks[i].id, hz);
        return 0;
      }
    fprintf(stderr, "rpi-info: unknown clock '%s'\n", argv[2]);
    return 2;
  }
  if (argc > 1) {
    fprintf(stderr, "usage: rpi-info [temp | throttled | clock arm|core|uart|emmc|emmc2]\n");
    return 2;
  }
  uint32_t v[4] = {0};
  if (prop(0x00000001, v, 0, 1) >= 0) printf("firmware:  0x%x\n", v[0]);
  if (prop(0x00010002, v, 0, 1) >= 0) printf("revision:  %06x\n", v[0]);
  if (prop(0x00010004, v, 0, 2) >= 0) printf("serial:    %08x%08x\n", v[1], v[0]);
  if (prop(0x00010003, v, 0, 2) >= 0) {
    const uint8_t *mac = (const uint8_t *)v;
    printf("mac:       %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  if (prop(0x00010005, v, 0, 2) >= 0) printf("arm mem:   %u MiB at 0x%x\n", v[1] >> 20, v[0]);
  if (prop(0x00010006, v, 0, 2) >= 0) printf("vc mem:    %u MiB at 0x%x\n", v[1] >> 20, v[0]);
  for (unsigned i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++) {
    long hz = clock_rate(clocks[i].id);
    if (hz >= 0) printf("clock %-5s %ld Hz\n", clocks[i].name, hz);
  }
  temp();
  return 0;
}

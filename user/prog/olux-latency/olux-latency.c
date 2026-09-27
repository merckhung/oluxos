/*
 * olux-latency - real-time characterisation (cyclictest-style).
 *
 *   olux-latency [-i INTERVAL_US] [-d SECONDS] [-n ROUNDTRIPS] [-l LIMIT_US]
 *
 * timer: a thread sleeps until absolute deadlines INTERVAL apart
 *        (clock_nanosleep, CLOCK_MONOTONIC) and records how late it woke.
 * ipc:   two processes bounce a byte over a pair of pipes; round-trip time.
 * Prints min / avg / p99 / max in microseconds. With -l, exits 1 if the
 * worst timer latency exceeds LIMIT_US (a budget for CI or a soak run).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

static void report(const char *what, uint64_t *v, size_t n) {
  if (!n) {
    printf("%s: no samples\n", what);
    return;
  }
  qsort(v, n, sizeof(*v), cmp_u64);
  uint64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += v[i];
  printf("%s: samples %zu min %llu avg %llu p99 %llu max %llu (us)\n", what, n, (unsigned long long)(v[0] / 1000),
         (unsigned long long)(sum / n / 1000), (unsigned long long)(v[n * 99 / 100] / 1000),
         (unsigned long long)(v[n - 1] / 1000));
}

static uint64_t timer_test(long interval_us, long seconds) {
  size_t n = (size_t)(seconds * 1000000 / interval_us);
  uint64_t *lat = calloc(n ? n : 1, sizeof(*lat));
  if (!lat) return 0;
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  size_t got = 0;
  for (size_t i = 0; i < n; i++) {
    next.tv_nsec += interval_us * 1000;
    while (next.tv_nsec >= 1000000000L) {
      next.tv_nsec -= 1000000000L;
      next.tv_sec++;
    }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL)) {
    }
    uint64_t want = (uint64_t)next.tv_sec * 1000000000ULL + (uint64_t)next.tv_nsec, t = now_ns();
    lat[got++] = t > want ? t - want : 0;
  }
  printf("timer: interval %ld us\n", interval_us);
  report("timer", lat, got);
  uint64_t worst = got ? lat[got - 1] : 0; /* sorted by report() */
  free(lat);
  return worst;
}

static void ipc_test(long rounds) {
  int a[2], b[2];
  if (pipe(a) || pipe(b)) {
    perror("pipe");
    return;
  }
  pid_t pid = fork();
  if (pid == 0) {
    close(a[1]); /* else the child never sees end-of-file */
    close(b[0]);
    char c;
    while (read(a[0], &c, 1) == 1)
      if (write(b[1], &c, 1) != 1) break;
    _exit(0);
  }
  close(a[0]);
  close(b[1]);
  uint64_t *rtt = calloc((size_t)rounds, sizeof(*rtt));
  size_t got = 0;
  for (long i = 0; rtt && i < rounds; i++) {
    char c = 'x';
    uint64_t t0 = now_ns();
    if (write(a[1], &c, 1) != 1 || read(b[0], &c, 1) != 1) break;
    rtt[got++] = now_ns() - t0;
  }
  close(a[1]);
  close(b[0]);
  waitpid(pid, NULL, 0);
  report("ipc", rtt, got);
  free(rtt);
}

int main(int argc, char **argv) {
  long interval = 1000, seconds = 5, rounds = 10000, limit = 0;
  int opt;
  while ((opt = getopt(argc, argv, "i:d:n:l:")) != -1) {
    switch (opt) {
      case 'i':
        interval = atol(optarg);
        break;
      case 'd':
        seconds = atol(optarg);
        break;
      case 'n':
        rounds = atol(optarg);
        break;
      case 'l':
        limit = atol(optarg);
        break;
      default:
        fprintf(stderr, "usage: olux-latency [-i INTERVAL_US] [-d SECONDS] [-n ROUNDTRIPS] [-l LIMIT_US]\n");
        return 2;
    }
  }
  if (interval < 50) interval = 50;
  uint64_t worst = seconds > 0 ? timer_test(interval, seconds) : 0;
  if (rounds > 0) ipc_test(rounds);
  if (limit && worst / 1000 > (uint64_t)limit) {
    printf("FAIL: worst timer latency %llu us exceeds the %ld us budget\n", (unsigned long long)(worst / 1000), limit);
    return 1;
  }
  return 0;
}

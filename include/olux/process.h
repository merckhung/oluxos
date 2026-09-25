#ifndef OLUX_PROCESS_H
#define OLUX_PROCESS_H

#include <olux/list.h>
#include <olux/sched.h>

struct mm;
struct process {
  struct mm *mm;
};

#endif

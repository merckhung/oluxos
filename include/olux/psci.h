#ifndef OLUX_PSCI_H
#define OLUX_PSCI_H

#include <olux/types.h>

int psci_cpu_on(u64 mpidr, phys_addr_t entry);
bool psci_available(void);

#endif

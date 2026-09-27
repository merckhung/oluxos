# Host unit tests for kernel library code (built natively with sanitizers).
HOSTCC ?= cc
ROOT := ../..
O ?= $(ROOT)/out/unit
SAN := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
CFLAGS := -O1 -g $(SAN) -Wall -Wno-unused-function
KFLAGS := $(CFLAGS) -include rename.h -I$(ROOT)/include -I$(ROOT)/arch/arm64/include -ffreestanding \
	-D__KERNEL__ -DOLUX_VERSION=\"test\" -DOLUX_GITREV=\"test\" -fno-builtin
QEMU ?= qemu-system-aarch64

TESTS := test_string test_vsprintf test_fdt

all: $(addprefix $(O)/,$(TESTS)) $(O)/virt.dtb
	@set -e; for t in $(TESTS); do $(O)/$$t $(O)/virt.dtb; done

$(O)/k_%.o: $(ROOT)/lib/%.c rename.h
	@mkdir -p $(O)
	$(HOSTCC) $(KFLAGS) -c $< -o $@

$(O)/test_string: test_string.c $(O)/k_string.o
	$(HOSTCC) $(CFLAGS) $^ -o $@
$(O)/test_vsprintf: test_vsprintf.c $(O)/k_vsprintf.o $(O)/k_string.o
	$(HOSTCC) $(CFLAGS) $^ -o $@
$(O)/test_fdt: test_fdt.c $(O)/k_fdt.o $(O)/k_string.o
	$(HOSTCC) $(CFLAGS) $^ -o $@

# A reference device tree straight from QEMU (skipped if QEMU is absent).
$(O)/virt.dtb:
	@mkdir -p $(O)
	-$(QEMU) -M virt,dumpdtb=$@ -cpu cortex-a72 -m 256M -nographic >/dev/null 2>&1 || true

.PHONY: all

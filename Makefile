# OluxOS build (AArch64: QEMU virt + Raspberry Pi 4B)
#
#   make                 kernel Image + initramfs
#   make run             boot in QEMU virt
#   make test            host unit tests + QEMU integration tests
#   make rpi4            SD-card boot directory for Raspberry Pi 4B
#   make sdcard          bootable SD card image (out/sdcard.img) for Raspberry Pi 4B
#
# Variables: CROSS (compiler prefix), O (output dir), V=1 (verbose),
#            DEBUG=1 (-O0 + extra checks), SMP (cpus for `make run`).

O        ?= out
V        ?= 0
DEBUG    ?= 0
SMP      ?= 4
MEM      ?= 1G

ifeq ($(CROSS),)
  ifneq ($(wildcard toolchains/cross/install/bin/aarch64-linux-musl-gcc),)
    CROSS := $(CURDIR)/toolchains/cross/install/bin/aarch64-linux-musl-
  else
    CROSS := aarch64-linux-gnu-
  endif
endif

CC       := $(CROSS)gcc
LD       := $(CROSS)ld
OBJCOPY  := $(CROSS)objcopy
OBJDUMP  := $(CROSS)objdump
NM       := $(CROSS)nm
HOSTCC   ?= cc
PYTHON   ?= python3

ifeq ($(V),1)
  Q :=
else
  Q := @
endif

VERSION  := 0.2.0
GITREV   := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)

# ---------------------------------------------------------------------------
# Kernel
# ---------------------------------------------------------------------------
KCFLAGS := -std=gnu11 -ffreestanding -fno-builtin -nostdinc \
	-isystem $(shell $(CC) -print-file-name=include) \
	-Iinclude -Iarch/arm64/include \
	-mgeneral-regs-only -mno-outline-atomics -mstrict-align \
	-fno-pie -fno-pic -fno-common -fno-strict-aliasing \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
	-fstack-protector-strong -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns \
	-ffunction-sections -fdata-sections -g \
	-Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare \
	-Wno-missing-field-initializers -Wstrict-prototypes -Wmissing-prototypes \
	-Wshadow=local -Wvla -Wimplicit-fallthrough \
	-DOLUX_VERSION=\"$(VERSION)\" -DOLUX_GITREV=\"$(GITREV)\" \
	-D__KERNEL__
ifeq ($(DEBUG),1)
  KCFLAGS += -O0 -DCONFIG_DEBUG=1
else
  KCFLAGS += -O2
endif
KASFLAGS := $(filter-out -std=gnu11 -Werror,$(KCFLAGS)) -D__ASSEMBLY__
KLDFLAGS := -nostdlib -static -z max-page-size=4096 -z noexecstack \
	--gc-sections --no-warn-rwx-segments --build-id=none

KSRCS := $(filter-out arch/arm64/kernel.lds.S,$(wildcard arch/arm64/*.S arch/arm64/*.c)) \
	$(wildcard kernel/*.c mm/*.c lib/*.c fs/*.c net/*.c) \
	$(wildcard drivers/*/*.c)
KOBJS := $(patsubst %,$(O)/%.o,$(basename $(KSRCS)))
KDEPS := $(KOBJS:.o=.d)

all: kernel initramfs

kernel: $(O)/Image

$(O)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(if $(Q),echo "  CC      $<")
	$(Q)$(CC) $(KCFLAGS) -MMD -MP -c $< -o $@

$(O)/%.o: %.S
	@mkdir -p $(dir $@)
	@$(if $(Q),echo "  AS      $<")
	$(Q)$(CC) $(KASFLAGS) -MMD -MP -c $< -o $@

$(O)/kernel.lds: arch/arm64/kernel.lds.S
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(KASFLAGS) -E -P -x c $< -o $@

# Two-pass link: pass 1 collects symbols, pass 2 embeds the symbol table
# used for backtraces in panics.
$(O)/olux.elf: $(KOBJS) $(O)/kernel.lds scripts/ksyms.py
	@$(if $(Q),echo "  LD      $@")
	$(Q)$(PYTHON) scripts/ksyms.py --empty $(O)/ksyms0.S
	$(Q)$(CC) $(KASFLAGS) -c $(O)/ksyms0.S -o $(O)/ksyms0.o
	$(Q)$(LD) $(KLDFLAGS) -T $(O)/kernel.lds -o $(O)/olux.tmp $(KOBJS) $(O)/ksyms0.o
	$(Q)$(NM) -n $(O)/olux.tmp | $(PYTHON) scripts/ksyms.py $(O)/ksyms.S
	$(Q)$(CC) $(KASFLAGS) -c $(O)/ksyms.S -o $(O)/ksyms.o
	$(Q)$(LD) $(KLDFLAGS) -T $(O)/kernel.lds -o $(O)/olux.tmp2 $(KOBJS) $(O)/ksyms.o
	$(Q)$(NM) -n $(O)/olux.tmp2 | $(PYTHON) scripts/ksyms.py $(O)/ksyms.S
	$(Q)$(CC) $(KASFLAGS) -c $(O)/ksyms.S -o $(O)/ksyms.o
	$(Q)$(LD) $(KLDFLAGS) -T $(O)/kernel.lds -o $@ $(KOBJS) $(O)/ksyms.o
	$(Q)rm -f $(O)/olux.tmp $(O)/olux.tmp2

$(O)/Image: $(O)/olux.elf
	@$(if $(Q),echo "  OBJCOPY $@")
	$(Q)$(OBJCOPY) -O binary $< $@

# ---------------------------------------------------------------------------
# Userspace + initramfs
# ---------------------------------------------------------------------------
USERSPACE_OUT := toolchains/userspace/out
UCC           := $(USERSPACE_OUT)/bin/oluxos-cc

$(UCC) $(USERSPACE_OUT)/bin/busybox &: third_party/musl/musl-1.2.5.tar.gz \
		third_party/busybox/busybox-1.36.1.tar.bz2 toolchains/userspace/build.sh \
		toolchains/userspace/busybox.config $(wildcard toolchains/userspace/linux-headers/linux/*.h)
	$(Q)OLUXOS_CROSS=$(CROSS) toolchains/userspace/build.sh all
	@touch $(UCC) $(USERSPACE_OUT)/bin/busybox

UCFLAGS := -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -Iuser/include -Iinclude/uapi \
	-fstack-protector-strong -D_GNU_SOURCE
USER_PROGS := $(notdir $(wildcard user/prog/*))
USER_BINS  := $(addprefix $(O)/user/,$(USER_PROGS))
USER_LIB   := $(wildcard user/lib/*.c)

$(O)/user/%: user/prog/%/*.c $(USER_LIB) $(wildcard user/include/*.h include/uapi/olux/*.h) $(UCC)
	@mkdir -p $(dir $@)
	@$(if $(Q),echo "  UCC     $@")
	$(Q)$(UCC) $(UCFLAGS) -o $@ $(filter %.c,$^) -lm

initramfs: $(O)/initramfs.cpio

$(O)/initramfs.cpio: $(USER_BINS) $(USERSPACE_OUT)/bin/busybox scripts/mkinitramfs.py \
		$(shell find rootfs -type f 2>/dev/null)
	@$(if $(Q),echo "  CPIO    $@")
	$(Q)$(PYTHON) scripts/mkinitramfs.py -o $@ --busybox $(USERSPACE_OUT)/bin/busybox \
		--skel rootfs $(foreach b,$(USER_BINS),--bin $(b))

# ---------------------------------------------------------------------------
# Disk images, boards, run targets
# ---------------------------------------------------------------------------
$(O)/disk.img: scripts/mkdisk.sh $(USERSPACE_OUT)/bin/busybox
	$(Q)scripts/mkdisk.sh $@

QEMU      ?= qemu-system-aarch64
QEMU_CPU  ?= cortex-a72
QEMU_GIC  ?= 2
QEMU_ARGS ?=
QEMU_VIRT  = $(QEMU) -M virt,gic-version=$(QEMU_GIC) -cpu $(QEMU_CPU) -smp $(SMP) -m $(MEM) \
	-kernel $(O)/Image -initrd $(O)/initramfs.cpio -nographic -no-reboot \
	-append "console=ttyAMA0" \
	-drive if=none,file=$(O)/disk.img,format=raw,id=hd0 \
	-device virtio-blk-device,drive=hd0 \
	-netdev user,id=n0,hostfwd=tcp::5555-:23 -device virtio-net-device,netdev=n0 \
	-device virtio-rng-device $(QEMU_ARGS)

run: all $(O)/disk.img
	$(QEMU_VIRT)

debug: all $(O)/disk.img
	$(QEMU_VIRT) -s -S

rpi4: all
	$(Q)scripts/mkrpi4.sh $(O) $(O)/rpi4

sdcard: rpi4
	$(Q)scripts/mksdcard.sh $(O)/rpi4 $(O)/sdcard.img

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
test: unit-test qemu-test

unit-test:
	$(Q)$(MAKE) -C tests/unit HOSTCC=$(HOSTCC)

qemu-test: all $(O)/disk.img
	$(Q)$(PYTHON) tests/qemu/run_tests.py --out $(O) --smp $(SMP)

clean:
	rm -rf $(O)

distclean: clean
	rm -rf $(USERSPACE_OUT) toolchains/cross/build

.PHONY: all kernel initramfs run debug rpi4 sdcard test unit-test qemu-test clean distclean

-include $(KDEPS)

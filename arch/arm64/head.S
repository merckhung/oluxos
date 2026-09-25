/*
 * OluxOS AArch64 entry point.
 *
 * Follows the Linux arm64 boot protocol so that QEMU (-kernel Image) and the
 * Raspberry Pi firmware (kernel8.img) both pass the device tree in x0 and
 * describe the initramfs in /chosen. The code before the MMU is enabled is
 * position independent: the kernel may be loaded at any 4 KiB-aligned
 * physical address.
 */
#include <asm/memory.h>
#include <asm/pgtable.h>
#include <asm/sysreg.h>

#define ICC_SRE_EL2 s3_4_c12_c9_5
#define ICH_HCR_EL2 s3_4_c12_c11_0

  .section ".head.text", "ax"
  .globl _text
_text:
  b       primary_entry           /* code0 */
  .long   0                       /* code1 */
  .quad   0                       /* text_offset */
  .quad   _kernel_size_le         /* image_size (includes bss) */
  .quad   0xa                     /* flags: LE, 4K pages, place anywhere */
  .quad   0, 0, 0                 /* reserved */
  .ascii  "ARM\x64"               /* magic */
  .long   0                       /* reserved (PE header offset) */

  .text
/*
 * Register usage until start_kernel:
 *   x20 = physical load address of _text
 *   x21 = physical address of the device tree blob
 */
  .globl primary_entry
primary_entry:
  mov     x21, x0
  bl      el2_setup
  adrp    x20, _text
  bl      create_page_tables
  bl      __cpu_setup
  bl      __enable_mmu
  ldr     x8, =__primary_switched
  br      x8

/* Runs at the kernel's virtual address with MMU and caches on. */
__primary_switched:
  ldr     x0, =__bss_start
  ldr     x1, =__bss_end
1:cmp     x0, x1
  b.hs    2f
  stp     xzr, xzr, [x0], #16
  b       1b
2:ldr     x0, =vectors
  msr     vbar_el1, x0
  isb
  ldr     x0, =init_stack
  add     sp, x0, #BOOT_STACK_SIZE
  mov     x29, xzr
  mov     x30, xzr
  mov     x0, x21
  mov     x1, x20
  bl      start_kernel
3:wfe
  b       3b

/*
 * Drop from EL2 (or EL3) to EL1h and configure the hypervisor so that the
 * kernel has direct access to the timers, FP/SIMD and GIC system registers.
 * Returns to the caller at EL1 with all exceptions masked.
 */
el2_setup:
  msr     SPsel, #1
  mrs     x0, CurrentEL
  cmp     x0, #(3 << 2)
  b.eq    .Lat_el3
  cmp     x0, #(2 << 2)
  b.eq    .Lat_el2
  ldr     x0, =SCTLR_EL1_MMU_OFF
  msr     sctlr_el1, x0
  isb
  ret

.Lat_el3:
  mov     x0, #0x531              /* RW, HCE, SMD, RES1, NS */
  msr     scr_el3, x0
  mov     x0, #(PSR_DAIF | PSR_MODE_EL2h)
  msr     spsr_el3, x0
  adr     x0, .Lat_el2
  msr     elr_el3, x0
  eret

.Lat_el2:
  ldr     x0, =SCTLR_EL1_MMU_OFF
  msr     sctlr_el1, x0
  mov     x0, #HCR_RW
  msr     hcr_el2, x0
  mrs     x0, cnthctl_el2
  orr     x0, x0, #3              /* EL1PCTEN | EL1PCEN */
  msr     cnthctl_el2, x0
  msr     cntvoff_el2, xzr
  mrs     x0, id_aa64pfr0_el1     /* GICv3 system register interface? */
  ubfx    x0, x0, #24, #4
  cbz     x0, 1f
  mrs     x0, ICC_SRE_EL2
  mov     x1, #(1 | 8)            /* SRE | Enable (lower EL may use SRE) */
  orr     x0, x0, x1
  msr     ICC_SRE_EL2, x0
  isb
  msr     ICH_HCR_EL2, xzr
1:mrs     x0, midr_el1
  msr     vpidr_el2, x0
  mrs     x0, mpidr_el1
  msr     vmpidr_el2, x0
  mov     x0, #0x33ff
  msr     cptr_el2, x0            /* no FP/SIMD traps */
  msr     hstr_el2, xzr
  msr     vttbr_el2, xzr
  mov     x0, #(PSR_DAIF | PSR_MODE_EL1h)
  msr     spsr_el2, x0
  msr     elr_el2, lr
  eret

/*
 * Build the initial translation tables (MMU off, so all addresses are
 * physical and every access is non-cacheable):
 *   idmap_pg_dir   - identity map (1 GiB block) of the RAM holding the kernel,
 *                    used only while switching the MMU on
 *   swapper_pg_dir - kernel image at KIMAGE_VADDR (4 KiB pages) and the DTB
 *                    at FIXMAP_START (2 MiB blocks)
 */
create_page_tables:
  adrp    x0, __pgtables_start
  adrp    x1, __pgtables_end
1:stp     xzr, xzr, [x0], #16
  cmp     x0, x1
  b.lo    1b

  /* identity map: the 1 GiB block(s) covering [_text, _end) */
  adrp    x0, idmap_pg_dir
  ldr     x3, =PROT_SECT_NORMAL
  lsr     x1, x20, #L1_SHIFT
  adrp    x2, _end
  sub     x2, x2, #1
  lsr     x2, x2, #L1_SHIFT
2:lsl     x4, x1, #L1_SHIFT
  orr     x4, x4, x3
  and     x5, x1, #(PT_ENTRIES - 1)
  str     x4, [x0, x5, lsl #3]
  add     x1, x1, #1
  cmp     x1, x2
  b.ls    2b

  /* swapper L1: kernel image and fixmap L2 tables */
  adrp    x0, swapper_pg_dir
  adrp    x1, kimage_l2
  orr     x1, x1, #3
  ldr     x2, =L1_INDEX(KIMAGE_VADDR)
  str     x1, [x0, x2, lsl #3]
  adrp    x1, fixmap_l2
  orr     x1, x1, #3
  ldr     x2, =L1_INDEX(FIXMAP_START)
  str     x1, [x0, x2, lsl #3]

  /* kimage L2 -> contiguous array of L3 tables */
  adrp    x0, kimage_l2
  adrp    x1, kimage_l3
  mov     x2, #0
3:orr     x3, x1, #3
  str     x3, [x0, x2, lsl #3]
  add     x1, x1, #PAGE_SIZE
  add     x2, x2, #1
  cmp     x2, #KIMAGE_L3_TABLES
  b.lo    3b

  /* kimage L3: one 4 KiB page per page of [_text, _end) */
  adrp    x0, kimage_l3
  ldr     x3, =PROT_KERNEL_RWX
  adrp    x1, _text
  adrp    x2, _end
  add     x2, x2, #(PAGE_SIZE - 1)
  and     x2, x2, #PAGE_MASK
4:orr     x4, x1, x3
  str     x4, [x0], #8
  add     x1, x1, #PAGE_SIZE
  cmp     x1, x2
  b.lo    4b

  /* fixmap: DTB window (two 2 MiB blocks), then the 4 KiB slot table */
  adrp    x0, fixmap_l2
  cbz     x21, 5f
  ldr     x3, =(PROT_SECT_NORMAL | PTE_PXN | PTE_UXN | PTE_RDONLY)
  and     x1, x21, #~(L2_SIZE - 1)
  orr     x4, x1, x3
  str     x4, [x0, #0]
  add     x1, x1, #L2_SIZE
  orr     x4, x1, x3
  str     x4, [x0, #8]
5:adrp    x1, fixmap_l3
  orr     x1, x1, #3
  str     x1, [x0, #16]

  /* discard any stale cache lines covering the tables we just wrote */
  adrp    x0, __pgtables_start
  adrp    x1, __pgtables_end
  mrs     x3, ctr_el0
  ubfx    x3, x3, #16, #4
  mov     x2, #4
  lsl     x2, x2, x3              /* dcache line size */
6:dc      ivac, x0
  add     x0, x0, x2
  cmp     x0, x1
  b.lo    6b
  dsb     sy
  ret

/* Configure MAIR/TCR for this CPU. Returns the SCTLR_EL1 value in x0. */
  .globl __cpu_setup
__cpu_setup:
  tlbi    vmalle1
  dsb     nsh
  mov     x0, #(3 << 20)          /* CPACR_EL1.FPEN: no FP/SIMD traps */
  msr     cpacr_el1, x0
  msr     mdscr_el1, xzr
  ldr     x0, =MAIR_VALUE
  msr     mair_el1, x0
  ldr     x0, =(TCR_T0SZ(VA_BITS) | TCR_T1SZ(VA_BITS) | TCR_TG0_4K | TCR_TG1_4K | \
                TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_INNER | \
                TCR_IRGN1_WBWA | TCR_ORGN1_WBWA | TCR_SH1_INNER)
  mrs     x1, id_aa64mmfr0_el1
  ubfx    x2, x1, #0, #3          /* PARange */
  cmp     x2, #5
  mov     x3, #5
  csel    x2, x2, x3, ls
  bfi     x0, x2, #TCR_IPS_SHIFT, #3
  ubfx    x2, x1, #4, #4          /* ASIDBits: 2 means 16-bit ASIDs */
  cmp     x2, #2
  b.ne    1f
  orr     x0, x0, #TCR_AS
1:msr     tcr_el1, x0
  ldr     x0, =SCTLR_EL1_MMU_ON
  ret

/* x0 = SCTLR value. Installs idmap + swapper and turns the MMU on. */
__enable_mmu:
  adrp    x1, idmap_pg_dir
  msr     ttbr0_el1, x1
  adrp    x1, swapper_pg_dir
  msr     ttbr1_el1, x1
  isb
  msr     sctlr_el1, x0
  isb
  ic      iallu
  dsb     nsh
  isb
  ret

/*
 * Secondary CPU entry (physical address, MMU off), reached via PSCI CPU_ON
 * or the Raspberry Pi spin table. x0 = context (unused).
 */
  .globl secondary_entry
secondary_entry:
  bl      el2_setup
  bl      __cpu_setup
  bl      __enable_mmu
  ldr     x8, =__secondary_switched
  br      x8

__secondary_switched:
  ldr     x0, =vectors
  msr     vbar_el1, x0
  isb
  ldr     x0, =secondary_boot_stack
  ldr     x0, [x0]
  mov     sp, x0
  mov     x29, xzr
  mov     x30, xzr
  bl      secondary_start_kernel
1:wfe
  b       1b

  .section ".pgtables", "aw", %nobits
  .balign PAGE_SIZE
  .globl idmap_pg_dir, swapper_pg_dir, fixmap_l3
idmap_pg_dir:   .skip PAGE_SIZE
swapper_pg_dir: .skip PAGE_SIZE
kimage_l2:      .skip PAGE_SIZE
kimage_l3:      .skip PAGE_SIZE * KIMAGE_L3_TABLES
fixmap_l2:      .skip PAGE_SIZE
fixmap_l3:      .skip PAGE_SIZE

  .bss
  .balign 16
  .globl init_stack
init_stack:     .skip BOOT_STACK_SIZE

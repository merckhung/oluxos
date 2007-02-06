/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * resource.c -- OluxOS IA32 resource routines
 *
 */
#include <ia32/types.h>
#include <driver/resource.h>
#include <driver/console.h>
#include <string.h>


__u32 pentium_msr_list[] = {

    0x00,   // P5_MC_ADDR
    0x01,   // P5_MC_TYPE
    0x10,   // TSC
    0x11,   // CESR
    0x12,   // CTR0
    0x13,   // CTR1
};


__u32 p6_msr_list[] = {

    0x00,   // P5_MC_ADDR
    0x01,   // P5_MC_TYPE
    0x10,   // TSC
    0x17,   // IA32_PLATFORM_ID
    0x1b,   // APIC_BASE
    0x2a,   // EBL_CR_POWERON
    0x33,   // TEST_CTL
    0x79,   // BIOS_UPDT_TRIG
    0x88,   // BBL_CR_D0
    0x89,   // BBL_CR_D1
    0x8a,   // BBL_CR_D2
    0x8b,   // BIOS_SIGN/BBL_CR_D3
    0xc1,   // PrefCtr0
    0xc2,   // PrefCtr1
    0xfe,   // MTRRcap
    0x116,  // BBL_CR_ADDR
    0x118,  // BBL_CR_DECC
    0x119,  // BBL_CR_CTL
    0x11a,  // BBL_CR_TRIG
    0x11b,  // BBL_CR_BUSY
    0x11e,  // BBL_CR_CTL3
    0x174,  // SYSENTER_CS_MSR
    0x175,  // SYSENTER_ESP_MSR
    0x176,  // SYSENTER_EIP_MSR
    0x179,  // MCG_CAP
    0x17a,  // MCG_STATUS
    0x17b,  // MCG_CTL
    0x186,  // PrefEvtSel0
    0x187,  // PrefEvtSel1
    0x1d9,  // DEBUGCTLMSR
    0x1db,  // LASTBRANCHFROMIP
    0x1dc,  // LASTBRANCHTOIP
    0x1dd,  // LASTINTFROMIP
    0x1de,  // LASTINTTOIP
    0x1e0,  // ROB_CR_BKUPTMPDR6
    0x200,  // MTRRphyBase0
    0x201,  // MTRRphyMask0
    0x202,  // MTRRphyBase1
    0x203,  // MTRRphyMask1
    0x204,  // MTRRphyBase2
    0x205,  // MTRRphyMask2
    0x206,  // MTRRphyBase3
    0x207,  // MTRRphyMask3
    0x208,  // MTRRphyBase4
    0x209,  // MTRRphyMask4
    0x20a,  // MTRRphyBase5
    0x20b,  // MTRRphyMask5
    0x20c,  // MTRRphyBase6
    0x20d,  // MTRRphyMask6
    0x20e,  // MTRRphyBase7
    0x20f,  // MTRRphyMask7
    0x250,  // MTRRfix64K_00000
    0x258,  // MTRRfix16K_80000
    0x259,  // MTRRfix16K_A0000
    0x268,  // MTRRfix4K_C0000
    0x269,  // MTRRfix4K_C8000
    0x26a,  // MTRRfix4K_D0000
    0x26b,  // MTRRfix4K_D8000
    0x26c,  // MTRRfix4K_E0000
    0x26d,  // MTRRfix4K_E8000
    0x26e,  // MTRRfix4K_F0000
    0x26f,  // MTRRfix4K_F8000
    0x2ff,  // MTRRdefType
    0x400,  // MC0_CTL
    0x401,  // MC0_STATUS
    0x402,  // MC0_ADDR
    0x403,  // MC0_MISC
    0x404,  // MC1_CTL
    0x405,  // MC1_STATUS
    0x406,  // MC1_ADDR
    0x407,  // MC1_MISC
    0x408,  // MC2_CTL
    0x409,  // MC2_STATUS
    0x40a,  // MC2_ADDR
    0x40b,  // MC2_MISC
    0x40c,  // MC4_CTL
    0x40d,  // MC4_STATUS
    0x40e,  // MC4_ADDR
    0x40f,  // MC4_MISC
    0x410,  // MC3_CTL
    0x411,  // MC3_STATUS
    0x412,  // MC3_ADDR
    0x413,  // MC3_MISC
};


__u32 pm_msr_list[] = {

    0x00,   // P5_MC_ADDR
    0x01,   // P5_MC_TYPE
    0x10,   // TSC
    0x17,   // IA32_PLATFORM_ID
    0x1b,   // APIC_BASE
    0x2a,   // EBL_CR_POWERON
    0x33,   // TEST_CTL
    0x79,   // BIOS_UPDT_TRIG
    0x88,   // BBL_CR_D0
    0x89,   // BBL_CR_D1
    0x8a,   // BBL_CR_D2
    0x8b,   // BIOS_SIGN/BBL_CR_D3
    0xc1,   // PrefCtr0
    0xc2,   // PrefCtr1
    0xfe,   // MTRRcap
    0x116,  // BBL_CR_ADDR
    0x118,  // BBL_CR_DECC
    0x119,  // BBL_CR_CTL
    0x11a,  // BBL_CR_TRIG
    0x11b,  // BBL_CR_BUSY
    0x11e,  // BBL_CR_CTL3
    0x174,  // SYSENTER_CS_MSR
    0x175,  // SYSENTER_ESP_MSR
    0x176,  // SYSENTER_EIP_MSR
    0x179,  // MCG_CAP
    0x17a,  // MCG_STATUS
    0x17b,  // MCG_CTL
    0x186,  // PrefEvtSel0
    0x187,  // PrefEvtSel1
    0x1d9,  // DEBUGCTLMSR
    0x1db,  // LASTBRANCHFROMIP
    0x1dc,  // LASTBRANCHTOIP
    0x1dd,  // LASTINTFROMIP
    0x1de,  // LASTINTTOIP
    0x1e0,  // ROB_CR_BKUPTMPDR6
    0x200,  // MTRRphyBase0
    0x201,  // MTRRphyMask0
    0x202,  // MTRRphyBase1
    0x203,  // MTRRphyMask1
    0x204,  // MTRRphyBase2
    0x205,  // MTRRphyMask2
    0x206,  // MTRRphyBase3
    0x207,  // MTRRphyMask3
    0x208,  // MTRRphyBase4
    0x209,  // MTRRphyMask4
    0x20a,  // MTRRphyBase5
    0x20b,  // MTRRphyMask5
    0x20c,  // MTRRphyBase6
    0x20d,  // MTRRphyMask6
    0x20e,  // MTRRphyBase7
    0x20f,  // MTRRphyMask7
    0x250,  // MTRRfix64K_00000
    0x258,  // MTRRfix16K_80000
    0x259,  // MTRRfix16K_A0000
    0x268,  // MTRRfix4K_C0000
    0x269,  // MTRRfix4K_C8000
    0x26a,  // MTRRfix4K_D0000
    0x26b,  // MTRRfix4K_D8000
    0x26c,  // MTRRfix4K_E0000
    0x26d,  // MTRRfix4K_E8000
    0x26e,  // MTRRfix4K_F0000
    0x26f,  // MTRRfix4K_F8000
    0x2ff,  // MTRRdefType
    0x400,  // MC0_CTL
    0x401,  // MC0_STATUS
    0x402,  // MC0_ADDR
    0x403,  // MC0_MISC
    0x404,  // MC1_CTL
    0x405,  // MC1_STATUS
    0x406,  // MC1_ADDR
    0x407,  // MC1_MISC
    0x408,  // MC2_CTL
    0x409,  // MC2_STATUS
    0x40a,  // MC2_ADDR
    0x40b,  // MC2_MISC
    0x40c,  // MC4_CTL
    0x40d,  // MC4_STATUS
    0x40e,  // MC4_ADDR
    0x40f,  // MC4_MISC
    0x410,  // MC3_CTL
    0x411,  // MC3_STATUS
    0x412,  // MC3_ADDR
    0x413,  // MC3_MISC
};


//
// ChkCpuidSup -- Check for CPUID instruction support
//
// Input:
//  None
//
// Return:
//  True  = 0
//  False = -1
//
__s8 ChkCpuidSup( void ) {


    return 0;
}


//
// ChkMSRSup -- Check for MSR feature support
//
// Input:
//  None
//
// Return:
//  True  = 0
//  False = -1
//
__s8 ChkMSRSup( void ) {


    return 0;
}


//
// ChkSMBIOSSup -- Check for SMBIOS feature support
//
// Input:
//  None
//
// Return:
//  True  = 0
//  False = -1
//
__u8 ChkSMBIOSSup( void ) {

    __u32 i;
    volatile __u8 *memptr = (__u8 *)SMBIOS_BASE;
    __u8 smbios = 0;

    // Search 1MB
    for( i = 0 ; i < 0x100000 ; i++ ) {
    
        if( !strncmp( (__u8 *)(memptr + i), SMBIOS_ANCH, 4 ) ) {
        
            smbios = 1;
            break;
        }
    }

    if( smbios ) {
    
        TcPrint( "Found SMBIOS AnchorString\n" );    
    }
    else {
    
        TcPrint( "Cannot Find SMBIOS AnchorString\n" );
    }

    return 0;
}



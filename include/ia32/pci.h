/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     PCI_ENABLE_BIT      0x80000000
#define     PCI_BUS_MAX         0x00ff
#define     PCI_DEV_MAX         0x1f
#define     PCI_FUN_MAX         0x07

#define     PCI_PORT_ADDR       0x0cf8
#define     PCI_PORT_DATA       0x0cfc


__u8 ia32_PCIReadConfigByte( __u32 address, __u8 offset );
void ia32_PCIWriteConfigByte( __u32 address, __u8 offset, __u8 value );


__u16 ia32_PCIReadConfigWord( __u32 address, __u8 offset );
void ia32_PCIWriteConfigWord( __u32 address, __u8 offset, __u16 value );


__u32 ia32_PCIReadConfigDWord( __u32 address, __u8 offset );
void ia32_PCIWriteConfigDWord( __u32 address, __u8 offset, __u32 value );


__u32 ia32_PCICalBaseAddr( __u8 bus, __u8 dev, __u8 func );


void ia32_PCIDetectDevice( void );




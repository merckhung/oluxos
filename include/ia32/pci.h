/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
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


__u8 PCIReadConfigByte( __u32 address, __u8 offset );
void PCIWriteConfigByte( __u32 address, __u8 offset, __u8 value );


__u16 PCIReadConfigWord( __u32 address, __u8 offset );
void PCIWriteConfigWord( __u32 address, __u8 offset, __u16 value );


__u32 PCIReadConfigDWord( __u32 address, __u8 offset );
void PCIWriteConfigDWord( __u32 address, __u8 offset, __u32 value );


__u32 PCICalBaseAddr( __u8 bus, __u8 dev, __u8 func );


void PCIDetectDevice( void );




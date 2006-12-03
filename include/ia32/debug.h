/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


typedef struct {

    unsigned long int eax;
    unsigned long int ebx;
    unsigned long int ecx;
    unsigned long int edx;

    unsigned long int esi;
    unsigned long int edi;
    unsigned long int ebp;
    unsigned long int esp;

    unsigned long int efl;

    unsigned int cs;
    unsigned int ds;
    unsigned int es;
    unsigned int fs;
    unsigned int gs;
    unsigned int ss;

    unsigned long int gdt;
    unsigned long int ldt;
    unsigned long int idt;

    unsigned long int cr0;
    unsigned long int cr2;
    unsigned long int cr3;
    unsigned long int cr4;

} ia32_DbgRegs_t;


static ia32_DbgRegs_t ia32_DbgRegs;



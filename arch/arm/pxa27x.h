//
// Intel PXA25x CPU Include File
//


//
// See PXA25x Developer's Manual P3-37
// Coprocessor 14, Register 6
//
#define TURBO_MODE  0x00000001  // Enter Turbo Mode
#define FCS_SEQ     0x00000002  // Enter Frequency Change Sequence


//
// See PXA25x Developer's Manual P3-38
// Coprocessor 14, Register 7
//
#define RUN_TURBO_MODE  0x00000000  // Run/Turbo Mode
#define IDLE_MODE       0x00000001  // Idle Mode
#define SLEEP_MODE      0x00000003  // Sleep Mode


//
// See ARM Architecture Reference Manual A2-9
// Program Status Registers(PSR)
// 
#define ARM_PSR_IRQ     0x00000080  // Disable Interrupt bit
#define ARM_PSR_FIQ     0x00000040  // Disable Fast Interrupt bit


#define ARM_PSR_USER_MODE       0x00000010  // CPU User Mode
#define ARM_PSR_FIQ_MODE        0x00000011  // CPU Fast Interrupt Mode
#define ARM_PSR_IRQ_MODE        0x00000012  // CPU Interrupt Mode
#define ARM_PSR_SUPERVISOR_MODE 0x00000013  // CPU Supervisor Mode
#define ARM_PSR_ABORT_MODE      0x00000017  // CPU Abort Mode
#define ARM_PSR_UNDEFINED_MODE  0x0000001b  // CPU Undefined Mode
#define ARM_PSR_SYSTEM_MODE     0x0000001f  // CPU System Mode




#ifndef WATERCHEESE_TYPES_H
#define WATERCHEESE_TYPES_H
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <Hypervisor/hv_vcpu_types.h>

#define WC_CUSTOM_PAYLOAD 1



// GICv3 distribution
#define GICD_BASE        0x08000000ULL
#define GICD_SIZE        0x00010000ULL

// GICv3 redistribution
#define GICR_BASE        0x080A0000ULL
#define GICR_SIZE        0x00f60000ULL

// UART
#define UART_BASE        0x09000000ULL
#define UART_SIZE        0x00001000ULL

// RTC
#define RTC_BASE         0x09010000ULL
#define RTC_SIZE         0x00001000ULL

// VIRTIO MMIO
#define VIRTIO_BASE      0x0A000000ULL
#define VIRTIO_SIZE      0x00004000ULL

// RAM
#define RAM_BASE         0x40000000ULL
#define RAM_SIZE         0x20000000ULL  /* 512 MiB */

#if WC_CUSTOM_PAYLOAD
#define PAYLOAD_BASE     0x40200000ULL
#define PAYLOAD_SIZE     0x00004000ULL
#define STACK_BASE       0x40204000ULL
#define STACK_SIZE       0x00004000ULL
#define DEFERRED_BASE    0x40400000ULL
#define DEFERRED_SIZE    0x00004000ULL
static_assert(!(((uint64_t)PAYLOAD_SIZE) > (UINT64_MAX - (uint64_t)STACK_SIZE)), "Stack size overflow"); // ensure the stack size doesn't overflow
static_assert(((uint64_t)PAYLOAD_SIZE + (uint64_t)STACK_SIZE) <= SIZE_MAX, "Payload size larger than available size");
#endif

typedef unsigned long long excp_syndrome_t;
// Syndrome bit extaction

// Exit code (ec)
#define EC(syndrome) ((uint8_t) (syndrome >> 26) & 0x3f)
// Instruction syndrome valid (isv)
#define ISV(syndrome) ((uint8_t) (syndrome >> 24) & 0x01)

// Access size (SAS)
#define SAS(syndrome) ((uint8_t) (syndrome >> 22) & 0x03)

// Load/store register
#define SRT(syndrome) ((uint8_t) (syndrome >> 16) & 0x1F)

// cache maintanence access
#define CM(syndrome) ((uint8_t) (syndrome >> 8) & 0x01)

// fault during stage 1 page table
#define S1PTW(syndrome) ((uint8_t) (syndrome >> 7) & 0x01)

// Write or Read
#define WnR(syndrome) ((uint8_t) (syndrome >> 6) & 0x01)

// DFSC
#define DFSC(syndrome) ((uint8_t)((syndrome) & 0x3f))
enum WC_ec_type {
    EC_UNCATEGORIZED          = 0x00,
    EC_WFX_TRAP               = 0x01,
    EC_CP15RTTRAP             = 0x03,
    EC_CP15RRTTRAP            = 0x04,
    EC_CP14RTTRAP             = 0x05,
    EC_CP14DTTRAP             = 0x06,
    EC_ADVSIMDFPACCESSTRAP    = 0x07,
    EC_FPIDTRAP               = 0x08,
    EC_PACTRAP                = 0x09,
    EC_BXJTRAP                = 0x0a,
    EC_CP14RRTTRAP            = 0x0c,
    EC_BTITRAP                = 0x0d,
    EC_ILLEGALSTATE           = 0x0e,
    EC_AA32_SVC               = 0x11,
    EC_AA32_HVC               = 0x12,
    EC_AA32_SMC               = 0x13,
    EC_AA64_SVC               = 0x15,
    EC_AA64_HVC               = 0x16,
    EC_AA64_SMC               = 0x17,
    EC_SYSTEMREGISTERTRAP     = 0x18,
    EC_SVEACCESSTRAP          = 0x19,
    EC_ERETTRAP               = 0x1a,
    EC_PACFAIL                = 0x1c,
    EC_SMETRAP                = 0x1d,
    EC_GPC                    = 0x1e,
    EC_INSNABORT              = 0x20,
    EC_INSNABORT_SAME_EL      = 0x21,
    EC_PCALIGNMENT            = 0x22,
    EC_DATAABORT              = 0x24,
    EC_DATAABORT_SAME_EL      = 0x25,
    EC_SPALIGNMENT            = 0x26,
    EC_MOP                    = 0x27,
    EC_AA32_FPTRAP            = 0x28,
    EC_AA64_FPTRAP            = 0x2c,
    EC_GCS                    = 0x2d,
    EC_SERROR                 = 0x2f,
    EC_BREAKPOINT             = 0x30,
    EC_BREAKPOINT_SAME_EL     = 0x31,
    EC_SOFTWARESTEP           = 0x32,
    EC_SOFTWARESTEP_SAME_EL   = 0x33,
    EC_WATCHPOINT             = 0x34,
    EC_WATCHPOINT_SAME_EL     = 0x35,
    EC_AA32_BKPT              = 0x38,
    EC_VECTORCATCH            = 0x3a,
    EC_AA64_BKPT              = 0x3c,
};

static inline hv_reg_t reg_idx_to_hv(uint64_t reg_idx) {
    switch(reg_idx) {
        case 0: return HV_REG_X0;
        case 1: return HV_REG_X1;
        case 2: return HV_REG_X2;
        case 3: return HV_REG_X3;
        case 4: return HV_REG_X4;
        case 5: return HV_REG_X5;
        case 6: return HV_REG_X6;
        case 7: return HV_REG_X7;
        case 8: return HV_REG_X8;
        case 9: return HV_REG_X9;
        case 10: return HV_REG_X10;
        case 11: return HV_REG_X11;
        case 12: return HV_REG_X12;
        case 13: return HV_REG_X13;
        case 14: return HV_REG_X14;
        case 15: return HV_REG_X15;
        case 16: return HV_REG_X16;
        case 17: return HV_REG_X17;
        case 18: return HV_REG_X18;
        case 19: return HV_REG_X19;
        case 20: return HV_REG_X20;
        case 21: return HV_REG_X21;
        case 22: return HV_REG_X22;
        case 23: return HV_REG_X23;
        case 24: return HV_REG_X24;
        case 25: return HV_REG_X25;
        case 26: return HV_REG_X26;
        case 27: return HV_REG_X27;
        case 28: return HV_REG_X28;
        case 29: return HV_REG_X29;
        case 30: return HV_REG_X30;
        case 31: return -1;
    }
    return -1;
}





#endif
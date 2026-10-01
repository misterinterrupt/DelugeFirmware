#ifndef __R_USB_PMSC_SCSI_H__
#define __R_USB_PMSC_SCSI_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB Mass Storage Bulk-Only Transport (BOT) Command Block Wrapper (CBW)
typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;          // 0x43425355 ("USBC")
    uint32_t dCBWTag;                // Command tag
    uint32_t dCBWDataTransferLength; // Transfer size in bytes
    uint8_t  bmCBWFlags;              // Direction flag (0x80 IN, 0x00 OUT)
    uint8_t  bCBWLUN;                // Logical Unit Number
    uint8_t  bCBWCBLength;           // Length of SCSI command block (1..16)
    uint8_t  CBWCB[16];              // SCSI Command Descriptor Block (CDB)
} usb_cbw_t;

// USB Mass Storage Bulk-Only Transport (BOT) Command Status Wrapper (CSW)
typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;          // 0x53425355 ("USBS")
    uint32_t dCSWTag;                // Same tag as CBW
    uint32_t dCSWDataResidue;        // Difference between expected and actual size
    uint8_t  bCSWStatus;             // 0 = Passed, 1 = Failed, 2 = Phase Error
} usb_csw_t;

void pmsc_scsi_init(void);
void pmsc_process_cbw(const uint8_t* cbw_bytes, uint32_t len);
void pmsc_bot_reset(void);
bool pmsc_is_ejected(void);
void pmsc_clear_eject(void);

#ifdef __cplusplus
}
#endif

#endif // __R_USB_PMSC_SCSI_H__

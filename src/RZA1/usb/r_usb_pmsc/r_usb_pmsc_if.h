#ifndef __R_USB_PMSC_IF_H__
#define __R_USB_PMSC_IF_H__

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_pmsc_scsi.h"

#define USB_GET_MAX_LUN          (0xFE00u)
#define USB_MASS_STORAGE_RESET   (0xFF00u)

#define USB_CFG_PMSC_BULK_IN     USB_PIPE1
#define USB_CFG_PMSC_BULK_OUT    USB_PIPE2

#ifdef __cplusplus
extern "C" {
#endif

void R_USB_PmscTask(void);

#ifdef __cplusplus
}
#endif

#endif // __R_USB_PMSC_IF_H__

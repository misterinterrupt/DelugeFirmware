/*
 * Compatibility layer for the ported Renesas FSP USB Peripheral Mass Storage
 * Class driver (r_usb_pmsc) onto the Deluge's RZ/A1 r_usb_basic stack.
 *
 * Provides the FSP types/macros the upstream sources expect, plus the
 * configuration knobs from the RZ/A1 PMSC application note (R01AN3507).
 */

#ifndef R_USB_PMSC_COMPAT_H
#define R_USB_PMSC_COMPAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FSP result codes used by the ported driver */
typedef enum {
    FSP_SUCCESS        = 0,
    FSP_ERR_USB_FAILED = 1,
} fsp_err_t;

#define FSP_PARAMETER_NOT_USED(x) ((void) (x))

/* Bare-metal flag values used by the FSP driver */
#ifndef USB_NO
#define USB_NO (0)
#define USB_YES (1)
#endif

/******************************************************************************
 * Driver configuration (R01AN3507 "Compile Setting" / r_usb_pmsc_config.h)
 ******************************************************************************/

/* Sectors transferred per PCD request. 1..127; each unit costs
 * USB_CFG_PMSC_ATAPI_BLOCK_UNIT bytes of RAM for the staging buffer.
 * Larger values reduce the number of USB task round-trips per host command.
 * 16 sectors (8 KB) keeps the SD card busy while the USB pipe drains. */
#ifndef USB_CFG_PMSC_TRANS_COUNT
#define USB_CFG_PMSC_TRANS_COUNT (16)
#endif

/* Bytes per logical block. The RZ/A1 PMSC driver supports 512 only. */
#ifndef USB_CFG_PMSC_ATAPI_BLOCK_UNIT
#define USB_CFG_PMSC_ATAPI_BLOCK_UNIT (512)
#endif

/* INQUIRY response identity. Must be exactly 8 / 16 / 4 bytes of data. */
#define USB_CFG_PMSC_VENDOR   "Synstrom"
#define USB_CFG_PMSC_PRODUCT "Deluge SD Card  "
#define USB_CFG_PMSC_REVISION "1.00"

#ifdef __cplusplus
}
#endif

#endif /* R_USB_PMSC_COMPAT_H */

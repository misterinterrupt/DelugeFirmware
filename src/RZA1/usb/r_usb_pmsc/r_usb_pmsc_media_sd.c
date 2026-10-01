/*
 * Media driver for the ported Renesas PMSC mass storage class driver,
 * backed by the Deluge's SDHI driver (the "storage media" of R01AN3507,
 * section 7.4).
 *
 * Called exclusively from the PMSC task context while USB disk mode owns
 * the card, so the calls are plain synchronous sd_read_sect/sd_write_sect
 * - deliberately NOT disk_read()/disk_write(), which interleave audio
 * cluster streaming with card access.
 */

#include "RZA1/usb/r_usb_pmsc/inc/r_usb_pmsc_compat.h"
#include "RZA1/usb/r_usb_pmsc/inc/r_usb_pmsc_driver.h"
#include "RZA1/sdhi/inc/sdif.h"
#include "RZA1/cpu_specific.h"
#include <stdint.h>
#include <stdbool.h>

uint32_t g_media_total_sector = 0;

/* Set when the host ejects the medium (START STOP UNIT with LoEj+Start=0),
 * queried by the USB Disk Mode menu to offer a graceful exit. */
static bool g_pmsc_media_ejected = false;

bool pmsc_is_ejected(void) {
    return g_pmsc_media_ejected;
}

void pmsc_clear_eject(void) {
    g_pmsc_media_ejected = false;
}

/* Called by the ATAPI driver on START STOP UNIT. unload: LoEj asserted. */
void pmsc_media_start_stop(bool unload) {
    g_pmsc_media_ejected = unload;
}

fsp_err_t r_usb_pmsc_media_initialize(void* p_context) {
    (void) p_context;

    unsigned long user_size = 0;
    if (sd_get_size(SD_PORT, &user_size, NULL) == SD_OK && user_size > 0) {
        g_media_total_sector = (uint32_t) user_size;
        g_pmsc_media_ejected = false;
        return FSP_SUCCESS;
    }
    return FSP_ERR_USB_FAILED;
}

fsp_err_t r_usb_pmsc_media_close(void) {
    return FSP_SUCCESS;
}

fsp_err_t r_usb_pmsc_media_read(uint8_t* const p_rbuffer, uint32_t const start_block,
                                uint8_t const block_count) {
    if (sd_read_sect(SD_PORT, p_rbuffer, start_block, block_count) != 0) {
        return FSP_ERR_USB_FAILED;
    }
    return FSP_SUCCESS;
}

fsp_err_t r_usb_pmsc_media_write(uint8_t const* const p_wbuffer, uint32_t const start_block,
                                 uint8_t const block_count) {
    if (sd_write_sect(SD_PORT, p_wbuffer, start_block, block_count, 0x0001u) != 0) {
        return FSP_ERR_USB_FAILED;
    }
    return FSP_SUCCESS;
}

void r_usb_pmsc_media_ioctl(usb_ioctl_cmd_t ioctl_cmd, uint32_t* ioctl_data) {
    unsigned long user_size = 0;

    switch (ioctl_cmd) {
        case USB_MEDIA_IOCTL_GET_NUM_BLOCKS:
            if (sd_get_size(SD_PORT, &user_size, NULL) == SD_OK && user_size > 0) {
                g_media_total_sector = (uint32_t) user_size;
            }
            *ioctl_data = g_media_total_sector;
            break;

        case USB_MEDIA_IOCTL_GET_BLOCK_SIZE:
            *ioctl_data = USB_CFG_PMSC_ATAPI_BLOCK_UNIT;
            break;

        case USB_MEDIA_IOCTL_GET_SECTOR_SIZE:
            *ioctl_data = USB_CFG_PMSC_ATAPI_BLOCK_UNIT;
            break;

        case USB_MEDIA_IOCTL_GET_WRITE_PROTECT_INFO:
            *ioctl_data = (uint32_t) (sd_iswp(SD_PORT) != 0);
            break;

        case USB_MEDIA_IOCTL_SYNC:
        default:
            /* No write-back cache sits between the PMSC driver and the card. */
            break;
    }
}

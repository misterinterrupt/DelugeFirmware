#include "r_usb_pmsc_scsi.h"
#include "RZA1/sdhi/inc/sdif.h"
#include "diskio.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include <string.h>

typedef enum {
    PMSC_STATE_IDLE = 0,
    PMSC_STATE_WAIT_CBW,
    PMSC_STATE_DATA_IN,
    PMSC_STATE_DATA_OUT,
    PMSC_STATE_SEND_CSW
} pmsc_state_t;

static volatile pmsc_state_t g_pmsc_state = PMSC_STATE_IDLE;
static bool g_pmsc_ejected = false;
static bool g_pmsc_active = false;
static uint32_t g_sd_sector_count = 0;

static uint8_t g_cbw_buffer[31] __attribute__((aligned(4)));
static uint8_t g_sec_buf[512] __attribute__((aligned(4)));

static uint32_t g_cbw_tag = 0;
static uint32_t g_cbw_transfer_len = 0;
static uint32_t g_scsi_lba = 0;
static uint16_t g_scsi_sector_count = 0;
static uint16_t g_scsi_sector_index = 0;
static uint8_t g_scsi_status = 0;

static usb_csw_t g_csw __attribute__((aligned(4)));

static void send_csw(uint32_t tag, uint32_t residue, uint8_t status);
static void send_scsi_data(uint32_t tag, const uint8_t* data, uint32_t len, uint32_t expected_len);

void pmsc_scsi_init(void) {
    g_pmsc_ejected = false;
    g_pmsc_active = true;
    g_pmsc_state = PMSC_STATE_IDLE;
    DSTATUS stat = disk_initialize(0);
    if ((stat & STA_NOINIT) == 0) {
        DWORD sector_count = 0;
        if (disk_ioctl(0, GET_SECTOR_COUNT, &sector_count) == RES_OK) {
            g_sd_sector_count = (uint32_t)sector_count;
        }
    }
}

bool pmsc_is_ejected(void) {
    return g_pmsc_ejected;
}

void pmsc_clear_eject(void) {
    g_pmsc_ejected = false;
    g_pmsc_active = false;
    g_pmsc_state = PMSC_STATE_IDLE;
}

void usb_pmsc_task(void) {
}

void usb_pmsc_mass_strage_reset(uint16_t value, uint16_t index, uint16_t length) {
    usb_pmsc_receive_cbw();
}

void usb_pmsc_get_max_lun(uint16_t value, uint16_t index, uint16_t length) {
    if (!g_pmsc_active) return;
    static uint8_t max_lun = 0;
    usb_ctrl_t ctrl;
    ctrl.module = USB_IP0;
    ctrl.type = USB_REQUEST;
    R_USB_Write(&ctrl, &max_lun, 1);
}

void usb_pmsc_receive_cbw(void) {
    if (!g_pmsc_active) return;
    g_pmsc_state = PMSC_STATE_WAIT_CBW;
    usb_ctrl_t ctrl;
    ctrl.module = USB_IP0;
    ctrl.type = USB_PMSC;
    R_USB_Read(&ctrl, g_cbw_buffer, sizeof(usb_cbw_t));
}

void usb_pmsc_read_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2) {
    if (!g_pmsc_active) return;

    if (g_pmsc_state == PMSC_STATE_WAIT_CBW) {
        pmsc_process_cbw(g_cbw_buffer, sizeof(usb_cbw_t));
    } else if (g_pmsc_state == PMSC_STATE_DATA_OUT) {
        if (disk_write(0, g_sec_buf, g_scsi_lba + g_scsi_sector_index, 1) != RES_OK) {
            g_scsi_status = 1;
        }
        g_scsi_sector_index++;
        if (g_scsi_sector_index < g_scsi_sector_count && g_scsi_status == 0) {
            usb_ctrl_t ctrl;
            ctrl.module = USB_IP0;
            ctrl.type = USB_PMSC;
            R_USB_Read(&ctrl, g_sec_buf, 512);
        } else {
            uint32_t transferred = g_scsi_sector_index * 512;
            uint32_t residue = (g_cbw_transfer_len > transferred) ? (g_cbw_transfer_len - transferred) : 0;
            send_csw(g_cbw_tag, residue, g_scsi_status);
        }
    }
}

void usb_pmsc_write_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2) {
    if (!g_pmsc_active) return;

    if (g_pmsc_state == PMSC_STATE_DATA_IN) {
        if (g_scsi_sector_count > 0) {
            g_scsi_sector_index++;
            if (g_scsi_sector_index < g_scsi_sector_count && g_scsi_status == 0) {
                if (disk_read(0, g_sec_buf, g_scsi_lba + g_scsi_sector_index, 1) != RES_OK) {
                    g_scsi_status = 1;
                }
                usb_ctrl_t ctrl;
                ctrl.module = USB_IP0;
                ctrl.type = USB_PMSC;
                R_USB_Write(&ctrl, g_sec_buf, 512);
                return;
            }
            uint32_t transferred = g_scsi_sector_index * 512;
            uint32_t residue = (g_cbw_transfer_len > transferred) ? (g_cbw_transfer_len - transferred) : 0;
            send_csw(g_cbw_tag, residue, g_scsi_status);
        } else {
            uint32_t residue = (g_cbw_transfer_len > sizeof(g_sec_buf)) ? (g_cbw_transfer_len - sizeof(g_sec_buf)) : 0;
            send_csw(g_cbw_tag, residue, 0);
        }
    } else if (g_pmsc_state == PMSC_STATE_SEND_CSW) {
        usb_pmsc_receive_cbw();
    }
}

static void send_csw(uint32_t tag, uint32_t residue, uint8_t status) {
    g_csw.dCSWSignature = 0x53425355; // "USBS"
    g_csw.dCSWTag = tag;
    g_csw.dCSWDataResidue = residue;
    g_csw.bCSWStatus = status;

    g_pmsc_state = PMSC_STATE_SEND_CSW;
    usb_ctrl_t ctrl;
    ctrl.module = USB_IP0;
    ctrl.type = USB_PMSC;
    R_USB_Write(&ctrl, (uint8_t*)&g_csw, sizeof(g_csw));
}

static void send_scsi_data(uint32_t tag, const uint8_t* data, uint32_t len, uint32_t expected_len) {
    g_cbw_tag = tag;
    g_cbw_transfer_len = expected_len;
    g_scsi_sector_count = 0;

    if (len > sizeof(g_sec_buf)) len = sizeof(g_sec_buf);
    memcpy(g_sec_buf, data, len);

    g_pmsc_state = PMSC_STATE_DATA_IN;
    usb_ctrl_t ctrl;
    ctrl.module = USB_IP0;
    ctrl.type = USB_PMSC;
    R_USB_Write(&ctrl, g_sec_buf, len);
}

void pmsc_process_cbw(const uint8_t* cbw_bytes, uint32_t len) {
    if (len < sizeof(usb_cbw_t)) {
        usb_pmsc_receive_cbw();
        return;
    }

    const usb_cbw_t* cbw = (const usb_cbw_t*)cbw_bytes;
    if (cbw->dCBWSignature != 0x43425355) { // "USBC"
        usb_pmsc_receive_cbw();
        return;
    }

    uint8_t opcode = cbw->CBWCB[0];

    switch (opcode) {
    case 0x00: // TEST UNIT READY
        send_csw(cbw->dCBWTag, 0, g_pmsc_ejected ? 1 : 0);
        break;

    case 0x12: { // INQUIRY
        static const uint8_t inq_data[36] = {
            0x00, // Direct Access Block Device
            0x80, // Removable medium
            0x02, // ANSI SCSI-2
            0x02, // Response Data Format
            31,   // Additional Length
            0, 0, 0,
            'S','y','n','t','h','s','t','r',
            'D','e','l','u','g','e',' ','S','D',' ','C','a','r','d',' ',' ',
            '1','.','0','0'
        };
        send_scsi_data(cbw->dCBWTag, inq_data, sizeof(inq_data), cbw->dCBWDataTransferLength);
        break;
    }

    case 0x25: { // READ CAPACITY (10)
        uint32_t last_lba = (g_sd_sector_count > 0) ? (g_sd_sector_count - 1) : 0;
        uint8_t cap_data[8];
        cap_data[0] = (uint8_t)(last_lba >> 24);
        cap_data[1] = (uint8_t)(last_lba >> 16);
        cap_data[2] = (uint8_t)(last_lba >> 8);
        cap_data[3] = (uint8_t)(last_lba & 0xFF);
        cap_data[4] = 0x00;
        cap_data[5] = 0x00;
        cap_data[6] = 0x02; // Sector size 512
        cap_data[7] = 0x00;
        send_scsi_data(cbw->dCBWTag, cap_data, sizeof(cap_data), cbw->dCBWDataTransferLength);
        break;
    }

    case 0x23: { // READ FORMAT CAPACITIES
        uint8_t fmt_cap[12] = {
            0x00, 0x00, 0x00, 0x08,
            (uint8_t)(g_sd_sector_count >> 24),
            (uint8_t)(g_sd_sector_count >> 16),
            (uint8_t)(g_sd_sector_count >> 8),
            (uint8_t)(g_sd_sector_count & 0xFF),
            0x02, 0x00, 0x02, 0x00
        };
        send_scsi_data(cbw->dCBWTag, fmt_cap, sizeof(fmt_cap), cbw->dCBWDataTransferLength);
        break;
    }

    case 0x1A: { // MODE SENSE (6)
        static const uint8_t mode6_data[4] = { 0x03, 0x00, 0x00, 0x00 };
        send_scsi_data(cbw->dCBWTag, mode6_data, sizeof(mode6_data), cbw->dCBWDataTransferLength);
        break;
    }

    case 0x5A: { // MODE SENSE (10)
        static const uint8_t mode10_data[8] = { 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        send_scsi_data(cbw->dCBWTag, mode10_data, sizeof(mode10_data), cbw->dCBWDataTransferLength);
        break;
    }

    case 0x03: { // REQUEST SENSE
        uint8_t sense_data[18] = {0};
        sense_data[0] = 0x70;
        sense_data[7] = 0x0A;
        if (g_pmsc_ejected) {
            sense_data[2] = 0x02; // NOT READY
            sense_data[12] = 0x3A; // MEDIUM NOT PRESENT
        } else {
            sense_data[2] = 0x00; // NO SENSE
        }
        send_scsi_data(cbw->dCBWTag, sense_data, sizeof(sense_data), cbw->dCBWDataTransferLength);
        break;
    }

    case 0x1E: // PREVENT ALLOW MEDIUM REMOVAL
        send_csw(cbw->dCBWTag, 0, 0);
        break;

    case 0x28: { // READ (10)
        g_scsi_lba = (cbw->CBWCB[2] << 24) | (cbw->CBWCB[3] << 16) | (cbw->CBWCB[4] << 8) | cbw->CBWCB[5];
        g_scsi_sector_count = (cbw->CBWCB[7] << 8) | cbw->CBWCB[8];
        g_scsi_sector_index = 0;
        g_scsi_status = 0;
        g_cbw_tag = cbw->dCBWTag;
        g_cbw_transfer_len = cbw->dCBWDataTransferLength;

        if (g_scsi_sector_count == 0) {
            send_csw(g_cbw_tag, 0, 0);
        } else {
            if (disk_read(0, g_sec_buf, g_scsi_lba, 1) != RES_OK) {
                g_scsi_status = 1;
            }
            g_pmsc_state = PMSC_STATE_DATA_IN;
            usb_ctrl_t ctrl;
            ctrl.module = USB_IP0;
            ctrl.type = USB_PMSC;
            R_USB_Write(&ctrl, g_sec_buf, 512);
        }
        break;
    }

    case 0x2A: { // WRITE (10)
        g_scsi_lba = (cbw->CBWCB[2] << 24) | (cbw->CBWCB[3] << 16) | (cbw->CBWCB[4] << 8) | cbw->CBWCB[5];
        g_scsi_sector_count = (cbw->CBWCB[7] << 8) | cbw->CBWCB[8];
        g_scsi_sector_index = 0;
        g_scsi_status = 0;
        g_cbw_tag = cbw->dCBWTag;
        g_cbw_transfer_len = cbw->dCBWDataTransferLength;

        if (g_scsi_sector_count == 0) {
            send_csw(g_cbw_tag, 0, 0);
        } else {
            g_pmsc_state = PMSC_STATE_DATA_OUT;
            usb_ctrl_t ctrl;
            ctrl.module = USB_IP0;
            ctrl.type = USB_PMSC;
            R_USB_Read(&ctrl, g_sec_buf, 512);
        }
        break;
    }

    case 0x1B: // START STOP UNIT (Eject Command)
        if ((cbw->CBWCB[4] & 0x03) == 0x02) { // Eject / unload
            g_pmsc_ejected = true;
        }
        send_csw(cbw->dCBWTag, 0, 0);
        break;

    default:
        send_csw(cbw->dCBWTag, cbw->dCBWDataTransferLength, 1);
        break;
    }
}

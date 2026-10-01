/*
 * Renesas RZ/A1 USB Peripheral Mass Storage Class (PMSC) Descriptors
 */

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"

#define USB_VENDORID (0x16D0)  /* Vendor ID */
#define USB_PRODUCTID (0x0CE3) /* Product ID for Mass Storage Mode */

#define PMSC_CONFIG_LENGTH (9 + 9 + 7 + 7) // Config + Interface + EP IN + EP OUT = 32 bytes

uint8_t g_pmsc_device[USB_DD_BLENGTH + (USB_DD_BLENGTH % 2)] = {
    USB_DD_BLENGTH,        /*  0:bLength */
    USB_DT_DEVICE,         /*  1:bDescriptorType */
    0x00, 0x02,            /*  2-3: bcdUSB 2.0 */
    0x00,                  /*  4:bDeviceClass (0 = defined in interface) */
    0x00,                  /*  5:bDeviceSubClass */
    0x00,                  /*  6:bDeviceProtocol */
    64,                    /*  7:bMaxPacketSize0 */
    (uint8_t)(USB_VENDORID & 0xFF),
    (uint8_t)(USB_VENDORID >> 8),
    (uint8_t)(USB_PRODUCTID & 0xFF),
    (uint8_t)(USB_PRODUCTID >> 8),
    0x00, 0x01,            /* 12-13: bcdDevice */
    1,                     /* 14: iManufacturer */
    2,                     /* 15: iProduct */
    3,                     /* 16: iSerialNumber */
    1                      /* 17: bNumConfigurations */
};

uint8_t g_pmsc_configuration[PMSC_CONFIG_LENGTH + (PMSC_CONFIG_LENGTH % 2)] = {
    // Configuration Descriptor
    9,                      /*  0: bLength */
    USB_DT_CONFIGURATION,   /*  1: bDescriptorType */
    PMSC_CONFIG_LENGTH, 0,  /*  2-3: wTotalLength */
    1,                      /*  4: bNumInterfaces */
    1,                      /*  5: bConfigurationValue */
    0,                      /*  6: iConfiguration */
    0xC0,                   /*  7: bmAttributes (Self Powered) */
    250,                    /*  8: bMaxPower (500mA) */

    // Interface Descriptor (Mass Storage Class)
    9,                      /*  0: bLength */
    USB_DT_INTERFACE,       /*  1: bDescriptorType */
    0,                      /*  2: bInterfaceNumber */
    0,                      /*  3: bAlternateSetting */
    2,                      /*  4: bNumEndpoints */
    0x08,                   /*  5: bInterfaceClass (Mass Storage) */
    0x06,                   /*  6: bInterfaceSubClass (SCSI Transparent) */
    0x50,                   /*  7: bInterfaceProtocol (Bulk-Only Transport) */
    0,                      /*  8: iInterface */

    // Endpoint Descriptor (Bulk IN)
    7,                      /*  0: bLength */
    USB_DT_ENDPOINT,        /*  1: bDescriptorType */
    0x81,                   /*  2: bEndpointAddress (EP1 IN) */
    0x02,                   /*  3: bmAttributes (Bulk) */
    64, 0,                  /*  4-5: wMaxPacketSize (64 bytes FS) */
    0,                      /*  6: bInterval */

    // Endpoint Descriptor (Bulk OUT)
    7,                      /*  0: bLength */
    USB_DT_ENDPOINT,        /*  1: bDescriptorType */
    0x02,                   /*  2: bEndpointAddress (EP2 OUT) */
    0x02,                   /*  3: bmAttributes (Bulk) */
    64, 0,                  /*  4-5: wMaxPacketSize (64 bytes FS) */
    0                       /*  6: bInterval */
};

static uint8_t g_pmsc_string_langid[] = { 4, 3, 0x09, 0x04 };
static uint8_t g_pmsc_string_vendor[] = { 22, 3, 'S', 0, 'y', 0, 'n', 0, 't', 0, 'h', 0, 's', 0, 't', 0, 'r', 0, 'o', 0, 'm', 0 };
static uint8_t g_pmsc_string_product[] = { 30, 3, 'D', 0, 'e', 0, 'l', 0, 'u', 0, 'g', 0, 'e', 0, ' ', 0, 'S', 0, 'D', 0, ' ', 0, 'C', 0, 'a', 0, 'r', 0, 'd', 0 };
static uint8_t g_pmsc_string_serial[] = { 18, 3, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '1', 0 };

uint8_t* g_pmsc_string_table[] = {
    g_pmsc_string_langid,
    g_pmsc_string_vendor,
    g_pmsc_string_product,
    g_pmsc_string_serial,
    0
};

/* Device Qualifier: describes the device's capabilities at the "other" speed.
 * Required by hosts negotiating High-Speed. */
uint8_t g_pmsc_qualifier[USB_QD_BLENGTH] = {
    USB_QD_BLENGTH,        /*  0: bLength */
    USB_DT_DEVICE_QUALIFIER, /*  1: bDescriptorType */
    0x00, 0x02,            /*  2-3: bcdUSB 2.0 */
    0x00,                  /*  4: bDeviceClass */
    0x00,                  /*  5: bDeviceSubClass */
    0x00,                  /*  6: bDeviceProtocol */
    64,                    /*  7: bMaxPacketSize0 */
    1,                     /*  8: bNumConfigurations */
    0                      /*  9: bReserved */
};

/* Configuration for High-Speed operation: identical layout to the FS
 * configuration, but the bulk endpoints use 512-byte packets.
 * (The driver swaps the bDescriptorType bytes of this and the FS
 * configuration when reporting at the respective speed - see
 * usb_peri_devdefault().) */
uint8_t g_pmsc_configuration_hs[PMSC_CONFIG_LENGTH + (PMSC_CONFIG_LENGTH % 2)] = {
    // Configuration Descriptor
    9,                      /*  0: bLength */
    USB_DT_OTHER_SPEED_CONF,/*  1: bDescriptorType (swapped at runtime) */
    PMSC_CONFIG_LENGTH, 0,  /*  2-3: wTotalLength */
    1,                      /*  4: bNumInterfaces */
    1,                      /*  5: bConfigurationValue */
    0,                      /*  6: iConfiguration */
    0xC0,                   /*  7: bmAttributes (Self Powered) */
    250,                    /*  8: bMaxPower (500mA) */

    // Interface Descriptor (Mass Storage Class)
    9,                      /*  0: bLength */
    USB_DT_INTERFACE,       /*  1: bDescriptorType */
    0,                      /*  2: bInterfaceNumber */
    0,                      /*  3: bAlternateSetting */
    2,                      /*  4: bNumEndpoints */
    0x08,                   /*  5: bInterfaceClass (Mass Storage) */
    0x06,                   /*  6: bInterfaceSubClass (SCSI Transparent) */
    0x50,                   /*  7: bInterfaceProtocol (Bulk-Only Transport) */
    0,                      /*  8: iInterface */

    // Endpoint Descriptor (Bulk IN)
    7,                      /*  0: bLength */
    USB_DT_ENDPOINT,        /*  1: bDescriptorType */
    0x81,                   /*  2: bEndpointAddress (EP1 IN) */
    0x02,                   /*  3: bmAttributes (Bulk) */
    0x00, 0x02,             /*  4-5: wMaxPacketSize (512 bytes HS) */
    0,                      /*  6: bInterval */

    // Endpoint Descriptor (Bulk OUT)
    7,                      /*  0: bLength */
    USB_DT_ENDPOINT,        /*  1: bDescriptorType */
    0x02,                   /*  2: bEndpointAddress (EP2 OUT) */
    0x02,                   /*  3: bmAttributes (Bulk) */
    0x00, 0x02,             /*  4-5: wMaxPacketSize (512 bytes HS) */
    0                       /*  6: bInterval */
};

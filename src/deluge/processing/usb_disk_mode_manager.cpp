#include "usb_disk_mode_manager.h"
#include "ff.h"
#include "RZA1/usb/r_usb_pmsc_scsi.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "processing/engines/audio_engine.h"

extern "C" void openUSBPeripheral();
extern "C" void closeUSBPeripheral();
extern "C" void openUSBPeripheralPMSC();
extern "C" void closeUSBPeripheralPMSC();

#include "playback/playback_handler.h"

extern PlaybackHandler playbackHandler;

namespace deluge::storage {

static FATFS g_diskModeFatFs;

void UsbDiskModeManager::enterUsbDiskMode() {
	if (active_) {
		return;
	}

	D_PRINTLN("Entering USB Disk Mode: Stopping playback and unmounting FatFs...");

	// 1. End active playback and recording
	playbackHandler.endPlayback();

	// 2. Unmount FatFs filesystem safely to hand block-level control to host PC
	f_mount(NULL, "", 0);

	// 3. Switch USB Peripheral mode to Mass Storage Class (PMSC)
	closeUSBPeripheral();
	pmsc_scsi_init();
	openUSBPeripheralPMSC();

	active_ = true;

	if (display->haveOLED()) {
		display->popupText("USB DISK MODE\nSD Card on PC");
	}
	else {
		display->displayPopup("USb.d");
	}
}

void UsbDiskModeManager::exitUsbDiskMode() {
	if (!active_) {
		return;
	}

	D_PRINTLN("Exiting USB Disk Mode: Restoring USB MIDI and remounting FatFs...");

	// 1. Teardown PMSC endpoints and re-initialize USB stack back into USB MIDI
	closeUSBPeripheralPMSC();
	pmsc_clear_eject();
	openUSBPeripheral();

	// 2. Remount FatFs filesystem
	f_mount(&g_diskModeFatFs, "", 1);

	active_ = false;

	if (display->haveOLED()) {
		display->popupText("Disk Ejected\nEngine Resumed");
	}
	else {
		display->displayPopup("READY");
	}
}

} // namespace deluge::storage

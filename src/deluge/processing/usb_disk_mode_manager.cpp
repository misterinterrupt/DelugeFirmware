#include "usb_disk_mode_manager.h"
#include "ff.h"
#include "RZA1/usb/r_usb_pmsc_scsi.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "playback/playback_handler.h"

extern "C" void openUSBPeripheral();
extern "C" void closeUSBPeripheral();
extern "C" void openUSBPeripheralPMSC();
extern "C" void closeUSBPeripheralPMSC();

extern PlaybackHandler playbackHandler;
extern AudioFileManager audioFileManager;
extern bool sdRoutineLock;
extern volatile uint32_t usbLock;

namespace deluge::storage {

static FATFS g_diskModeFatFs;

void UsbDiskModeManager::enterUsbDiskMode() {
	if (active_) {
		return;
	}

	D_PRINTLN("Entering USB Disk Mode: Stopping playback and unmounting FatFs...");

	// 1. End active playback and recording, and unassign all audio voices
	playbackHandler.endPlayback();
	AudioEngine::unassignAllVoices(true);

	// 2. Mark card ejected in audioFileManager and lock SD routine access
	audioFileManager.cardEjected = true;
	sdRoutineLock = true;

	// 3. Unmount FatFs filesystem safely to hand block-level control to host PC
	f_mount(NULL, "", 0);

	// 4. Lock USB stack and switch USB Peripheral mode to Mass Storage Class (PMSC)
	usbLock = 1;
	closeUSBPeripheral();
	pmsc_scsi_init();
	openUSBPeripheralPMSC();
	usbLock = 0;

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

	// 1. Lock USB stack, teardown PMSC endpoints and re-initialize USB stack back into USB MIDI
	usbLock = 1;
	closeUSBPeripheralPMSC();
	pmsc_clear_eject();
	openUSBPeripheral();
	usbLock = 0;

	// 2. Remount FatFs filesystem
	f_mount(&g_diskModeFatFs, "", 1);

	// 3. Restore SD routine lock and card status
	sdRoutineLock = false;
	audioFileManager.cardEjected = false;

	active_ = false;

	if (display->haveOLED()) {
		display->popupText("Disk Ejected\nEngine Resumed");
	}
	else {
		display->displayPopup("READY");
	}
}

} // namespace deluge::storage

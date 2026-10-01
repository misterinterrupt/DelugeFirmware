#include "usb_disk_mode_manager.h"
#include "ff.h"
#include "RZA1/usb/r_usb_pmsc/inc/r_usb_pmsc.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "playback/playback_handler.h"
#include "OSLikeStuff/task_scheduler.h"

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

// Watches for the host ejecting the medium (START STOP UNIT via the PMSC
// driver) and exits disk mode automatically, wherever the UI happens to be.
static uint8_t g_ejectWatchTaskId = 0;

static void ejectWatchRoutine() {
	if (pmsc_is_ejected()) {
		UsbDiskModeManager::getInstance().exitUsbDiskMode();
	}
}

void UsbDiskModeManager::enterUsbDiskMode() {
	if (active_) {
		return;
	}

	D_PRINTLN("Entering USB Disk Mode: Stopping playback and unmounting FatFs...");

	// 1. End active playback and recording, and unassign all audio voices
	playbackHandler.endPlayback();
	AudioEngine::unassignAllVoices(true);

	// 2. Mark card disabled in audioFileManager so background routines don't touch SD
	audioFileManager.cardDisabled = true;

	// 3. Unmount FatFs filesystem safely to hand block-level control to host PC
	f_mount(NULL, "", 0);

	// 4. Lock USB stack and switch USB Peripheral mode to Mass Storage Class (PMSC)
	usbLock = 1;
	closeUSBPeripheral();
	usb_pmsc_init(); // canonical PMSC driver state reset; the task self-inits ATAPI on first pump
	openUSBPeripheralPMSC();
	usbLock = 0;

	active_ = true;

	g_ejectWatchTaskId = addRepeatingTask(&ejectWatchRoutine, 32, 0.1, 0.25, 0.5, "usbdisk eject watch");

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

	// 3. Restore SD card status
	audioFileManager.cardDisabled = false;

	if (g_ejectWatchTaskId) {
		removeTask(g_ejectWatchTaskId);
		g_ejectWatchTaskId = 0;
	}

	active_ = false;

	if (display->haveOLED()) {
		display->popupText("Disk Ejected\nEngine Resumed");
	}
	else {
		display->displayPopup("READY");
	}
}

} // namespace deluge::storage

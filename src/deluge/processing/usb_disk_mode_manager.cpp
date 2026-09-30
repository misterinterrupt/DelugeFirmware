#include "usb_disk_mode_manager.h"
#include "ff.h"
#include "RZA1/usb/r_usb_pmsc_scsi.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "playback/playback_handler.h"
#include <cstdio>
#include "OSLikeStuff/task_scheduler.h"

extern "C" void openUSBPeripheral();
extern "C" void closeUSBPeripheral();
extern "C" void openUSBPeripheralPMSC();
extern "C" void closeUSBPeripheralPMSC();

// Temporary disk-mode bring-up diagnostics from the USB driver
extern "C" volatile uint32_t g_usb_dbg_reset;
extern "C" volatile uint32_t g_usb_dbg_ctrt;
extern "C" volatile uint32_t g_usb_dbg_brdy0;
extern "C" volatile uint32_t g_usb_dbg_pump;
extern "C" volatile uint32_t g_usb_dbg_gdesc;

extern PlaybackHandler playbackHandler;
extern AudioFileManager audioFileManager;
extern bool sdRoutineLock;
extern volatile uint32_t usbLock;

namespace deluge::storage {

static FATFS g_diskModeFatFs;

// TEMPORARY: render USB event counters on the display while disk mode is active,
// so enumeration failures can be triaged from the device itself.
// R = bus resets, C = SETUP interrupts, D = GET_DESCRIPTOR handled, P = PCD events.
static uint8_t g_usbDebugTaskId = 0;

static void renderUsbDebugCounters() {
	char buf[64];
	snprintf(buf, sizeof(buf), "R%lu C%lu D%lu P%lu", (unsigned long)g_usb_dbg_reset,
	         (unsigned long)g_usb_dbg_ctrt, (unsigned long)g_usb_dbg_gdesc, (unsigned long)g_usb_dbg_pump);
	display->setText(buf);
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
	pmsc_scsi_init();
	openUSBPeripheralPMSC();
	usbLock = 0;

	active_ = true;

	// Temporary diagnostics readout (see renderUsbDebugCounters)
	g_usbDebugTaskId = addRepeatingTask([]() { renderUsbDebugCounters(); }, 35, 0.05, 0.2, 0.5, "usb dbg");
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

	active_ = false;

	if (g_usbDebugTaskId) {
		removeTask(g_usbDebugTaskId);
		g_usbDebugTaskId = 0;
	}
}

} // namespace deluge::storage

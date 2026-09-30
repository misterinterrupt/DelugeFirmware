#pragma once

#include <cstdint>
#include <stdbool.h>

namespace deluge::storage {

class UsbDiskModeManager {
public:
	static UsbDiskModeManager& getInstance() {
		static UsbDiskModeManager instance;
		return instance;
	}

	bool isUsbDiskModeActive() const { return active_; }
	bool isActive() const { return active_; }

	/// Enter USB Disk Mode: halt audio engine, flush/unmount FatFs, init USB PMSC
	void enterUsbDiskMode();

	/// Exit USB Disk Mode: teardown PMSC, restore USB MIDI, remount FatFs, restart engine
	void exitUsbDiskMode();

private:
	UsbDiskModeManager() = default;
	bool active_ = false;
};

} // namespace deluge::storage

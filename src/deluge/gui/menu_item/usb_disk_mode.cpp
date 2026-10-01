/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "usb_disk_mode.h"
#include "RZA1/usb/r_usb_pmsc/inc/r_usb_pmsc.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "processing/usb_disk_mode_manager.h"

extern SoundEditor soundEditor;

namespace deluge::gui::menu_item {

UsbDiskMode usbDiskModeMenuItem;

UsbDiskMode::UsbDiskMode()
    : MenuItem(deluge::l10n::String::STRING_FOR_USB_DISK_MODE, deluge::l10n::String::STRING_FOR_USB_DISK_MODE) {
}

void UsbDiskMode::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	deluge::storage::UsbDiskModeManager::getInstance().enterUsbDiskMode();
	drawValue();
}

MenuItem* UsbDiskMode::selectButtonPress() {
	if (deluge::storage::UsbDiskModeManager::getInstance().isActive()) {
		deluge::storage::UsbDiskModeManager::getInstance().exitUsbDiskMode();
	}
	return nullptr;
}

ActionResult UsbDiskMode::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (on && (b == deluge::hid::button::BACK || b == deluge::hid::button::SELECT_ENC)) {
		if (deluge::storage::UsbDiskModeManager::getInstance().isActive()) {
			deluge::storage::UsbDiskModeManager::getInstance().exitUsbDiskMode();
		}
		if (soundEditor.getCurrentMenuItem() == this) {
			soundEditor.goUpOneLevel();
		}
		return ActionResult::DEALT_WITH;
	}
	return ActionResult::DEALT_WITH;
}

ActionResult UsbDiskMode::timerCallback() {
	if (deluge::storage::UsbDiskModeManager::getInstance().isActive() && pmsc_is_ejected()) {
		deluge::storage::UsbDiskModeManager::getInstance().exitUsbDiskMode();
		if (soundEditor.getCurrentMenuItem() == this) {
			soundEditor.goUpOneLevel();
		}
	}
	return ActionResult::DEALT_WITH;
}

void UsbDiskMode::renderOLED() {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	canvas.clear();
	canvas.drawStringCentredShrinkIfNecessary("USB DISK MODE", 22, 12, 16);
	canvas.drawStringCentredShrinkIfNecessary("SD Card on PC", 22, 32, 14);
}

void UsbDiskMode::drawValue() {
	if (display->haveOLED()) {
		renderOLED();
	}
	else {
		display->setText("USb.d");
	}
}

} // namespace deluge::gui::menu_item

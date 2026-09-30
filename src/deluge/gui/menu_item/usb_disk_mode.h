/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "gui/menu_item/menu_item.h"

namespace deluge::gui::menu_item {

class UsbDiskMode final : public MenuItem {
public:
	UsbDiskMode();

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	MenuItem* selectButtonPress() override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult timerCallback() override;
	void renderOLED() override;
	void drawValue();
};

extern UsbDiskMode usbDiskModeMenuItem;

} // namespace deluge::gui::menu_item

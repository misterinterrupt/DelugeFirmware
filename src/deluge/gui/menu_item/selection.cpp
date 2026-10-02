#include "selection.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item {
void Selection::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	if (display->have7SEG()) {
		const auto options = this->getOptions();
		display->setScrollingText(options[this->getValue()].data());
	}
}

void Selection::drawPixelsForOled() {
	int32_t current = getValue();
	// Earliest item that might be on the screen.
	int32_t before = std::max<int32_t>(0, current - kOLEDMenuNumOptionsVisible);
	int32_t nBefore = current - before;
	// First item offscreen.
	int32_t after = std::min<int32_t>(current + kOLEDMenuNumOptionsVisible, size());
	int32_t nAfter = after - current;

	// Ideally we'd have the selected item in the middle (rounding down for even cases)
	// ...but sometimes that's not going to happen.
	size_t pos = (kOLEDMenuNumOptionsVisible - 1) / 2;
	size_t tail = kOLEDMenuNumOptionsVisible - pos;
	if (nBefore < pos) {
		pos = nBefore;
	}
	else if (nAfter < tail) {
		tail = nAfter;
		pos = std::min<int32_t>(kOLEDMenuNumOptionsVisible - tail, nBefore);
	}

	MenuItem::drawItemsForOled(std::span{getOptions().data(), getOptions().size()}, pos, current - pos);
}

// renders check box on OLED and dot on 7seg
void Selection::displayToggleValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawName();
	}
}

// handles rendering of the community features menu items that are identified as toggles
void Selection::renderSubmenuItemTypeForOled(int32_t yPixel) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t startX = getSubmenuItemTypeRenderIconStart();

	if (isToggle()) {
		if (getToggleValue()) {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);
		}
		else {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);
		}
	}
	else {
		image.drawGraphicMultiLine(deluge::hid::display::OLED::submenuArrowIcon, startX, yPixel, kSubmenuIconSpacingX);
	}
}

void Selection::formatValueForVisualMenu(char* outBuffer, size_t bufferSize) {
	if (outBuffer && bufferSize > 0) {
		auto opts = this->getOptions();
		int32_t val = this->getValue();
		if (val >= 0 && val < static_cast<int32_t>(opts.size())) {
			std::string_view optStr = opts[val];
			size_t len = std::min(optStr.length(), bufferSize - 1);
			memcpy(outBuffer, optStr.data(), len);
			outBuffer[len] = '\0';
		}
		else {
			intToString(val, outBuffer);
		}
	}
}

} // namespace deluge::gui::menu_item

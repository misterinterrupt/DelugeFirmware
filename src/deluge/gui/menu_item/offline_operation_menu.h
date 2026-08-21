#pragma once

#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/submenu.h"
#include <string_view>

#include "gui/menu_item/toggle.h"

namespace deluge::gui::menu_item {

extern bool gOfflineJobOverwrite;

class SaveModeMenuItem : public ToggleBool {
public:
	SaveModeMenuItem();
	std::string_view getName() const override { return "OVERWRITE"; }
};

class NormalizeActionMenuItem : public Integer {
public:
	NormalizeActionMenuItem();
	std::string_view getName() const override { return "NORMALIZE"; }
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;

protected:
	int32_t getMaxValue() const override { return 300; }   // +3.00 dB
	int32_t getMinValue() const override { return -2400; } // -24.00 dB
	void drawPixelsForOled() override;
	void drawValue() override;
};

class ReverseActionMenuItem : public MenuItem {
public:
	ReverseActionMenuItem();
	std::string_view getName() const override { return "REVERSE"; }
	void readValueAgain() override;
	MenuItem* selectButtonPress() override;
};

class GainActionMenuItem : public Integer {
public:
	GainActionMenuItem();
	std::string_view getName() const override { return "GAIN"; }
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;

protected:
	int32_t getMaxValue() const override { return 600; }  // +6.00 dB
	int32_t getMinValue() const override { return -600; } // -6.00 dB
	void drawPixelsForOled() override;
	void drawValue() override;
};

class LevelerActionMenuItem : public MenuItem {
public:
	LevelerActionMenuItem();
	std::string_view getName() const override { return "LEVELER"; }
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;
	void drawPixelsForOled() override;
	void readValueAgain() override;

private:
	int32_t thresholdVal_ = -600; // -6.00 dB default
};

class OfflineOperationMenu : public Submenu {
public:
	OfflineOperationMenu();
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override;
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override;
};

extern OfflineOperationMenu offlineOperationMenu;

} // namespace deluge::gui::menu_item

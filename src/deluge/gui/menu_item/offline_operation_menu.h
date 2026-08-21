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

class NormalizeActionMenuItem : public MenuItem {
public:
	NormalizeActionMenuItem();
	std::string_view getName() const override { return "NORMALIZE"; }
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;
	void drawPixelsForOled() override;
	void readValueAgain() override;

private:
	int32_t targetCeilingVal_ = 0; // 0.00 dB default
};

class ReverseActionMenuItem : public MenuItem {
public:
	ReverseActionMenuItem();
	std::string_view getName() const override { return "REVERSE"; }
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;
	void drawPixelsForOled() override;
	void readValueAgain() override;

private:
	int32_t modeVal_ = 0; // -1 = DETECT, 0 = FULL, 1..100 = 1%..100% slices
};

class GainActionMenuItem : public MenuItem {
public:
	GainActionMenuItem();
	std::string_view getName() const override { return "GAIN"; }
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;
	void drawPixelsForOled() override;
	void readValueAgain() override;

private:
	int32_t gainVal_ = 0; // 0.00 dB default
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

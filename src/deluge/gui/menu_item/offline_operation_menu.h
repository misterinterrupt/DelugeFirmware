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
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	void drawPixelsForOled() override;
	void readValueAgain() override;

private:
	void updateDistortionConfig();
	int32_t gainVal_ = 0; // 0.00 dB default
	int32_t activePage_ = 0; // 0 = Page 1 (GAIN/Waveform), 1 = Page 2 (DISTORTION)

	uint8_t distType_ = 0; // 0 = OFF, 1 = SATURATE, 2 = FOLD, 3 = BITCRUSH, 4 = RECTIFY
	int32_t distSelectedParam_ = 0; // 0 = TYPE, 1 = PARAM1, 2 = PARAM2

	int32_t satDrive_ = 30; // 3.0x
	int32_t satWarmth_ = 20; // 20%

	int32_t foldFolds_ = 2; // 2 folds
	int32_t foldBias_ = 0; // 0%

	int32_t crushBits_ = 8; // 8 bit
	int32_t crushDownsample_ = 4; // 4x

	int32_t rectMode_ = 0; // HALF
	int32_t rectCrossover_ = 10; // 10%

	int32_t fmDepth_ = 30; // 3.0x
	int32_t fmFeedback_ = 40; // 40%

	int32_t ringFreq_ = 440; // 440 Hz
	int32_t ringMix_ = 50; // 50%

	int32_t distOutGainVal_ = 0; // 0.0 dB default (-48.0 dB .. +48.0 dB)
};

MenuItem* getGainActionMenuItem();

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

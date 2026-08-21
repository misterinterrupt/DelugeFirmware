#pragma once

#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/submenu.h"
#include <string_view>

#include "gui/menu_item/toggle.h"
#include "model/sample/sample.h"

namespace deluge::gui::menu_item {

extern bool gOfflineJobOverwrite;

class OfflineWaveformCache {
public:
	static constexpr uint32_t kOverviewColumns = 128;

	bool hasValidScan(Sample* sample) const { return scanned_ && (scannedSample_ == sample); }

	void invalidate() {
		scanned_ = false;
		scannedSample_ = nullptr;
	}

	bool scanSample(Sample* sample);

	uint8_t const* getOverview() const { return waveformOverview_; }
	int8_t const* getOverviewMin() const { return waveformOverviewMin_; }
	int8_t const* getOverviewMax() const { return waveformOverviewMax_; }

private:
	bool scanned_ = false;
	Sample* scannedSample_ = nullptr;
	uint8_t waveformOverview_[kOverviewColumns]{0};
	int8_t waveformOverviewMin_[kOverviewColumns]{0};
	int8_t waveformOverviewMax_[kOverviewColumns]{0};
};

extern OfflineWaveformCache gOfflineWaveformCache;

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
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

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
	void drawPixelsForOled() override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
};

class GainActionMenuItem : public Integer {
public:
	GainActionMenuItem();
	std::string_view getName() const override { return "GAIN"; }
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

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
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

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

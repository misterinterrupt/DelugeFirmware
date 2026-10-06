/*
 * Menu items for the modular master FX chain: per-slot BLOC selection, the routing
 * topology algorithm, and the core per-slot bloc params. These live in the visual
 * menu's FX BLOCS pages (song mode); all writes go through the PAU lifecycle —
 * FxBloc::setModel allocates lazily and defers the swap to a render boundary, and
 * param writes are cache-and-defer.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/fx_bloc/fx_bloc.hpp"
#include "dsp/fx_bloc/master_fx_chain.hpp"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/ui.h"
#include "gui/ui/visual_menu/visual_menu_data.h"
#include "gui/ui/visual_menu/visual_menu_ui.h"
#include "hid/display/display.h"
#include "processing/engines/audio_engine.h"
#include "util/misc.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace deluge::gui::menu_item::fx_bloc {

/// Resolves the FX chain for the active editing context: the selected track's Sound /
/// GlobalEffectable when inside a clip, or AudioEngine::masterFxChain when at the song level.
dsp::FxChain* getCurrentFxChain();

/// Selects which bloc a master/track slot holds. COMPRESSOR/FILTER are excluded until their
/// adapters land (selecting them today would be a silent passthrough).
class MasterSlotBloc final : public Selection {
public:
	static constexpr dsp::FxBloc::Model kOptions[] = {
	    dsp::FxBloc::Model::EMPTY,       dsp::FxBloc::Model::FREEVERB, dsp::FxBloc::Model::MUTABLE,
	    dsp::FxBloc::Model::KDFX_REVERB, dsp::FxBloc::Model::KDFX_DELAY,
	    dsp::FxBloc::Model::KDFX_CHORUS, dsp::FxBloc::Model::KDFX_TONE,
	    dsp::FxBloc::Model::KDFX_FLANGER, dsp::FxBloc::Model::KDFX_PHASER,
	    dsp::FxBloc::Model::KDFX_TREMOLO, dsp::FxBloc::Model::KDFX_AUTOPAN,
	    dsp::FxBloc::Model::KDFX_DIST, dsp::FxBloc::Model::KDFX_TUBEAMP,
	    dsp::FxBloc::Model::KDFX_LASER, dsp::FxBloc::Model::KDFX_CHO_DLY,
	    dsp::FxBloc::Model::KDFX_CHO_REV, dsp::FxBloc::Model::KDFX_FLG_DLY,
	    dsp::FxBloc::Model::KDFX_FLG_REV, dsp::FxBloc::Model::KDFX_FLG_LASER,
	    dsp::FxBloc::Model::KDFX_SHPR_REV,
	    dsp::FxBloc::Model::COMPRESSOR, dsp::FxBloc::Model::GLOBAL_SEND,
	};

	MasterSlotBloc(int32_t slotIndex) : Selection(l10n::String::EMPTY_STRING), slot_(slotIndex) {}

	void readCurrentValue() override {
		auto* chain = getCurrentFxChain();
		auto wanted = chain != nullptr ? chain->slot(slot_).getModel() : dsp::FxBloc::Model::EMPTY;
		for (size_t i = 0; i < sizeof(kOptions) / sizeof(kOptions[0]); i++) {
			if (kOptions[i] == wanted) {
				this->setValue(static_cast<int32_t>(i));
				return;
			}
		}
		this->setValue(0); // COMPRESSOR/FILTER legacy states display as EMPTY
	}

	void writeCurrentValue() override {
		auto target = kOptions[this->getValue()];
		auto* chain = getCurrentFxChain();
		if (chain != nullptr) {
			if (!chain->slot(slot_).setModel(target)) {
				// Pool exhausted: surface it and revert the displayed value.
				display->displayError(Error::INSUFFICIENT_RAM);
				readCurrentValue();
				return;
			}
		}
		// The param-page layout follows the newly selected bloc's table; rebuild and
		// rebind the open menu (page count may have changed under the cursor).
		deluge::gui::ui::visual_menu::rebuildFxBlocPages();
		if (getCurrentUI() == &deluge::gui::ui::visual_menu::visualMenuUI) {
			deluge::gui::ui::visual_menu::visualMenuUI.fxBlocPagesRebuilt();
		}
	}

	deluge::vector<std::string_view> getOptions() override {
		// Shown on the full-width Bloc pages, so longer names fit.
		return {"EMPTY",    "FREEVERB", "MUTABLE",    "KDFX REV",    "KDFX DLY",  "KDFX CHO",
		        "KDFX TONE", "KDFX FLANGER", "KDFX PHASER", "KDFX TREMOLO", "KDFX PAN", "KDFX DIST",
		        "KDFX TUBEAMP", "COMPRESSOR", "MST SEND"};
	}

private:
	int32_t slot_;
};

/// Selects the master/track chain's routing topology algorithm.
class MasterRouting final : public Selection {
public:
	MasterRouting() : Selection(l10n::String::EMPTY_STRING) {}

	void readCurrentValue() override {
		auto* chain = getCurrentFxChain();
		if (chain != nullptr) {
			this->setValue(util::to_underlying(chain->getRouting()));
		}
	}

	void writeCurrentValue() override {
		auto* chain = getCurrentFxChain();
		if (chain != nullptr) {
			chain->setRouting(static_cast<dsp::FxChain::Routing>(this->getValue()));
		}
	}

	deluge::vector<std::string_view> getOptions() override {
		// Compact topology diagrams — words get cut off in the 32px visual-menu columns.
		return {"1|2|3", "1+2+3", "1|2+3", "1+2|3"};
	}
};

/// One entry of the selected bloc's param table. The label, kind and backing param
/// all resolve through the slot's CURRENT model, so the pages adapt when the bloc
/// changes — delay shows TIME/FDBK/TONE, chorus RATE/DEPTH/FDBK, and so on.
///
/// Value space: int params step by plain integers; continuous params are stored in
/// units of 1/100 of their display step (step = 10^-decimals of the real unit, 1%
/// for uncalibrated), so the encoder can address two extra decimal places without
/// skipping increments — select-click cycles the decimal position, shift doubles
/// the step (see VisualMenuUI::selectEncoderAction).
class MasterSlotParam final : public Integer {
public:
	static constexpr int32_t kFineScale = 100;

	MasterSlotParam(int32_t slotIndex, int32_t defIndex)
	    : Integer(l10n::String::EMPTY_STRING), slot_(slotIndex), defIndex_(defIndex) {}

	[[nodiscard]] bool supportsFineEdit() const override {
		auto d = def();
		return d.param != dsp::FxBloc::Param::None && !d.isInt;
	}

	[[nodiscard]] int8_t numDecimalPositions() const override {
		auto d = def();
		if (d.isInt || d.param == dsp::FxBloc::Param::None) {
			return 1;
		}
		if (d.decimals == 2) {
			return 3; // ones, tenths, hundredths
		}
		if (d.decimals == 1) {
			return 2; // ones, tenths
		}
		if (spanSteps(d) > 500) {
			return 3; // ones, tens, hundreds
		}
		if (spanSteps(d) > 50) {
			return 2; // ones, tens
		}
		return 1;
	}

	[[nodiscard]] int32_t stepForDecimalPos(int8_t pos) const override {
		auto d = def();
		if (d.isInt || d.param == dsp::FxBloc::Param::None) {
			return 1;
		}
		if (d.decimals == 2) {
			// pos 0: ones (1.00) -> 100 * kFineScale = 10000
			// pos 1: tenths (0.10) -> 10 * kFineScale = 1000
			// pos 2: hundredths (0.01) -> 1 * kFineScale = 100
			if (pos == 1) return 10 * kFineScale;
			if (pos == 2) return 1 * kFineScale;
			return 100 * kFineScale;
		}
		if (d.decimals == 1) {
			// pos 0: ones (1.0) -> 10 * kFineScale = 1000
			// pos 1: tenths (0.1) -> 1 * kFineScale = 100
			if (pos == 1) return 1 * kFineScale;
			return 10 * kFineScale;
		}
		// d.decimals == 0
		// pos 0: ones (1) -> 1 * kFineScale = 100
		// pos 1: tens (10) -> 10 * kFineScale = 1000
		// pos 2: hundreds (100) -> 100 * kFineScale = 10000
		if (pos == 1) return 10 * kFineScale;
		if (pos == 2) return 100 * kFineScale;
		return 1 * kFineScale;
	}

	[[nodiscard]] int32_t coarseEditStep() const override {
		auto d = def();
		if (d.isInt) {
			return 1;
		}
		float target = spanSteps(d) / 100.0f;
		if (target <= 1.0f) {
			return kFineScale;
		}
		float mag = std::pow(10.0f, std::floor(std::log10(target)));
		float n = target / mag;
		float nice = (n <= 1.0f) ? 1.0f : (n <= 2.0f) ? 2.0f : (n <= 5.0f) ? 5.0f : 10.0f;
		return static_cast<int32_t>(std::max<float>(1.0f, std::lround(nice * mag))) * kFineScale;
	}

	[[nodiscard]] std::string_view getName() const override {
		dsp::FxBloc::ParamDef d = def();
		return d.param != dsp::FxBloc::Param::None ? std::string_view{d.label} : std::string_view{"--"};
	}

	[[nodiscard]] int32_t getMinValue() const override { return def().isInt ? def().intMin : 0; }
	[[nodiscard]] int32_t getMaxValue() const override {
		auto d = def();
		if (d.isInt) {
			return d.intMax;
		}
		return static_cast<int32_t>(std::lround(spanSteps(d) * kFineScale));
	}

	[[nodiscard]] const char* getUnit() override { return def().isInt ? "" : "%"; }

	void readCurrentValue() override {
		auto* chain = getCurrentFxChain();
		if (chain == nullptr) {
			this->setValue(0);
			return;
		}
		auto& bloc = chain->slot(slot_);
		auto d = def();
		float raw = bloc.getParam(d.param);
		if (d.isInt) {
			this->setValue(static_cast<int32_t>(std::lround(raw)));
			return;
		}
		float real = dsp::FxBloc::displayValue(d, raw);
		this->setValue(static_cast<int32_t>(std::lround((real - dispMin(d)) / stepOf(d) * kFineScale)));
		this->setValue(std::clamp(this->getValue(), getMinValue(), getMaxValue()));
	}

	void writeCurrentValue() override {
		auto* chain = getCurrentFxChain();
		if (chain == nullptr) {
			return;
		}
		auto& bloc = chain->slot(slot_);
		auto d = def();
		if (d.isInt) {
			bloc.setParam(d.param, static_cast<float>(this->getValue()));
			return;
		}
		float real = dispMin(d) + this->getValue() / static_cast<float>(kFineScale) * stepOf(d);
		bloc.setParam(d.param, dsp::FxBloc::tFromReal(d, real));
	}

	void formatValueForVisualMenu(char* outBuffer, size_t bufferSize) override {
		auto* chain = getCurrentFxChain();
		if (chain == nullptr) {
			if (outBuffer && bufferSize > 0) {
				snprintf(outBuffer, bufferSize, "--");
			}
			return;
		}
		auto& bloc = chain->slot(slot_);
		auto d = def();
		if (d.param == dsp::FxBloc::Param::None || bloc.isEmpty()) {
			if (outBuffer && bufferSize > 0) {
				outBuffer[0] = '-';
				outBuffer[1] = '-';
				outBuffer[2] = '\0';
			}
			return;
		}
		if (d.isInt && d.intOptions != nullptr) {
			const char* s = d.intOptions[std::clamp(this->getValue(), d.intMin, d.intMax) - d.intMin];
			snprintf(outBuffer, bufferSize, "%s", s);
			return;
		}
		if (d.isInt) {
			Integer::formatValueForVisualMenu(outBuffer, bufferSize);
			return;
		}
		float real = dispMin(d) + this->getValue() / static_cast<float>(kFineScale) * stepOf(d);
		if (d.param == dsp::FxBloc::Param::InGain && real <= -95.0f) {
			snprintf(outBuffer, bufferSize, "-INF");
			return;
		}
		if (real >= 900.0f) {
			snprintf(outBuffer, bufferSize, "INF"); // RT60 infinite sustain (manual ceiling)
			return;
		}
		// Real-unit readout from the fine value space, compacted to fit a 32px
		// column (~5 chars): shed a decimal, then switch to K-notation for large
		// values, then drop the unit rather than truncate ("7.0K", "-24DB").
		const char* unit = d.unit != nullptr ? d.unit : "%";
		char num[12];
		snprintf(num, sizeof(num), "%.*f%s", d.decimals, real, unit);
		if (strlen(num) > 5) {
			snprintf(num, sizeof(num), "%.*f%s", std::max(0, d.decimals - 1), real, unit);
			if (strlen(num) > 5) {
				if (std::fabs(real) >= 995.0f) {
					snprintf(num, sizeof(num), "%.*fK", 1, real / 1000.0f);
				}
				if (strlen(num) > 5) {
					snprintf(num, sizeof(num), "%.0f", real);
				}
			}
		}
		snprintf(outBuffer, bufferSize, "%s", num);
	}

private:
	[[nodiscard]] dsp::FxBloc::ParamDef def() const {
		auto* chain = getCurrentFxChain();
		auto model = chain != nullptr ? chain->slot(slot_).getModel() : dsp::FxBloc::Model::EMPTY;
		auto defs = dsp::FxBloc::paramDefs(model);
		if (defIndex_ < 0 || static_cast<size_t>(defIndex_) >= defs.size()) {
			return {}; // None — inert column
		}
		return defs[defIndex_];
	}

	/// Display-unit step: 10^-decimals of the real unit, 1% when uncalibrated.
	static float stepOf(const dsp::FxBloc::ParamDef& d) {
		if (d.unit == nullptr) {
			return 1.0f;
		}
		float s = 1.0f;
		for (uint8_t i = 0; i < d.decimals && i < 6; i++) {
			s /= 10.0f;
		}
		return s;
	}
	static float dispMin(const dsp::FxBloc::ParamDef& d) { return d.bipolar ? -d.realMax : d.realMin; }
	static float spanSteps(const dsp::FxBloc::ParamDef& d) {
		float span = d.bipolar ? 2.0f * d.realMax : d.realMax - d.realMin;
		return span / stepOf(d);
	}

	int32_t slot_;
	int32_t defIndex_;
};

} // namespace deluge::gui::menu_item::fx_bloc

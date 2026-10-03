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
#include "hid/display/display.h"
#include "processing/engines/audio_engine.h"
#include "util/misc.h"
#include <cmath>
#include <cstdint>
#include <string_view>

namespace deluge::gui::menu_item::fx_bloc {

/// Selects which bloc a master slot holds. COMPRESSOR/FILTER are excluded until their
/// adapters land (selecting them today would be a silent passthrough).
class MasterSlotBloc final : public Selection {
public:
	static constexpr dsp::FxBloc::Model kOptions[] = {
	    dsp::FxBloc::Model::EMPTY,       dsp::FxBloc::Model::FREEVERB, dsp::FxBloc::Model::MUTABLE,
	    dsp::FxBloc::Model::KDFX_REVERB, dsp::FxBloc::Model::KDFX_DELAY,
	    dsp::FxBloc::Model::KDFX_CHORUS, dsp::FxBloc::Model::KDFX_TONE,
	    dsp::FxBloc::Model::KDFX_FLANGER, dsp::FxBloc::Model::KDFX_PHASER,
	    dsp::FxBloc::Model::KDFX_TREMOLO, dsp::FxBloc::Model::KDFX_AUTOPAN,
	};

	MasterSlotBloc(int32_t slotIndex) : Selection(l10n::String::EMPTY_STRING), slot_(slotIndex) {}

	void readCurrentValue() override {
		auto wanted = AudioEngine::masterFxChain.slot(slot_).getModel();
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
		if (!AudioEngine::masterFxChain.slot(slot_).setModel(target)) {
			// Pool exhausted: surface it and revert the displayed value.
			display->displayError(Error::INSUFFICIENT_RAM);
			readCurrentValue();
		}
	}

	deluge::vector<std::string_view> getOptions() override {
		// Shown on the full-width Bloc pages, so longer names fit.
		return {"EMPTY",    "FREEVERB", "MUTABLE",    "KDFX REV",    "KDFX DLY",  "KDFX CHO",
		        "KDFX TONE", "KDFX FLANGER", "KDFX PHASER", "KDFX TREMOLO", "KDFX PAN"};
	}

private:
	int32_t slot_;
};

/// Selects the master chain's routing topology algorithm.
class MasterRouting final : public Selection {
public:
	MasterRouting() : Selection(l10n::String::EMPTY_STRING) {}

	void readCurrentValue() override {
		this->setValue(util::to_underlying(AudioEngine::masterFxChain.getRouting()));
	}

	void writeCurrentValue() override {
		AudioEngine::masterFxChain.setRouting(
		    static_cast<dsp::MasterFxChain::Routing>(this->getValue()));
	}

	deluge::vector<std::string_view> getOptions() override {
		// Compact topology diagrams — words get cut off in the 32px visual-menu columns.
		return {"1|2|3", "1+2+3", "1|2+3", "1+2|3"};
	}
};

/// One of the selected bloc's three headline params (percent-scaled). The label and
/// the backing param resolve through the slot's CURRENT model, so the page adapts
/// when the bloc changes — delay shows TIME/FDBK/TONE, chorus RATE/DEPTH/FDBK, etc.
class MasterSlotParam final : public Integer {
public:
	MasterSlotParam(int32_t slotIndex, int32_t ordinal)
	    : Integer(l10n::String::EMPTY_STRING), slot_(slotIndex), ordinal_(ordinal) {}

	[[nodiscard]] std::string_view getName() const override {
		auto info = dsp::FxBloc::headlineParams(
		                    AudioEngine::masterFxChain.slot(slot_).getModel())[ordinal_];
		return info.label != nullptr ? std::string_view{info.label} : std::string_view{"--"};
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }

	[[nodiscard]] const char* getUnit() override { return "%"; }

	void readCurrentValue() override {
		auto& bloc = AudioEngine::masterFxChain.slot(slot_);
		auto info = dsp::FxBloc::headlineParams(bloc.getModel())[ordinal_];
		this->setValue(static_cast<int32_t>(std::round(bloc.getParam(info.param) * 100.0f)));
	}

	void writeCurrentValue() override {
		auto& bloc = AudioEngine::masterFxChain.slot(slot_);
		auto info = dsp::FxBloc::headlineParams(bloc.getModel())[ordinal_];
		bloc.setParam(info.param, static_cast<float>(this->getValue()) / 100.0f);
	}

	void formatValueForVisualMenu(char* outBuffer, size_t bufferSize) override {
		auto& bloc = AudioEngine::masterFxChain.slot(slot_);
		auto info = dsp::FxBloc::headlineParams(bloc.getModel())[ordinal_];
		if (info.param == dsp::FxBloc::Param::None || bloc.isEmpty()) {
			if (outBuffer && bufferSize > 0) {
				outBuffer[0] = '-';
				outBuffer[1] = '-';
				outBuffer[2] = '\0';
			}
			return;
		}
		Integer::formatValueForVisualMenu(outBuffer, bufferSize);
	}

private:
	int32_t slot_;
	int32_t ordinal_;
};

} // namespace deluge::gui::menu_item::fx_bloc

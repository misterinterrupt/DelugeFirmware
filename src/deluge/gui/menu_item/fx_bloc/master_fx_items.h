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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
	    dsp::FxBloc::Model::COMPRESSOR,
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
		        "KDFX TONE", "KDFX FLANGER", "KDFX PHASER", "KDFX TREMOLO", "KDFX PAN", "COMPRESSOR"};
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

/// One entry of the selected bloc's param table (percent-scaled floats, plain
/// integers for enum-ish params). The label, kind and backing param all resolve
/// through the slot's CURRENT model, so the pages adapt when the bloc changes —
/// delay shows TIME/FDBK/TONE, chorus RATE/DEPTH/FDBK, and so on.
class MasterSlotParam final : public Integer {
public:
	MasterSlotParam(int32_t slotIndex, int32_t defIndex)
	    : Integer(l10n::String::EMPTY_STRING), slot_(slotIndex), defIndex_(defIndex) {}

	[[nodiscard]] std::string_view getName() const override {
		dsp::FxBloc::ParamDef d = def();
		return d.param != dsp::FxBloc::Param::None ? std::string_view{d.label} : std::string_view{"--"};
	}

	[[nodiscard]] int32_t getMinValue() const override { return def().isInt ? def().intMin : 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return def().isInt ? def().intMax : 100; }

	[[nodiscard]] const char* getUnit() override { return def().isInt ? "" : "%"; }

	void readCurrentValue() override {
		auto& bloc = AudioEngine::masterFxChain.slot(slot_);
		float v = bloc.getParam(def().param);
		this->setValue(def().isInt ? static_cast<int32_t>(std::lround(v))
		                           : static_cast<int32_t>(std::round(v * 100.0f)));
	}

	void writeCurrentValue() override {
		auto& bloc = AudioEngine::masterFxChain.slot(slot_);
		auto d = def();
		bloc.setParam(d.param, d.isInt ? static_cast<float>(this->getValue())
		                               : static_cast<float>(this->getValue()) / 100.0f);
	}

	void formatValueForVisualMenu(char* outBuffer, size_t bufferSize) override {
		auto& bloc = AudioEngine::masterFxChain.slot(slot_);
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
		if (d.isInt || d.unit == nullptr) {
			Integer::formatValueForVisualMenu(outBuffer, bufferSize);
			return;
		}
		// Real-unit readout: the menu value is travel percent; convert through the
		// engine's own range (FxBloc::displayValue handles the curve shapes).
		float t = static_cast<float>(this->getValue()) / 100.0f;
		float real = dsp::FxBloc::displayValue(d, t);
		snprintf(outBuffer, bufferSize, "%.*f%s", d.decimals, real, d.unit);
	}

private:
	[[nodiscard]] dsp::FxBloc::ParamDef def() const {
		return dsp::FxBloc::paramDefs(AudioEngine::masterFxChain.slot(slot_).getModel())[defIndex_];
	}

	int32_t slot_;
	int32_t defIndex_;
};

} // namespace deluge::gui::menu_item::fx_bloc

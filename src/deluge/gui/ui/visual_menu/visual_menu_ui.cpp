/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "visual_menu_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/visual_menu/visual_menu_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::ui::visual_menu {

VisualMenuUI visualMenuUI;

void VisualMenuUI::pushSlotContext(const VisualSlot& slot, SlotSourceContext& ctx) {
	ctx.index = soundEditor.currentSourceIndex;
	ctx.source = soundEditor.currentSource;
	ctx.sampleControls = soundEditor.currentSampleControls;
	if (slot.sourceIndexOverride >= 0 && soundEditor.currentSound) {
		soundEditor.currentSourceIndex = slot.sourceIndexOverride;
		soundEditor.currentSource = &soundEditor.currentSound->sources[slot.sourceIndexOverride];
		soundEditor.currentSampleControls = &soundEditor.currentSource->sampleControls;
	}
}

void VisualMenuUI::popSlotContext(const SlotSourceContext& ctx) {
	soundEditor.currentSourceIndex = ctx.index;
	soundEditor.currentSource = ctx.source;
	soundEditor.currentSampleControls = ctx.sampleControls;
}

VisualMenuUI::VisualMenuUI() {
	categories_ = getSynthCategories();
}

void VisualMenuUI::updateButtonLEDs() {
	int8_t active = getActiveHeldSlot();
	indicator_leds::setLedState(IndicatorLED::SYNTH, active == 0);
	indicator_leds::setLedState(IndicatorLED::KIT, active == 1);
	indicator_leds::setLedState(IndicatorLED::MIDI, active == 2);
	indicator_leds::setLedState(IndicatorLED::CV, active == 3);
}

bool VisualMenuUI::opened() {
	selectedSlot_ = -1;
	decimalCursorSlot_ = -1;
	decimalEditPos_ = 0;
	soundEditor.numberEditPos = 0;
	soundEditor.numberEditSize = 1;
	for (size_t i = 0; i < 4; ++i) {
		slotHeld_[i] = false;
		turnedWhileHeld_[i] = false;
		wasAlreadySelected_[i] = false;
	}
	applySourceIndexForCurrentPage();
	updateButtonLEDs();
	renderUIsForOled();
	return true;
}

void VisualMenuUI::focusRegained() {
	selectedSlot_ = -1;
	decimalCursorSlot_ = -1;
	decimalEditPos_ = 0;
	soundEditor.numberEditPos = 0;
	soundEditor.numberEditSize = 1;
	for (size_t i = 0; i < 4; ++i) {
		slotHeld_[i] = false;
		turnedWhileHeld_[i] = false;
		wasAlreadySelected_[i] = false;
	}
	// Coming back down from a UI that was above us, the editing context may have moved
	// (clip/row/affect-entire changed while we weren't current).
	refreshEditContext();
	applySourceIndexForCurrentPage();
	updateButtonLEDs();
	renderUIsForOled();
}

bool VisualMenuUI::exitUI() {
	closeVisualMenu();
	return true;
}

void VisualMenuUI::closeVisualMenu() {
	// BACK's long-press timer can fire exitUI() after the menu has already closed; make this a no-op
	// then so we never close a UI that's beneath us in the stack.
	if (getCurrentUI() != this) {
		return;
	}
	selectedSlot_ = -1;
	decimalCursorSlot_ = -1;
	for (size_t i = 0; i < 4; ++i) {
		slotHeld_[i] = false;
		turnedWhileHeld_[i] = false;
		wasAlreadySelected_[i] = false;
	}
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	// If an interaction mode got stranded while we were current (its release eaten by our pad
	// or button consumption), leaving it set blocks the root view's input after we close —
	// including the song button, slot presses, and (for a stranded save-hold) accidental
	// stem-export launches on the next RECORD press.
	exitStrandedModes();
	close();
}

int8_t VisualMenuUI::getActiveHeldSlot() const {
	// Only consult our own event-driven hold state here. Buttons::isButtonPressed can disagree with it
	// (e.g. after a forced release from the PIC's NO_PRESSES_HAPPENING sweep) and the hold tracker is
	// what the whole state machine runs on.
	for (size_t i = 0; i < 4; ++i) {
		if (slotHeld_[i]) {
			return static_cast<int8_t>(i);
		}
	}
	return selectedSlot_;
}

ActionResult VisualMenuUI::padAction(int32_t x, int32_t y, int32_t velocity) {
	// The menu owns only the select encoder and the four slot buttons — pads belong to
	// the view underneath in every root (user directive: the rest of the panel stays
	// live while the menu is open). Releases must ALWAYS reach the root view: a
	// forwarded press can enter modes beyond our press gate (note pads enter
	// UI_MODE_NOTES_PRESSED, kit-row drags enter UI_MODE_DRAGGING_KIT_NOTEROW); if the
	// matching release is swallowed because of the mode the press created, the mode
	// strands and its guard then eats every slot press and encoder turn — the "KIT
	// button won't edit" freeze.
	if (velocity == 0) {
		return getRootUI()->padAction(x, y, velocity);
	}

	if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING) {
		return getRootUI()->padAction(x, y, velocity);
	}
	return ActionResult::DEALT_WITH;
}

bool VisualMenuUI::setupEditContext(Clip* clip, const MenuItem* item) {
	currentClip_ = clip;
	contextIsClipMinder_ = rootUIIsClipMinderScreen();

	// A kit clip with no (sound) drum row selected cannot resolve a per-drum editing context; fall back to
	// the kit's affect-entire (global FX) context instead of silently failing.
	bool kitGlobalFX = false;
	if (!soundEditor.setup(clip, item)) {
		InstrumentClip* kitClip = nullptr;
		if (rootUIIsClipMinderScreen() && clip && clip->type == ClipType::INSTRUMENT) {
			kitClip = static_cast<InstrumentClip*>(clip);
		}
		if (kitClip && kitClip->output && kitClip->output->type == OutputType::KIT) {
			soundEditor.setupKitGlobalFXMenu = true;
			if (!soundEditor.setup(clip, item)) {
				soundEditor.setupKitGlobalFXMenu = false;
				return false;
			}
			kitGlobalFX = true;
			soundEditor.setupKitGlobalFXMenu = false; // don't let the flag poison later setups
		}
		else {
			return false;
		}
	}

	if (!rootUIIsClipMinderScreen() || clip == nullptr) {
		categories_ = getGlobalFxCategories();
	}
	else if (clip->type == ClipType::INSTRUMENT) {
		auto* instClip = static_cast<InstrumentClip*>(clip);
		if (instClip->output != nullptr) {
			switch (instClip->output->type) {
			case OutputType::SYNTH:
				categories_ = getSynthCategories();
				break;
			case OutputType::KIT:
				categories_ = kitGlobalFX ? getGlobalFxCategories() : getKitCategories();
				break;
			case OutputType::MIDI_OUT:
			case OutputType::CV:
				categories_ = getMidiCvCategories();
				break;
			default:
				categories_ = getSynthCategories();
				break;
			}
		}
		else {
			categories_ = getSynthCategories();
		}
	}
	else if (clip->type == ClipType::AUDIO) {
		categories_ = getAudioClipCategories();
	}
	else {
		categories_ = getSynthCategories();
	}

	// Capture the context signature so drift is detectable by refreshEditContext().
	contextDrum_ = nullptr;
	contextAffectEntire_ = false;
	if (contextIsClipMinder_ && clip && clip->type == ClipType::INSTRUMENT) {
		auto* instClip = static_cast<InstrumentClip*>(clip);
		contextAffectEntire_ = instClip->affectEntire;
		if (!kitGlobalFX && instClip->output && instClip->output->type == OutputType::KIT) {
			contextDrum_ = static_cast<Kit*>(instClip->output)->selectedDrum;
		}
	}

	return true;
}

bool VisualMenuUI::refreshEditContext() {
	if (getCurrentUI() != this) {
		return false;
	}

	if (rootUIIsClipMinderScreen() != contextIsClipMinder_) {
		closeVisualMenu();
		return false;
	}

	if (!contextIsClipMinder_) {
		return true; // song-level context has no finer signature
	}

	Clip* clip = getCurrentClip();
	if (clip == nullptr) {
		closeVisualMenu();
		return false;
	}

	if (clip == currentClip_) {
		bool drifted = false;
		if (clip->type == ClipType::INSTRUMENT) {
			auto* instClip = static_cast<InstrumentClip*>(clip);
			if (instClip->affectEntire != contextAffectEntire_) {
				drifted = true;
			}
			Drum* drum = nullptr;
			if (instClip->output && instClip->output->type == OutputType::KIT) {
				drum = static_cast<Kit*>(instClip->output)->selectedDrum;
			}
			if (drum != contextDrum_) {
				drifted = true;
			}
		}
		if (!drifted) {
			return true;
		}
	}

	// Context moved under us (different clip, re-selected row, affect-entire toggled).
	// Re-derive it as a whole — never edit through a mixed old/new pairing.
	if (!setupEditContext(clip, nullptr)) {
		closeVisualMenu();
		return false;
	}

	// The category set may have changed shape; clamp navigation into it. Slot mapping is
	// against the old page, so drop any selection.
	if (currentCategoryIndex_ >= static_cast<int32_t>(categories_.size())) {
		currentCategoryIndex_ = 0;
	}
	if (!categories_.empty()) {
		const auto& cat = categories_[currentCategoryIndex_];
		if (currentPageIndex_ >= static_cast<int32_t>(cat.pages.size())) {
			currentPageIndex_ = 0;
		}
	}
	selectedSlot_ = -1;
	decimalCursorSlot_ = -1;
	return true;
}

bool VisualMenuUI::openForClip(Clip* clip, const MenuItem* targetItem) {
	if (!setupEditContext(clip, targetItem)) {
		return false;
	}

	currentCategoryIndex_ = 0;
	currentPageIndex_ = 0;
	selectedSlot_ = -1;
	decimalCursorSlot_ = -1;
	decimalEditPos_ = 0;
	for (size_t i = 0; i < 4; ++i) {
		slotHeld_[i] = false;
		turnedWhileHeld_[i] = false;
		wasAlreadySelected_[i] = false;
	}

	if (targetItem != nullptr) {
		int32_t catIdx = 0, pageIdx = 0, slotIdx = 0;
		if (findMenuItemLocation(categories_, targetItem, &catIdx, &pageIdx, &slotIdx)) {
			currentCategoryIndex_ = catIdx;
			currentPageIndex_ = pageIdx;
			selectedSlot_ = slotIdx;
			decimalCursorSlot_ = slotIdx;
		}
	}

	return openUI(this);
}

void VisualMenuUI::applySourceIndexForCurrentPage() {
	if (categories_.empty() || currentCategoryIndex_ >= static_cast<int32_t>(categories_.size())) {
		return;
	}
	const auto& cat = categories_[currentCategoryIndex_];
	if (cat.pages.empty() || currentPageIndex_ >= static_cast<int32_t>(cat.pages.size())) {
		return;
	}
	const auto& page = cat.pages[currentPageIndex_];
	for (const auto& slot : page.slots) {
		if (slot.item != nullptr) {
			SlotSourceContext ctx;
			pushSlotContext(slot, ctx);
			slot.item->syncValueForVisualMenu();
			popSlotContext(ctx);
		}
	}
}

ActionResult VisualMenuUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// The PIC's NO_PRESSES_HAPPENING message is derived from pad state only and can arrive while a slot
	// button is still physically held; those synthetic releases must not disturb our hold tracking.
	if (!on && Buttons::noPressesSweepActive && (b == SYNTH || b == KIT || b == MIDI || b == CV)) {
		return ActionResult::DEALT_WITH;
	}

	// Root-view navigation escapes the overlay first, so the menu can't be left dangling
	// over a root it didn't open on. Everything else is forwarded live (tail of this function).
	if (b == SESSION_VIEW || b == CLIP_VIEW || b == KEYBOARD) {
		if (on && !inCardRoutine) {
			closeVisualMenu();
		}
		UI* root = getRootUI();
		return root != nullptr ? root->buttonAction(b, on, inCardRoutine) : ActionResult::DEALT_WITH;
	}

	// A press while a foreign interaction mode is active means that mode is stranded — the menu
	// itself never creates one (only AUDITIONING may accompany it). A stranded mode used to eat
	// every slot press ("KIT won't edit"); clear it and carry on instead of swallowing input.
	if (on && currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_AUDITIONING) {
		exitStrandedModes();
	}

	int8_t slotPressed = -1;
	if (b == SYNTH) {
		slotPressed = 0;
	}
	else if (b == KIT) {
		slotPressed = 1;
	}
	else if (b == MIDI) {
		slotPressed = 2;
	}
	else if (b == CV) {
		slotPressed = 3;
	}

	if (slotPressed >= 0) {
		if (on) {
			// Slot press re-syncs the item's cached value (below), which reads through the
			// soundEditor context — validate it first.
			if (!refreshEditContext()) {
				return ActionResult::DEALT_WITH;
			}
			bool switchingSlot = (selectedSlot_ != slotPressed);
			slotHeld_[slotPressed] = true;
			turnedWhileHeld_[slotPressed] = false;
			wasAlreadySelected_[slotPressed] = (selectedSlot_ == slotPressed);
			selectedSlot_ = slotPressed;
			decimalCursorSlot_ = slotPressed;
			if (switchingSlot) {
				// Don't leak the previous slot's digit scaling into this one
				decimalEditPos_ = 0;
				soundEditor.numberEditPos = 0;
				soundEditor.numberEditSize = 1;
				// Re-sync this slot's cached value so the first encoder turn edits the real current
				// value even if the underlying param changed since the page was rendered
				if (currentCategoryIndex_ < static_cast<int32_t>(categories_.size())) {
					const auto& cat = categories_[currentCategoryIndex_];
					if (!cat.pages.empty() && currentPageIndex_ < static_cast<int32_t>(cat.pages.size())) {
						const auto& slot = cat.pages[currentPageIndex_].slots[slotPressed];
						if (slot.item != nullptr) {
							SlotSourceContext ctx;
							pushSlotContext(slot, ctx);
							slot.item->syncValueForVisualMenu();
							popSlotContext(ctx);
						}
					}
				}
			}
		}
		else {
			slotHeld_[slotPressed] = false;
			if (turnedWhileHeld_[slotPressed]) {
				turnedWhileHeld_[slotPressed] = false;
				// Held down and turned -> release immediately on button up, unless another slot is still
				// physically held, in which case hand selection over to that slot
				if (selectedSlot_ == slotPressed) {
					int8_t nextSlot = -1;
					for (size_t i = 0; i < 4; ++i) {
						if (slotHeld_[i]) {
							nextSlot = static_cast<int8_t>(i);
							break;
						}
					}
					selectedSlot_ = nextSlot;
					decimalCursorSlot_ = nextSlot;
				}
			}
			else if (selectedSlot_ == slotPressed) {
				// Only the slot that currently owns the selection may end it. Releases of superseded slots
				// (e.g. a previously latched slot, or a hold that another press took over from) must not
				// re-latch or clear the selection.
				if (wasAlreadySelected_[slotPressed]) {
					// Tapped without turning -> toggle latch off
					selectedSlot_ = -1;
					decimalCursorSlot_ = -1;
				}
				// else: remain latched
			}
		}
		updateButtonLEDs();
		renderUIsForOled();
		return ActionResult::DEALT_WITH;
	}
	else if (b == BACK) {
		if (on) {
			if (selectedSlot_ >= 0) {
				// Back unselects the active slot first
				selectedSlot_ = -1;
				decimalCursorSlot_ = -1;
				for (size_t i = 0; i < 4; ++i) {
					slotHeld_[i] = false;
					turnedWhileHeld_[i] = false;
				}
				updateButtonLEDs();
				renderUIsForOled();
			}
			else {
				closeVisualMenu();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	else if (b == SELECT_ENC) {
		if (on) {
			if (selectedSlot_ >= 0 && currentCategoryIndex_ < static_cast<int32_t>(categories_.size())) {
				const auto& page = categories_[currentCategoryIndex_].pages[currentPageIndex_];
				if (selectedSlot_ < static_cast<int32_t>(page.slots.size())) {
					const auto& slot = page.slots[selectedSlot_];
					if (slot.item != nullptr) {
						// Cycle decimal edit cursor if on decimal slot
						decimalCursorSlot_ = selectedSlot_;
						int8_t maxPos = slot.item->numDecimalPositions();
						if (maxPos > 1) {
							decimalEditPos_ = (decimalEditPos_ + 1) % maxPos;
						}
						else {
							decimalEditPos_ = 0;
						}
						soundEditor.numberEditPos = decimalEditPos_;
						soundEditor.numberEditSize = 1;
						for (int32_t i = 0; i < decimalEditPos_; i++) {
							soundEditor.numberEditSize *= 10;
						}
					}
				}
			}
			else {
				selectedSlot_ = 0;
				decimalCursorSlot_ = 0;
				decimalEditPos_ = 0;
				updateButtonLEDs();
			}
			renderUIsForOled();
		}
		return ActionResult::DEALT_WITH;
	}

	// This menu owns only the select encoder and the four slot buttons; every other button
	// belongs to the view underneath. Forward so the rest of the front panel stays live while
	// the menu is open; a NOT_DEALT_WITH from the root falls through to the global handlers
	// in Buttons::buttonAction exactly as it would without the menu.
	UI* root = getRootUI();
	if (root != nullptr) {
		return root->buttonAction(b, on, inCardRoutine);
	}
	return ActionResult::DEALT_WITH;
}

ActionResult VisualMenuUI::horizontalEncoderAction(int32_t offset) {
	UI* root = getRootUI();
	return root != nullptr ? root->horizontalEncoderAction(offset) : ActionResult::DEALT_WITH;
}

ActionResult VisualMenuUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	UI* root = getRootUI();
	return root != nullptr ? root->verticalEncoderAction(offset, inCardRoutine) : ActionResult::DEALT_WITH;
}

void VisualMenuUI::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (UI* root = getRootUI()) {
		root->modEncoderAction(whichModEncoder, offset);
	}
}

void VisualMenuUI::modButtonAction(uint8_t whichButton, bool on) {
	if (UI* root = getRootUI()) {
		root->modButtonAction(whichButton, on);
	}
}

void VisualMenuUI::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	if (UI* root = getRootUI()) {
		root->modEncoderButtonAction(whichModEncoder, on);
	}
}

ActionResult VisualMenuUI::timerCallback() {
	// Root-view timers (clip-press, song-button etc.) must keep firing while the menu is open.
	if (UI* root = getRootUI()) {
		return root->timerCallback();
	}
	return ActionResult::DEALT_WITH;
}

void VisualMenuUI::exitStrandedModes() {
	exitUIMode(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW);
	exitUIMode(UI_MODE_AUDITIONING);
	exitUIMode(UI_MODE_NOTES_PRESSED);
	exitUIMode(UI_MODE_HOLDING_SAVE_BUTTON);
	exitUIMode(UI_MODE_HOLDING_LOAD_BUTTON);
	exitUIMode(UI_MODE_HOLDING_SONG_BUTTON);
}

void VisualMenuUI::selectEncoderAction(int8_t offset) {
	if (categories_.empty()) {
		return;
	}

	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_AUDITIONING) {
		return;
	}

	// Every encoder interaction touches the soundEditor context (page sync or param edit),
	// and getCurrentModelStack() pairs live root state with the captured fields — so the
	// whole signature (clip, selected drum, affect-entire, root kind) must still match.
	if (!refreshEditContext()) {
		return;
	}

	int8_t activeSlot = getActiveHeldSlot();

	// 2. If a slot is active, edit its parameter value smoothly. Fine-edit items
	// keep the slot edit even with shift held (shift = faster); stock items fall
	// through to the shift-category navigation below, exactly as before.
	if (activeSlot >= 0 && currentCategoryIndex_ < static_cast<int32_t>(categories_.size())) {
		const auto& page = categories_[currentCategoryIndex_].pages[currentPageIndex_];
		if (activeSlot < static_cast<int32_t>(page.slots.size())) {
			const auto& slot = page.slots[activeSlot];
			if (slot.item != nullptr
			    && (slot.item->supportsFineEdit() || !Buttons::isShiftButtonPressed())) {
				if (decimalCursorSlot_ != activeSlot) {
					decimalEditPos_ = 0;
					soundEditor.numberEditPos = 0;
					soundEditor.numberEditSize = 1;
				}
				turnedWhileHeld_[activeSlot] = true;
				selectedSlot_ = activeSlot;
				decimalCursorSlot_ = activeSlot;

				SlotSourceContext ctx;
				pushSlotContext(slot, ctx);
				if (slot.item->supportsFineEdit()) {
					int32_t scale = slot.item->stepForDecimalPos(decimalEditPos_);
					if (Buttons::isShiftButtonPressed()) {
						scale *= 10;
					}
					slot.item->selectEncoderAction(offset * scale);
				}
				else {
					slot.item->selectEncoderAction(offset);
				}
				popSlotContext(ctx);
				if (!soundEditor.inSettingsMenu()) {
					soundEditor.markInstrumentAsEdited();
				}

				updateButtonLEDs();
				renderUIsForOled();
				return;
			}
		}
	}

	// 1b. Shift + Select Knob navigates categories directly (reached when no fine
	// slot is being edited).
	if (Buttons::isShiftButtonPressed()) {
		int32_t numCats = static_cast<int32_t>(categories_.size());
		currentCategoryIndex_ = (currentCategoryIndex_ + (offset % numCats) + numCats) % numCats;
		currentPageIndex_ = 0;
		selectedSlot_ = -1;
		decimalCursorSlot_ = -1;
		for (size_t i = 0; i < 4; ++i) {
			slotHeld_[i] = false;
			turnedWhileHeld_[i] = false;
		}
		updateButtonLEDs();
		applySourceIndexForCurrentPage();
		renderUIsForOled();
		return;
	}

	// 3. Otherwise (no slot selected), navigate pages across categories
	int32_t numPages = static_cast<int32_t>(categories_[currentCategoryIndex_].pages.size());
	int32_t newPageIndex = currentPageIndex_ + offset;

	while (newPageIndex >= numPages && numPages > 0) {
		newPageIndex -= numPages;
		currentCategoryIndex_ = (currentCategoryIndex_ + 1) % static_cast<int32_t>(categories_.size());
		numPages = static_cast<int32_t>(categories_[currentCategoryIndex_].pages.size());
	}
	while (newPageIndex < 0) {
		currentCategoryIndex_ = (currentCategoryIndex_ + static_cast<int32_t>(categories_.size()) - 1) %
		                        static_cast<int32_t>(categories_.size());
		numPages = static_cast<int32_t>(categories_[currentCategoryIndex_].pages.size());
		if (numPages > 0) {
			newPageIndex += numPages;
		}
		else {
			newPageIndex = 0;
			break;
		}
	}
	currentPageIndex_ = newPageIndex;

	applySourceIndexForCurrentPage();
	renderUIsForOled();
}

void VisualMenuUI::fxBlocPagesRebuilt() {
	if (contextIsClipMinder_) {
		return; // FX BLOCS pages only exist in the song-mode category set
	}
	categories_ = getGlobalFxCategories();
	if (currentCategoryIndex_ >= static_cast<int32_t>(categories_.size())) {
		currentCategoryIndex_ = static_cast<int32_t>(categories_.size()) - 1;
	}
	auto& pages = categories_[currentCategoryIndex_].pages;
	if (currentPageIndex_ >= static_cast<int32_t>(pages.size())) {
		currentPageIndex_ = static_cast<int32_t>(pages.size()) - 1;
	}
	// Deliberately NOT clearing slotHeld_/selectedSlot_: the rebuild fires on every
	// detent of a bloc selector while its slot button is still physically held —
	// wiping the selection there auto-deselected the item after one change. The
	// held slot stays valid across the reflow (columns are per-page positions).
}

void VisualMenuUI::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (categories_.empty()) {
		canvas.clear();
		return;
	}

	const auto& cat = categories_[currentCategoryIndex_];
	if (cat.pages.empty() || currentPageIndex_ >= static_cast<int32_t>(cat.pages.size())) {
		canvas.clear();
		return;
	}

	const auto& page = cat.pages[currentPageIndex_];
	int8_t heldSlot = getActiveHeldSlot();

	VisualMenuRenderer::renderPage(canvas, cat, currentCategoryIndex_, static_cast<int32_t>(categories_.size()), page,
	                               currentPageIndex_, static_cast<int32_t>(cat.pages.size()), heldSlot,
	                               decimalCursorSlot_, decimalEditPos_);
}

} // namespace deluge::gui::ui::visual_menu

/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "model/clip/instrument_clip_minder.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/menus.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_kit_row_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note.h"
#include <algorithm>
#include <vector>
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "playback/mode/arrangement.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "util/lookuptables/lookuptables.h"
#include <cstring>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

int16_t InstrumentClipMinder::defaultRootNote;
bool InstrumentClipMinder::toggleScaleModeOnButtonRelease;
bool InstrumentClipMinder::flashDefaultRootNoteOn;
uint8_t InstrumentClipMinder::editingMIDICCForWhichModKnob;

InstrumentClipMinder::InstrumentClipMinder() {
}

void InstrumentClipMinder::selectEncoderAction(int32_t offset) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	if (currentUIMode == UI_MODE_SELECTING_MIDI_CC) {
		if (editingMIDICCForWhichModKnob < kNumPhysicalModKnobs) {
			MIDIInstrument* instrument = (MIDIInstrument*)getCurrentOutput();
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(instrument, &getCurrentInstrumentClip()->paramManager);

			int32_t newCC;

			if (!Buttons::isButtonPressed(deluge::hid::button::SELECT_ENC)) {
				newCC = instrument->changeControlNumberForModKnob(offset, editingMIDICCForWhichModKnob,
				                                                  instrument->modKnobMode);
				view.setKnobIndicatorLevels();
			}
			else {
				newCC = instrument->moveAutomationToDifferentCC(offset, editingMIDICCForWhichModKnob,
				                                                instrument->modKnobMode, modelStackWithThreeMainThings);
				if (newCC == -1) {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_FURTHER_UNUSED_MIDI_PARAMS));
					return;
				}
			}

			bool automationExists = instrument->doesAutomationExistOnMIDIParam(modelStackWithThreeMainThings, newCC);

			drawMIDIControlNumber(newCC, automationExists);
		}
	}
	else {
		view.navigateThroughPresetsForInstrumentClip(offset, modelStack);
	}
}

void InstrumentClipMinder::redrawNumericDisplay() {
	if (display->have7SEG()) {
		if (getCurrentUI()->toClipMinder()) { // Seems a redundant check now? Maybe? Or not?
			view.displayOutputName(getCurrentOutput(), false);
		}
	}
}

void InstrumentClipMinder::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	view.displayOutputName(getCurrentOutput(), false, getCurrentClip());
}

// GCC is fine with 29 or 5 for the size, but does not like that it could be either
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="
void InstrumentClipMinder::drawMIDIControlNumber(int32_t controlNumber, bool automationExists) {

	char buffer[display->haveOLED() ? 30 : 5];
	bool finish = false;
	if (controlNumber == CC_NUMBER_NONE) {
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_PARAM));
	}
	else if (controlNumber == CC_NUMBER_PITCH_BEND) {
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_PITCH_BEND));
	}
	else if (controlNumber == CC_NUMBER_AFTERTOUCH) {
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHANNEL_PRESSURE));
	}
	else if (controlNumber == CC_NUMBER_Y_AXIS) {
		// in mono expression this is mod wheel, and y-axis is not directly controllable
		strcpy(buffer, deluge::l10n::get(deluge::l10n::String::STRING_FOR_MOD_WHEEL));
	}
	else {
		buffer[0] = 'C';
		buffer[1] = 'C';
		if (display->haveOLED()) {
			buffer[2] = ' ';
			intToString(controlNumber, &buffer[3]);
		}
		else {
			char* numberStartPos = (controlNumber < 100) ? (buffer + 2) : (buffer + 1);
			intToString(controlNumber, numberStartPos);
		}
	}

	if (display->haveOLED()) {
		if (automationExists) {
			strcat(buffer, "\n(automated)");
		}
		display->popupText(buffer);
	}
	else {
		display->setText(buffer, true, automationExists ? 3 : 255, false);
	}
}
#pragma GCC pop
bool InstrumentClipMinder::createNewInstrument(OutputType newOutputType, bool is_dx) {
	Error error;

	OutputType oldOutputType = getCurrentOutputType();

	InstrumentClip* clip = getCurrentInstrumentClip();

	// don't allow clip type change if clip is not empty
	// only impose this restriction if switching to/from kit clip
	if ((oldOutputType != newOutputType) && ((oldOutputType == OutputType::KIT) || (newOutputType == OutputType::KIT))
	    && (!clip->isEmpty() || !clip->output->isEmpty())) {
		return false;
	}

	bool shouldReplaceWholeInstrument = currentSong->shouldOldOutputBeReplaced(clip);

	String newName;
	char const* thingName = (newOutputType == OutputType::SYNTH) ? "SYNT" : "KIT";
	error = Browser::currentDir.set(getInstrumentFolder(newOutputType));
	if (error != Error::NONE) {
gotError:
		display->displayError(error);
		return false;
	}

	error = loadInstrumentPresetUI.getUnusedSlot(newOutputType, &newName, thingName);
	if (error != Error::NONE) {
		goto gotError;
	}

	if (newName.isEmpty()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_FURTHER_UNUSED_INSTRUMENT_NUMBERS));
		return false;
	}

	ParamManagerForTimeline newParamManager;
	Instrument* newInstrument = storageManager.createNewInstrument(newOutputType, &newParamManager);
	if (!newInstrument) {
		error = Error::INSUFFICIENT_RAM;
		goto gotError;
	}

	// Set dirPath.
	error = newInstrument->dirPath.set(getInstrumentFolder(newOutputType));
	if (error != Error::NONE) {
		void* toDealloc = dynamic_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		delugeDealloc(toDealloc);
		goto gotError;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E059", "H059");

	clip->backupPresetSlot();

	if (newOutputType == OutputType::KIT) {
		display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NEW_KIT_CREATED));
	}
	else {
		if (is_dx) {
			display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NEW_FM_SYNTH_CREATED));
		}
		else {
			display->consoleText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NEW_SYNTH_CREATED));
		}
	}

	if (newOutputType == OutputType::SYNTH) {
		((SoundInstrument*)newInstrument)->setupAsBlankSynth(&newParamManager, is_dx);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// If replacing whole Instrument
	if (shouldReplaceWholeInstrument) {
		// newInstrument->loadAllSamples(true); // There'll be no samples cos it's new and blank
		//  This is how we feed a ParamManager into the replaceInstrument() function
		currentSong->backUpParamManager((ModControllableAudio*)newInstrument->toModControllable(), NULL,
		                                &newParamManager, true);
		currentSong->replaceInstrument(getCurrentInstrument(), newInstrument, false);
	}

	// Or if just adding new Instrument
	else {
		// There'll be no samples cos it's new and blank
		// TODO: deal with errors
		Error error = clip->changeInstrument(modelStack, newInstrument, &newParamManager,
		                                     InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, NULL, false);

		currentSong->addOutput(newInstrument);
	}

	newInstrument->editedByUser = true;
	newInstrument->existsOnCard = false;

	if (newOutputType == OutputType::KIT) {
		// If we weren't a Kit already...
		if (oldOutputType != OutputType::KIT) {
			clip->yScroll = 0;
		}

		// Or if we were...
		else {
			clip->ensureScrollWithinKitBounds();
		}
	}

	view.instrumentChanged(modelStack, newInstrument);

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E060", "H060");

	setLedStates();

	newInstrument->name.set(&newName);

	if (is_dx) {
		soundEditor.setup(getCurrentInstrumentClip(), &dxMenu, 0);
		openUI(&soundEditor);
	}

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		redrawNumericDisplay();
	}

	return true;
}

void InstrumentClipMinder::displayOrLanguageChanged() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		redrawNumericDisplay();
	}
}

void InstrumentClipMinder::setLedStates() {
	OutputType outputType = getCurrentOutputType();

	indicator_leds::setLedState(IndicatorLED::SYNTH, outputType == OutputType::SYNTH);
	indicator_leds::setLedState(IndicatorLED::KIT, outputType == OutputType::KIT);
	indicator_leds::setLedState(IndicatorLED::MIDI, outputType == OutputType::MIDI_OUT);
	indicator_leds::setLedState(IndicatorLED::CV, outputType == OutputType::CV);

	InstrumentClip* clip = getCurrentInstrumentClip();

	bool inAutomationView = ((getCurrentUI() == &automationView) && !automationView.inNoteEditor());
	if (!inAutomationView) {
		// only light cross screen led up if you're in the automation view note editor or outside automation view
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, clip->wrapEditing);
	}

	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, clip->isScaleModeClip());
	indicator_leds::setLedState(IndicatorLED::BACK, false);

#ifdef currentClipStatusButtonX
	view.drawCurrentClipPad(clip);
#endif

	view.setLedStates();
	playbackHandler.setLedStates();
}

void InstrumentClipMinder::focusRegained() {
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(getCurrentInstrumentClip());
	MIDITranspose::exitScaleModeForMIDITransposeClips();
	if (display->have7SEG()) {
		redrawNumericDisplay();
	}
}

ActionResult InstrumentClipMinder::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	// If holding save button...
	if (currentUIMode == UI_MODE_HOLDING_SAVE_BUTTON && on) {
		currentUIMode = UI_MODE_NONE;
		indicator_leds::setLedState(IndicatorLED::SAVE, false);

		if (b == SYNTH && getCurrentOutputType() == OutputType::SYNTH
		    || b == KIT && getCurrentOutputType() == OutputType::KIT
		    || b == MIDI && getCurrentOutputType() == OutputType::MIDI_OUT) {
			openUI(&saveInstrumentPresetUI);
		}
	}

	// If holding load button...
	else if (currentUIMode == UI_MODE_HOLDING_LOAD_BUTTON && on) {
		currentUIMode = UI_MODE_NONE;
		indicator_leds::setLedState(IndicatorLED::LOAD, false);
		OutputType newOutputType;
		if (b == SYNTH) {
			newOutputType = OutputType::SYNTH;
		}
		else if (b == KIT) {
			newOutputType = OutputType::KIT;
		}
		else if (b == MIDI) {
			newOutputType = OutputType::MIDI_OUT;
		}
		InstrumentClip* clip = getCurrentInstrumentClip();
		Output* output = clip->output;
		OutputType oldOutputType = output->type;
		Instrument* instrument = (Instrument*)output;
		// don't allow clip type change if clip is not empty
		// only impose this restriction if switching to/from kit clip
		if (!((oldOutputType != newOutputType)
		      && ((oldOutputType == OutputType::KIT) || (newOutputType == OutputType::KIT))
		      && (!clip->isEmpty() || !clip->output->isEmpty()))) {
			loadInstrumentPresetUI.setupLoadInstrument(newOutputType, instrument, clip);
			openUI(&loadInstrumentPresetUI);
		}
	}

	// Select button, without shift
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if ((getCurrentOutputType() == OutputType::KIT) && (getCurrentInstrumentClip()->affectEntire)) {
				soundEditor.setupKitGlobalFXMenu = true;
			}

			if (!soundEditor.setup(getCurrentClip())) {
				return ActionResult::DEALT_WITH;
			}
			openUI(&soundEditor);
		}
	}

	// Affect-entire
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (getCurrentOutputType() == OutputType::KIT) {

				getCurrentInstrumentClip()->affectEntire = !getCurrentInstrumentClip()->affectEntire;
				view.setActiveModControllableTimelineCounter(getCurrentInstrumentClip());
			}
		}
	}

	// Back button to clear Clip
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			// Clear Clip
			Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, getCurrentClip());

			UI* currentUI = getCurrentUI();

			// If you're in Automation View, only clear automation if you're not in the Note Editor
			// or also clear Automation when default setting to only clear automation in Automation View is false
			bool clearAutomation = ((currentUI == &automationView && !automationView.inNoteEditor())
			                        || (currentUI != &automationView && !FlashStorage::automationClear));

			// If you're in Automation View, only clear Notes and MPE if you're in the Note Editor
			// Always clear Notes and MPE when you're not in Automation View
			bool clearSequenceAndMPE =
			    ((currentUI != &automationView) || (currentUI == &automationView && automationView.inNoteEditor()));

			getCurrentInstrumentClip()->clear(action, modelStack, clearAutomation, clearSequenceAndMPE);

			// New default as part of Automation Clip View Implementation
			// If this is enabled, then when you are in a regular Instrument Clip View (Synth, Kit, MIDI, CV), clearing
			// a clip will only clear the Notes (automations remain intact). If this is enabled, if you want to clear
			// automations, you will enter Automation Clip View and clear the clip there. If this is enabled, the
			// message displayed on the OLED screen is adjusted to reflect the nature of what is being cleared

			// if you're in automation view but not in the note editor, you're clearing non-MPE automation
			if (currentUI == &automationView) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_AUTOMATION_CLEARED));
			}
			// if you're not in automation view and automationClear default is on, you're only clearing Notes and MPE
			else if (FlashStorage::automationClear) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_CLEARED));
			}
			// if you're not in automation view and automationClear default is off, you're clearing everything
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_CLIP_CLEARED));
			}

			uiNeedsRendering(currentUI, 0xFFFFFFFF, 0);
		}
	}

	// Which-instrument-type buttons
	else if (b == SYNTH) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (Buttons::isButtonPressed(MOD7)) { // FM
				createNewInstrument(OutputType::SYNTH, true);
			}
			else if (Buttons::isShiftButtonPressed()) {
				createNewInstrument(OutputType::SYNTH);
			}
			else {
				changeOutputType(OutputType::SYNTH);
			}
		}
	}

	else if (b == MIDI) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeOutputType(OutputType::MIDI_OUT);
		}
	}

	else if (b == CV) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeOutputType(OutputType::CV);
		}
	}

	else {
		return ClipMinder::buttonAction(b, on);
	}

	return ActionResult::DEALT_WITH;
}

bool InstrumentClipMinder::changeOutputType(OutputType newOutputType) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	bool success = view.changeOutputType(newOutputType, modelStack);

	if (success) {
		setLedStates(); // Might need to change the scale LED's state
	}

	return success;
}

void InstrumentClipMinder::calculateDefaultRootNote() {
	// If there are any other Clips in scale-mode, we use their root note
	if (currentSong->anyScaleModeClips()) {
		defaultRootNote = currentSong->key.rootNote;

		// Otherwise, intelligently guess the root note
	}
	else {
		defaultRootNote = getCurrentInstrumentClip()->guessRootNote(currentSong, currentSong->key.rootNote);
	}
}

void InstrumentClipMinder::drawActualNoteCode(int16_t noteCode) {
	// If we're in Chords mode, don't display the note name because the Chord class will display the chord name
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip && (clip->onKeyboardScreen || getRootUI() == &keyboardScreen || getCurrentUI() == &keyboardScreen)
	    && ((clip->keyboardState.currentLayout == KeyboardLayoutType::KeyboardLayoutTypeChordLibrary)
	        || (clip->keyboardState.currentLayout == KeyboardLayoutType::KeyboardLayoutTypeChord))) {
		return;
	}

	std::vector<int16_t> activeNotes;
	if (noteCode >= 0) {
		activeNotes.push_back(noteCode);
	}

	// 1. From MelodicInstrument notesAuditioned
	Instrument* inst = getCurrentInstrument();
	if (inst && inst->type != OutputType::KIT) {
		MelodicInstrument* melodic = (MelodicInstrument*)inst;
		for (int32_t i = 0; i < melodic->notesAuditioned.getNumElements(); i++) {
			EarlyNote* en = (EarlyNote*)melodic->notesAuditioned.getElementAddress(i);
			if (en && en->stillActive) {
				activeNotes.push_back(en->note);
			}
		}
	}

	// 2. From KeyboardScreen currentNotesState
	if ((clip && clip->onKeyboardScreen) || getRootUI() == &keyboardScreen || getCurrentUI() == &keyboardScreen) {
		const auto& notesState = keyboardScreen.getCurrentNotesState();
		for (size_t i = 0; i < notesState.count; i++) {
			activeNotes.push_back(notesState.notes[i].note);
		}
	}

	// 3. From InstrumentClipView & AutomationView audition pads & step edit pad presses
	if (getCurrentUI() == &instrumentClipView || getRootUI() == &instrumentClipView
	    || getCurrentUI() == &automationView || getRootUI() == &automationView) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (instrumentClipView.auditionPadIsPressed[y] && clip) {
				activeNotes.push_back(clip->getYNoteFromYDisplay(y, currentSong));
			}
		}

		if (instrumentClipView.numEditPadPresses > 0 && clip) {
			for (int32_t ep = 0; ep < kEditPadPressBufferSize; ep++) {
				if (instrumentClipView.editPadPresses[ep].isActive) {
					uint8_t xDisp = instrumentClipView.editPadPresses[ep].xDisplay;
					uint8_t yDisp = instrumentClipView.editPadPresses[ep].yDisplay;
					activeNotes.push_back(clip->getYNoteFromYDisplay(yDisp, currentSong));

					int32_t squareStart = instrumentClipView.getPosFromSquare(xDisp);
					int32_t effectiveLength = clip->getLoopLength();
					uint32_t squareWidth = instrumentClipView.getSquareWidth(xDisp, effectiveLength);

					for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
						NoteRow* nr = clip->noteRows.getElement(r);
						if (!nr) continue;
						for (int32_t nIdx = 0; nIdx < nr->notes.getNumElements(); nIdx++) {
							Note* n = nr->notes.getElement(nIdx);
							if (n && (n->pos < squareStart + (int32_t)squareWidth)
							    && (n->pos + (int32_t)n->getLength() > squareStart)) {
								activeNotes.push_back(nr->y);
							}
						}
					}
				}
			}
		}
	}

	// 4. Sequence vertical column notes for any auditioned/held notes that overlap a step in clip sequence
	if (clip && !activeNotes.empty()) {
		std::vector<int16_t> initialNotes = activeNotes;
		for (int16_t heldNote : initialNotes) {
			NoteRow* nrHeld = clip->getNoteRowForYNote(heldNote);
			if (nrHeld && nrHeld->notes.getNumElements() > 0) {
				// Find target position for matching vertical notes
				int32_t targetPos = -1;
				if (playbackHandler.isEitherClockActive()) {
					targetPos = clip->getActualCurrentPosAsIfPlayingInForwardDirection();
				}
				for (int32_t nIdx = 0; nIdx < nrHeld->notes.getNumElements(); nIdx++) {
					Note* n = nrHeld->notes.getElement(nIdx);
					if (!n) continue;
					// If playback is active, check if note overlaps playhead, otherwise pick matching note
					if (targetPos >= 0 && (n->pos > targetPos || n->pos + (int32_t)n->getLength() <= targetPos)) {
						continue;
					}
					int32_t nStart = n->pos;
					int32_t nEnd = n->pos + (int32_t)n->getLength();
					for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
						NoteRow* nrOther = clip->noteRows.getElement(r);
						if (!nrOther || nrOther == nrHeld) continue;
						for (int32_t oIdx = 0; oIdx < nrOther->notes.getNumElements(); oIdx++) {
							Note* nOther = nrOther->notes.getElement(oIdx);
							if (nOther && (nOther->pos < nEnd) && (nOther->pos + (int32_t)nOther->getLength() > nStart)) {
								activeNotes.push_back(nrOther->y);
							}
						}
					}
					if (targetPos < 0) {
						break; // When not playing, match against the first relevant note in row to avoid merging distant steps
					}
				}
			}
		}
	}

	// Deduplicate and sort activeNotes
	std::sort(activeNotes.begin(), activeNotes.end());
	activeNotes.erase(std::unique(activeNotes.begin(), activeNotes.end()), activeNotes.end());

	if (activeNotes.empty()) {
		return;
	}

	char oledBuf[128] = {0};
	char sevenSegBuf[32] = {0};
	deluge::gui::ui::keyboard::formatNotesOverlayText(activeNotes.data(), activeNotes.size(), oledBuf, sizeof(oledBuf),
	                                                  sevenSegBuf, sizeof(sevenSegBuf));

	if (display->haveOLED()) {
		display->popupText(oledBuf);
	}
	else {
		display->setText(sevenSegBuf, false, 255, true);
	}
}

void InstrumentClipMinder::cycleThroughScales() {
	displayScaleName(currentSong->cycleThroughScales());
}

// Returns if the scale could be changed or not
bool InstrumentClipMinder::setScale(Scale newScale) {
	Scale result = currentSong->setScale(newScale);
	if (result == NO_SCALE) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_CHANGE_SCALE));
		return false;
	}
	else {
		displayScaleName(result);
		return true;
	}
}

void InstrumentClipMinder::displayScaleName(Scale scale) {
	display->displayPopup(getScaleName(scale));
}

void InstrumentClipMinder::displayCurrentScaleName() {
	displayScaleName(currentSong->getCurrentScale());
}

// Returns whether currentClip is now active on Output / Instrument
bool InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible(ModelStack* modelStack) {
	bool clipIsActiveOnInstrument = (getCurrentInstrumentClip()->isActiveOnOutput());

	if (!clipIsActiveOnInstrument) {
		if (currentPlaybackMode->isOutputAvailable(getCurrentOutput())) {
			getCurrentOutput()->setActiveClip(modelStack->addTimelineCounter(getCurrentInstrumentClip()));
			clipIsActiveOnInstrument = true;
		}
	}

	return clipIsActiveOnInstrument;
}

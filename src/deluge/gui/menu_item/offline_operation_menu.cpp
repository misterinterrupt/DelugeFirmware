#include "gui/menu_item/offline_operation_menu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "model/sample/sample.h"
#include "processing/offline/normalize_job.h"
#include "processing/sound/sound.h"
#include "storage/multi_range/multisample_range.h"

namespace deluge::gui::menu_item {

OfflineOperationMenu offlineOperationMenu;

OfflineOperationMenu::OfflineOperationMenu() : MenuItem(deluge::l10n::String::EMPTY_STRING) {}

bool OfflineOperationMenu::isRelevant(ModControllableAudio* modControllable, int32_t /*whichThing*/) {
	// Only makes sense for subtractive synths using samples as oscillator
	// sources (same restriction as the audio recorder that used to occupy
	// this shortcut slot).
	Sound* sound = static_cast<Sound*>(modControllable);
	return (sound->getSynthMode() == SynthMode::SUBTRACTIVE);
}

MenuPermission OfflineOperationMenu::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                   int32_t whichThing,
                                                                   ::MultiRange** currentRange) {
	if (!isRelevant(modControllable, whichThing)) {
		return MenuPermission::NO;
	}

	// Entering via a shortcut pad does not populate
	// soundEditor.currentMultiRange - following the AudioRecorder pattern,
	// we select the first range (C3) ourselves so that
	// getCurrentAudioFileHolder() has valid state to work with.
	Sound* sound = static_cast<Sound*>(modControllable);
	Source* source = &sound->sources[whichThing];
	::MultiRange* firstRange = source->getOrCreateFirstRange();
	if (firstRange == nullptr) {
		return MenuPermission::NO;
	}
	*currentRange = firstRange;
	return MenuPermission::YES;
}

void OfflineOperationMenu::readValueAgain() {
	updateDisplay();
}

void OfflineOperationMenu::updateDisplay() {
	display->setText("NORMALIZE");
}

MenuItem* OfflineOperationMenu::selectButtonPress() {
	// checkPermissionToBeginSession has run by now, so the editor state is
	// populated the same way it is for every other sample menu.
	AudioFileHolder* holder = soundEditor.getCurrentAudioFileHolder();
	Sample* currentSample = nullptr;
	if (holder && holder->audioFile && holder->audioFileType == AudioFileType::SAMPLE) {
		currentSample = (Sample*)holder->audioFile;
	}

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	// Starts the asynchronous normalize job (or shows a popup explaining why
	// it couldn't start). Returns immediately either way.
	normalizeJob().start(currentSample);

	updateDisplay();
	return nullptr;
}

void OfflineOperationMenu::selectEncoderAction(int32_t /*offset*/) {
	// Single operation for now; encoder has nothing to change.
	updateDisplay();
}

} // namespace deluge::gui::menu_item

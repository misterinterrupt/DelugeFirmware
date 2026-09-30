#include "gui/menu_item/sample/utils.h"

#include "processing/sound/sound.h"
#include "processing/source.h"

bool isSampleModeSample(ModControllableAudio* modControllable, int32_t whichThing) {
	Sound* sound = static_cast<Sound*>(modControllable);
	Source* source = &sound->sources[whichThing];

	return (sound->getSynthMode() == ::SynthMode::SUBTRACTIVE || sound->isDrum())
	       && source->oscType == OscType::SAMPLE && source->hasAtLeastOneAudioFileLoaded();
}

#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/chords.h"

using deluge::gui::ui::keyboard::ChordList;
using deluge::gui::ui::keyboard::NONE;
using deluge::gui::ui::keyboard::Voicing;

TEST_GROUP(ChordTests) {
	ChordList chordList;
};

TEST(ChordTests, getChordBoundsCheck) {
	// For each chord, iterate through all voicing offsets and then some
	for (int chordNo = 0; chordNo < kUniqueChords; chordNo++) {
		// iterate through -5 to twice the number of possible voicings to check bounds
		for (int voicingOffset = -5; voicingOffset < 2 * kUniqueVoicings; voicingOffset++) {
			// Set the voicing offset, even if it's out of bounds
			chordList.voicingOffset[chordNo] = voicingOffset;
			// Get the voicing, should return between voicing 0 and last valid voicing
			Voicing voicing = chordList.getChordVoicing(chordNo);

			// Check that the voicing is a valid voicing (at least one offset is not NONE)
			bool valid = false;
			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				int32_t offset = voicing.offsets[i];
				if (offset != NONE) {
					valid = true;
				}
			}
			CHECK(valid);
		}
	}
}

TEST(ChordTests, adjustChordRowOffsetBoundsCheck) {
	// Test that the chord row offset is bounded by 0 and kOffScreenChords
	// Test the lower bound
	chordList.chordRowOffset = 0;
	chordList.adjustChordRowOffset(-1);
	CHECK_EQUAL(0, chordList.chordRowOffset);

	// Test the upper bound
	chordList.chordRowOffset = kOffScreenChords;
	chordList.adjustChordRowOffset(1);
	CHECK_EQUAL(kOffScreenChords, chordList.chordRowOffset);

	// Test that doing a 0 offset doesn't change the value
	chordList.chordRowOffset = kUniqueChords / 2;
	chordList.adjustChordRowOffset(0);
	CHECK_EQUAL(kUniqueChords / 2, chordList.chordRowOffset);

	// Test that a 1 offset increases the value by 1
	chordList.chordRowOffset = 0;
	chordList.adjustChordRowOffset(1);
	CHECK_EQUAL(1, chordList.chordRowOffset);

	// Test that a -1 offset decreases the value by 1
	chordList.chordRowOffset = kOffScreenChords;
	chordList.adjustChordRowOffset(-1);
	CHECK_EQUAL(kOffScreenChords - 1, chordList.chordRowOffset);
}

TEST(ChordTests, adjustVoicingOffsetBoundsCheck) {
	for (int chordNo = 0; chordNo < kUniqueChords; chordNo++) {
		// Test that the voicing offset is bounded by 0 and kUniqueVoicings - 1
		// Test the lower bound
		chordList.voicingOffset[chordNo] = 0;
		chordList.adjustVoicingOffset(chordNo, -1);
		CHECK_EQUAL(0, chordList.voicingOffset[chordNo]);

		// Test the upper bound
		chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
		chordList.adjustVoicingOffset(chordNo, 1);
		CHECK_EQUAL(kUniqueVoicings - 1, chordList.voicingOffset[chordNo]);

		// Test that doing a 0 offset doesn't change the value
		chordList.voicingOffset[chordNo] = kUniqueVoicings / 2;
		chordList.adjustVoicingOffset(chordNo, 0);
		CHECK_EQUAL(kUniqueVoicings / 2, chordList.voicingOffset[chordNo]);

		// Test that a 1 offset increases the value by 1
		chordList.voicingOffset[chordNo] = 0;
		chordList.adjustVoicingOffset(chordNo, 1);
		CHECK_EQUAL(1, chordList.voicingOffset[chordNo]);

		// Test that a -1 offset decreases the value by 1
		chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
		chordList.adjustVoicingOffset(chordNo, -1);
		CHECK_EQUAL(kUniqueVoicings - 2, chordList.voicingOffset[chordNo]);
	}
}

TEST(ChordTests, DyadIntervalIdentification) {
	using deluge::gui::ui::keyboard::identifyNotesRelationship;
	using deluge::gui::ui::keyboard::ChordMatchResult;

	int16_t notes[2] = {60, 67}; // C4, G4
	ChordMatchResult result = identifyNotesRelationship(notes, 2);
	CHECK(result.isDyad);
	STRCMP_EQUAL("Perfect 5th", result.intervalName);
	STRCMP_EQUAL("P5", result.shortIntervalName);
}

TEST(ChordTests, MajorTriadIdentification) {
	using deluge::gui::ui::keyboard::identifyNotesRelationship;
	using deluge::gui::ui::keyboard::ChordMatchResult;

	int16_t notes[3] = {60, 64, 67}; // C4, E4, G4
	ChordMatchResult result = identifyNotesRelationship(notes, 3);
	CHECK(result.isChord);
	CHECK(!result.isInversion);
	STRCMP_EQUAL("C", result.rootName);
	STRCMP_EQUAL("Major", result.chordName);
}

TEST(ChordTests, MinorTriadIdentification) {
	using deluge::gui::ui::keyboard::identifyNotesRelationship;
	using deluge::gui::ui::keyboard::ChordMatchResult;

	int16_t notes[3] = {60, 63, 67}; // C4, Eb4, G4
	ChordMatchResult result = identifyNotesRelationship(notes, 3);
	CHECK(result.isChord);
	CHECK(!result.isInversion);
	STRCMP_EQUAL("C", result.rootName);
	STRCMP_EQUAL("m", result.chordName);
}

TEST(ChordTests, MultiOctaveChordIdentification) {
	using deluge::gui::ui::keyboard::identifyNotesRelationship;
	using deluge::gui::ui::keyboard::ChordMatchResult;

	int16_t notes[3] = {48, 64, 79}; // C3, E4, G5
	ChordMatchResult result = identifyNotesRelationship(notes, 3);
	CHECK(result.isChord);
	STRCMP_EQUAL("C", result.rootName);
	STRCMP_EQUAL("Major", result.chordName);
}

TEST(ChordTests, InversionChordIdentification) {
	using deluge::gui::ui::keyboard::identifyNotesRelationship;
	using deluge::gui::ui::keyboard::ChordMatchResult;

	int16_t notes[3] = {64, 67, 72}; // E4, G4, C5 (C Major 1st inversion)
	ChordMatchResult result = identifyNotesRelationship(notes, 3);
	CHECK(result.isChord);
	CHECK(result.isInversion);
	STRCMP_EQUAL("C", result.rootName);
	STRCMP_EQUAL("E", result.bassName);
}

TEST(ChordTests, Dominant7thIdentification) {
	using deluge::gui::ui::keyboard::identifyNotesRelationship;
	using deluge::gui::ui::keyboard::ChordMatchResult;

	int16_t notes[4] = {60, 64, 67, 70}; // C4, E4, G4, Bb4
	ChordMatchResult result = identifyNotesRelationship(notes, 4);
	CHECK(result.isChord);
	STRCMP_EQUAL("C", result.rootName);
	STRCMP_EQUAL("7", result.chordName);
}


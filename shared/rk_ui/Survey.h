// Raumklang UI: speaker positions measured with a phone ("Raumklang Messen" for iPhone, LiDAR/AR).
//
// File format (JSON, UTF-8, extension .raumklang-messung or .json):
//   {
//     "format": "raumklang-survey", "version": 1,
//     "source": "iphone-lidar",            // free text: device and method
//     "created": "2026-11-18T12:00:00Z",
//     "frame": "raumklang",                 // x right, y front, z up, metres, origin = listener's ears
//     "listenerHeight": 1.2,                // ear height above the floor that was used
//     "speakers": [ { "name": "1", "position": [x, y, z], "aim": [x, y, z], "confidence": 0.9 } ]
//   }
// Every value is validated: a broken file is rejected with a message, never crashes the app.
#pragma once

#include <juce_core/juce_core.h>
#include <rk/layout.hpp>

#include <vector>

namespace rkui {

struct SurveySpeaker {
    juce::String name;
    rk::Vec3 position;
    rk::Vec3 aim;          // zero = unknown
    float confidence = 1.0f; // 0..1, how sure the phone was (LiDAR hit on a surface = high)
};

struct Survey {
    juce::String source;
    juce::String created;
    float listenerHeight = 1.2f;
    std::vector<SurveySpeaker> speakers;
};

/// Parses a survey. Returns false and a short German message (translate with tr()) on failure.
bool surveyFromJson(const juce::String& json, Survey& out, juce::String& error);
juce::String surveyToJson(const Survey& s);

enum class SurveyMatch {
    InOrder = 0,   // 1st tapped speaker = speaker 1 (output 1), and so on
    Nearest = 1,   // each speaker takes the measured position closest to its planned one
    Replace = 2,   // a new layout with exactly the measured speakers
};

struct SurveyApplied {
    rk::SpeakerLayout layout;
    std::vector<char> updated; // per speaker of `layout`: position came from the survey
    int unmatchedMeasured = 0; // measured speakers that found no place
};

/// Applies the survey to a layout. Gain, delay, groups and names of existing speakers are kept.
SurveyApplied applySurvey(const rk::SpeakerLayout& current, const Survey& survey, SurveyMatch match);

// ---------------------------------------------------------------------------------------------
// A measurement result waiting for "Übernehmen": from the microphone or from the phone.

struct ProposedSpeaker {
    bool measured = false;
    bool hasPosition = false;
    rk::Vec3 position;
    bool hasAim = false;
    rk::Vec3 aim;            // zero = towards the listener
    bool hasGain = false;
    float gainDb = 0.0f;
    bool hasDelay = false;
    float delayMs = 0.0f;
    float uncertainty = -1.0f; // metres, < 0 unknown
    juce::String note;       // short, German, e.g. "Nicht erkannt"
    bool warn = false;
};

struct Proposal {
    std::vector<ProposedSpeaker> speakers; // one per speaker of the current layout
    bool replaces = false;                 // phone survey as a new layout
    rk::SpeakerLayout replacement;
    bool canPositions = false, canDelays = false, canLevels = false;
};

struct ProposalOptions {
    bool positions = true, delays = true, levels = true;
};

/// The layout after accepting. Positions keep "outwards" / "inwards" aims relative to the new place.
rk::SpeakerLayout applyProposal(const rk::SpeakerLayout& current, const Proposal& p, ProposalOptions o);
Proposal proposalFromSurvey(const rk::SpeakerLayout& current, const Survey& s, SurveyMatch match);

} // namespace rkui

// Raumklang app: automatic speaker calibration with a measuring microphone.
//
// The controller runs on the message thread. The audio thread only plays the sweep and records
// (rk::calib::SweepRunner in AudioHost); the analysis of each recording runs on a worker thread.
// Every step can be cancelled, and a missing microphone, a silent speaker or a clipping input
// ends in a plain-language message instead of a wrong result.
#pragma once

#include "AudioHost.h"

#include <rk_ui/Survey.h>

#include <memory>
#include <optional>

namespace rkapp {

struct CalibrationSettings {
    int micInput = 0;       // index among the active device inputs
    bool positions = true;  // several measuring points: positions too. Otherwise only delay and level.
    int numPoints = 8;
    float levelDb = -18.0f;
    float temperature = 20.0f;
    float tapeMetres = 0.0f; // optional: measured distance from M1 to speaker 1, 0 = none
};

class Calibration {
public:
    enum class Phase { Ready, Measuring, Result };

    Calibration(AudioHost& host, const rk::SpeakerLayout& layout, const CalibrationSettings& settings);
    ~Calibration();

    /// Empty if the measurement can start, otherwise why not (German, short).
    juce::String problemBeforeStart() const;

    /// Plays the sweep on every speaker in turn and records it at measuring point `point`.
    bool measurePoint(int point);
    void cancel();
    /// Call from a timer (~30 Hz). Returns true if something visible changed.
    bool poll();

    Phase phase() const noexcept { return phase_; }
    const CalibrationSettings& settings() const noexcept { return settings_; }
    const std::vector<rk::Vec3>& points() const noexcept { return points_; }
    /// 0 open, 1 measured, 2 problem
    const std::vector<int>& pointState() const noexcept { return pointState_; }
    int currentPoint() const noexcept { return point_; }
    int playingSpeaker() const noexcept { return playing_; }
    /// Speakers measured at the current point so far / to measure.
    int done() const noexcept { return doneCount_; }
    int total() const noexcept { return static_cast<int>(order_.size()); }
    /// Next point to measure, -1 when all are done.
    int nextPoint() const;
    int measuredPoints() const;
    /// Last problem in plain words (empty = none).
    const juce::String& message() const noexcept { return message_; }

    /// Where the microphone goes, in plain words ("1,0 m vorne links, 50 cm höher").
    juce::String describePoint(int point) const;

    struct Cell {
        bool measured = false;
        rk::calib::Measurement m;
    };
    const Cell& cell(int speaker, int point) const;

    /// Computes the result from everything measured so far. Needs at least the first point.
    bool evaluate();
    const rkui::Proposal& proposal() const noexcept { return proposal_; }
    /// Overall verdict for the status card.
    struct Verdict {
        bool ok = false;
        juce::String title, detail;
        int good = 0, total = 0;
    };
    const Verdict& verdict() const noexcept { return verdict_; }
    /// Back to measuring (e.g. to repeat a point).
    void reopen() { phase_ = Phase::Ready; }

private:
    void startNextSpeaker();
    void finishPoint();

    AudioHost& host_;
    rk::SpeakerLayout layout_;
    CalibrationSettings settings_;
    rk::calib::SweepParams sweepParams_;
    std::vector<float> sweep_;
    std::vector<rk::Vec3> points_;
    std::vector<int> pointState_;
    std::vector<int> order_; // speakers (outputs) to measure
    std::vector<Cell> cells_; // speaker * points + point
    Phase phase_ = Phase::Ready;
    int point_ = -1;
    int orderPos_ = 0;
    int playing_ = -1;
    int doneCount_ = 0;
    int pendingJobs_ = 0;
    double sweepStarted_ = 0.0;
    juce::String message_;
    rkui::Proposal proposal_;
    Verdict verdict_;

    struct Shared; // results handed back by the worker thread
    std::shared_ptr<Shared> shared_;
    juce::ThreadPool pool_{1};
};

} // namespace rkapp

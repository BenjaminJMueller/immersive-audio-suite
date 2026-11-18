#include "Calibration.h"

#include <rk_ui/Strings.h>

using rkui::tr;
using rkui::u8;

namespace rkapp {

struct Calibration::Shared {
    juce::CriticalSection lock;
    struct Result {
        int speaker = 0, point = 0;
        Cell cell;
    };
    std::vector<Result> results;
    std::vector<float> sweep;
    rk::calib::SweepParams params;
};

Calibration::Calibration(AudioHost& host, const rk::SpeakerLayout& layout, const CalibrationSettings& settings)
    : host_(host), layout_(layout), settings_(settings), shared_(std::make_shared<Shared>()) {
    settings_.numPoints = juce::jlimit(1, rk::calib::kMaxPoints, settings_.numPoints);
    settings_.levelDb = juce::jlimit(-50.0f, -6.0f, std::isfinite(settings_.levelDb) ? settings_.levelDb : -18.0f);
    sweepParams_.sampleRate = host_.sampleRate();
    sweepParams_.f2 = std::min(20000.0, 0.45 * sweepParams_.sampleRate);
    sweepParams_.seconds = 1.5;
    sweepParams_.tailSeconds = 0.9;
    sweepParams_.levelDb = settings_.levelDb;
    sweepParams_ = rk::calib::sanitize(sweepParams_);
    sweep_ = rk::calib::makeSweep(sweepParams_);
    shared_->sweep = sweep_;
    shared_->params = sweepParams_;
    host_.calibration.prepare(sweep_, rk::calib::recordLength(sweepParams_));

    points_ = rk::calib::suggestPoints(layout_, settings_.positions ? settings_.numPoints : 1);
    pointState_.assign(points_.size(), 0);
    const int outs = host_.numDeviceOutputs();
    for (int m = 0; m < (int)layout_.speakers.size(); ++m)
        if (layout_.speakers[(size_t)m].enabled && m < outs) order_.push_back(m);
    cells_.assign(layout_.speakers.size() * points_.size(), Cell{});
}

Calibration::~Calibration() {
    host_.calibration.cancel();
    pool_.removeAllJobs(true, 4000);
}

juce::String Calibration::problemBeforeStart() const {
    if (host_.numDeviceOutputs() == 0) return tr("Kein Audiogerät. Unter 4 Ausgabe ein Gerät wählen.");
    if (host_.numDeviceInputs() == 0) return tr("Kein Eingang. Ein Audiogerät mit Mikrofoneingang wählen.");
    if (settings_.micInput < 0 || settings_.micInput >= host_.numDeviceInputs()) return tr("Bitte den Mikrofoneingang wählen.");
    if (order_.empty()) return tr("Kein Lautsprecher hat einen Ausgang.");
    return {};
}

const Calibration::Cell& Calibration::cell(int speaker, int point) const {
    static const Cell empty;
    if (speaker < 0 || point < 0 || speaker >= (int)layout_.speakers.size() || point >= (int)points_.size()) return empty;
    return cells_[(size_t)speaker * points_.size() + (size_t)point];
}

int Calibration::nextPoint() const {
    for (int j = 0; j < (int)pointState_.size(); ++j)
        if (pointState_[(size_t)j] != 1) return j;
    return -1;
}

int Calibration::measuredPoints() const {
    int n = 0;
    for (int s : pointState_) n += s == 1 ? 1 : 0;
    return n;
}

bool Calibration::measurePoint(int point) {
    if (phase_ == Phase::Measuring || point < 0 || point >= (int)points_.size()) return false;
    message_ = problemBeforeStart();
    if (message_.isNotEmpty()) return false;
    if (host_.calibration.busy()) return false;
    for (int m = 0; m < (int)layout_.speakers.size(); ++m) cells_[(size_t)m * points_.size() + (size_t)point] = Cell{};
    point_ = point;
    orderPos_ = 0;
    doneCount_ = 0;
    pointState_[(size_t)point] = 0;
    phase_ = Phase::Measuring;
    host_.engine.speakerTest = -1;
    if (host_.simulating()) {
        // Test room: the "user" puts the microphone on the point, a few centimetres off.
        juce::Random rnd(point + 7);
        host_.moveSimulatedMicrophone(points_[(size_t)point] + rk::Vec3{(rnd.nextFloat() - 0.5f) * 0.06f, (rnd.nextFloat() - 0.5f) * 0.06f, 0.0f});
    }
    startNextSpeaker();
    return phase_ == Phase::Measuring;
}

void Calibration::startNextSpeaker() {
    if (orderPos_ >= (int)order_.size()) {
        playing_ = -1;
        return;
    }
    const int sp = order_[(size_t)orderPos_];
    if (!host_.calibration.start(sp, settings_.micInput)) {
        message_ = tr("Die Messung konnte nicht starten.");
        pointState_[(size_t)point_] = 2;
        phase_ = Phase::Ready;
        playing_ = -1;
        return;
    }
    playing_ = sp;
    sweepStarted_ = juce::Time::getMillisecondCounterHiRes();
}

void Calibration::cancel() {
    host_.calibration.cancel();
    if (phase_ == Phase::Measuring && point_ >= 0) pointState_[(size_t)point_] = 0;
    orderPos_ = (int)order_.size();
    playing_ = -1;
    phase_ = Phase::Ready;
    message_.clear();
}

bool Calibration::poll() {
    if (phase_ != Phase::Measuring) return false;
    // Results from the worker.
    std::vector<Shared::Result> results;
    {
        const juce::ScopedLock sl(shared_->lock);
        results.swap(shared_->results);
    }
    bool clipped = false;
    for (const auto& r : results) {
        --pendingJobs_;
        if (r.point != point_ || r.speaker < 0 || r.speaker >= (int)layout_.speakers.size()) continue;
        cells_[(size_t)r.speaker * points_.size() + (size_t)r.point] = r.cell;
        ++doneCount_;
        clipped = clipped || r.cell.m.clipped;
    }
    if (clipped) {
        host_.calibration.cancel();
        orderPos_ = (int)order_.size();
        playing_ = -1;
        message_ = tr("Zu laut: Messlautstärke oder Mikrofonpegel senken.");
    }

    const auto st = host_.calibration.state();
    if (st == rk::calib::SweepRunner::State::Done) {
        std::vector<float> rec, loop;
        if (host_.calibration.take(rec, loop)) {
            const int sp = playing_, pt = point_;
            auto shared = shared_;
            ++pendingJobs_;
            pool_.addJob([shared, rec = std::move(rec), sp, pt] {
                Shared::Result r;
                r.speaker = sp;
                r.point = pt;
                r.cell.measured = true;
                r.cell.m = rk::calib::analyse(rec, shared->sweep, shared->params);
                const juce::ScopedLock sl(shared->lock);
                shared->results.push_back(std::move(r));
            });
            ++orderPos_;
            startNextSpeaker();
        }
    } else if (host_.calibration.busy()) {
        const double limit = 1000.0 * (rk::calib::recordLength(sweepParams_) / sweepParams_.sampleRate) + 3000.0;
        if (juce::Time::getMillisecondCounterHiRes() - sweepStarted_ > limit) {
            host_.calibration.cancel();
            orderPos_ = (int)order_.size();
            playing_ = -1;
            message_ = tr("Keine Audiodaten. Läuft das Audiogerät?");
        }
    }
    if (orderPos_ >= (int)order_.size() && pendingJobs_ <= 0 && !host_.calibration.busy()) finishPoint();
    return true;
}

void Calibration::finishPoint() {
    playing_ = -1;
    phase_ = Phase::Ready;
    if (point_ < 0) return;
    juce::StringArray missing;
    int heard = 0;
    for (int sp : order_) {
        const Cell& c = cell(sp, point_);
        if (c.measured && c.m.arrival.valid && !c.m.clipped) ++heard;
        else missing.add(juce::String(sp + 1));
    }
    if (message_.isNotEmpty()) {
        pointState_[(size_t)point_] = 2;
    } else if (heard * 2 < (int)order_.size()) {
        pointState_[(size_t)point_] = 2;
        message_ = tr("Nur") + " " + juce::String(heard) + " " + tr("von") + " " + juce::String(order_.size()) + " " +
                   tr("Lautsprechern gehört. Mikrofoneingang und Pegel prüfen.");
    } else {
        pointState_[(size_t)point_] = 1;
        // In a donut the column hides some speakers from some points: that is expected.
        if (!missing.isEmpty() && !settings_.positions) message_ = tr("Nicht gehört") + ": " + missing.joinIntoString(", ");
    }
}

juce::String Calibration::describePoint(int point) const {
    if (point < 0 || point >= (int)points_.size()) return {};
    const rk::Vec3 p = points_[(size_t)point];
    juce::String where;
    const float d = std::hypot(p.x, p.y);
    if (d < 0.05f) {
        where = tr("am Hörplatz");
    } else {
        const float az = rk::toPolar({p.x, p.y, 0.0f}).azimuthDeg;
        const char* dirs[] = {"vorne", "vorne links", "links", "hinten links", "hinten", "hinten rechts", "rechts", "vorne rechts"};
        const int sector = ((int)std::floor((az + 22.5f + 360.0f) / 45.0f)) % 8;
        where = rkui::num(d, 1) + " m " + tr(dirs[sector]) + " " + tr("vom Hörplatz");
    }
    if (p.z > 0.05f) where << ", " << juce::String(juce::roundToInt(p.z * 100.0f)) << " cm " << tr("höher");
    else if (p.z < -0.05f) where << ", " << juce::String(juce::roundToInt(-p.z * 100.0f)) << " cm " << tr("tiefer");
    else where << ", " << tr("auf Ohrhöhe");
    return where;
}

bool Calibration::evaluate() {
    using namespace rk::calib;
    const int ns = (int)layout_.speakers.size();
    const int np = (int)points_.size();
    std::vector<Observation> obs;
    std::vector<char> heard((size_t)ns, 0);
    for (int sp : order_)
        for (int j = 0; j < np; ++j) {
            if (pointState_[(size_t)j] != 1) continue;
            const Cell& c = cell(sp, j);
            if (!c.measured || !c.m.arrival.valid || c.m.clipped) continue;
            Observation o;
            o.speaker = sp;
            o.point = j;
            o.seconds = c.m.arrival.seconds;
            o.levelDb = c.m.arrival.levelDb;
            o.weight = juce::jlimit(0.2f, 1.0f, (c.m.arrival.snrDb - 15.0f) / 20.0f) * juce::jlimit(0.3f, 1.0f, c.m.arrival.directness * 1.5f);
            obs.push_back(o);
            heard[(size_t)sp] = 1;
        }
    if (obs.empty()) {
        message_ = tr("Noch nichts gemessen.");
        return false;
    }
    const float c = speedOfSound(settings_.temperature);

    std::vector<rk::Vec3> measuredPts;
    for (int j = 0; j < np; ++j)
        if (pointState_[(size_t)j] == 1) measuredPts.push_back(points_[(size_t)j]);
    const bool geometry = settings_.positions && measuredPts.size() >= 4 && pointsSpanVolume(measuredPts);

    SolveResult solved;
    if (geometry) {
        SolveInput in;
        for (const auto& s : layout_.speakers) in.speakerGuess.push_back(s.position);
        in.pointGuess = points_;
        in.observations = obs;
        in.speedOfSound = c;
        if (settings_.tapeMetres > 0.05f && ns > 0) in.distances.push_back({0, 0, settings_.tapeMetres});
        solved = solvePositions(in);
    }

    AlignInput ai;
    ai.numSpeakers = ns;
    ai.observations = obs;
    ai.referencePoint = 0;
    ai.geometry = geometry ? &solved : nullptr;
    ai.referencePosition = geometry && !solved.points.empty() ? solved.points[0] : points_[0];
    ai.speedOfSound = c;
    for (const auto& s : layout_.speakers) ai.isSubwoofer.push_back(s.isSubwoofer);
    const auto aligned = align(ai);

    std::vector<std::pair<int, int>> swaps;
    if (geometry) {
        std::vector<rk::Vec3> planned, measured;
        std::vector<bool> valid;
        for (int i = 0; i < ns; ++i) {
            planned.push_back(layout_.speakers[(size_t)i].position);
            measured.push_back(solved.speakers[(size_t)i].position);
            valid.push_back(solved.speakers[(size_t)i].solved);
        }
        swaps = suspectedSwaps(planned, measured, valid);
    }

    proposal_ = rkui::Proposal{};
    proposal_.canPositions = geometry;
    proposal_.canDelays = proposal_.canLevels = true;
    proposal_.speakers.resize((size_t)ns);
    int good = 0;
    float worst = 0.0f;
    const bool hasOutput = !order_.empty();
    for (int i = 0; i < ns; ++i) {
        auto& q = proposal_.speakers[(size_t)i];
        const auto& sp = layout_.speakers[(size_t)i];
        const bool inOrder = std::find(order_.begin(), order_.end(), i) != order_.end();
        if (!inOrder) {
            q.note = sp.enabled ? tr("Kein Ausgang") : tr("Aus");
            continue;
        }
        if (!heard[(size_t)i] || i >= (int)aligned.size() || !aligned[(size_t)i].measured) {
            q.note = tr("Nicht gehört");
            q.warn = true;
            continue;
        }
        const auto& a = aligned[(size_t)i];
        q.measured = true;
        q.hasDelay = true;
        q.delayMs = a.delayMs;
        q.hasGain = !sp.isSubwoofer;
        q.gainDb = a.gainDb;
        if (a.veryQuiet) { q.note = tr("Sehr leise"); q.warn = true; }
        if (geometry && !sp.isSubwoofer) {
            const auto& f = solved.speakers[(size_t)i];
            // Shown as about 2 sigma: in simulations the real error stays within that.
            q.uncertainty = 2.0f * f.uncertainty;
            if (f.solved) {
                q.hasPosition = true;
                q.position = f.position;
                worst = std::max(worst, q.uncertainty);
            } else if (!q.warn) {
                q.note = tr("Position unsicher");
                q.warn = true;
            }
        }
        for (const auto& s : swaps)
            if (s.first == i && !q.warn) {
                q.note = tr("Vertauscht mit") + " " + juce::String(s.second + 1) + "?";
                q.warn = true;
            }
        if (q.note.isEmpty()) {
            if (q.hasPosition) q.note = u8("\xc2\xb1 ") + juce::String(juce::jmax(1, juce::roundToInt(q.uncertainty * 100.0f))) + " cm";
            else if (a.distance > 0.0f) q.note = rkui::num(a.distance, 2) + " m";
            else q.note = rkui::num(a.delayMs, 1) + " ms";
        }
        if (!q.warn) ++good;
    }
    verdict_ = Verdict{};
    verdict_.good = good;
    verdict_.total = (int)order_.size();
    if (hasOutput && good == verdict_.total) {
        verdict_.ok = true;
        verdict_.title = !geometry ? tr("Sitzt gut") : worst < 0.10f ? tr("Sitzt sehr gut") : worst < 0.25f ? tr("Sitzt gut") : tr("Sitzt ungefähr");
        verdict_.detail = geometry ? tr("Positionen auf") + u8(" \xc2\xb1 ") + juce::String(juce::jmax(1, juce::roundToInt(worst * 100.0f))) + " cm " + tr("genau")
                                   : tr("Laufzeit und Pegel am Messpunkt 1 ausgeglichen");
        if (geometry && worst >= 0.25f)
            verdict_.detail << ". " << tr("Mehr Messpunkte, weiter auseinander, oder ein gemessener Abstand machen es genauer.");
    } else {
        verdict_.title = tr("Bitte prüfen");
        verdict_.detail = juce::String(good) + " " + tr("von") + " " + juce::String(verdict_.total) + " " + tr("Lautsprechern ohne Auffälligkeit");
    }
    if (settings_.positions && !geometry)
        verdict_.detail << ". " << tr("Für Positionen mindestens 4 Messpunkte, einer davon höher.");
    phase_ = Phase::Result;
    message_.clear();
    return true;
}

} // namespace rkapp

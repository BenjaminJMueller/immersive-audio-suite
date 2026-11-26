#include "Sofa.h"

#include "Strings.h"

#include <mysofa.h>

#include <limits>

namespace rkui {

juce::String SofaResult::summary() const {
    if (set == nullptr) return error;
    const double khz = set->sampleRate / 1000.0;
    const juce::String rate = num(khz, std::abs(khz - std::round(khz)) < 0.05 ? 0 : 1);
    return juce::String((int)set->directions.size()) + " " + tr("Richtungen") + ", " + rate + " kHz";
}

SofaResult loadSofa(const juce::File& file) {
    SofaResult r;
    if (!file.existsAsFile()) {
        r.error = tr("Datei nicht gefunden");
        return r;
    }
    if (file.getSize() > 512ll * 1024 * 1024) {
        r.error = tr("Datei zu groß");
        return r;
    }
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || data.getSize() == 0) {
        r.error = tr("Datei lässt sich nicht lesen");
        return r;
    }
    return loadSofa(data.getData(), data.getSize(), file.getFileNameWithoutExtension());
}

SofaResult loadSofa(const void* data, size_t size, const juce::String& name) {
    SofaResult r;
    if (data == nullptr || size < 8 || size > (size_t)std::numeric_limits<int>::max()) {
        r.error = tr("Keine SOFA-Datei");
        return r;
    }
    int err = 0;
    MYSOFA_HRTF* h = mysofa_load_data(static_cast<const char*>(data), size, &err);
    if (h == nullptr || err != MYSOFA_OK) {
        if (h != nullptr) mysofa_free(h);
        r.error = tr("Keine SOFA-Datei");
        return r;
    }
    struct Free { void operator()(MYSOFA_HRTF* p) const { mysofa_free(p); } };
    std::unique_ptr<MYSOFA_HRTF, Free> hold(h);

    if (mysofa_check(h) != MYSOFA_OK || h->R != 2 || h->M == 0 || h->N == 0 ||
        h->DataIR.elements != h->M * h->R * h->N || h->SourcePosition.elements != h->M * h->C || h->C != 3 ||
        h->DataSamplingRate.elements < 1) {
        r.error = tr("Die Datei enthält keine Kopfübertragungsfunktionen für zwei Ohren (SimpleFreeFieldHRIR)");
        return r;
    }
    mysofa_tospherical(h); // SourcePosition: azimuth (left positive), elevation, distance

    auto set = std::make_shared<rk::HrirSet>();
    set->sampleRate = h->DataSamplingRate.values[0];
    set->name = name.substring(0, 60).toStdString();
    // Receiver 0 is the left ear in SOFA files; the position says so when it is given.
    int leftRx = 0;
    if (h->ReceiverPosition.elements >= 6) {
        const float y0 = h->ReceiverPosition.values[1], y1 = h->ReceiverPosition.values[4];
        if (std::isfinite(y0) && std::isfinite(y1) && y1 > y0) leftRx = 1;
    }
    // Delays stored separately (Data.Delay, in samples): per measurement or once for all.
    auto delayOf = [h](unsigned m, unsigned rx) {
        const auto& d = h->DataDelay;
        float v = 0.0f;
        if (d.values != nullptr && d.elements == h->M * h->R) v = d.values[m * h->R + rx];
        else if (d.values != nullptr && d.elements == h->R) v = d.values[rx];
        return std::isfinite(v) ? juce::jlimit(0, 4096, (int)std::lround(v)) : 0;
    };
    int maxDelay = 0;
    for (unsigned m = 0; m < h->M; ++m)
        for (unsigned rx = 0; rx < 2; ++rx) maxDelay = juce::jmax(maxDelay, delayOf(m, rx));
    const int n = (int)h->N;
    set->length = n + maxDelay;
    if ((size_t)set->length * h->M > 64u * 1024 * 1024) {
        r.error = tr("Datei zu groß");
        return r;
    }
    set->directions.resize(h->M);
    set->left.assign((size_t)h->M * (size_t)set->length, 0.0f);
    set->right.assign((size_t)h->M * (size_t)set->length, 0.0f);
    for (unsigned m = 0; m < h->M; ++m) {
        const float* p = h->SourcePosition.values + m * 3;
        set->directions[m] = rk::toCartesian({p[0], juce::jlimit(-90.0f, 90.0f, p[1]), 1.0f});
        for (unsigned rx = 0; rx < 2; ++rx) {
            auto& dst = (rx == (unsigned)leftRx) ? set->left : set->right;
            const float* ir = h->DataIR.values + ((size_t)m * h->R + rx) * (size_t)n;
            const int d = delayOf(m, rx);
            std::copy(ir, ir + n, dst.begin() + (ptrdiff_t)((size_t)m * (size_t)set->length + (size_t)d));
        }
    }
    if (!set->finalize()) {
        r.error = tr("Die Messdaten in der Datei sind leer oder beschädigt");
        return r;
    }
    r.set = std::move(set);
    return r;
}

} // namespace rkui

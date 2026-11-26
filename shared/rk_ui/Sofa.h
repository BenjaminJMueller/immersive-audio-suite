#pragma once

#include <juce_core/juce_core.h>
#include <rk/binaural.hpp>

#include <memory>

namespace rkui {

struct SofaResult {
    std::shared_ptr<const rk::HrirSet> set; // null on error
    juce::String error;                     // in the interface language
    /// One line for the interface, e.g. "710 Richtungen, 44,1 kHz".
    juce::String summary() const;
};

/// Reads a SOFA file (AES69, SimpleFreeFieldHRIR) with measured head-related impulse
/// responses. Any thread; can take a moment for large files. Never throws: a broken or
/// unsuitable file gives an error text instead.
SofaResult loadSofa(const juce::File& file);
/// Same, from memory (tests, fuzzing).
SofaResult loadSofa(const void* data, size_t size, const juce::String& name);

} // namespace rkui

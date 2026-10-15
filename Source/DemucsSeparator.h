#pragma once

#include "StemSeparator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

//==============================================================================
// QSplit Phase 2 — real AI stem separation via Demucs v4 (htdemucs).
//
// Runs the MIT-licensed htdemucs ONNX model (fp16 weights, 158 MB) through
// ONNX Runtime on a background thread. The model is NOT bundled — it is
// downloaded on first use into ~/Documents/Headroom Audio/QSplit/Models/.
//
// Model I/O (fixed):
//   input  "mix":   [1, 2, 343980] float32  (stereo, 44.1 kHz, ~7.8 s)
//   output "stems": [1, 4, 2, 343980] float32, stem order = drums, bass,
//                   other, vocals  (remapped to the StemSeparator order
//                   vocals, drums, bass, other on the way out).
//
// Chunking mirrors the reference demucs-onnx Python implementation:
// quarter-segment overlap-add with a triangular fade window, normalized by
// accumulated weight. Graph optimization is DISABLED — the default ORT
// optimizer hangs on this 13k-node graph (verified 2026-10-15).
class DemucsSeparator : public StemSeparator
{
public:
    DemucsSeparator();
    ~DemucsSeparator() override;

    juce::String getName() const override
    {
        return "DemucsSeparator — htdemucs (ONNX, MIT)";
    }

    // Returns the local model path (downloaded on first use).
    static juce::File getModelFile();

    // True if the model file exists locally and looks valid (>100 MB).
    static bool isModelAvailable();

    // URL the model is downloaded from on first use.
    static juce::String getModelDownloadURL();

    bool separate (const juce::AudioBuffer<float>& input,
                   double sampleRate,
                   std::array<juce::AudioBuffer<float>, 4>& stems,
                   std::function<void (float)> progressCallback,
                   std::atomic<bool>& cancelFlag) override;

private:
    struct OrtImpl;
    std::unique_ptr<OrtImpl> impl;

    bool ensureSessionLoaded (juce::String& errorOut);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DemucsSeparator)
};

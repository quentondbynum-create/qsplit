#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <thread>

//==============================================================================
// QSplit stem-separation interface.
//
// Phase 2 implements this with a real AI model (ONNX Demucs/htdemucs).
// The processor, UI, transport, mixer, and export code all talk to this
// interface only, so swapping the implementation requires no other changes.
//
// Contract:
//  - input: stereo song buffer (any length), sampleRate as given.
//  - stems: 4 output buffers the implementation must fill, same length and
//    channel count (2) as the input. Index order: Vocals, Drums, Bass, Other.
//  - progressCallback: 0.0..1.0, invoked from the WORKER thread. The caller
//    marshals it to the message thread as needed.
//  - cancelFlag: poll periodically; abort promptly and return false if set.
//  - Return true on success, false on failure or cancellation.
class StemSeparator
{
public:
    enum Stem
    {
        Vocals = 0,
        Drums  = 1,
        Bass   = 2,
        Other  = 3,
        NumStems = 4
    };

    static juce::String getStemName (int stem)
    {
        switch (stem)
        {
            case Vocals: return "Vocals";
            case Drums:  return "Drums";
            case Bass:   return "Bass";
            case Other:  return "Other";
            default:     return {};
        }
    }

    // Brand colors for the four stems (shared with the UI).
    static juce::Colour getStemColour (int stem)
    {
        switch (stem)
        {
            case Vocals: return juce::Colour (0xff00e5ff); // cyan
            case Drums:  return juce::Colour (0xffff2e88); // magenta
            case Bass:   return juce::Colour (0xffff9f1c); // orange
            case Other:  return juce::Colour (0xffb6ff2e); // lime
            default:     return juce::Colours::white;
        }
    }

    virtual ~StemSeparator() = default;

    virtual juce::String getName() const = 0;

    virtual bool separate (const juce::AudioBuffer<float>& input,
                           double sampleRate,
                           std::array<juce::AudioBuffer<float>, 4>& stems,
                           std::function<void (float)> progressCallback,
                           std::atomic<bool>& cancelFlag) = 0;
};

//==============================================================================
// STUB — Phase 1 placeholder.
//
// Copies the whole song into the "Other" stem and leaves Vocals/Drums/Bass
// silent, so the transport, mixer, cleanup gate, bounce/export, and presets
// all work end-to-end before the AI model lands. Clearly marked so nobody
// mistakes it for real separation.
class StubSeparator : public StemSeparator
{
public:
    juce::String getName() const override
    {
        return "StubSeparator — Phase 1 placeholder (no real separation)";
    }

    bool separate (const juce::AudioBuffer<float>& input,
                   double /*sampleRate*/,
                   std::array<juce::AudioBuffer<float>, 4>& stems,
                   std::function<void (float)> progressCallback,
                   std::atomic<bool>& cancelFlag) override
    {
        const int numSamples = input.getNumSamples();

        for (int s = 0; s < 4; ++s)
        {
            stems[s].setSize (2, numSamples, false, true, true);

            if (s == Other)
            {
                const int chs = juce::jmin (2, input.getNumChannels());
                for (int ch = 0; ch < chs; ++ch)
                    stems[s].copyFrom (ch, 0, input, ch, 0, numSamples);
            }
        }

        // Simulate progressive work so the progress bar animates.
        constexpr int steps = 20;
        for (int i = 0; i <= steps; ++i)
        {
            if (cancelFlag.load())
                return false;

            std::this_thread::sleep_for (std::chrono::milliseconds (60));
            progressCallback (static_cast<float> (i) / static_cast<float> (steps));
        }

        return ! cancelFlag.load();
    }
};

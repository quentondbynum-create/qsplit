#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <functional>

#include "StemSeparator.h"
#include "DemucsSeparator.h"

//==============================================================================
// Simple STFT spectral gate — the "Cleanup" stage.
//
// Bins below an adaptive threshold are attenuated; the per-bin mask is
// smoothed (fast attack, slow release) to keep musical noise down.
// Phase 1 implementation: good enough to scrub background bleed/artifacts,
// not a full denoiser. 1024-pt FFT, 256 hop, Hann WOLA.
class SpectralGate
{
public:
    SpectralGate();

    void prepare (double sampleRate);
    void setAmount (float amount01); // 0 = bypass
    void reset();

    // In-place stereo processing, any block size.
    void process (juce::AudioBuffer<float>& buffer);

    // Group delay of the overlap-add chain, for offline latency compensation.
    static constexpr int getLatencySamples() { return fftSize - hopSize; }

private:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1;

    void processFrame (int channel);

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                                juce::dsp::WindowingFunction<float>::hann };

    std::array<std::vector<float>, 2> inFifo, outFifo;
    std::array<int, 2> fifoPos { 0, 0 };
    std::vector<float> fftData;    // 2 * fftSize interleaved
    std::vector<float> magnitudes; // numBins scratch
    std::vector<float> hannTable;  // fftSize analysis/synthesis window
    std::array<std::vector<float>, 2> mask; // smoothed per-bin attenuation

    int samplesSinceFrame = 0;
    float amount = 0.0f;
    bool prepared = false;
};

//==============================================================================
/*
    QSplit — AI stem separator for Headroom Audio (Phase 1).

    Flow: drop in a song -> background separation (ThreadPool, never the audio
    thread) -> 4 stem buffers -> internal transport plays them through a
    per-stem mixer (mute/solo/level) -> per-stem spectral-gate cleanup ->
    master gain -> out. Each stem can be bounced to WAV (what you hear is
    what you get: the bounce runs the same cleanup gate).

    Phase 1 used StubSeparator; Phase 2 uses DemucsSeparator (ONNX htdemucs).
*/
class QSplitProcessor : public juce::AudioProcessor
{
public:
    enum class SepState { NoSong, SongLoaded, Separating, Ready, Failed };

    QSplitProcessor();
    ~QSplitProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- parameters ----
    juce::AudioProcessorValueTreeState apvts;

    static constexpr const char* kCleanup = "cleanup"; // %, 0..100
    static constexpr const char* kMaster  = "master";  // gain, 0..2

    static juce::String levelParamId (int stem) { return "level" + juce::String (stem); }
    static juce::String muteParamId  (int stem) { return "mute"  + juce::String (stem); }
    static juce::String soloParamId  (int stem) { return "solo"  + juce::String (stem); }

    // ---- song / separation ----
    bool loadSongFile (const juce::File& file); // message thread
    void startSeparation();                     // message thread
    void cancelSeparation();                    // message thread

    SepState getSeparationState() const { return sepState.load(); }
    float getSeparationProgress() const { return sepProgress.load(); }
    bool hasSong() const { return songBuffer.getNumSamples() > 0; }
    juce::String getSongName() const { return songName; }
    double getSongLengthSeconds() const;
    double getSongSampleRate() const { return songSampleRate; }
    const juce::AudioBuffer<float>& getSongBuffer() const { return songBuffer; }

    // ---- transport (internal, independent of DAW transport) ----
    void play();
    void pause();
    void togglePlay();
    void stopTransport();
    bool isPlaying() const { return playing.load(); }
    double getPlayheadSeconds() const;
    void setPlayheadSeconds (double seconds);

    // ---- export: stem + cleanup gate applied, unity gain ----
    bool renderStemToWav (int stemIndex, const juce::File& destFile);

    // ---- UI callbacks (invoked on the message thread) ----
    std::function<void()> onSeparationFinished;
    std::function<void()> onTransportChanged;

private:
    class SeparationJob : public juce::ThreadPoolJob
    {
    public:
        explicit SeparationJob (QSplitProcessor& o);
        JobStatus runJob() override;
    private:
        QSplitProcessor& owner;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void resetGates();

    juce::ThreadPool threadPool { 1 };
    std::unique_ptr<StemSeparator> separator;

    juce::AudioBuffer<float> songBuffer;
    double songSampleRate = 44100.0;
    juce::String songName;

    std::array<juce::AudioBuffer<float>, 4> stems;

    std::atomic<SepState> sepState { SepState::NoSong };
    std::atomic<float> sepProgress { 0.0f };
    std::atomic<bool> cancelFlag { false };

    juce::CriticalSection stemLock;

    std::atomic<bool> playing { false };
    std::atomic<double> playheadSamples { 0.0 }; // fractional, in song-rate samples

    std::array<SpectralGate, 4> cleanupGates;
    float lastCleanupAmount = -1.0f;
    std::atomic<bool> gatesNeedReset { false };

    juce::AudioBuffer<float> mixTemp;
    double currentSampleRate = 44100.0;
};

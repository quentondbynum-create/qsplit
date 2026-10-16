#include "DemucsSeparator.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
// Demucs v4 (htdemucs) constants — must match the exported ONNX graph.
namespace
{
constexpr int kModelSampleRate = 44100;
constexpr int kModelChannels   = 2;
constexpr int kSegmentSamples  = 343980;          // ~7.8 s
constexpr int kOverlapSamples  = kSegmentSamples / 4;
constexpr int kStrideSamples   = kSegmentSamples - kOverlapSamples;

// Model stem order -> StemSeparator stem order.
constexpr int kModelToInterface[4] =
{
    StemSeparator::Drums,   // model row 0 = drums
    StemSeparator::Bass,    // model row 1 = bass
    StemSeparator::Other,   // model row 2 = other
    StemSeparator::Vocals,  // model row 3 = vocals
};

// Triangular fade window for overlap-add (ports demucs_onnx.inference).
std::vector<float> makeTransitionWindow()
{
    std::vector<float> w (kSegmentSamples, 1.0f);
    for (int i = 0; i < kOverlapSamples; ++i)
    {
        const float f = static_cast<float> (i) / static_cast<float> (kOverlapSamples);
        w[i] = f;
        w[kSegmentSamples - 1 - i] = f;
    }
    return w;
}

// Resample a stereo AudioBuffer to a new sample rate (Catmull-Rom).
juce::AudioBuffer<float> resampleBuffer (const juce::AudioBuffer<float>& in,
                                         double srcRate, double dstRate)
{
    if (std::abs (srcRate - dstRate) < 1.0)
        return juce::AudioBuffer<float> (in);

    const int inLen  = in.getNumSamples();
    const int outLen = juce::jmax (1, (int) std::llround ((double) inLen * dstRate / srcRate));
    const int chs    = juce::jmin (2, in.getNumChannels());

    juce::AudioBuffer<float> out (2, outLen);
    out.clear();

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = in.getReadPointer (juce::jmin (ch, chs - 1));
        float* dst = out.getWritePointer (ch);
        for (int i = 0; i < outLen; ++i)
        {
            const double pos = (double) i * (double) inLen / (double) outLen;
            const int p0 = (int) pos;
            const float frac = (float) (pos - p0);
            const float s0 = src[juce::jlimit (0, inLen - 1, p0 - 1)];
            const float s1 = src[juce::jlimit (0, inLen - 1, p0)];
            const float s2 = src[juce::jlimit (0, inLen - 1, p0 + 1)];
            const float s3 = src[juce::jlimit (0, inLen - 1, p0 + 2)];
            // Catmull-Rom interpolation
            const float f2 = frac * frac;
            const float f3 = f2 * frac;
            dst[i] = 0.5f * ((2.0f * s1) + (-s0 + s2) * frac
                     + (2.0f * s0 - 5.0f * s1 + 4.0f * s2 - s3) * f2
                     + (-s0 + 3.0f * s1 - 3.0f * s2 + s3) * f3);
        }
    }
    return out;
}
} // namespace

//==============================================================================
struct DemucsSeparator::OrtImpl
{
    // NOTE: Ort::Env and friends are created LAZILY in ensureSessionLoaded(),
    // never in the constructor. If ORT fails to initialize (missing dylib,
    // architecture mismatch, etc.), the exception is caught and reported as a
    // string instead of crashing the plugin at open time.
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::SessionOptions> sessionOptions;
    std::unique_ptr<Ort::Session> session;
    bool loaded = false;
    juce::String loadError;

    OrtImpl() = default;
};

DemucsSeparator::DemucsSeparator() : impl (std::make_unique<OrtImpl>()) {}
DemucsSeparator::~DemucsSeparator() = default;

juce::File DemucsSeparator::getModelFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio")
        .getChildFile ("QSplit")
        .getChildFile ("Models")
        .getChildFile ("htdemucs_fp16.onnx");
}

bool DemucsSeparator::isModelAvailable()
{
    const auto f = getModelFile();
    return f.existsAsFile() && f.getSize() > 100 * 1024 * 1024;
}

juce::String DemucsSeparator::getModelDownloadURL()
{
    // MIT-licensed htdemucs fp16 weights, hosted on Hugging Face (official source).
    return "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs_fp16weights.onnx";
}

bool DemucsSeparator::ensureSessionLoaded (juce::String& errorOut)
{
    if (impl->loaded)
        return true;

    if (! isModelAvailable())
    {
        errorOut = "AI model not downloaded yet.";
        return false;
    }

    try
    {
        // Lazy ORT initialization — everything that can throw lives here,
        // so a broken ONNX Runtime can never crash the plugin at open time.
        if (impl->env == nullptr)
        {
            impl->env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "QSplit");
            impl->sessionOptions = std::make_unique<Ort::SessionOptions>();
            // CRITICAL: the default graph optimizer hangs on this 13k-node graph
            // (verified 2026-10-15 — session creation never returns). Disable it.
            impl->sessionOptions->SetGraphOptimizationLevel (
                GraphOptimizationLevel::ORT_DISABLE_ALL);
            impl->sessionOptions->SetIntraOpNumThreads (
                juce::jmax (1, (int) juce::SystemStats::getNumCpus() / 2));
        }

        const auto path = getModelFile().getFullPathName();
       #if JUCE_WINDOWS
        impl->session = std::make_unique<Ort::Session> (
            *impl->env, path.toWideCharPointer(), *impl->sessionOptions);
       #else
        impl->session = std::make_unique<Ort::Session> (
            *impl->env, path.toRawUTF8(), *impl->sessionOptions);
       #endif
        impl->loaded = true;
        return true;
    }
    catch (const Ort::Exception& e)
    {
        errorOut = "ONNX Runtime: " + juce::String (e.what());
        impl->loadError = errorOut;
        return false;
    }
    catch (const std::exception& e)
    {
        errorOut = "AI engine failed to start: " + juce::String (e.what());
        impl->loadError = errorOut;
        return false;
    }
    catch (...)
    {
        errorOut = "AI engine failed to start (unknown error).";
        impl->loadError = errorOut;
        return false;
    }
}

bool DemucsSeparator::separate (const juce::AudioBuffer<float>& input,
                                double sampleRate,
                                std::array<juce::AudioBuffer<float>, 4>& stems,
                                std::function<void (float)> progressCallback,
                                std::atomic<bool>& cancelFlag)
{
    juce::String error;
    if (! ensureSessionLoaded (error))
    {
        progressCallback (-1.0f); // signals failure to the UI
        return false;
    }

    // 1. Resample to the model's native 44.1 kHz stereo.
    juce::AudioBuffer<float> mix44 = resampleBuffer (input, sampleRate, kModelSampleRate);
    const int totalLen = mix44.getNumSamples();
    if (totalLen <= 0)
        return false;

    const float* mixL = mix44.getReadPointer (0);
    const float* mixR = mix44.getReadPointer (1);

    // 2. Chunked overlap-add inference.
    const int nChunks = juce::jmax (1, (totalLen + kStrideSamples - 1) / kStrideSamples);
    const auto window = makeTransitionWindow();

    // Accumulators: 4 stems x stereo x totalLen, plus a shared weight track.
    std::vector<std::vector<float>> acc (4, std::vector<float> ((size_t) totalLen * 2, 0.0f));
    std::vector<float> weight ((size_t) totalLen, 0.0f);

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu (
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    const std::array<int64_t, 3> inShape { 1, kModelChannels, kSegmentSamples };

    std::vector<float> chunkIn ((size_t) kSegmentSamples * 2);

    for (int ci = 0; ci < nChunks; ++ci)
    {
        if (cancelFlag.load())
            return false;

        const int start = ci * kStrideSamples;
        const int end   = juce::jmin (start + kSegmentSamples, totalLen);
        const int chunkLen = end - start;

        // Fill chunk (zero-padded), planar L/R -> interleaved model layout [1,2,N].
        std::fill (chunkIn.begin(), chunkIn.end(), 0.0f);
        for (int i = 0; i < chunkLen; ++i)
        {
            chunkIn[(size_t) i] = mixL[start + i];
            chunkIn[(size_t) kSegmentSamples + i] = mixR[start + i];
        }

        Ort::Value inTensor = Ort::Value::CreateTensor<float> (
            memInfo, chunkIn.data(), chunkIn.size(), inShape.data(), inShape.size());

        const char* inNames[]  = { "mix" };
        const char* outNames[] = { "stems" };

        std::vector<Ort::Value> out;
        try
        {
            out = impl->session->Run (Ort::RunOptions { nullptr },
                                      inNames, &inTensor, 1, outNames, 1);
        }
        catch (const Ort::Exception& e)
        {
            juce::String err = "Inference failed: " + juce::String (e.what());
            (void) err;
            progressCallback (-1.0f);
            return false;
        }

        // Output: [1, 4, 2, 343980] float32.
        const float* stemData = out[0].GetTensorData<float>();

        for (int m = 0; m < 4; ++m)           // model stem row
        {
            const int ui = kModelToInterface[m]; // interface stem index
            const float* rowL = stemData + (size_t) m * 2 * kSegmentSamples;
            const float* rowR = rowL + kSegmentSamples;
            float* accL = acc[(size_t) ui].data();
            float* accR = accL + totalLen;

            for (int i = 0; i < chunkLen; ++i)
            {
                const float w = window[(size_t) i];
                accL[start + i] += rowL[i] * w;
                accR[start + i] += rowR[i] * w;
            }
        }
        for (int i = 0; i < chunkLen; ++i)
            weight[(size_t) start + i] += window[(size_t) i];

        progressCallback (0.05f + 0.9f * (float) (ci + 1) / (float) nChunks);
    }

    if (cancelFlag.load())
        return false;

    // 3. Normalize by accumulated weight (ports: out[s] /= max(weight, 1e-8)).
    const int outLen44 = totalLen;
    for (int s = 0; s < 4; ++s)
    {
        stems[s].setSize (2, outLen44, false, true, true);
        float* dL = stems[s].getWritePointer (0);
        float* dR = stems[s].getWritePointer (1);
        const float* aL = acc[(size_t) s].data();
        const float* aR = aL + totalLen;
        for (int i = 0; i < outLen44; ++i)
        {
            const float w = juce::jmax (weight[(size_t) i], 1e-8f);
            dL[i] = aL[i] / w;
            dR[i] = aR[i] / w;
        }
    }

    // 4. Resample back to the session sample rate if needed.
    if (std::abs (sampleRate - kModelSampleRate) >= 1.0)
    {
        for (int s = 0; s < 4; ++s)
            stems[s] = resampleBuffer (stems[s], kModelSampleRate, sampleRate);
    }

    progressCallback (1.0f);
    return ! cancelFlag.load();
}

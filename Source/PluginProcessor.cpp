#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// SpectralGate
SpectralGate::SpectralGate()
{
    inFifo[0].assign (fftSize, 0.0f);
    inFifo[1].assign (fftSize, 0.0f);
    outFifo[0].assign (fftSize, 0.0f);
    outFifo[1].assign (fftSize, 0.0f);
    fftData.assign (2 * fftSize, 0.0f);
    magnitudes.assign (numBins, 0.0f);
    mask[0].assign (numBins, 1.0f);
    mask[1].assign (numBins, 1.0f);

    hannTable.assign (fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        hannTable[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                * (float) i / (float) fftSize));
}

void SpectralGate::prepare (double /*sampleRate*/)
{
    reset();
    prepared = true;
}

void SpectralGate::setAmount (float amount01)
{
    amount = juce::jlimit (0.0f, 1.0f, amount01);
}

void SpectralGate::reset()
{
    std::fill (inFifo[0].begin(), inFifo[0].end(), 0.0f);
    std::fill (inFifo[1].begin(), inFifo[1].end(), 0.0f);
    std::fill (outFifo[0].begin(), outFifo[0].end(), 0.0f);
    std::fill (outFifo[1].begin(), outFifo[1].end(), 0.0f);
    std::fill (mask[0].begin(), mask[0].end(), 1.0f);
    std::fill (mask[1].begin(), mask[1].end(), 1.0f);
    fifoPos[0] = fifoPos[1] = 0;
    samplesSinceFrame = 0;
}

void SpectralGate::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || ! prepared)
        return;

    if (amount <= 0.001f)
        return; // bypass: buffer already holds dry signal (in-place)

    const int numCh = juce::jmin (2, buffer.getNumChannels());

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const int wp = fifoPos[ch];
            inFifo[ch][wp] = buffer.getSample (ch, n);

            float y = outFifo[ch][wp];
            outFifo[ch][wp] = 0.0f;

            fifoPos[ch] = (wp + 1) % fftSize;
            buffer.setSample (ch, n, y);
        }

        if (++samplesSinceFrame >= hopSize)
        {
            samplesSinceFrame = 0;
            for (int ch = 0; ch < numCh; ++ch)
                processFrame (ch);
        }
    }
}

void SpectralGate::processFrame (int channel)
{
    // Gather the oldest fftSize samples, apply analysis window.
    const int rp = fifoPos[channel];
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = inFifo[channel][(rp + i) % fftSize] * hannTable[i];

    fft.performRealOnlyForwardTransform (fftData.data());

    float peak = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        const float re = fftData[2 * k];
        const float im = fftData[2 * k + 1];
        const float mag = std::sqrt (re * re + im * im);
        magnitudes[k] = mag;
        if (mag > peak)
            peak = mag;
    }

    const float thresh = amount * amount * peak * 0.35f;
    auto& m = mask[channel];

    for (int k = 0; k < numBins; ++k)
    {
        const float mag = magnitudes[k];
        const float target = (mag >= thresh) ? 1.0f
                                             : std::pow (mag / (thresh + 1e-6f), 1.5f);
        const float coeff = (target > m[k]) ? 0.5f : 0.08f; // fast attack, slow release
        m[k] += coeff * (target - m[k]);

        fftData[2 * k]     *= m[k];
        fftData[2 * k + 1] *= m[k];
    }

    fft.performRealOnlyInverseTransform (fftData.data());

    // Synthesis window + 1/(N * 1.5) (Hann^2 sums to 1.5 at 75% overlap),
    // overlap-added into the output FIFO at the frame start.
    constexpr float scale = 1.0f / (float) (fftSize * 1.5);
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (rp + i) % fftSize;
        outFifo[channel][idx] += fftData[i] * hannTable[i] * scale;
    }
}

//==============================================================================
// QSplitProcessor
QSplitProcessor::QSplitProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QSplitParams", createParameterLayout()),
      separator (std::make_unique<StubSeparator>())
{
}

QSplitProcessor::~QSplitProcessor()
{
    cancelFlag.store (true);
    threadPool.removeAllJobs (true, 5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout QSplitProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        kCleanup, "Cleanup", 0.0f, 100.0f, 20.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        kMaster, "Master", 0.0f, 2.0f, 1.0f));

    for (int i = 0; i < 4; ++i)
    {
        const juce::String stemName = StemSeparator::getStemName (i);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            levelParamId (i), stemName + " Level", 0.0f, 1.5f, 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            muteParamId (i), stemName + " Mute", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            soloParamId (i), stemName + " Solo", false));
    }

    return { params.begin(), params.end() };
}

void QSplitProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    for (auto& g : cleanupGates)
        g.prepare (sampleRate);
    lastCleanupAmount = -1.0f;
    gatesNeedReset.store (true);
}

void QSplitProcessor::releaseResources() {}

bool QSplitProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::disabled())
        return false;
    return true;
   #endif
}

void QSplitProcessor::resetGates()
{
    gatesNeedReset.store (true);
}

void QSplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    buffer.clear();

    if (gatesNeedReset.exchange (false))
        for (auto& g : cleanupGates)
            g.reset();

    if (sepState.load() != SepState::Ready || ! playing.load() || numSamples == 0)
        return;

    if (! stemLock.tryEnter())
        return; // stems are being swapped; skip this block rather than blocking

    const int songLen = songBuffer.getNumSamples();
    if (songLen <= 0)
    {
        stemLock.exit();
        return;
    }

    double pos = playheadSamples.load();
    const double rateRatio = songSampleRate / currentSampleRate;

    const float cleanup = apvts.getRawParameterValue (kCleanup)->load();
    const float master  = apvts.getRawParameterValue (kMaster)->load();
    const float amt01 = juce::jlimit (0.0f, 1.0f, cleanup / 100.0f);
    if (std::abs (amt01 - lastCleanupAmount) > 0.0005f)
    {
        for (auto& g : cleanupGates)
            g.setAmount (amt01);
        lastCleanupAmount = amt01;
    }

    bool anySolo = false;
    for (int s = 0; s < 4; ++s)
        if (apvts.getRawParameterValue (soloParamId (s))->load() > 0.5f)
        {
            anySolo = true;
            break;
        }

    float levels[4];
    bool active[4];
    for (int s = 0; s < 4; ++s)
    {
        const bool solo = apvts.getRawParameterValue (soloParamId (s))->load() > 0.5f;
        const bool mute = apvts.getRawParameterValue (muteParamId (s))->load() > 0.5f;
        levels[s] = apvts.getRawParameterValue (levelParamId (s))->load();
        active[s] = anySolo ? solo : ! mute;
    }

    if (mixTemp.getNumChannels() < numCh || mixTemp.getNumSamples() < numSamples)
        mixTemp.setSize (numCh, numSamples, false, false, true);

    for (int s = 0; s < 4; ++s)
    {
        if (! active[s] || levels[s] <= 0.0001f)
            continue;
        if (stems[s].getNumSamples() < songLen)
            continue;

        mixTemp.clear();
        const float lvl = levels[s];

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = stems[s].getReadPointer (ch);
            float* dst = mixTemp.getWritePointer (ch);
            double p = pos;

            for (int i = 0; i < numSamples; ++i)
            {
                const int idx = (int) p;
                const float frac = (float) (p - idx);
                const float a = (idx >= 0 && idx < songLen) ? src[idx] : 0.0f;
                const float b = (idx + 1 >= 0 && idx + 1 < songLen) ? src[idx + 1] : 0.0f;
                dst[i] = (a + (b - a) * frac) * lvl;
                p += rateRatio;
            }
        }

        cleanupGates[s].process (mixTemp);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.addFrom (ch, 0, mixTemp, ch, 0, numSamples);
    }

    buffer.applyGain (master);

    pos += numSamples * rateRatio;
    bool finished = false;
    if (pos >= songLen)
    {
        pos = 0.0;
        playing.store (false);
        finished = true;
    }
    playheadSamples.store (pos);
    stemLock.exit();

    if (finished && onTransportChanged)
    {
        auto cb = onTransportChanged;
        juce::MessageManager::callAsync ([cb] { cb(); });
    }
}

//==============================================================================
// Song loading / separation
bool QSplitProcessor::loadSongFile (const juce::File& file)
{
    stopTransport();
    cancelSeparation();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return false;

    // Sanity cap: 20 minutes at the file's rate.
    const int maxSamples = (int) (20.0 * 60.0 * reader->sampleRate);
    const int numSamples = juce::jmin ((int) reader->lengthInSamples, maxSamples);
    if (numSamples <= 0)
        return false;

    juce::AudioBuffer<float> buf;
    buf.setSize (2, numSamples, false, true, true);

    if (reader->numChannels > 1)
        reader->read (&buf, 0, numSamples, 0, true, true);
    else
    {
        reader->read (&buf, 0, numSamples, 0, true, false);
        buf.copyFrom (1, 0, buf, 0, 0, numSamples);
    }

    {
        const juce::ScopedLock sl (stemLock);
        songBuffer = std::move (buf);
        for (auto& s : stems)
            s.setSize (0, 0);
    }

    songSampleRate = reader->sampleRate;
    songName = file.getFileNameWithoutExtension();
    playheadSamples.store (0.0);
    sepProgress.store (0.0f);
    sepState.store (SepState::SongLoaded);
    return true;
}

void QSplitProcessor::startSeparation()
{
    if (sepState.load() == SepState::Separating || ! hasSong())
        return;

    stopTransport();
    cancelFlag.store (false);
    sepProgress.store (0.0f);
    sepState.store (SepState::Separating);
    threadPool.addJob (new SeparationJob (*this), true);
}

void QSplitProcessor::cancelSeparation()
{
    if (sepState.load() == SepState::Separating)
    {
        cancelFlag.store (true);
        threadPool.removeAllJobs (true, 15000);
    }
}

QSplitProcessor::SeparationJob::SeparationJob (QSplitProcessor& o)
    : ThreadPoolJob ("QSplit separation"), owner (o)
{
}

juce::ThreadPoolJob::JobStatus QSplitProcessor::SeparationJob::runJob()
{
    juce::AudioBuffer<float> songCopy;
    {
        const juce::ScopedLock sl (owner.stemLock);
        songCopy.makeCopyOf (owner.songBuffer);
    }

    const double sr = owner.songSampleRate;
    std::atomic<bool>& cancel = owner.cancelFlag;

    std::array<juce::AudioBuffer<float>, 4> localStems;
    const bool ok = owner.separator->separate (
        songCopy, sr, localStems,
        [this] (float p) { owner.sepProgress.store (p); },
        cancel);

    if (ok && ! cancel.load())
    {
        const juce::ScopedLock sl (owner.stemLock);
        owner.stems = std::move (localStems);
        owner.sepState.store (SepState::Ready);
        owner.sepProgress.store (1.0f);
    }
    else if (cancel.load())
    {
        owner.sepState.store (SepState::SongLoaded);
        owner.sepProgress.store (0.0f);
    }
    else
    {
        owner.sepState.store (SepState::Failed);
    }

    juce::MessageManager::callAsync ([this]
    {
        if (owner.onSeparationFinished)
            owner.onSeparationFinished();
    });

    return jobHasFinished;
}

//==============================================================================
// Transport
void QSplitProcessor::play()
{
    if (sepState.load() != SepState::Ready)
        return;

    resetGates();
    playing.store (true);
    if (onTransportChanged)
    {
        auto cb = onTransportChanged;
        juce::MessageManager::callAsync ([cb] { cb(); });
    }
}

void QSplitProcessor::pause()
{
    playing.store (false);
    if (onTransportChanged)
    {
        auto cb = onTransportChanged;
        juce::MessageManager::callAsync ([cb] { cb(); });
    }
}

void QSplitProcessor::togglePlay()
{
    if (isPlaying())
        pause();
    else
        play();
}

void QSplitProcessor::stopTransport()
{
    playing.store (false);
    playheadSamples.store (0.0);
    resetGates();
    if (onTransportChanged)
    {
        auto cb = onTransportChanged;
        juce::MessageManager::callAsync ([cb] { cb(); });
    }
}

double QSplitProcessor::getPlayheadSeconds() const
{
    return songSampleRate > 0.0 ? playheadSamples.load() / songSampleRate : 0.0;
}

void QSplitProcessor::setPlayheadSeconds (double seconds)
{
    const double len = getSongLengthSeconds();
    playheadSamples.store (juce::jlimit (0.0, len, seconds) * songSampleRate);
    resetGates();
}

double QSplitProcessor::getSongLengthSeconds() const
{
    return songSampleRate > 0.0 ? (double) songBuffer.getNumSamples() / songSampleRate : 0.0;
}

//==============================================================================
// Export — stem with the current Cleanup gate applied (WYSIWYG vs audition)
bool QSplitProcessor::renderStemToWav (int stemIndex, const juce::File& destFile)
{
    if (sepState.load() != SepState::Ready || stemIndex < 0 || stemIndex > 3)
        return false;

    juce::AudioBuffer<float> render;
    {
        const juce::ScopedLock sl (stemLock);
        render.makeCopyOf (stems[stemIndex]);
    }

    if (render.getNumSamples() == 0)
        return false;

    const float cleanup = apvts.getRawParameterValue (kCleanup)->load();
    const float amt01 = juce::jlimit (0.0f, 1.0f, cleanup / 100.0f);

    if (amt01 > 0.001f)
    {
        SpectralGate gate;
        gate.prepare (songSampleRate);
        gate.setAmount (amt01);

        // Latency compensation: pad, process, drop the delayed head.
        const int lat = SpectralGate::getLatencySamples();
        juce::AudioBuffer<float> padded (2, render.getNumSamples() + lat);
        padded.clear();
        for (int ch = 0; ch < 2; ++ch)
            padded.copyFrom (ch, lat, render, ch, 0, render.getNumSamples());

        gate.process (padded);

        for (int ch = 0; ch < 2; ++ch)
            render.copyFrom (ch, 0, padded, ch, lat, render.getNumSamples());
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream (destFile.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFormat.createWriterFor (stream.get(), songSampleRate, 2, 16, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release(); // writer owns the stream now
    writer->writeFromAudioSampleBuffer (render, 0, render.getNumSamples());
    return true;
}

//==============================================================================
// Boilerplate
juce::AudioProcessorEditor* QSplitProcessor::createEditor()
{
    return new QSplitEditor (*this);
}

bool QSplitProcessor::hasEditor() const { return true; }
const juce::String QSplitProcessor::getName() const { return JucePlugin_Name; }
bool QSplitProcessor::acceptsMidi() const { return false; }
bool QSplitProcessor::producesMidi() const { return false; }
bool QSplitProcessor::isMidiEffect() const { return false; }
double QSplitProcessor::getTailLengthSeconds() const { return 0.0; }
int QSplitProcessor::getNumPrograms() { return 1; }
int QSplitProcessor::getCurrentProgram() { return 0; }
void QSplitProcessor::setCurrentProgram (int) {}
const juce::String QSplitProcessor::getProgramName (int) { return {}; }
void QSplitProcessor::changeProgramName (int, const juce::String&) {}

void QSplitProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void QSplitProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QSplitProcessor();
}

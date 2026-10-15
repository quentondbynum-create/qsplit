#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

//==============================================================================
bool isAudioFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".mp3" || ext == ".aif" || ext == ".aiff"
        || ext == ".flac" || ext == ".ogg";
}

DropForwarder::DropForwarder (Handler h) : handler (std::move (h)) {}

bool DropForwarder::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (juce::File (f)))
            return true;
    return false;
}

void DropForwarder::filesDropped (const juce::StringArray& files, int, int)
{
    if (handler)
        handler (files);
}

//==============================================================================
DropZone::DropZone (DropForwarder::Handler dropHandler, std::function<void()> browseHandler)
    : DropForwarder (std::move (dropHandler))
{
    mainLabel.setText ("DROP A SONG HERE", juce::dontSendNotification);
    mainLabel.setFont (juce::Font (26.0f, juce::Font::bold));
    mainLabel.setColour (juce::Label::textColourId, QSplitUI::text);
    mainLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (mainLabel);

    subLabel.setText ("wav · mp3 · aif · flac · ogg", juce::dontSendNotification);
    subLabel.setFont (juce::Font (13.0f));
    subLabel.setColour (juce::Label::textColourId, QSplitUI::dim);
    subLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (subLabel);

    browseButton.onClick = std::move (browseHandler);
    addAndMakeVisible (browseButton);
}

void DropZone::setSongInfo (const juce::String& name, double seconds, bool has)
{
    hasSong = has;
    if (has)
    {
        mainLabel.setText (name, juce::dontSendNotification);
        const int mins = (int) (seconds / 60.0);
        const int secs = (int) std::round (seconds - mins * 60);
        subLabel.setText ("Length " + juce::String (mins) + ":"
                          + juce::String::formatted ("%02d", secs)
                          + "  ·  drop another song to replace",
                          juce::dontSendNotification);
    }
    else
    {
        mainLabel.setText ("DROP A SONG HERE", juce::dontSendNotification);
        subLabel.setText ("wav · mp3 · aif · flac · ogg", juce::dontSendNotification);
    }
    repaint();
}

void DropZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (QSplitUI::panel);
    g.fillRoundedRectangle (r, 10.0f);

    g.setColour ((hasSong ? QSplitUI::accent : QSplitUI::dim).withAlpha (0.7f));
    juce::Path p;
    p.addRoundedRectangle (r.reduced (1.0f), 9.0f);
    float dashLengths[2] = { 9.0f, 7.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashLengths, 2);
    g.strokePath (dashed, juce::PathStrokeType (1.5f));
}

void DropZone::resized()
{
    auto a = getLocalBounds().reduced (10);
    browseButton.setBounds (a.removeFromRight (110).reduced (0, 30));
    mainLabel.setBounds (a.removeFromTop (a.getHeight() / 2));
    subLabel.setBounds (a);
}

//==============================================================================
WaveformView::WaveformView (DropForwarder::Handler dropHandler)
    : DropForwarder (std::move (dropHandler))
{
}

void WaveformView::setPeaks (std::vector<float> newPeaks)
{
    peaks = std::move (newPeaks);
    repaint();
}

void WaveformView::setPlayhead01 (double p)
{
    if (std::abs (p - playhead01) > 0.0005)
    {
        playhead01 = p;
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (QSplitUI::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);

    if (! peaks.empty())
    {
        g.setColour (QSplitUI::accent.withAlpha (0.8f));
        const int w = r.getWidth();
        const int h = r.getHeight();
        const int n = (int) peaks.size();

        for (int x = 0; x < w; ++x)
        {
            const int idx = juce::jmin (n - 1, (x * n) / juce::jmax (1, w));
            const float pk = juce::jlimit (0.0f, 1.0f, peaks[(size_t) idx]);
            const int barH = juce::jmax (2, (int) (pk * (h - 14)));
            const int y = (h - barH) / 2;
            g.fillRect (x + 5, 5 + y, 1, barH);
        }
    }
    else
    {
        g.setColour (QSplitUI::dim);
        g.setFont (14.0f);
        g.drawText ("waveform appears after you drop a song",
                    r, juce::Justification::centred, true);
    }

    const int px = 5 + (int) (playhead01 * (r.getWidth() - 10));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine ((float) px, 5.0f, (float) px, (float) (r.getHeight() - 5), 2.0f);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (onSeek)
    {
        const double f = (double) (e.x - 5) / (double) juce::jmax (1, getWidth() - 10);
        onSeek (juce::jlimit (0.0, 1.0, f));
    }
}

//==============================================================================
void DragLabel::mouseDown (const juce::MouseEvent&)
{
    if (onDragStart)
        onDragStart();
}

//==============================================================================
StemStrip::StemStrip (QSplitProcessor& p, int stemIndex, DropForwarder::Handler dropHandler)
    : DropForwarder (std::move (dropHandler)),
      processor (p),
      index (stemIndex),
      colour (StemSeparator::getStemColour (stemIndex))
{
    nameLabel.setText (StemSeparator::getStemName (stemIndex), juce::dontSendNotification);
    nameLabel.setFont (juce::Font (17.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, colour);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setTooltip ("Drag me into your DAW to drop the bounced stem WAV");
    nameLabel.onDragStart = [this] { if (onDragOut) onDragOut (index); };
    addAndMakeVisible (nameLabel);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.darker (0.2f));
    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::green.darker (0.2f));
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    bounceButton.onClick = [this] { if (onBounce) onBounce (index); };
    addAndMakeVisible (bounceButton);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    levelSlider.setColour (juce::Slider::thumbColourId, colour);
    addAndMakeVisible (levelSlider);

    auto& apvts = processor.apvts;
    levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, QSplitProcessor::levelParamId (index), levelSlider);
    muteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, QSplitProcessor::muteParamId (index), muteButton);
    soloAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, QSplitProcessor::soloParamId (index), soloButton);

    setStemsReady (false);
}

void StemStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (QSplitUI::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 4.0f, 2.0f);

    g.setColour (QSplitUI::dim);
    g.setFont (10.0f);
    g.drawText ("drag name into DAW", r.removeFromBottom (16.0f),
                juce::Justification::centred, true);
}

void StemStrip::resized()
{
    auto a = getLocalBounds().reduced (10);
    a.removeFromTop (4);
    nameLabel.setBounds (a.removeFromTop (26));
    a.removeFromTop (4);

    auto row = a.removeFromTop (28);
    const int bw = (row.getWidth() - 8) / 2;
    muteButton.setBounds (row.removeFromLeft (bw));
    row.removeFromLeft (8);
    soloButton.setBounds (row);

    a.removeFromTop (8);
    levelSlider.setBounds (a.removeFromTop (28));
    a.removeFromTop (8);
    bounceButton.setBounds (a.removeFromTop (28));
}

void StemStrip::setStemsReady (bool ready)
{
    muteButton.setEnabled (ready);
    soloButton.setEnabled (ready);
    levelSlider.setEnabled (ready);
    bounceButton.setEnabled (ready);
}

//==============================================================================
TransportBar::TransportBar (DropForwarder::Handler dropHandler)
    : DropForwarder (std::move (dropHandler))
{
    playButton.onClick = [this] { if (onPlayToggle) onPlayToggle(); };
    stopButton.onClick = [this] { if (onStop) onStop(); };
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);

    timeLabel.setFont (juce::Font (17.0f, juce::Font::bold));
    timeLabel.setColour (juce::Label::textColourId, QSplitUI::text);
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
    addAndMakeVisible (timeLabel);

    setTransportEnabled (false);
}

void TransportBar::resized()
{
    auto a = getLocalBounds();
    playButton.setBounds (a.removeFromLeft (96).reduced (4));
    stopButton.setBounds (a.removeFromLeft (96).reduced (4));
    timeLabel.setBounds (a.reduced (4));
}

void TransportBar::setPlaying (bool p)
{
    if (p != playing)
    {
        playing = p;
        playButton.setButtonText (p ? "Pause" : "Play");
    }
}

void TransportBar::setTimeText (const juce::String& t)
{
    timeLabel.setText (t, juce::dontSendNotification);
}

void TransportBar::setTransportEnabled (bool e)
{
    playButton.setEnabled (e);
    stopButton.setEnabled (e);
}

//==============================================================================
QSplitEditor::QSplitEditor (QSplitProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      dropZone ([this] (const juce::StringArray& f) { handleDroppedFiles (f); },
                [this] { openFileBrowser(); }),
      waveformView ([this] (const juce::StringArray& f) { handleDroppedFiles (f); }),
      transportBar ([this] (const juce::StringArray& f) { handleDroppedFiles (f); })
{
    setSize (1000, 700);

    // Headroom Audio brand mark — small and subtle in the top-left corner
    logoImage.setImage (juce::ImageCache::getFromMemory (BinaryData::headroomlogo_png,
                                                        BinaryData::headroomlogo_pngSize));
    addAndMakeVisible (logoImage);

    titleLabel.setText ("QSPLIT", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (30.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    // ---- preset menu (standard for every Headroom Audio plugin) ----
    presetBox.setTooltip ("Preset");
    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        const int pid = presetBox.getSelectedId();
        const auto it = comboToPreset.find (pid);
        if (it != comboToPreset.end())
            applyPreset (it->second);
    };

    heartButton.setTooltip ("Favorite this preset");
    heartButton.onClick = [this] { toggleFavorite(); };
    addAndMakeVisible (heartButton);

    prevButton.onClick = [this] { cyclePreset (-1); };
    nextButton.onClick = [this] { cyclePreset (1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    saveButton.onClick = [this] { promptSavePreset(); };
    addAndMakeVisible (saveButton);

    // ---- main UI ----
    addAndMakeVisible (dropZone);

    progressBar.setVisible (false);
    addAndMakeVisible (progressBar);
    cancelButton.setVisible (false);
    cancelButton.onClick = [this] { processor.cancelSeparation(); };
    addAndMakeVisible (cancelButton);

    addAndMakeVisible (waveformView);
    waveformView.onSeek = [this] (double f)
    {
        processor.setPlayheadSeconds (f * processor.getSongLengthSeconds());
    };

    addAndMakeVisible (transportBar);
    transportBar.onPlayToggle = [this] { processor.togglePlay(); };
    transportBar.onStop = [this] { processor.stopTransport(); };

    for (int i = 0; i < 4; ++i)
    {
        stemStrips[i] = std::make_unique<StemStrip> (
            processor, i,
            [this] (const juce::StringArray& f) { handleDroppedFiles (f); });
        stemStrips[i]->onBounce = [this] (int s) { bounceStemToFile (s); };
        stemStrips[i]->onDragOut = [this] (int s) { dragStemOut (s); };
        addAndMakeVisible (stemStrips[i].get());
    }

    cleanupSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    cleanupSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    cleanupSlider.setColour (juce::Slider::rotarySliderFillColourId, QSplitUI::accent2);
    cleanupSlider.setTooltip ("Spectral-gate cleanup: scrubs bleed and artifacts from the stems");
    addAndMakeVisible (cleanupSlider);

    masterSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible (masterSlider);

    cleanupLabel.setText ("Cleanup", juce::dontSendNotification);
    cleanupLabel.setJustificationType (juce::Justification::centred);
    cleanupLabel.setColour (juce::Label::textColourId, QSplitUI::text);
    addAndMakeVisible (cleanupLabel);

    masterLabel.setText ("Master", juce::dontSendNotification);
    masterLabel.setJustificationType (juce::Justification::centred);
    masterLabel.setColour (juce::Label::textColourId, QSplitUI::text);
    addAndMakeVisible (masterLabel);

    statusLabel.setText ("Drop a song to begin.", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, QSplitUI::dim);
    addAndMakeVisible (statusLabel);

    cleanupAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, QSplitProcessor::kCleanup, cleanupSlider);
    masterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, QSplitProcessor::kMaster, masterSlider);

    processor.onSeparationFinished = [this] { triggerAsyncUpdate(); };
    processor.onTransportChanged = [this] { triggerAsyncUpdate(); };

    buildPresetList();
    refreshPresetCombo();
    refreshSongUI();
    updateTransportUI();

    startTimerHz (30);
}

QSplitEditor::~QSplitEditor()
{
    processor.onSeparationFinished = nullptr;
    processor.onTransportChanged = nullptr;
}

void QSplitEditor::paint (juce::Graphics& g)
{
    g.fillAll (QSplitUI::bg);

    // subtle accent line under the top bar
    g.setGradientFill (juce::ColourGradient (QSplitUI::accent, 0.0f, 0.0f,
                                             QSplitUI::accent2, (float) getWidth(), 0.0f, false));
    g.fillRect (12, 56, getWidth() - 24, 2);
}

void QSplitEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (44);
    logoImage.setBounds (top.removeFromLeft (38).reduced (3));
    top.removeFromLeft (2);
    titleLabel.setBounds (top.removeFromLeft (150));

    saveButton.setBounds (top.removeFromRight (70).reduced (2));
    top.removeFromRight (6);
    heartButton.setBounds (top.removeFromRight (40).reduced (2));
    top.removeFromRight (6);
    nextButton.setBounds (top.removeFromRight (36).reduced (2));
    top.removeFromRight (4);
    prevButton.setBounds (top.removeFromRight (36).reduced (2));
    top.removeFromRight (8);
    top.removeFromLeft (20);
    presetBox.setBounds (top.removeFromLeft (280).reduced (2));

    area.removeFromTop (10);
    dropZone.setBounds (area.removeFromTop (102));
    area.removeFromTop (8);

    if (processor.getSeparationState() == QSplitProcessor::SepState::Separating)
    {
        auto pr = area.removeFromTop (26);
        cancelButton.setBounds (pr.removeFromRight (100).reduced (2));
        progressBar.setBounds (pr.reduced (2));
        area.removeFromTop (6);
    }

    waveformView.setBounds (area.removeFromTop (94));
    area.removeFromTop (8);
    transportBar.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    auto stems = area.removeFromTop (186);
    const int w = stems.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        stemStrips[i]->setBounds (stems.removeFromLeft (w).reduced (4, 0));

    area.removeFromTop (8);

    auto cleanBlock = area.removeFromLeft (120);
    cleanupSlider.setBounds (cleanBlock.removeFromTop (96));
    cleanupLabel.setBounds (cleanBlock.removeFromTop (22));

    area.removeFromLeft (12);

    auto masterBlock = area.removeFromLeft (120);
    masterSlider.setBounds (masterBlock.removeFromTop (96));
    masterLabel.setBounds (masterBlock.removeFromTop (22));

    area.removeFromLeft (12);
    statusLabel.setBounds (area.removeFromTop (40));
}

void QSplitEditor::timerCallback()
{
    const auto state = processor.getSeparationState();
    const bool separating = (state == QSplitProcessor::SepState::Separating);

    if (progressBar.isVisible() != separating)
    {
        progressBar.setVisible (separating);
        cancelButton.setVisible (separating);
        resized();
    }
    if (separating)
        progressValue = processor.getSeparationProgress();

    const double len = processor.getSongLengthSeconds();
    const double pos = processor.getPlayheadSeconds();
    waveformView.setPlayhead01 (len > 0.0 ? pos / len : 0.0);
    transportBar.setTimeText (formatTime (pos) + "  /  " + formatTime (len));
    transportBar.setPlaying (processor.isPlaying());
}

void QSplitEditor::handleAsyncUpdate()
{
    refreshAfterSeparation();
    updateTransportUI();
}

//==============================================================================
bool QSplitEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (juce::File (f)))
            return true;
    return false;
}

void QSplitEditor::filesDropped (const juce::StringArray& files, int, int)
{
    handleDroppedFiles (files);
}

void QSplitEditor::handleDroppedFiles (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (isAudioFile (file) && file.existsAsFile())
        {
            if (processor.loadSongFile (file))
            {
                refreshSongUI();
                if (! DemucsSeparator::isModelAvailable())
                    statusLabel.setText ("Downloading AI model (158 MB, one-time)…",
                                         juce::dontSendNotification);
                else
                    statusLabel.setText ("Separating…", juce::dontSendNotification);
                processor.startSeparation();
                resized();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Could not load",
                    "That file couldn't be read as audio.");
            }
            return;
        }
    }
}

void QSplitEditor::openFileBrowser()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Load a song", juce::File(), "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& c)
                          {
                              const auto r = c.getResults();
                              if (r.size() > 0)
                                  handleDroppedFiles (juce::StringArray (r[0].getFullPathName()));
                          });
}

void QSplitEditor::refreshSongUI()
{
    const bool has = processor.hasSong();
    dropZone.setSongInfo (processor.getSongName(),
                          processor.getSongLengthSeconds(), has);
    computePeaks();
    updateTransportUI();

    const bool ready = processor.getSeparationState() == QSplitProcessor::SepState::Ready;
    for (auto& s : stemStrips)
        s->setStemsReady (ready);
    transportBar.setTransportEnabled (ready);
}

void QSplitEditor::refreshAfterSeparation()
{
    const auto state = processor.getSeparationState();
    const bool ready = (state == QSplitProcessor::SepState::Ready);

    for (auto& s : stemStrips)
        s->setStemsReady (ready);
    transportBar.setTransportEnabled (ready);
    resized(); // hides the progress strip

    if (state == QSplitProcessor::SepState::Failed)
    {
        const float p = processor.getSeparationProgress();
        if (p == -2.0f)
            statusLabel.setText ("AI model download failed — check your connection and try again.",
                                 juce::dontSendNotification);
        else
            statusLabel.setText ("Separation failed.", juce::dontSendNotification);
    }
    else if (ready)
        statusLabel.setText ("Stems ready — press Play to audition before bouncing.",
                             juce::dontSendNotification);
}

void QSplitEditor::updateTransportUI()
{
    transportBar.setPlaying (processor.isPlaying());
}

void QSplitEditor::computePeaks()
{
    const auto& buf = processor.getSongBuffer();
    const int numSamples = buf.getNumSamples();

    std::vector<float> peaks;
    if (numSamples > 0)
    {
        constexpr int numCols = 1200;
        peaks.assign ((size_t) numCols, 0.0f);
        const float* d0 = buf.getReadPointer (0);
        const float* d1 = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : d0;

        for (int c = 0; c < numCols; ++c)
        {
            const int start = (c * numSamples) / numCols;
            const int end = ((c + 1) * numSamples) / numCols;
            float pk = 0.0f;
            for (int i = start; i < end; i += 7)
            {
                const float v = std::abs (d0[i]);
                if (v > pk) pk = v;
                const float v2 = std::abs (d1[i]);
                if (v2 > pk) pk = v2;
            }
            peaks[(size_t) c] = pk;
        }
    }
    waveformView.setPeaks (std::move (peaks));
}

juce::String QSplitEditor::formatTime (double seconds)
{
    const int s = (int) std::round (seconds);
    return juce::String (s / 60) + ":" + juce::String::formatted ("%02d", s % 60);
}

//==============================================================================
void QSplitEditor::bounceStemToFile (int stemIndex)
{
    if (processor.getSeparationState() != QSplitProcessor::SepState::Ready)
        return;

    auto chooser = std::make_shared<juce::FileChooser> (
        "Bounce " + StemSeparator::getStemName (stemIndex),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (StemSeparator::getStemName (stemIndex) + ".wav"),
        "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser, stemIndex] (const juce::FileChooser& c)
                          {
                              const auto results = c.getResults();
                              if (results.size() > 0)
                              {
                                  juce::File f = results[0];
                                  if (f.getFileExtension().isEmpty())
                                      f = f.withFileExtension ("wav");

                                  if (processor.renderStemToWav (stemIndex, f))
                                      statusLabel.setText ("Bounced: " + f.getFileName(),
                                                           juce::dontSendNotification);
                                  else
                                      juce::AlertWindow::showMessageBoxAsync (
                                          juce::AlertWindow::WarningIcon,
                                          "Bounce failed",
                                          "Could not write the stem WAV file.");
                              }
                          });
}

void QSplitEditor::dragStemOut (int stemIndex)
{
    if (processor.getSeparationState() != QSplitProcessor::SepState::Ready)
        return;

    juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("QSplit_" + StemSeparator::getStemName (stemIndex)
                                        + ".wav");
    if (! processor.renderStemToWav (stemIndex, tmp))
        return;

    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        juce::StringArray (tmp.getFullPathName()), false, this);
}

//==============================================================================
// Presets
juce::File QSplitEditor::getPresetDir() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio/QSplit/Presets");
}

juce::File QSplitEditor::getFavoritesFile() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio/QSplit/favorites.txt");
}

juce::ValueTree QSplitEditor::presetStateWith (
    std::initializer_list<std::pair<const char*, float>> values)
{
    auto state = processor.apvts.copyState();

    for (auto child : state)
    {
        if (! child.hasType ("PARAM"))
            continue;

        const juce::String pid = child.getProperty ("id").toString();
        if (auto* param = processor.apvts.getParameter (pid))
        {
            float norm = param->getDefaultValue();
            for (auto& kv : values)
                if (pid == kv.first)
                {
                    norm = param->convertTo0to1 (kv.second);
                    break;
                }
            child.setProperty ("value", norm, nullptr);
        }
    }
    return state;
}

void QSplitEditor::buildPresetList()
{
    presets.clear();
    using P = std::pair<const char*, float>;

    presets.push_back ({ "Init", presetStateWith ({}), true });
    presets.push_back ({ "Vocal Focus",
        presetStateWith ({ P { "cleanup", 30.0f }, P { "level0", 1.25f },
                           P { "level1", 0.6f },  P { "level2", 0.7f },
                           P { "level3", 0.5f } }), true });
    presets.push_back ({ "Drum Focus",
        presetStateWith ({ P { "cleanup", 30.0f }, P { "level1", 1.25f },
                           P { "level0", 0.6f },  P { "level2", 0.8f },
                           P { "level3", 0.5f } }), true });
    presets.push_back ({ "Maximum Cleanup",
        presetStateWith ({ P { "cleanup", 85.0f } }), true });

    const juce::File dir = getPresetDir();
    if (dir.isDirectory())
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.qsplitpreset"))
            if (auto xml = juce::XmlDocument::parse (f))
                presets.push_back ({ f.getFileNameWithoutExtension(),
                                      juce::ValueTree::fromXml (*xml), false });

    loadFavorites();
}

void QSplitEditor::refreshPresetCombo()
{
    presetBox.clear (juce::dontSendNotification);
    comboToPreset.clear();

    int itemId = 1;
    std::vector<int> favIdx;
    for (int i = 0; i < (int) presets.size(); ++i)
        if (favorites.contains (presets[i].name))
            favIdx.push_back (i);

    if (! favIdx.empty())
    {
        for (int i : favIdx)
        {
            presetBox.addItem ("♥ " + presets[(size_t) i].name, itemId);
            comboToPreset[itemId] = i;
            ++itemId;
        }
        presetBox.addSeparator();
    }

    bool addedSep = false;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (std::find (favIdx.begin(), favIdx.end(), i) != favIdx.end())
            continue;
        if (! presets[(size_t) i].isFactory && ! addedSep)
        {
            presetBox.addSeparator();
            addedSep = true;
        }
        presetBox.addItem (presets[(size_t) i].name, itemId);
        comboToPreset[itemId] = i;
        ++itemId;
    }

    updateHeartButton();
}

void QSplitEditor::applyPreset (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return;

    processor.apvts.replaceState (presets[(size_t) index].state);
    updateHeartButton();
}

void QSplitEditor::cyclePreset (int direction)
{
    // Collect selectable item ids in display order.
    std::vector<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
    {
        const int pid = presetBox.getItemId (i);
        if (pid > 0 && comboToPreset.count (pid) > 0)
            ids.push_back (pid);
    }
    if (ids.empty())
        return;

    const int cur = presetBox.getSelectedId();
    auto it = std::find (ids.begin(), ids.end(), cur);
    size_t pos = (it == ids.end()) ? 0
                 : ((it - ids.begin() + ids.size() + direction) % ids.size());
    presetBox.setSelectedId (ids[pos]); // fires onChange -> applyPreset
}

void QSplitEditor::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Name your preset:",
                                      juce::AlertWindow::NoIcon);
    aw->addTextEditor ("presetName", "My Preset");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<QSplitEditor> safeThis (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [aw, safeThis] (int result)
        {
            const juce::String name = aw->getTextEditorContents ("presetName").trim();
            const bool doSave = (result == 1 && safeThis != nullptr && name.isNotEmpty());
            delete aw;
            if (doSave)
                safeThis->saveUserPreset (name);
        }));
}

void QSplitEditor::saveUserPreset (const juce::String& name)
{
    const juce::File dir = getPresetDir();
    dir.createDirectory();

    const juce::String safe = name.replaceCharacter ('/', '_').replaceCharacter ('\\', '_');

    auto state = processor.apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml != nullptr)
        xml->writeTo (dir.getChildFile (safe + ".qsplitpreset"));

    buildPresetList();
    refreshPresetCombo();

    for (auto& kv : comboToPreset)
        if (presets[(size_t) kv.second].name == safe
            && ! presets[(size_t) kv.second].isFactory)
        {
            presetBox.setSelectedId (kv.first, juce::dontSendNotification);
            break;
        }
    updateHeartButton();
}

void QSplitEditor::loadFavorites()
{
    favorites.clear();
    const juce::File f = getFavoritesFile();
    if (f.existsAsFile())
    {
        juce::StringArray lines;
        f.readLines (lines);
        for (auto& l : lines)
        {
            l = l.trim();
            if (l.isNotEmpty())
                favorites.addIfNotAlreadyThere (l);
        }
    }
}

void QSplitEditor::saveFavorites()
{
    const juce::File f = getFavoritesFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (favorites.joinIntoString ("\n"));
}

void QSplitEditor::toggleFavorite()
{
    const int pid = presetBox.getSelectedId();
    const auto it = comboToPreset.find (pid);
    if (it == comboToPreset.end())
        return;

    const juce::String name = presets[(size_t) it->second].name;
    if (favorites.contains (name))
        favorites.removeString (name);
    else
        favorites.add (name);

    saveFavorites();
    refreshPresetCombo();

    // re-select the toggled preset
    for (auto& kv : comboToPreset)
        if (presets[(size_t) kv.second].name == name)
        {
            presetBox.setSelectedId (kv.first, juce::dontSendNotification);
            break;
        }
    updateHeartButton();
}

void QSplitEditor::updateHeartButton()
{
    bool fav = false;
    const int pid = presetBox.getSelectedId();
    const auto it = comboToPreset.find (pid);
    if (it != comboToPreset.end())
        fav = favorites.contains (presets[(size_t) it->second].name);

    heartButton.setButtonText (fav ? "♥" : "♡");
    heartButton.setColour (juce::TextButton::textColourOnId,
                           fav ? QSplitUI::accent2 : QSplitUI::dim);
}

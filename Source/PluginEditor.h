#pragma once

#include "PluginProcessor.h"

#include <functional>
#include <map>
#include <vector>

//==============================================================================
namespace QSplitUI
{
    const juce::Colour bg     = juce::Colour (0xff0d0d12);
    const juce::Colour panel  = juce::Colour (0xff16161d);
    const juce::Colour panel2 = juce::Colour (0xff1e1e26);
    const juce::Colour accent = juce::Colour (0xff00e5ff); // cyan
    const juce::Colour accent2 = juce::Colour (0xffff2e88); // magenta
    const juce::Colour text   = juce::Colour (0xfff2f2f5);
    const juce::Colour dim    = juce::Colour (0xff8a8a99);
}

bool isAudioFile (const juce::File& f);

//==============================================================================
// Mixin: forwards file drops to a handler. Inherit alongside juce::Component.
// (QFlip taught us: EVERY covering child component must implement this,
// otherwise drops landing on children are silently rejected.)
class DropForwarder : public juce::FileDragAndDropTarget
{
public:
    using Handler = std::function<void (const juce::StringArray&)>;
    explicit DropForwarder (Handler h);
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
private:
    Handler handler;
};

//==============================================================================
class DropZone : public juce::Component, public DropForwarder
{
public:
    DropZone (DropForwarder::Handler dropHandler, std::function<void()> browseHandler);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void setSongInfo (const juce::String& name, double seconds, bool hasSong);
private:
    juce::Label mainLabel, subLabel;
    juce::TextButton browseButton { "Browse" };
    bool hasSong = false;
};

//==============================================================================
class WaveformView : public juce::Component, public DropForwarder
{
public:
    explicit WaveformView (DropForwarder::Handler dropHandler);
    void setPeaks (std::vector<float> newPeaks); // max-abs per column, 0..1
    void setPlayhead01 (double p);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    std::function<void (double)> onSeek; // 0..1
private:
    std::vector<float> peaks;
    double playhead01 = 0.0;
};

//==============================================================================
class DragLabel : public juce::Label
{
public:
    std::function<void()> onDragStart;
    void mouseDown (const juce::MouseEvent&) override;
};

//==============================================================================
class StemStrip : public juce::Component, public DropForwarder
{
public:
    StemStrip (QSplitProcessor& p, int stemIndex, DropForwarder::Handler dropHandler);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void setStemsReady (bool ready);

    std::function<void (int)> onBounce;  // save-dialog export
    std::function<void (int)> onDragOut; // external drag-drop export

private:
    QSplitProcessor& processor;
    const int index;
    const juce::Colour colour;

    DragLabel nameLabel;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, bounceButton { "Bounce" };
    juce::Slider levelSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach, soloAttach;
};

//==============================================================================
class TransportBar : public juce::Component, public DropForwarder
{
public:
    explicit TransportBar (DropForwarder::Handler dropHandler);
    void resized() override;
    void setPlaying (bool p);
    void setTimeText (const juce::String& t);
    void setTransportEnabled (bool e);

    std::function<void()> onPlayToggle;
    std::function<void()> onStop;
private:
    juce::TextButton playButton { "Play" }, stopButton { "Stop" };
    juce::Label timeLabel;
    bool playing = false;
};

//==============================================================================
struct QSplitPreset
{
    juce::String name;
    juce::ValueTree state;
    bool isFactory = true;
};

//==============================================================================
class QSplitEditor : public juce::AudioProcessorEditor,
                     public juce::Timer,
                     public juce::FileDragAndDropTarget,
                     private juce::AsyncUpdater
{
public:
    explicit QSplitEditor (QSplitProcessor&);
    ~QSplitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void handleDroppedFiles (const juce::StringArray& files);

private:
    void handleAsyncUpdate() override;

    void openFileBrowser();
    void refreshSongUI();
    void refreshAfterSeparation();
    void updateTransportUI();
    void computePeaks();
    static juce::String formatTime (double seconds);

    void bounceStemToFile (int stemIndex);
    void dragStemOut (int stemIndex);

    // ---- presets (standard for every Headroom Audio plugin) ----
    juce::File getPresetDir() const;
    juce::File getFavoritesFile() const;
    void buildPresetList();
    void refreshPresetCombo();
    void applyPreset (int index);
    void cyclePreset (int direction);
    void promptSavePreset();
    void saveUserPreset (const juce::String& name);
    juce::ValueTree presetStateWith (std::initializer_list<std::pair<const char*, float>> values);
    void loadFavorites();
    void saveFavorites();
    void toggleFavorite();
    void updateHeartButton();

    QSplitProcessor& processor;

    // top bar
    juce::ImageComponent logoImage;
    juce::Label titleLabel;
    juce::ComboBox presetBox;
    juce::TextButton heartButton { "♥" }, prevButton { "<" },
                     nextButton { ">" }, saveButton { "Save" };
    std::vector<QSplitPreset> presets;
    juce::StringArray favorites;
    std::map<int, int> comboToPreset; // ComboBox itemId -> presets index

    DropZone dropZone;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton { "Cancel" };

    WaveformView waveformView;
    TransportBar transportBar;
    std::array<std::unique_ptr<StemStrip>, 4> stemStrips;

    juce::Slider cleanupSlider, masterSlider;
    juce::Label cleanupLabel, masterLabel, statusLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cleanupAttach, masterAttach;
};

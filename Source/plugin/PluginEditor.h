#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Themes.h"

// The big Freeze latch. Paints from the processor's effective state (latch, or MIDI).
class FreezeLatch : public juce::Component
{
public:
    FreezeLatch (DustMoteProcessor& p, DustLookAndFeel& l) : proc (p), lnf (l) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.toggleFreeze(); repaint(); }

private:
    DustMoteProcessor& proc;
    DustLookAndFeel& lnf;
};

class DustMoteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DustMoteEditor (DustMoteProcessor&);
    ~DustMoteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void pumpForSnapshot() { timerCallback(); }   // used by tools/Snapshots

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob& addKnob (const juce::String& id, const juce::String& label, bool bipolar = false);
    void placeKnob (Knob&, juce::Rectangle<int> area);
    void placeRow (std::initializer_list<Knob*> row, juce::Rectangle<int> area);
    void applyTheme (const juce::String& name);
    void rebuildPresetBox();
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
    void drawDustView (juce::Graphics&, juce::Rectangle<int>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& label);
    void updateTrails();
    float pitchToY (float semis, float top, float height) const;

    DustMoteProcessor& proc;
    DustLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox presetBox, themeBox, midiBox, noteBox, divBox, qualityBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> midiAttach, noteAttach, divAttach, qualityAttach;
    juce::ToggleButton syncButton { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttach;
    std::array<juce::ToggleButton, 4> modeButtons;
    std::array<juce::ToggleButton, dm::kNumPitches> chips;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, dm::kNumPitches> chipAttach;
    FreezeLatch latch;

    Knob *density = nullptr, *size = nullptr, *spray = nullptr, *scan = nullptr, *shape = nullptr,
         *pitchRand = nullptr, *detune = nullptr,
         *reverse = nullptr, *spread = nullptr, *drift = nullptr, *speed = nullptr,
         *loopLen = nullptr, *decay = nullptr, *loopLevel = nullptr, *xfade = nullptr,
         *feedback = nullptr, *tone = nullptr, *mix = nullptr, *output = nullptr;

    juce::Rectangle<int> header, dustArea, freezePanel, loopTitleArea, grainsPanel, motionPanel, colourPanel, pitchPanel, outPanel,
                         meterArea, freezeInfoArea;
    juce::Image trails, grainTexture;
    float inMeter = 0, outMeter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustMoteEditor)
};

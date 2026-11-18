#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

class DustMoteProcessor : public juce::AudioProcessor
{
public:
    DustMoteProcessor();
    ~DustMoteProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI hooks (message thread) ----
    const dm::Status& getStatus() const { return engine.getStatus(); }
    dm::Params currentParams() const { return readParams(); }
    bool isFrozen() const { return readParams().freeze; }     // latch XOR MIDI
    void toggleFreeze();                                       // flips the effective state
    double getBpm() const { return bpm.load(); }

    juce::String getThemeName() const { return themeName; }
    void setThemeName (const juce::String& t) { themeName = t; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String pitchId (int i);
    static juce::String noteName (int midiNote);

private:
    dm::Params readParams() const;
    void handleMidi (const juce::MidiBuffer&);

    dm::Engine engine;
    std::atomic<bool> midiToggle { false };   // Toggle mode: flipped by each matching note-on
    std::atomic<int> midiHeld { 0 };          // Hold mode: matching notes held
    std::atomic<double> bpm { 120.0 };
    juce::String themeName { "Lantern" };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustMoteProcessor)
};

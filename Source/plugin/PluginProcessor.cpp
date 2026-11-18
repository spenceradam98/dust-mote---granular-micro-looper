#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
const StringArray loopModes { "Off", "Record", "Overdub", "Play" };
const StringArray loopDivs { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
const StringArray midiModes { "MIDI Off", "Toggle", "Hold" };
const StringArray qualities { "Eco (16 grains)", "Normal (32 grains)", "High (64 grains)" };
}

String DustMoteProcessor::pitchId (int i)
{
    const int s = dm::kPitchSemis[(size_t) i];
    return s < 0 ? "pm" + String (-s) : "pp" + String (s);
}

String DustMoteProcessor::noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (names[n % 12]) + String (n / 12 - 1);
}

AudioProcessorValueTreeState::ParameterLayout DustMoteProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (float v, int) { return String (v, 0) + "%"; };
    auto ms = [] (float v, int) { return v >= 1000 ? String (v / 1000.0f, 2) + " s" : String (v, 0) + " ms"; };
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    auto fl = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> fmt)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    };
    auto percent = [&] (const char* id, const char* name, float def) { fl (id, name, NormalisableRange<float> (0, 100, 0.1f), def, pct); };

    const dm::Params d;
    // grains
    fl ("density", "Density", NormalisableRange<float> (1, 64, 0.01f, 0.5f), d.density,
        [] (float v, int) { return String (v, v < 10 ? 1 : 0) + " /s"; });
    fl ("size", "Grain Size", NormalisableRange<float> (10, 1000, 0.1f, 0.4f), d.sizeMs, ms);
    percent ("spray", "Spray", d.spray * 100);
    percent ("scan", "Scan", d.scan * 100);
    percent ("shape", "Shape", d.shape * 100);
    // pitch
    for (int i = 0; i < dm::kNumPitches; ++i)
    {
        const int s = dm::kPitchSemis[(size_t) i];
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { pitchId (i), 1 },
                                                           "Pitch " + String (s > 0 ? "+" : "") + String (s), (d.pitchMask >> i) & 1u));
    }
    percent ("pitchRand", "Pitch Random", d.pitchRandom * 100);
    fl ("detune", "Detune", NormalisableRange<float> (0, 50, 0.1f), d.detuneCents, [] (float v, int) { return String (v, 1) + " c"; });
    // motion
    percent ("reverse", "Reverse", d.reverse * 100);
    percent ("spread", "Stereo Spread", d.spread * 100);
    percent ("drift", "Drift", d.drift * 100);
    {
        NormalisableRange<float> r (0.25f, 2.0f, 0.001f);
        r.setSkewForCentre (1.0f);
        fl ("speed", "Speed", r, d.speed, [] (float v, int) { return String (v, 2) + "x"; });
    }
    // loop
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "loopMode", 1 }, "Loop Mode", loopModes, (int) d.loopMode));
    fl ("loopLen", "Loop Length", NormalisableRange<float> (20, 8000, 1, 0.4f), d.loopMs, ms);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "loopDiv", 1 }, "Loop Division", loopDivs, d.loopDiv));
    percent ("decay", "Loop Decay", d.decay * 100);
    percent ("loopLevel", "Loop Level", d.loopLevel * 100);
    // freeze
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
    fl ("freezeXfade", "Freeze Crossfade", NormalisableRange<float> (5, 2000, 1, 0.4f), d.freezeMs, ms);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "freezeMidi", 1 }, "Freeze MIDI", midiModes, 0));
    {
        StringArray notes { "Any note" };
        for (int n = 0; n < 128; ++n) notes.add (noteName (n));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "freezeNote", 1 }, "Freeze Note", notes, 0));
    }
    // global
    percent ("feedback", "Feedback", d.feedback * 100);
    fl ("tone", "Tone", NormalisableRange<float> (-100, 100, 0.1f), d.tone * 100,
        [] (float v, int) { return std::abs (v) < 0.5f ? String ("Flat") : (v < 0 ? "Dark " : "Bright ") + String (std::abs (v), 0); });
    percent ("mix", "Mix", d.mix * 100);
    fl ("output", "Output", NormalisableRange<float> (-24, 12, 0.1f), 0.0f, db);
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 1 }, "Quality", qualities, (int) d.quality));
    return { p.begin(), p.end() };
}

DustMoteProcessor::DustMoteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DustMote", createLayout())
{
    setCurrentProgram (0);
}

bool DustMoteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    const bool okOut = out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
    const bool okIn = in == AudioChannelSet::mono() || in == AudioChannelSet::stereo();
    return okOut && okIn;
}

void DustMoteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
}

dm::Params DustMoteProcessor::readParams() const
{
    auto v = [this] (const String& id) { return apvts.getRawParameterValue (id)->load(); };
    dm::Params p;
    p.density = v ("density");
    p.sizeMs = v ("size");
    p.spray = v ("spray") / 100.0f;
    p.scan = v ("scan") / 100.0f;
    p.shape = v ("shape") / 100.0f;
    p.pitchMask = 0;
    for (int i = 0; i < dm::kNumPitches; ++i)
        if (v (pitchId (i)) > 0.5f) p.pitchMask |= 1u << i;
    p.pitchRandom = v ("pitchRand") / 100.0f;
    p.detuneCents = v ("detune");
    p.reverse = v ("reverse") / 100.0f;
    p.spread = v ("spread") / 100.0f;
    p.drift = v ("drift") / 100.0f;
    p.speed = v ("speed");
    p.loopMode = (dm::LoopMode) (int) v ("loopMode");
    p.loopMs = v ("loopLen");
    p.loopDiv = (int) v ("loopDiv");
    p.decay = v ("decay") / 100.0f;
    p.loopLevel = v ("loopLevel") / 100.0f;
    p.freezeMs = v ("freezeXfade");
    p.feedback = v ("feedback") / 100.0f * 0.95f;
    p.tone = v ("tone") / 100.0f;
    p.mix = v ("mix") / 100.0f;
    p.outputDb = v ("output");
    p.sync = v ("sync") > 0.5f;
    p.quality = (dm::Quality) (int) v ("quality");

    bool freeze = v ("freeze") > 0.5f;
    const int midiMode = (int) v ("freezeMidi");
    if (midiMode == 1) freeze = freeze != midiToggle.load();
    if (midiMode == 2) freeze = freeze || midiHeld.load() > 0;
    p.freeze = freeze;
    return p;
}

void DustMoteProcessor::toggleFreeze()
{
    if (auto* prm = apvts.getParameter ("freeze"))
        prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
}

void DustMoteProcessor::handleMidi (const MidiBuffer& midi)
{
    const int mode = (int) apvts.getRawParameterValue ("freezeMidi")->load();
    if (mode == 0) return;
    const int want = (int) apvts.getRawParameterValue ("freezeNote")->load() - 1;   // -1 = any
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const bool match = msg.isNoteOnOrOff() && (want < 0 || msg.getNoteNumber() == want);
        if (msg.isNoteOn() && match)
        {
            if (mode == 1) midiToggle.store (! midiToggle.load());
            else midiHeld.store (midiHeld.load() + 1);
        }
        else if (msg.isNoteOff() && match && mode == 2)
            midiHeld.store (std::max (0, midiHeld.load() - 1));
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            midiHeld.store (0);
    }
}

void DustMoteProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    handleMidi (midi);
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, buffer.getNumSamples());

    dm::HostTime t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { t.valid = true; t.bpm = *b; }
            if (auto q = pos->getPpqPosition()) t.ppq = *q;
            t.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }
    bpm.store (t.valid ? t.bpm : 120.0);
    engine.setHostTime (t);
    engine.setParams (readParams());
    engine.process (buffer.getArrayOfReadPointers(), inCh, buffer.getArrayOfWritePointers(), outCh, buffer.getNumSamples());
}

int DustMoteProcessor::getNumPrograms() { return (int) dm::factoryPresets().size(); }

const String DustMoteProcessor::getProgramName (int index)
{
    const auto& ps = dm::factoryPresets();
    return isPositiveAndBelow (index, (int) ps.size()) ? String (ps[(size_t) index].name) : String();
}

void DustMoteProcessor::setCurrentProgram (int index)
{
    const auto& ps = dm::factoryPresets();
    if (! isPositiveAndBelow (index, (int) ps.size())) return;
    currentProgram = index;
    const auto& p = ps[(size_t) index].p;
    auto set = [this] (const String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("density", p.density);
    set ("size", p.sizeMs);
    set ("spray", p.spray * 100);
    set ("scan", p.scan * 100);
    set ("shape", p.shape * 100);
    for (int i = 0; i < dm::kNumPitches; ++i) set (pitchId (i), (p.pitchMask >> i) & 1u ? 1.0f : 0.0f);
    set ("pitchRand", p.pitchRandom * 100);
    set ("detune", p.detuneCents);
    set ("reverse", p.reverse * 100);
    set ("spread", p.spread * 100);
    set ("drift", p.drift * 100);
    set ("speed", p.speed);
    set ("loopMode", (float) (int) p.loopMode);
    set ("loopLen", p.loopMs);
    set ("loopDiv", (float) p.loopDiv);
    set ("decay", p.decay * 100);
    set ("loopLevel", p.loopLevel * 100);
    set ("freeze", p.freeze ? 1.0f : 0.0f);
    set ("freezeXfade", p.freezeMs);
    set ("feedback", p.feedback / 0.95f * 100);
    set ("tone", p.tone * 100);
    set ("mix", p.mix * 100);
    set ("sync", p.sync ? 1.0f : 0.0f);
    set ("quality", (float) (int) p.quality);
}

void DustMoteProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("theme", themeName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void DustMoteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            themeName = tree.getProperty ("theme", "Lantern").toString();
            apvts.replaceState (tree);
        }
}

AudioProcessorEditor* DustMoteProcessor::createEditor() { return new DustMoteEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DustMoteProcessor(); }

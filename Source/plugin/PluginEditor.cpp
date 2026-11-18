#include "PluginEditor.h"

using namespace juce;

namespace
{
constexpr int W = 1000, H = 680;
constexpr float kStart = MathConstants<float>::pi * -0.75f, kEnd = MathConstants<float>::pi * 0.75f;
const char* kModeNames[] = { "OFF", "REC", "DUB", "PLAY" };
}

// ============================================================================ freeze latch
void FreezeLatch::paint (Graphics& g)
{
    const auto& t = lnf.theme();
    const bool on = proc.isFrozen();
    const float wg = proc.getStatus().writeGain.load();
    auto r = getLocalBounds().toFloat().reduced (8.0f);

    if (on)
    {
        // glow: a few soft rings outside the latch
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (t.accent.withAlpha (0.06f * (float) (5 - i) * (t.glow ? 1.0f : 0.6f)));
            g.fillRoundedRectangle (r.expanded ((float) i * 1.8f), 12.0f + (float) i);
        }
        g.setGradientFill (ColourGradient (t.accent.brighter (0.25f), r.getX(), r.getY(), t.accent2, r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 12.0f);
    }
    else
    {
        g.setColour (t.bg2);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (t.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
    }

    // latch "LED"
    const auto led = Rectangle<float> (10, 10).withCentre ({ r.getX() + 18.0f, r.getY() + 18.0f });
    g.setColour (on ? t.text.withAlpha (0.95f) : t.dim.withAlpha (0.5f));
    g.fillEllipse (led);

    const Colour txt = on ? t.accent.contrasting (0.9f) : t.text;
    g.setColour (txt);
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText (on ? "FROZEN" : "FREEZE", r.withTrimmedBottom (14), Justification::centred);
    g.setFont (FontOptions (11.0f));
    g.setColour (txt.withAlpha (0.75f));
    g.drawText (on ? "buffer held - click to release" : "click to hold the buffer", r.withTrimmedTop (r.getHeight() * 0.55f), Justification::centred);

    // write-head level: shows the crossfade in progress
    auto bar = r.reduced (16.0f, 0).removeFromBottom (12.0f).withHeight (3.0f);
    g.setColour (txt.withAlpha (0.2f));
    g.fillRoundedRectangle (bar, 1.5f);
    g.setColour (txt.withAlpha (0.8f));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * wg), 1.5f);
}

// ============================================================================ setup
DustMoteEditor::DustMoteEditor (DustMoteProcessor& p) : AudioProcessorEditor (&p), proc (p), latch (p, lnf)
{
    setLookAndFeel (&lnf);
    lnf.setTheme (themeByName (proc.getThemeName()));

    // ---- header ----
    rebuildPresetBox();
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    prevPreset.onClick = [this] {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    };
    nextPreset.onClick = [this] {
        proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms());
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    };
    for (int i = 0; i < (int) allThemes().size(); ++i) themeBox.addItem (allThemes()[(size_t) i].name, i + 1);
    themeBox.setText (lnf.theme().name, dontSendNotification);
    themeBox.onChange = [this] { applyTheme (themeBox.getText()); };
    themeBox.setTooltip ("Theme (saved with the session)");
    for (auto* c : { (Component*) &presetBox, (Component*) &prevPreset, (Component*) &nextPreset, (Component*) &themeBox })
        addAndMakeVisible (c);

    // ---- grains ----
    density = &addKnob ("density", "DENSITY");
    size = &addKnob ("size", "SIZE");
    spray = &addKnob ("spray", "SPRAY");
    scan = &addKnob ("scan", "SCAN");
    shape = &addKnob ("shape", "SHAPE");
    density->slider.setTooltip ("Grains per second. With Sync on it snaps to a note value (1/1 to 1/64).");
    size->slider.setTooltip ("Length of each grain");
    spray->slider.setTooltip ("Random jitter of where each grain starts (and of the timing between grains)");
    scan->slider.setTooltip ("Where in the buffer grains come from: 0 = what you just played, 100 = the oldest audio in the region");
    shape->slider.setTooltip ("Grain window: 0 = smooth Hann bell, 100 = flat Tukey with short edges (more percussive)");

    // ---- pitch ----
    for (int i = 0; i < dm::kNumPitches; ++i)
    {
        const int s = dm::kPitchSemis[(size_t) i];
        auto& c = chips[(size_t) i];
        c.setButtonText (s == 0 ? String ("0") : (s > 0 ? "+" : "") + String (s));
        c.setClickingTogglesState (true);
        c.setTooltip ("Grains may play at " + c.getButtonText() + " semitones");
        chipAttach[(size_t) i] = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, DustMoteProcessor::pitchId (i), c);
        addAndMakeVisible (c);
    }
    pitchRand = &addKnob ("pitchRand", "RANDOM");
    detune = &addKnob ("detune", "DETUNE");
    pitchRand->slider.setTooltip ("0 = every grain plays the enabled pitch nearest to 0. Higher = more grains pick a random enabled pitch.");
    detune->slider.setTooltip ("Random detune per grain, in cents");

    // ---- motion ----
    reverse = &addKnob ("reverse", "REVERSE");
    spread = &addKnob ("spread", "SPREAD");
    drift = &addKnob ("drift", "DRIFT");
    speed = &addKnob ("speed", "SPEED", true);
    reverse->slider.setTooltip ("Chance that a grain plays backwards");
    spread->slider.setTooltip ("Random stereo position per grain");
    drift->slider.setTooltip ("Slow random wandering of the scan position");
    speed->slider.setTooltip ("Playback rate of grains and the loop, 0.25x to 2x (changes pitch, like tape)");

    // ---- freeze ----
    addAndMakeVisible (latch);
    xfade = &addKnob ("freezeXfade", "XFADE");
    xfade->slider.setTooltip ("How long the write head takes to fade out when you freeze (and back in when you release)");
    midiBox.addItemList ({ "MIDI Off", "MIDI Toggle", "MIDI Hold" }, 1);
    midiAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "freezeMidi", midiBox);
    midiBox.setTooltip ("Toggle: each note-on flips Freeze. Hold: frozen while the note is held.");
    noteBox.addItem ("Any note", 1);
    for (int n = 0; n < 128; ++n) noteBox.addItem (DustMoteProcessor::noteName (n), n + 2);
    noteAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "freezeNote", noteBox);
    noteBox.setTooltip ("Which MIDI note controls Freeze");
    addAndMakeVisible (midiBox);
    addAndMakeVisible (noteBox);

    // ---- loop ----
    for (int i = 0; i < 4; ++i)
    {
        auto& b = modeButtons[(size_t) i];
        b.setButtonText (kModeNames[i]);
        b.setClickingTogglesState (false);
        b.onClick = [this, i] {
            if (auto* prm = proc.apvts.getParameter ("loopMode")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (b);
    }
    modeButtons[0].setTooltip ("Rolling capture: grains come from the last 8 seconds");
    modeButtons[1].setTooltip ("Rolling capture limited to the loop length (grains stay inside the last loop)");
    modeButtons[2].setTooltip ("Overdub: the last loop length is captured and plays, new input is layered on with Decay");
    modeButtons[3].setTooltip ("Play: the captured loop plays and nothing new is recorded");
    loopLen = &addKnob ("loopLen", "LENGTH");
    decay = &addKnob ("decay", "DECAY");
    loopLevel = &addKnob ("loopLevel", "LOOP LVL");
    decay->slider.setTooltip ("Overdub feedback: how much of the loop survives each pass");
    loopLevel->slider.setTooltip ("Level of the loop playback (Overdub and Play)");
    divBox.addItemList ({ "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }, 1);
    divAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "loopDiv", divBox);
    divBox.setTooltip ("Loop length when Sync is on");
    syncAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "sync", syncButton);
    syncButton.setClickingTogglesState (true);
    syncButton.setTooltip ("Lock grain timing and loop length to the host tempo");
    addAndMakeVisible (divBox);
    addAndMakeVisible (syncButton);

    // ---- colour & output ----
    feedback = &addKnob ("feedback", "FEEDBACK");
    tone = &addKnob ("tone", "TONE", true);
    mix = &addKnob ("mix", "MIX");
    output = &addKnob ("output", "OUTPUT", true);
    feedback->slider.setTooltip ("Grains back into the buffer, through a gentle low-pass and a soft limiter (cannot blow up)");
    tone->slider.setTooltip ("Tilt EQ on the grains: dark to bright");
    qualityBox.addItemList ({ "Eco (16 grains)", "Normal (32 grains)", "High (64 grains)" }, 1);
    qualityAttach = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "quality", qualityBox);
    qualityBox.setTooltip ("Caps the grain count to save CPU. Eco also uses cheaper linear interpolation.");
    addAndMakeVisible (qualityBox);

    grainTexture = Image (Image::ARGB, W, H, true);
    {
        Graphics tg (grainTexture);
        Random rnd (11);
        for (int i = 0; i < 2600; ++i)
        {
            tg.setColour (Colours::black.withAlpha (0.03f + 0.04f * rnd.nextFloat()));
            tg.fillRect ((float) rnd.nextInt (W), (float) rnd.nextInt (H), 1.2f, 1.2f);
        }
    }

    applyTheme (lnf.theme().name);
    setSize (W, H);
    startTimerHz (30);
}

DustMoteEditor::~DustMoteEditor() { setLookAndFeel (nullptr); }

void DustMoteEditor::rebuildPresetBox()
{
    presetBox.clear (dontSendNotification);
    const auto& ps = dm::factoryPresets();
    String lastCat;
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        if (lastCat != ps[(size_t) i].category) { presetBox.addSectionHeading (ps[(size_t) i].category); lastCat = ps[(size_t) i].category; }
        presetBox.addItem (ps[(size_t) i].name, i + 1);
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
}

DustMoteEditor::Knob& DustMoteEditor::addKnob (const String& id, const String& label, bool bipolar)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    auto& s = k->slider;
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::TextBoxBelow, false, 74, 16);
    s.setRotaryParameters (kStart, kEnd, true);
    s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    if (bipolar) s.getProperties().set ("bipolar", true);
    addAndMakeVisible (s);
    k->attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    if (auto* prm = proc.apvts.getParameter (id))
        s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void DustMoteEditor::applyTheme (const String& name)
{
    const auto& t = themeByName (name);
    lnf.setTheme (t);
    proc.setThemeName (t.name);
    for (int i = 0; i < dm::kNumPitches; ++i)
        chips[(size_t) i].getProperties().set ("colour", (int64) t.dustColour ((float) dm::kPitchSemis[(size_t) i]).getARGB());
    for (auto* k : { pitchRand, detune })
        k->slider.getProperties().set ("colour", (int64) t.dustColour (12.0f).getARGB());
    for (auto& k : knobs) k->slider.setColour (Slider::textBoxTextColourId, t.dim);
    trails = Image();
    sendLookAndFeelChange();
    repaint();
}

// ============================================================================ layout
void DustMoteEditor::placeKnob (Knob& k, Rectangle<int> area)
{
    k.slider.setBounds (area.withTrimmedTop (14));   // label row on top
}

void DustMoteEditor::placeRow (std::initializer_list<Knob*> row, Rectangle<int> area)
{
    const int kw = area.getWidth() / (int) row.size();
    for (auto* k : row) placeKnob (*k, area.removeFromLeft (kw));
}

void DustMoteEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    header = r.removeFromTop (44);
    {
        auto h = header;
        h.removeFromLeft (268);
        themeBox.setBounds (h.removeFromRight (140).reduced (0, 8));
        h.removeFromRight (16);
        prevPreset.setBounds (h.removeFromLeft (30).reduced (0, 8));
        presetBox.setBounds (h.removeFromLeft (300).reduced (4, 8));
        nextPreset.setBounds (h.removeFromLeft (30).reduced (0, 8));
    }
    r.removeFromTop (8);

    // ---- top: dust view + freeze & loop ----
    auto top = r.removeFromTop (330);
    freezePanel = top.removeFromRight (352);
    top.removeFromRight (10);
    dustArea = top;
    {
        auto p = freezePanel.reduced (12);
        p.removeFromTop (20);
        auto row1 = p.removeFromTop (104);
        latch.setBounds (row1.removeFromLeft (230).expanded (6));
        row1.removeFromLeft (6);
        placeKnob (*xfade, row1);
        p.removeFromTop (8);
        auto row2 = p.removeFromTop (24);
        midiBox.setBounds (row2.removeFromLeft (150));
        row2.removeFromLeft (8);
        noteBox.setBounds (row2.removeFromLeft (110));
        freezeInfoArea = row2.withTrimmedLeft (4);
        p.removeFromTop (10);
        loopTitleArea = p.removeFromTop (18);
        auto row3 = p.removeFromTop (26);
        const int bw = row3.getWidth() / 4;
        for (auto& b : modeButtons) b.setBounds (row3.removeFromLeft (bw).reduced (2, 0));
        p.removeFromTop (4);
        auto row4 = p;
        auto side = row4.removeFromRight (86);
        placeRow ({ loopLen, decay, loopLevel }, row4);
        side.removeFromTop (22);
        syncButton.setBounds (side.removeFromTop (26));
        side.removeFromTop (8);
        divBox.setBounds (side.removeFromTop (24));
    }

    r.removeFromTop (10);
    auto mid = r.removeFromTop (128);
    grainsPanel = mid.removeFromLeft (412);
    mid.removeFromLeft (10);
    motionPanel = mid.removeFromLeft (330);
    mid.removeFromLeft (10);
    colourPanel = mid;
    placeRow ({ density, size, spray, scan, shape }, grainsPanel.reduced (10).withTrimmedTop (16));
    placeRow ({ reverse, spread, drift, speed }, motionPanel.reduced (10).withTrimmedTop (16));
    placeRow ({ feedback, tone }, colourPanel.reduced (10).withTrimmedTop (16));

    r.removeFromTop (10);
    auto bottom = r;
    pitchPanel = bottom.removeFromLeft (610);
    bottom.removeFromLeft (10);
    outPanel = bottom;
    {
        auto p = pitchPanel.reduced (10).withTrimmedTop (16);
        auto knobsArea = p.removeFromRight (160);
        placeRow ({ pitchRand, detune }, knobsArea);
        auto chipRow = p.withSizeKeepingCentre (p.getWidth(), 34).translated (0, -4);
        const int cw = chipRow.getWidth() / dm::kNumPitches;
        for (auto& c : chips) c.setBounds (chipRow.removeFromLeft (cw).reduced (3, 0));
    }
    {
        auto p = outPanel.reduced (10).withTrimmedTop (16);
        meterArea = p.removeFromRight (56);
        auto q = p.removeFromRight (120);
        qualityBox.setBounds (q.withSizeKeepingCentre (q.getWidth(), 24).translated (0, 6));
        placeRow ({ mix, output }, p);
    }
}

// ============================================================================ painting
void DustMoteEditor::drawPanel (Graphics& g, Rectangle<int> area, const String& title)
{
    const auto& t = lnf.theme();
    auto r = area.toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (t.edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (t.dim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (title, area.reduced (12, 8).removeFromTop (14), Justification::left);
    }
}

void DustMoteEditor::drawMeter (Graphics& g, Rectangle<float> r, float level, const String& label)
{
    const auto& t = lnf.theme();
    g.setColour (t.bg2);
    g.fillRoundedRectangle (r, 3.0f);
    const float db = level > 0 ? 20.0f * std::log10 (level) : -100.0f;
    const float frac = jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
    auto fill = r.reduced (2).withTrimmedTop ((r.getHeight() - 4) * (1.0f - frac));
    g.setColour (db > -1.0f ? Colour (0xffff4040) : t.accent);
    g.fillRoundedRectangle (fill, 2.0f);
    g.setColour (t.dim);
    g.setFont (FontOptions (9.0f, Font::bold));
    g.drawText (label, r.withY (r.getBottom() + 2).withHeight (11).withSizeKeepingCentre (30, 11), Justification::centred);
}

float DustMoteEditor::pitchToY (float semis, float top, float height) const
{
    // -12 at the bottom, +24 at the top, with a little room at both ends
    const float f = jlimit (0.0f, 1.0f, (semis + 14.0f) / 40.0f);
    return top + height * (1.0f - f);
}

void DustMoteEditor::updateTrails()
{
    auto area = dustArea.reduced (2);
    if (area.isEmpty()) return;
    if (! trails.isValid() || trails.getWidth() != area.getWidth() || trails.getHeight() != area.getHeight())
        trails = Image (Image::ARGB, area.getWidth(), area.getHeight(), true);
    trails.multiplyAllAlphas (0.78f);

    const auto& t = lnf.theme();
    const auto& st = proc.getStatus();
    Graphics g (trails);
    const float w = (float) area.getWidth(), h = (float) area.getHeight();
    for (int v = 0; v < dm::kMaxGrains; ++v)
    {
        const auto& gv = st.grains[(size_t) v];
        if (! gv.active.load (std::memory_order_relaxed)) continue;
        const float x = 8.0f + gv.x.load (std::memory_order_relaxed) * (w - 16.0f);
        const float semis = gv.semis.load (std::memory_order_relaxed);
        const float pan = gv.pan.load (std::memory_order_relaxed);
        const float y = pitchToY (semis, 18.0f, h - 46.0f) + pan * 10.0f;
        const float a = std::sqrt (jlimit (0.0f, 1.0f, gv.amp.load (std::memory_order_relaxed)));
        const float sz = 2.5f + a * 7.0f;
        const auto col = t.dustColour (semis);
        if (t.glow)
        {
            g.setColour (col.withAlpha (0.16f * a));
            g.fillEllipse (Rectangle<float> (sz * 3.2f, sz * 3.2f).withCentre ({ x, y }));
        }
        g.setColour (col.withAlpha (0.25f + 0.75f * a));
        g.fillEllipse (Rectangle<float> (sz, sz).withCentre ({ x, y }));
    }
}

void DustMoteEditor::drawDustView (Graphics& g, Rectangle<int> area)
{
    const auto& t = lnf.theme();
    const auto& st = proc.getStatus();
    const auto params = proc.currentParams();
    auto r = area.toFloat();

    g.setColour (t.bg2.darker (t.bg2.getBrightness() > 0.5f ? 0.02f : 0.35f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (t.edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    auto inner = area.reduced (2);
    const float x0 = (float) inner.getX() + 8.0f, wv = (float) inner.getWidth() - 16.0f;
    const float top = (float) inner.getY() + 18.0f, hv = (float) inner.getHeight() - 46.0f;

    // pitch guide lines
    g.setFont (FontOptions (9.0f));
    for (int s : { -12, 0, 12, 24 })
    {
        const float y = pitchToY ((float) s, top, hv);
        g.setColour (t.dim.withAlpha (s == 0 ? 0.25f : 0.12f));
        g.drawHorizontalLine ((int) y, x0, x0 + wv);
        g.setColour (t.dim.withAlpha (0.6f));
        g.drawText ((s > 0 ? "+" : "") + String (s), Rectangle<float> (x0 + 2, y - 11, 30, 10), Justification::left);
    }

    // buffer waveform (the region grains are drawn from), newest audio on the right
    const int bufLen = jmax (1, st.bufferLen.load());
    const int binSize = (bufLen + dm::kOverviewBins - 1) / dm::kOverviewBins;
    const int end = st.writePos.load();
    const int len = jmax (1, st.regionLen.load());
    const int filled = st.filled.load();
    const float midY = pitchToY (0.0f, top, hv);
    const bool loopMode = st.loopMode.load() >= 2;
    const int cols = (int) wv / 2;
    for (int c = 0; c < cols; ++c)
    {
        const double f0 = (double) c / cols, f1 = (double) (c + 1) / cols;
        const int a = (int) (end - len + f0 * len), b = (int) (end - len + f1 * len);
        if (! loopMode && end - a > filled) continue;
        float peak = 0;
        for (int i = a; i <= b; i += jmax (1, binSize))
        {
            int idx = i % bufLen; if (idx < 0) idx += bufLen;
            peak = jmax (peak, st.overview[(size_t) jmin (idx / binSize, dm::kOverviewBins - 1)].load (std::memory_order_relaxed));
        }
        const float hgt = jmin (1.0f, peak) * hv * 0.42f;
        g.setColour (t.dim.withAlpha (0.28f));
        g.fillRect (x0 + (float) c * 2.0f, midY - hgt, 1.4f, hgt * 2.0f + 1.0f);
    }

    // scan window
    {
        const float sx = x0 + st.scanPos.load() * wv;
        const float sw = st.sprayWidth.load() * wv;
        g.setColour (t.accent.withAlpha (0.07f));
        g.fillRect (Rectangle<float> (jmax (x0, sx - sw * 0.5f), top - 10, jmin (sw, wv), hv + 20).getIntersection ({ x0, top - 10, wv, hv + 20 }));
        g.setColour (t.accent.withAlpha (0.45f));
        g.drawVerticalLine ((int) sx, top - 10, top + hv + 10);
    }

    // loop playhead / write head
    if (loopMode)
    {
        const float lx = x0 + st.loopPhase.load() * wv;
        g.setColour (t.accent2.withAlpha (0.9f));
        g.fillRect (lx - 1.0f, top - 10, 2.0f, hv + 20);
    }
    else
    {
        const float wg = st.writeGain.load();
        g.setColour (t.accent2.withAlpha (0.2f + 0.7f * wg));
        g.fillRect (x0 + wv - 2.0f, top - 10, 3.0f, hv + 20);
    }

    // particles (with fading trails)
    if (trails.isValid()) g.drawImageAt (trails, inner.getX(), inner.getY());
    for (int v = 0; v < dm::kMaxGrains; ++v)   // crisp cores on top of the trails
    {
        const auto& gv = st.grains[(size_t) v];
        if (! gv.active.load (std::memory_order_relaxed)) continue;
        const float semis = gv.semis.load (std::memory_order_relaxed);
        const float x = (float) inner.getX() + 8.0f + gv.x.load (std::memory_order_relaxed) * wv;
        const float y = (float) inner.getY() + pitchToY (semis, 18.0f, (float) inner.getHeight() - 46.0f)
                        + gv.pan.load (std::memory_order_relaxed) * 10.0f;
        const float a = std::sqrt (jlimit (0.0f, 1.0f, gv.amp.load (std::memory_order_relaxed)));
        const float sz = 2.0f + a * 4.0f;
        g.setColour (t.dustColour (semis).brighter (0.4f).withAlpha (0.4f + 0.6f * a));
        g.fillEllipse (Rectangle<float> (sz, sz).withCentre ({ x, y }));
    }

    // frozen tint
    if (proc.isFrozen())
    {
        g.setColour (t.accent.withAlpha (0.05f));
        g.fillRoundedRectangle (r.reduced (1), 8.0f);
        g.setColour (t.accent);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("FROZEN", inner.reduced (12, 6).removeFromTop (16), Justification::right);
    }

    // footer readout
    {
        auto f = inner.reduced (12, 6).removeFromBottom (16);
        g.setFont (FontOptions (11.0f));
        g.setColour (t.dim);
        const double sr = jmax (1.0, (double) bufLen / dm::kBufferSeconds);
        const String region = String ((double) len / sr, 2) + " s";
        static const char* modeText[] = { "Rolling capture", "Recording loop", "Overdub loop", "Loop playback" };
        String s = String (proc.isFrozen() && st.loopMode.load() < 2 ? "Frozen buffer" : modeText[jlimit (0, 3, st.loopMode.load())]) + "  |  " + region + "  |  "
                   + String (st.activeGrains.load()) + " grains";
        if (params.sync) s << "  |  " << String (proc.getBpm(), 1) << " bpm";
        g.drawText (s, f, Justification::left);
        g.drawText ("older  <   buffer   >  now", f, Justification::right);
    }
}

void DustMoteEditor::paint (Graphics& g)
{
    const auto& t = lnf.theme();
    g.setGradientFill (ColourGradient (t.bg2, 0, 0, t.bg, 0, (float) H, false));
    g.fillAll();
    if (t.grain) g.drawImageAt (grainTexture, 0, 0);

    // header: wordmark with a few motes
    {
        auto h = header;
        g.setColour (t.text);
        g.setFont (FontOptions (30.0f, Font::bold));
        auto word = h.removeFromLeft (170);
        g.drawText ("DUST MOTE", word, Justification::centredLeft);
        g.setColour (t.accent);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText ("granular\nmicro-looper", h.removeFromLeft (90), Justification::centredLeft, 2);
        Random rnd (3);
        for (int i = 0; i < 7; ++i)
        {
            const float x = (float) word.getX() + 6 + rnd.nextFloat() * 150, y = (float) word.getY() + 4 + rnd.nextFloat() * 8;
            g.setColour (t.dustColour (rnd.nextFloat() * 36 - 12).withAlpha (0.8f));
            g.fillEllipse (x, y, 2.5f, 2.5f);
        }
    }

    drawDustView (g, dustArea);
    drawPanel (g, freezePanel, "FREEZE");
    {
        g.setColour (t.edge);
        g.drawHorizontalLine (loopTitleArea.getY() - 4, (float) freezePanel.getX() + 12, (float) freezePanel.getRight() - 12);
        g.setColour (t.dim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText ("MICRO-LOOP", loopTitleArea, Justification::left);
        g.setFont (FontOptions (11.0f));
        const auto p = proc.currentParams();
        const int mm = (int) proc.apvts.getRawParameterValue ("freezeMidi")->load();
        g.drawText (mm == 0 ? String() : (p.freeze ? "frozen" : "listening"), freezeInfoArea, Justification::centredLeft);
    }
    drawPanel (g, grainsPanel, "GRAINS");
    drawPanel (g, motionPanel, "MOTION");
    drawPanel (g, colourPanel, "COLOUR");
    drawPanel (g, pitchPanel, "PITCH SET");
    drawPanel (g, outPanel, "OUTPUT");
    {
        g.setColour (t.dim);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText ("QUALITY", qualityBox.getBounds().translated (0, -16).withHeight (14), Justification::centred);
    }

    {
        auto m = meterArea.toFloat().reduced (0, 4).withTrimmedBottom (12);
        const float mw = 12;
        auto pair = m.withSizeKeepingCentre (mw * 2 + 12, m.getHeight());
        drawMeter (g, pair.removeFromLeft (mw), inMeter, "IN");
        pair.removeFromLeft (12);
        drawMeter (g, pair.removeFromLeft (mw), outMeter, "OUT");
    }

    g.setFont (FontOptions (10.0f, Font::bold));
    for (auto& k : knobs)
        if (k->slider.isVisible())
        {
            g.setColour (t.dim);
            g.drawText (k->label, k->slider.getBounds().withY (k->slider.getY() - 14).withHeight (14), Justification::centred);
        }
}

void DustMoteEditor::timerCallback()
{
    const auto& st = proc.getStatus();
    inMeter = jmax (st.inLevel.load(), inMeter * 0.85f);
    outMeter = jmax (st.outLevel.load(), outMeter * 0.85f);

    const auto p = proc.currentParams();
    for (int i = 0; i < 4; ++i)
        modeButtons[(size_t) i].setToggleState ((int) p.loopMode == i, dontSendNotification);
    loopLen->slider.setEnabled (! p.sync);
    divBox.setEnabled (p.sync);
    decay->slider.setEnabled (p.loopMode == dm::LoopMode::Overdub);
    loopLevel->slider.setEnabled (p.loopMode == dm::LoopMode::Overdub || p.loopMode == dm::LoopMode::Play);
    noteBox.setEnabled ((int) proc.apvts.getRawParameterValue ("freezeMidi")->load() != 0);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);

    updateTrails();
    latch.repaint();
    repaint (dustArea);
    repaint (meterArea);
    repaint (freezeInfoArea);
}

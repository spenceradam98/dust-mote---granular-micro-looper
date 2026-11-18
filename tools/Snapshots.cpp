// Renders PNG screenshots of the Dust Mote editor for every theme, feeding it a guitar-like
// chord so the dust view has grains and a waveform. Usage: Snapshots <outDir>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static void fillChord (AudioBuffer<float>& b, int64& pos, double sr)
{
    static const double notes[] = { 110.0, 164.81, 220.0, 277.18, 329.63 };
    for (int i = 0; i < b.getNumSamples(); ++i, ++pos)
    {
        const double t = (double) pos / sr;
        const double strum = std::fmod (t, 2.0);                    // re-strum every 2 s
        float x = 0;
        for (int n = 0; n < 5; ++n)
        {
            const double on = strum - n * 0.03;
            if (on < 0) continue;
            const double env = std::exp (-on * 1.6);
            for (int h = 1; h <= 4; ++h)
                x += (float) (0.12 * env / h * std::sin (MathConstants<double>::twoPi * notes[n] * h * t));
        }
        b.setSample (0, i, x);
        b.setSample (1, i, x * 0.9f);
    }
}

static int presetIndex (const char* name)
{
    const auto& ps = dm::factoryPresets();
    for (int i = 0; i < (int) ps.size(); ++i) if (String (ps[(size_t) i].name) == name) return i;
    return 0;
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI gui;
    const File outDir (argc > 1 ? argv[1] : "shots");
    outDir.createDirectory();
    const double sr = 48000.0;
    const int bs = 512;

    struct Shot { const char* theme; const char* preset; bool freeze; int loopMode; const char* file; };
    const Shot shots[] = {
        { "Lantern", "Shimmer Mist", false, -1, "lantern" },
        { "Moth", "Octave Dust", false, -1, "moth" },
        { "Frost", "Frozen Shimmer", true, -1, "frost" },
        { "Neon Dust", "Glitch Halo", false, 3, "neon" },
    };
    for (auto& s : shots)
    {
        DustMoteProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, bs);
        proc.setCurrentProgram (presetIndex (s.preset));
        proc.setThemeName (s.theme);
        if (auto* f = proc.apvts.getParameter ("freeze")) f->setValueNotifyingHost (0.0f);
        proc.prepareToPlay (sr, bs);
        auto ed = std::make_unique<DustMoteEditor> (proc);
        AudioBuffer<float> buf (2, bs);
        MidiBuffer midi;
        int64 pos = 0;
        const int blocks = (int) (6.0 * sr / bs);
        for (int k = 0; k < blocks; ++k)
        {
            fillChord (buf, pos, sr);
            proc.processBlock (buf, midi);
            if (k == blocks - 60)
            {
                if (s.freeze) proc.toggleFreeze();
                if (s.loopMode >= 0)
                    if (auto* m = proc.apvts.getParameter ("loopMode")) m->setValueNotifyingHost (m->convertTo0to1 ((float) s.loopMode));
            }
            if (k > blocks - 12) ed->pumpForSnapshot();
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = outDir.getChildFile (String (s.file) + ".png");
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "wrote " << f.getFullPathName() << "\n";
        ed.reset();
        proc.releaseResources();
    }
    return 0;
}

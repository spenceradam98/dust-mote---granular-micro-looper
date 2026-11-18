// Factory presets.
#pragma once

#include <initializer_list>
#include <vector>
#include "Engine.h"

namespace dm
{
struct Preset { const char* name; const char* category; Params p; };

inline uint32_t pitches (std::initializer_list<int> semis)
{
    uint32_t m = 0;
    for (int s : semis)
        for (int i = 0; i < kNumPitches; ++i)
            if (kPitchSemis[(size_t) i] == s) m |= 1u << i;
    return m;
}

// Grain basics in one call; the rest is set field by field in the list below.
inline Params grainsP (float density, float sizeMs, float spray, float scan, float shape, float mix)
{
    Params p;
    p.density = density; p.sizeMs = sizeMs; p.spray = spray; p.scan = scan; p.shape = shape; p.mix = mix;
    return p;
}

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = [] {
        std::vector<Preset> v;
        auto add = [&] (const char* name, const char* cat, Params p) { v.push_back ({ name, cat, p }); };
        using L = LoopMode;

        // ---- Clouds ----
        { auto p = grainsP (14, 160, 0.30f, 0.15f, 0.0f, 0.45f); p.spread = 0.6f; p.tone = 0.0f;
          add ("Cloud Chamber", "Clouds", p); }
        { auto p = grainsP (32, 220, 0.55f, 0.35f, 0.0f, 0.55f); p.spread = 0.9f; p.drift = 0.4f; p.detuneCents = 8; p.tone = -0.2f;
          add ("Fog Bank", "Clouds", p); }
        { auto p = grainsP (48, 90, 0.80f, 0.40f, 0.1f, 0.5f); p.spread = 1.0f; p.detuneCents = 14; p.tone = 0.3f;
          add ("Dust Storm", "Clouds", p); }
        { auto p = grainsP (6, 600, 0.20f, 0.30f, 0.0f, 0.5f); p.spread = 0.7f; p.drift = 0.6f; p.speed = 0.5f; p.tone = -0.4f;
          add ("Slow Motes", "Clouds", p); }
        { auto p = grainsP (24, 300, 0.40f, 0.25f, 0.0f, 0.6f); p.spread = 0.8f; p.feedback = 0.45f; p.tone = -0.3f; p.drift = 0.3f;
          add ("Ambient Wall", "Clouds", p); }
        { auto p = grainsP (20, 140, 0.25f, 0.05f, 0.2f, 0.35f); p.spread = 0.5f;
          add ("Halo Thickener", "Clouds", p); }

        // ---- Pitch ----
        { auto p = grainsP (18, 180, 0.30f, 0.15f, 0.0f, 0.5f); p.pitchMask = pitches ({ 0, 12 }); p.pitchRandom = 0.5f; p.spread = 0.7f;
          add ("Octave Dust", "Pitch", p); }
        { auto p = grainsP (24, 260, 0.35f, 0.20f, 0.0f, 0.55f); p.pitchMask = pitches ({ 12, 19, 24 }); p.pitchRandom = 0.6f; p.spread = 0.9f;
          p.feedback = 0.35f; p.tone = 0.3f; add ("Shimmer Mist", "Pitch", p); }
        { auto p = grainsP (16, 220, 0.30f, 0.20f, 0.0f, 0.5f); p.pitchMask = pitches ({ -12, 0, 7 }); p.pitchRandom = 0.7f; p.spread = 0.8f;
          add ("Power Chord Cloud", "Pitch", p); }
        { auto p = grainsP (12, 300, 0.25f, 0.20f, 0.0f, 0.5f); p.pitchMask = pitches ({ -12 }); p.spread = 0.4f; p.tone = -0.3f;
          add ("Sub Swell", "Pitch", p); }
        { auto p = grainsP (28, 120, 0.50f, 0.25f, 0.1f, 0.5f); p.pitchMask = pitches ({ -5, 0, 5, 7, 12 }); p.pitchRandom = 1.0f; p.spread = 1.0f;
          add ("Fifths & Fourths", "Pitch", p); }
        { auto p = grainsP (20, 200, 0.30f, 0.20f, 0.0f, 0.4f); p.detuneCents = 25; p.spread = 1.0f;
          add ("Chorus Haze", "Pitch", p); }
        { auto p = grainsP (36, 70, 0.60f, 0.20f, 0.3f, 0.5f); p.pitchMask = pitches ({ 12, 19, 24 }); p.pitchRandom = 1.0f; p.spread = 1.0f; p.tone = 0.5f;
          add ("Glass Sparkle", "Pitch", p); }

        // ---- Reverse & motion ----
        { auto p = grainsP (16, 280, 0.35f, 0.25f, 0.0f, 0.55f); p.reverse = 1.0f; p.spread = 0.8f;
          add ("Reverse Rain", "Motion", p); }
        { auto p = grainsP (10, 450, 0.25f, 0.30f, 0.0f, 0.5f); p.reverse = 0.5f; p.pitchMask = pitches ({ 0, 12 }); p.pitchRandom = 0.4f;
          p.spread = 0.9f; p.feedback = 0.3f; add ("Backwards Bloom", "Motion", p); }
        { auto p = grainsP (14, 200, 0.20f, 0.10f, 0.0f, 0.5f); p.speed = 0.5f; p.drift = 0.3f; p.tone = -0.3f;
          add ("Half-Speed Ghost", "Motion", p); }
        { auto p = grainsP (8, 500, 0.40f, 0.50f, 0.0f, 0.5f); p.drift = 1.0f; p.spread = 0.8f;
          add ("Wandering Scan", "Motion", p); }
        { auto p = grainsP (22, 150, 0.30f, 0.20f, 0.0f, 0.5f); p.speed = 2.0f; p.reverse = 0.3f; p.spread = 0.7f;
          add ("Double-Time Flutter", "Motion", p); }

        // ---- Loops & stutter ----
        { auto p = grainsP (8, 60, 0.05f, 0.0f, 0.6f, 0.6f); p.loopMode = L::Record; p.loopMs = 180; p.sync = true; p.loopDiv = 0;
          p.spread = 0.3f; add ("Micro Stutter", "Loops", p); }
        { auto p = grainsP (12, 120, 0.15f, 0.0f, 0.4f, 0.55f); p.loopMode = L::Record; p.sync = true; p.loopDiv = 1;
          p.reverse = 0.25f; p.spread = 0.6f; add ("Eighth-Note Chop", "Loops", p); }
        { auto p = grainsP (10, 250, 0.40f, 0.30f, 0.0f, 0.5f); p.loopMode = L::Overdub; p.loopMs = 2000; p.decay = 0.85f;
          p.speed = 1.0f; p.spread = 0.8f; add ("Overdub Haze", "Loops", p); }
        { auto p = grainsP (18, 90, 0.50f, 0.30f, 0.2f, 0.6f); p.loopMode = L::Record; p.loopMs = 1500; p.speed = 0.5f;
          p.reverse = 0.4f; p.tone = -0.4f; add ("Tape Shards", "Loops", p); }
        { auto p = grainsP (6, 200, 0.20f, 0.20f, 0.0f, 0.6f); p.loopMode = L::Overdub; p.sync = true; p.loopDiv = 4; p.decay = 0.7f;
          p.loopLevel = 0.9f; p.spread = 0.7f; add ("One-Bar Looper", "Loops", p); }
        { auto p = grainsP (14, 140, 0.30f, 0.20f, 0.0f, 0.55f); p.loopMode = L::Record; p.loopMs = 800; p.pitchMask = pitches ({ 0, 12 });
          p.pitchRandom = 0.5f; p.sync = true; p.loopDiv = 2; add ("Beat Repeat Octaves", "Loops", p); }

        // ---- Freeze & drones ----
        { auto p = grainsP (24, 400, 0.50f, 0.25f, 0.0f, 0.65f); p.freeze = true; p.freezeMs = 300; p.spread = 0.9f; p.drift = 0.4f;
          p.tone = -0.2f; add ("Drone Bed (Freeze on)", "Freeze", p); }
        { auto p = grainsP (30, 350, 0.60f, 0.20f, 0.0f, 0.6f); p.spread = 1.0f; p.drift = 0.3f; p.detuneCents = 6; p.freezeMs = 400;
          add ("Frozen Chord Pad", "Freeze", p); }
        { auto p = grainsP (28, 300, 0.50f, 0.20f, 0.0f, 0.6f); p.pitchMask = pitches ({ 0, 12, 19 }); p.pitchRandom = 0.5f; p.spread = 1.0f;
          p.freezeMs = 500; p.tone = 0.2f; add ("Frozen Shimmer", "Freeze", p); }
        { auto p = grainsP (20, 600, 0.40f, 0.30f, 0.0f, 0.65f); p.pitchMask = pitches ({ -12, 0 }); p.pitchRandom = 0.5f; p.speed = 0.5f;
          p.spread = 0.8f; p.tone = -0.5f; p.freezeMs = 600; add ("Deep Freeze", "Freeze", p); }

        // ---- Glitch ----
        { auto p = grainsP (40, 30, 0.90f, 0.30f, 0.8f, 0.5f); p.reverse = 0.5f; p.pitchMask = pitches ({ -12, 0, 12, 24 }); p.pitchRandom = 0.8f;
          p.spread = 1.0f; add ("Glitch Halo", "Glitch", p); }
        { auto p = grainsP (64, 15, 1.00f, 0.50f, 1.0f, 0.45f); p.spread = 1.0f; p.reverse = 0.5f; p.quality = Quality::High;
          add ("Bit Swarm", "Glitch", p); }
        { auto p = grainsP (4, 40, 0.80f, 0.40f, 0.9f, 0.6f); p.pitchMask = pitches ({ -7, 5, 19 }); p.pitchRandom = 1.0f; p.spread = 1.0f;
          p.sync = true; add ("Sparse Blips", "Glitch", p); }
        return v;
    }();
    return list;
}
} // namespace dm

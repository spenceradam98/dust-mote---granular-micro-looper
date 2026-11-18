// Dust Mote engine: rolling capture buffer, micro-looper, Freeze latch and a 64-voice grain cloud.
// No JUCE dependency so it can be unit-tested on its own. Nothing allocates after prepare().
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "Dsp.h"

namespace dm
{
constexpr int kMaxGrains = 64;
constexpr int kNumPitches = 9;
constexpr std::array<int, kNumPitches> kPitchSemis { -12, -7, -5, 0, 5, 7, 12, 19, 24 };
constexpr double kBufferSeconds = 16.0;     // capture buffer length
constexpr double kScanSeconds = 8.0;        // how far back grains reach when not looping
constexpr double kMaxLoopSeconds = 12.0;
constexpr int kOverviewBins = 2048;         // waveform overview for the UI, over the whole buffer

enum class LoopMode { Off = 0, Record = 1, Overdub = 2, Play = 3 };
enum class Quality { Eco = 0, Normal = 1, High = 2 };

// Musical divisions, in beats (quarter notes).
constexpr std::array<double, 7> kLoopDivBeats { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };   // 1/16 .. 4 bars
constexpr std::array<double, 9> kGrainDivBeats { 4.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };

struct Params
{
    // Grains
    float density = 12.0f;        // grains per second, 1..64
    float sizeMs = 120.0f;        // 10..1000
    float spray = 0.2f;           // 0..1 position jitter
    float scan = 0.15f;           // 0..1, 0 = newest audio, 1 = oldest in the region
    float shape = 0.0f;           // 0 = Hann, 1 = flat Tukey
    // Pitch
    uint32_t pitchMask = 1u << 3; // bit i = kPitchSemis[i] enabled; default 0 st only
    float pitchRandom = 0.0f;     // 0..1, chance a grain picks a random enabled pitch instead of the home pitch
    float detuneCents = 0.0f;     // 0..50, random per grain
    // Motion
    float reverse = 0.0f;         // 0..1 probability
    float spread = 0.5f;          // 0..1 stereo spread
    float drift = 0.0f;           // 0..1 slow random scan
    float speed = 1.0f;           // 0.25..2 playback rate
    // Loop
    LoopMode loopMode = LoopMode::Off;
    float loopMs = 1000.0f;       // free length, 20..8000
    int loopDiv = 2;              // kLoopDivBeats index when synced
    float decay = 0.8f;           // overdub feedback 0..1
    float loopLevel = 1.0f;       // loop playback level 0..1
    // Freeze
    bool freeze = false;
    float freezeMs = 80.0f;       // write crossfade time, 5..2000
    // Global
    float feedback = 0.0f;        // 0..0.95, grains back into the buffer
    float tone = 0.0f;            // -1 dark .. +1 bright tilt (wet only)
    float mix = 0.5f;             // 0..1
    float outputDb = 0.0f;
    bool sync = false;
    Quality quality = Quality::Normal;
};

struct HostTime
{
    bool valid = false;           // host supplied tempo
    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;             // position at the start of the block, in quarter notes
};

// What the UI shows. Written by the audio thread once per block, read by the editor.
struct GrainView
{
    std::atomic<bool> active { false };
    std::atomic<float> x { 0 };       // 0 = oldest (left) .. 1 = newest (right) in the shown region
    std::atomic<float> semis { 0 };
    std::atomic<float> amp { 0 };
    std::atomic<float> pan { 0 };
    std::atomic<bool> reversed { false };
};

struct Status
{
    std::array<GrainView, kMaxGrains> grains;
    std::array<std::atomic<float>, kOverviewBins> overview {};
    std::atomic<int> writePos { 0 };          // newest sample index (exclusive) of the shown region's end
    std::atomic<int> regionLen { 1 };         // samples shown
    std::atomic<int> bufferLen { 1 };
    std::atomic<int> filled { 0 };            // samples ever written, capped at bufferLen
    std::atomic<float> scanPos { 0 };         // current scan centre, same axis as GrainView::x
    std::atomic<float> sprayWidth { 0 };
    std::atomic<float> loopPhase { 0 };       // loop playhead 0..1 (loop modes)
    std::atomic<float> writeGain { 1 };       // 1 = recording, 0 = frozen
    std::atomic<bool> frozen { false };
    std::atomic<int> loopMode { 0 };
    std::atomic<int> activeGrains { 0 };
    std::atomic<float> inLevel { 0 }, outLevel { 0 };
    std::atomic<uint32_t> spawnCount { 0 };
};

class Engine
{
public:
    Engine();

    // Allocates the capture buffer (about 12 MB at 96 kHz). Call from a non-audio thread.
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const Params& p) { params = p; }
    const Params& getParams() const { return params; }
    void setHostTime (const HostTime& t) { host = t; }

    // in/out are channel arrays; mono or stereo. In-place is allowed.
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    const Status& getStatus() const { return status; }

    // ---- helpers shared with the tests ----
    int bufferLength() const { return bufLen; }
    int writeIndex() const { return wpos; }
    float bufferSample (int ch, int index) const;           // raw buffer read, index wrapped
    float historySample (int ch, int delay) const { return bufferSample (ch, wpos - 1 - delay); }
    int activeGrainCount() const;
    int maxVoices() const;
    double loopLengthSamples() const;
    double grainInterval() const;                           // samples between grains (free or synced)
    static int nearestGrainDiv (double grainsPerSecond, double bpm);

private:
    struct Grain
    {
        bool active = false;
        bool circular = false;      // loop region (wraps) or rolling history (does not)
        int regionEnd = 0;          // absolute index one past the newest sample of the region
        double regionLen = 1;
        double pos = 0;             // position in region coordinates, 0 = region start (oldest)
        double inc = 1;             // signed playback increment per sample
        int length = 1, age = 0;
        float alpha = 1;            // window taper
        float gainL = 0, gainR = 0, mono = 0;
        float semis = 0, amp = 0;
        bool reversed = false;
    };

    void spawnGrain();
    float readCubic (const std::vector<float>& b, double absPos) const;
    float readLinear (const std::vector<float>& b, double absPos) const;
    float readRegion (const std::vector<float>& b, int regionEnd, double regionLen, double pos, bool circular, bool cubic) const;
    int wrap (int i) const { i %= bufLen; return i < 0 ? i + bufLen : i; }
    void writeSample (int index, float l, float r);
    void publishStatus();

    double sr = 48000;
    int bufLen = 1;
    std::vector<float> bufL, bufR;
    int wpos = 0;                  // rolling write head (also the loop region's end in loop modes)
    int filled = 0;
    double overdubPos = 0, loopPos = 0;
    LoopMode lastMode = LoopMode::Off;

    Params params;
    HostTime host;
    Status status;

    std::array<Grain, kMaxGrains> grains {};
    WindowTable window;
    Rng rng;
    double spawnCountdown = 0;
    double internalPpq = 0;
    long long lastGridIndex = -1;

    Ramp writeGain;
    Smoother dryS, wetS, outS, loopGainS;
    float lowGain = 1, highGain = 1;
    double bpm = 120.0;
    OnePoleLP toneL, toneR, fbL, fbR;
    float fbSampleL = 0, fbSampleR = 0;
    float driftValue = 0, driftTarget = 0;
    int driftCountdown = 0;
    float inPeak = 0, outPeak = 0;

    // overview tracking
    int ovBinSize = 1, ovCurBin = -1;
    float ovAccum = 0;
};
} // namespace dm

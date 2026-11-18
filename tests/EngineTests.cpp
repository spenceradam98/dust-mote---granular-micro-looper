// Engine tests: run with ctest or directly. No framework, exits non-zero on failure.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "Engine.h"
#include "Presets.h"

using namespace dm;

// ---- allocation counter: the audio path must never allocate ----
static std::atomic<bool> trackAllocs { false };
static std::atomic<int> allocCount { 0 };
static bool trackProcess = false;   // count allocations made inside Engine::process only
void* operator new (std::size_t n)
{
    if (trackAllocs.load()) allocCount.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::vector<float> sine (double hz, double sr, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2 * kPi * hz * i / sr);
    return v;
}

// Runs the engine over a mono signal in blocks, returns the stereo output.
struct Run { std::vector<float> l, r; };
static Run run (Engine& e, const std::vector<float>& x, int block = 256)
{
    Run o { std::vector<float> (x.size()), std::vector<float> (x.size()) };
    for (size_t i = 0; i < x.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, x.size() - i);
        const float* in[] = { x.data() + i, x.data() + i };
        float* out[] = { o.l.data() + i, o.r.data() + i };
        if (trackProcess) trackAllocs = true;
        e.process (in, 2, out, 2, n);
        trackAllocs = false;
    }
    return o;
}

static double goertzel (const std::vector<float>& x, size_t from, double hz, double sr)
{
    const double w = 2 * kPi * hz / sr, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < x.size(); ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static float maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

static bool allFinite (const Run& o)
{
    for (size_t i = 0; i < o.l.size(); ++i) if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i])) return false;
    return true;
}

static void testNoAllocation()
{
    const double sr = 48000;
    Engine e;
    Params p; p.quality = Quality::High; p.density = 64; p.sizeMs = 1000; p.spray = 1; p.pitchMask = 0x1ff; p.pitchRandom = 1;
    p.reverse = 0.5f; p.feedback = 0.5f;
    e.setParams (p);
    e.prepare (sr, 512);
    auto x = sine (220, sr, (int) sr * 3);
    trackProcess = true;
    run (e, x);
    p.loopMode = LoopMode::Overdub; e.setParams (p); run (e, x);
    p.freeze = true; p.sync = true; e.setParams (p); run (e, x);
    trackProcess = false;
    CHECK (allocCount.load() == 0, "process() allocated %d times", allocCount.load());
    CHECK (e.maxVoices() == kMaxGrains, "High quality uses all %d voices", kMaxGrains);
}

static void testVoiceCaps()
{
    Engine e; Params p; p.density = 64; p.sizeMs = 1000;
    for (auto q : { Quality::Eco, Quality::Normal, Quality::High })
    {
        p.quality = q; e.setParams (p); e.prepare (48000, 512);
        run (e, sine (220, 48000, 48000 * 3));
        CHECK (e.activeGrainCount() <= e.maxVoices(), "quality %d: %d grains active, cap %d", (int) q, e.activeGrainCount(), e.maxVoices());
        CHECK (e.activeGrainCount() >= e.maxVoices() - 2, "quality %d: pool should fill at max density and size (%d)", (int) q, e.activeGrainCount());
    }
}

static void testBufferWraps()
{
    const double sr = 8000;   // small rate keeps the 16 s buffer short
    Engine e; Params p; p.mix = 0; e.setParams (p); e.prepare (sr, 64);
    const int len = e.bufferLength();
    // a ramp that never repeats inside the buffer, longer than 2.5 buffers
    std::vector<float> x ((size_t) (len * 2.5));
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) ((i % 9973) / 9973.0) * 0.8f - 0.4f;
    run (e, x, 61);
    const size_t n = x.size();
    int bad = 0;
    for (int d : { 0, 1, 7, 1000, len / 2, len - 2 })
        if (std::abs (e.historySample (0, d) - x[n - 1 - (size_t) d]) > 1e-6f) ++bad;
    CHECK (bad == 0, "history after wrap does not match the input (%d mismatches)", bad);
    CHECK (e.writeIndex() == (int) (n % (size_t) len), "write head %d, expected %d", e.writeIndex(), (int) (n % (size_t) len));
}

static void testFreezeCrossfade()
{
    const double sr = 48000;
    Engine e; Params p; p.mix = 0; p.freezeMs = 50; e.setParams (p); e.prepare (sr, 256);
    auto x = sine (220, sr, (int) sr);
    const float sineStep = (float) (2 * kPi * 220 / sr * 0.5);
    run (e, x);
    const int before = e.writeIndex();
    p.freeze = true; e.setParams (p);
    run (e, x);
    const int frozenAt = e.writeIndex();
    const int fadeLen = frozenAt - before;
    CHECK (std::abs (fadeLen - (int) (0.05 * sr)) <= 256, "write head should stop after the 50 ms crossfade, moved %d", fadeLen);
    // the recorded material tapers to silence with no step bigger than the sine's own slope
    std::vector<float> tail;
    for (int i = before - 2000; i < frozenAt; ++i) tail.push_back (e.bufferSample (0, i));
    CHECK (maxStep (tail, 0, tail.size()) <= sineStep * 1.05f, "freeze fade has a step of %.4f (sine slope %.4f)", maxStep (tail, 0, tail.size()), sineStep);
    CHECK (std::abs (tail.back()) < 0.01f, "frozen buffer ends at %.4f, expected silence", tail.back());
    CHECK (e.getStatus().writeGain.load() == 0.0f, "write gain should be 0 when frozen");
    // while frozen nothing is written
    const float probe = e.bufferSample (0, frozenAt - 5000);
    run (e, sine (440, sr, (int) sr));
    CHECK (e.writeIndex() == frozenAt && e.bufferSample (0, frozenAt - 5000) == probe, "buffer changed while frozen");
    // un-freeze fades back in smoothly
    p.freeze = false; e.setParams (p);
    run (e, x);
    std::vector<float> resume;
    for (int i = frozenAt - 200; i < frozenAt + 4000; ++i) resume.push_back (e.bufferSample (0, i));
    CHECK (maxStep (resume, 0, resume.size()) <= sineStep * 1.05f, "un-freeze has a step of %.4f", maxStep (resume, 0, resume.size()));
}

static void testLoopSeam()
{
    const double sr = 48000;
    Engine e; Params p;
    p.mix = 1; p.loopMode = LoopMode::Record; p.loopMs = 237;   // not a whole number of periods
    p.density = 1; p.sizeMs = 10; p.pitchMask = 0;               // grains nearly silent: listen to the loop head
    e.setParams (p); e.prepare (sr, 256);
    const float amp = 0.5f;
    auto x = sine (330, sr, (int) sr, amp);
    run (e, x);
    p.loopMode = LoopMode::Play; p.density = 0.5f; e.setParams (p);
    std::vector<float> silence ((size_t) sr * 2, 0.0f);
    // drain grains first so only the loop is heard
    p.sizeMs = 10; e.setParams (p);
    auto o = run (e, silence);
    const float slope = (float) (2 * kPi * 330 / sr * amp);
    const size_t from = (size_t) (0.05 * sr);   // after the loop gain ramp
    float peak = 0;
    for (size_t i = from; i < o.l.size(); ++i) peak = std::max (peak, std::abs (o.l[i]));
    CHECK (peak > 0.25f, "loop playback too quiet (%.3f)", peak);
    CHECK (maxStep (o.l, from, o.l.size()) < slope * 2.5f, "loop seam clicks: step %.4f (sine slope %.4f)", maxStep (o.l, from, o.l.size()), slope);
}

static void testPitchChip()
{
    const double sr = 48000;
    Engine e; Params p; p.mix = 1; p.pitchMask = pitches ({ 12 }); p.density = 30; p.sizeMs = 120; p.spray = 0.1f; p.spread = 0;
    e.setParams (p); e.prepare (sr, 256);
    auto o = run (e, sine (440, sr, (int) sr * 2));
    const size_t from = (size_t) (0.5 * sr);
    const double up = goertzel (o.l, from, 880, sr), base = goertzel (o.l, from, 440, sr);
    CHECK (up > base * 10, "+12 chip: energy at 880 Hz %.3g vs 440 Hz %.3g", up, base);

    p.pitchMask = pitches ({ -12 }); e.setParams (p); e.reset();
    o = run (e, sine (440, sr, (int) sr * 2));
    CHECK (goertzel (o.l, from, 220, sr) > goertzel (o.l, from, 440, sr) * 10, "-12 chip should land at 220 Hz");

    // no chips enabled behaves like unison
    p.pitchMask = 0; e.setParams (p); e.reset();
    o = run (e, sine (440, sr, (int) sr * 2));
    CHECK (goertzel (o.l, from, 440, sr) > goertzel (o.l, from, 880, sr) * 10, "no chips: grains stay at the input pitch");
}

static void testFeedbackBounded()
{
    const double sr = 48000;
    Engine e; Params p; p.mix = 1; p.feedback = 0.95f; p.density = 64; p.sizeMs = 400; p.quality = Quality::High; p.spray = 0.2f;
    e.setParams (p); e.prepare (sr, 512);
    auto x = sine (110, sr, (int) sr * 20, 0.9f);
    auto o = run (e, x, 512);
    float peak = 0;
    for (float v : o.l) peak = std::max (peak, std::abs (v));
    CHECK (allFinite (o), "feedback produced NaN/inf");
    CHECK (peak < 4.0f, "feedback ran away: peak %.2f", peak);

    // overdub with decay 1 and full feedback stays bounded too
    p.loopMode = LoopMode::Overdub; p.decay = 1; p.loopMs = 500; e.setParams (p);
    o = run (e, x, 512);
    peak = 0;
    for (float v : o.l) peak = std::max (peak, std::abs (v));
    CHECK (allFinite (o) && peak < 6.0f, "overdub at decay 1 ran away: peak %.2f", peak);
}

static void testSync()
{
    const double sr = 48000;
    Engine e; Params p; p.sync = true; p.density = 4;   // 120 bpm: 4 grains/s snaps to 1/8 notes
    e.setParams (p); e.prepare (sr, 256);
    CHECK (Engine::nearestGrainDiv (4, 120) == 3, "4 grains/s at 120 bpm should snap to 1/8 (got %d)", Engine::nearestGrainDiv (4, 120));
    HostTime t; t.valid = true; t.playing = true; t.bpm = 120;
    std::vector<float> x ((size_t) sr * 2, 0.1f);
    const uint32_t before = e.getStatus().spawnCount.load();
    for (size_t i = 0; i < x.size(); i += 256)
    {
        t.ppq = (double) i / sr * 2.0 - 0.02;   // 2 beats per second; first grid line just after the start
        e.setHostTime (t);
        const float* in[] = { x.data() + i };
        float tmpL[256], tmpR[256];
        float* out[] = { tmpL, tmpR };
        e.process (in, 1, out, 2, 256);
    }
    const uint32_t spawned = e.getStatus().spawnCount.load() - before;
    CHECK (spawned == 8, "2 s of 1/8 notes at 120 bpm should spawn 8 grains, got %u", spawned);

    p.loopDiv = 4;   // 1 bar
    e.setParams (p);
    CHECK (std::abs (e.loopLengthSamples() - 2.0 * sr) < 2, "synced 1-bar loop at 120 bpm should be 2 s (%.0f)", e.loopLengthSamples());
}

static void testPresets()
{
    const auto& ps = factoryPresets();
    CHECK (ps.size() >= 28, "expected about 30 presets, got %d", (int) ps.size());
    const double sr = 44100;
    auto x = sine (196, sr, (int) sr);
    for (auto& pr : ps)
    {
        Engine e; e.setParams (pr.p); e.prepare (sr, 512);
        auto o = run (e, x, 512);
        CHECK (allFinite (o), "preset %s produced NaN/inf", pr.name);
        CHECK (pr.p.density >= 1 && pr.p.density <= 64 && pr.p.sizeMs >= 10 && pr.p.sizeMs <= 1000, "preset %s out of range", pr.name);
    }
}

int main()
{
    testNoAllocation();
    testVoiceCaps();
    testBufferWraps();
    testFreezeCrossfade();
    testLoopSeam();
    testPitchChip();
    testFeedbackBounded();
    testSync();
    testPresets();
    if (failures == 0) std::printf ("All engine tests passed\n");
    return failures == 0 ? 0 : 1;
}

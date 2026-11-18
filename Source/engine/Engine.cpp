#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace dm
{
namespace
{
bool isLoopMode (LoopMode m) { return m == LoopMode::Overdub || m == LoopMode::Play; }
constexpr int kMargin = 4;   // samples kept clear at region edges for the interpolator
}

Engine::Engine() { rng.seed (0x1234567u); }

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    bufLen = (int) std::ceil (sr * kBufferSeconds) + 8;
    bufL.assign ((size_t) bufLen, 0.0f);
    bufR.assign ((size_t) bufLen, 0.0f);
    ovBinSize = (bufLen + kOverviewBins - 1) / kOverviewBins;
    dryS.setTime (sr, 20);
    wetS.setTime (sr, 20);
    outS.setTime (sr, 20);
    loopGainS.setTime (sr, 15);
    toneL.setCutoff (sr, 800);
    toneR.setCutoff (sr, 800);
    fbL.setCutoff (sr, 5000);
    fbR.setCutoff (sr, 5000);
    status.bufferLen.store (bufLen);
    reset();
}

void Engine::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    wpos = 0;
    filled = 0;
    overdubPos = loopPos = 0;
    lastMode = params.loopMode;
    for (auto& g : grains) g.active = false;
    spawnCountdown = 0;
    internalPpq = 0;
    lastGridIndex = -1;
    writeGain.value = writeGain.target = params.freeze ? 0.0f : 1.0f;
    const float mixAngle = params.mix * (float) kPi * 0.5f;
    dryS.reset (std::cos (mixAngle));
    wetS.reset (std::sin (mixAngle));
    outS.reset (dbToGain (params.outputDb));
    loopGainS.reset (0);
    toneL.reset(); toneR.reset(); fbL.reset(); fbR.reset();
    fbSampleL = fbSampleR = 0;
    driftValue = driftTarget = 0;
    driftCountdown = 0;
    inPeak = outPeak = 0;
    ovCurBin = -1;
    ovAccum = 0;
    for (auto& b : status.overview) b.store (0);
}

int Engine::maxVoices() const
{
    switch (params.quality)
    {
        case Quality::Eco: return 16;
        case Quality::Normal: return 32;
        default: return kMaxGrains;
    }
}

int Engine::nearestGrainDiv (double grainsPerSecond, double tempo)
{
    const double beatsPerGrain = (tempo / 60.0) / std::max (0.01, grainsPerSecond);
    int best = 0;
    double bestErr = 1e9;
    for (int i = 0; i < (int) kGrainDivBeats.size(); ++i)
    {
        const double err = std::abs (std::log (kGrainDivBeats[(size_t) i] / beatsPerGrain));
        if (err < bestErr) { bestErr = err; best = i; }
    }
    return best;
}

double Engine::grainInterval() const
{
    if (params.sync)
        return kGrainDivBeats[(size_t) nearestGrainDiv (params.density, bpm)] * 60.0 / bpm * sr;
    return sr / std::clamp ((double) params.density, 0.5, 64.0);
}

double Engine::loopLengthSamples() const
{
    double ms = params.loopMs;
    if (params.sync)
        ms = kLoopDivBeats[(size_t) std::clamp (params.loopDiv, 0, (int) kLoopDivBeats.size() - 1)] * 60.0 / bpm * 1000.0;
    ms = std::clamp (ms, 20.0, kMaxLoopSeconds * 1000.0);
    return std::floor (ms * 0.001 * sr);
}

int Engine::activeGrainCount() const
{
    int n = 0;
    for (auto& g : grains) n += g.active ? 1 : 0;
    return n;
}

float Engine::bufferSample (int ch, int index) const
{
    const auto& b = ch == 0 ? bufL : bufR;
    return b[(size_t) wrap (index)];
}

float Engine::readLinear (const std::vector<float>& b, double absPos) const
{
    const double fl = std::floor (absPos);
    const int i = (int) fl;
    const float t = (float) (absPos - fl);
    const float a = b[(size_t) wrap (i)], c = b[(size_t) wrap (i + 1)];
    return a + t * (c - a);
}

float Engine::readCubic (const std::vector<float>& b, double absPos) const
{
    const double fl = std::floor (absPos);
    const int i = (int) fl;
    const float t = (float) (absPos - fl);
    return hermite (b[(size_t) wrap (i - 1)], b[(size_t) wrap (i)], b[(size_t) wrap (i + 1)], b[(size_t) wrap (i + 2)], t);
}

// Reads at a position inside a region that ends at regionEnd. Loop regions wrap, and the last
// few ms before the wrap are crossfaded with the audio just before the region start, so the
// seam is continuous.
float Engine::readRegion (const std::vector<float>& b, int regionEnd, double regionLen, double pos, bool circular, bool cubic) const
{
    const double start = (double) regionEnd - regionLen;
    if (! circular)
        return cubic ? readCubic (b, start + pos) : readLinear (b, start + pos);

    double p = std::fmod (pos, regionLen);
    if (p < 0) p += regionLen;
    const float v = cubic ? readCubic (b, start + p) : readLinear (b, start + p);
    const double xf = std::min (0.01 * sr, regionLen * 0.25);
    if (p <= regionLen - xf) return v;
    const float f = (float) ((p - (regionLen - xf)) / xf);
    const float v2 = cubic ? readCubic (b, start + p - regionLen) : readLinear (b, start + p - regionLen);
    const float a = f * (float) kPi * 0.5f;
    return v * std::cos (a) + v2 * std::sin (a);
}

void Engine::writeSample (int index, float l, float r)
{
    index = wrap (index);
    bufL[(size_t) index] = l;
    bufR[(size_t) index] = r;
    const int bin = std::min (index / ovBinSize, kOverviewBins - 1);
    if (bin != ovCurBin)
    {
        if (ovCurBin >= 0) status.overview[(size_t) ovCurBin].store (ovAccum, std::memory_order_relaxed);
        ovCurBin = bin;
        ovAccum = 0;
    }
    ovAccum = std::max (ovAccum, std::max (std::abs (l), std::abs (r)));
}

void Engine::spawnGrain()
{
    const int voices = maxVoices();
    Grain* g = nullptr;
    for (int i = 0; i < voices; ++i)
        if (! grains[(size_t) i].active) { g = &grains[(size_t) i]; break; }
    if (g == nullptr) return;

    const bool loop = isLoopMode (params.loopMode);
    double regionLen;
    if (loop) regionLen = loopLengthSamples();
    else
    {
        const double want = params.loopMode == LoopMode::Record ? loopLengthSamples() : kScanSeconds * sr;
        regionLen = std::min (want, (double) filled);
    }
    if (regionLen < 64) return;

    // pitch
    int enabled[kNumPitches];
    int n = 0, home = -1;
    for (int i = 0; i < kNumPitches; ++i)
        if (params.pitchMask & (1u << i))
        {
            enabled[n++] = i;
            if (home < 0 || std::abs (kPitchSemis[(size_t) i]) < std::abs (kPitchSemis[(size_t) home])) home = i;
        }
    float semis = 0;
    if (n > 0)
    {
        int pick = home;
        if (rng.uni() < params.pitchRandom) pick = enabled[std::min (n - 1, (int) (rng.uni() * (float) n))];
        semis = (float) kPitchSemis[(size_t) pick];
    }
    semis += params.detuneCents * 0.01f * rng.bi();

    const double rate = std::clamp ((double) params.speed, 0.25, 2.0) * std::pow (2.0, semis / 12.0);
    const bool reversed = rng.uni() < params.reverse;

    int length = std::max (16, (int) (std::clamp ((double) params.sizeMs, 10.0, 1000.0) * 0.001 * sr));
    const double c = std::clamp ((double) (params.scan + driftValue * params.drift * 0.5f + params.spray * 0.5f * rng.bi()), 0.0, 1.0);

    double pos;
    if (loop)
    {
        pos = regionLen * (1.0 - c);
    }
    else
    {
        const double usable = regionLen - 2 * kMargin;
        double traj = length * rate;
        if (traj > usable * 0.9)
        {
            length = std::max (16, (int) (usable * 0.9 / rate));
            traj = length * rate;
        }
        const double span = std::max (0.0, usable - traj);
        const double delay = reversed ? kMargin + c * span : kMargin + traj + c * span;
        pos = regionLen - delay;
    }

    const double interval = grainInterval();
    // Overlapping grains of the same material add partly coherently, so normalise a bit harder
    // than 1/sqrt(n). The overlap can never exceed the voice cap.
    const double overlap = std::min ((double) length / std::max (1.0, interval), (double) voices);
    const float norm = (float) (1.0 / std::pow (std::max (1.0, overlap * 0.5), 0.7));
    const float pan = params.spread * rng.bi();
    const float pa = (pan + 1.0f) * (float) kPi * 0.25f;

    g->active = true;
    g->circular = loop;
    g->regionEnd = wpos;
    g->regionLen = regionLen;
    g->pos = pos;
    g->inc = reversed ? -rate : rate;
    g->length = length;
    g->age = 0;
    g->alpha = 1.0f - 0.9f * std::clamp (params.shape, 0.0f, 1.0f);
    g->gainL = std::cos (pa) * 1.41421356f * norm;
    g->gainR = std::sin (pa) * 1.41421356f * norm;
    g->mono = std::abs (pan);
    g->semis = semis;
    g->amp = 0;
    g->reversed = reversed;
    status.spawnCount.fetch_add (1, std::memory_order_relaxed);
}

void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
{
    if (numSamples <= 0 || bufLen <= 1) return;

    // ---- block setup ----
    bpm = host.valid && host.bpm > 1 ? host.bpm : 120.0;
    const double spb = sr * 60.0 / bpm;
    double ppq = host.valid && host.playing ? host.ppq : internalPpq;

    const LoopMode mode = params.loopMode;
    if (mode != lastMode)
    {
        if (isLoopMode (mode) && ! isLoopMode (lastMode)) { overdubPos = 0; loopPos = 0; }
        lastMode = mode;
    }
    const bool loop = isLoopMode (mode);
    const double L = loopLengthSamples();
    if (loop)
    {
        overdubPos = std::fmod (overdubPos, L);
        loopPos = std::fmod (loopPos, L);
        if (loopPos < 0) loopPos += L;
    }

    writeGain.set (params.freeze ? 0.0f : 1.0f, sr, std::clamp ((double) params.freezeMs, 1.0, 5000.0));
    loopGainS.target = loop ? std::clamp (params.loopLevel, 0.0f, 1.0f) : 0.0f;
    const float mixAngle = std::clamp (params.mix, 0.0f, 1.0f) * (float) kPi * 0.5f;
    dryS.target = std::cos (mixAngle);
    wetS.target = std::sin (mixAngle);
    outS.target = dbToGain (params.outputDb);
    {
        const float t = std::clamp (params.tone, -1.0f, 1.0f);
        lowGain = std::pow (10.0f, -t * 9.0f / 20.0f);
        highGain = std::pow (10.0f, t * 9.0f / 20.0f);
    }
    const float fbAmt = std::clamp (params.feedback, 0.0f, 0.95f);
    const float decay = std::clamp (params.decay, 0.0f, 1.0f);
    const double speed = std::clamp ((double) params.speed, 0.25, 2.0);
    const bool cubic = params.quality != Quality::Eco;

    // drift: slow random walk of the scan position
    driftCountdown -= numSamples;
    if (driftCountdown <= 0)
    {
        driftTarget = rng.bi();
        driftCountdown = (int) (sr * (1.0 + 2.0 * rng.uni()));
    }
    driftValue += (driftTarget - driftValue) * (float) (1.0 - std::exp (-numSamples / (sr * 0.8)));

    const double interval = grainInterval();
    const double divBeats = kGrainDivBeats[(size_t) nearestGrainDiv (params.density, bpm)];
    const int voices = maxVoices();

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = numIn > 0 ? in[0][i] : 0.0f;
        const float inR = numIn > 1 ? in[1][i] : inL;
        inPeak = std::max (inPeak, std::max (std::abs (inL), std::abs (inR)));

        // ---- schedule ----
        if (params.sync)
        {
            const long long grid = (long long) std::floor ((ppq + i / spb) / divBeats + 1e-9);
            if (grid != lastGridIndex) { lastGridIndex = grid; spawnGrain(); }
        }
        else if (--spawnCountdown <= 0)
        {
            spawnGrain();
            spawnCountdown += std::max (1.0, interval * (1.0 + params.spray * 0.5 * rng.bi()));
            if (spawnCountdown < 1) spawnCountdown = 1;
        }

        // ---- grains ----
        float gl = 0, gr = 0;
        for (int v = 0; v < voices; ++v)
        {
            auto& g = grains[(size_t) v];
            if (! g.active) continue;
            const float w = window.at ((float) g.age / (float) g.length, g.alpha);
            const float sL = readRegion (bufL, g.regionEnd, g.regionLen, g.pos, g.circular, cubic);
            const float sR = readRegion (bufR, g.regionEnd, g.regionLen, g.pos, g.circular, cubic);
            const float m = 0.5f * (sL + sR);
            gl += (sL + (m - sL) * g.mono) * w * g.gainL;
            gr += (sR + (m - sR) * g.mono) * w * g.gainR;
            g.amp = w;
            g.pos += g.inc;
            if (++g.age >= g.length) g.active = false;
        }
        // grains beyond the voice cap (after a Quality change) fade by finishing normally
        for (int v = voices; v < kMaxGrains; ++v)
            grains[(size_t) v].active = false;

        // ---- loop playback ----
        const float lg = loopGainS.next();
        float wetL = gl, wetR = gr;
        if (lg > 1.0e-5f && L >= 64)
        {
            wetL += lg * readRegion (bufL, wpos, L, loopPos, true, true);
            wetR += lg * readRegion (bufR, wpos, L, loopPos, true, true);
        }
        if (loop)
        {
            loopPos += speed;
            if (loopPos >= L) loopPos -= L;
        }

        // ---- write ----
        const float wg = writeGain.next();
        const float fL = softLimit (fbL.process (fbSampleL) * fbAmt);
        const float fR = softLimit (fbR.process (fbSampleR) * fbAmt);
        if (! loop)
        {
            if (wg > 0.0f || writeGain.target > 0.0f)
            {
                writeSample (wpos, softLimit ((inL + fL) * wg), softLimit ((inR + fR) * wg));
                wpos = wrap (wpos + 1);
                if (filled < bufLen) ++filled;
            }
        }
        else if (mode == LoopMode::Overdub && wg > 0.0f)
        {
            const int idx = wrap (wpos - (int) L + (int) overdubPos);
            const float keep = 1.0f - wg * (1.0f - decay);
            writeSample (idx, softLimit (bufL[(size_t) idx] * keep + wg * (inL + fL)),
                              softLimit (bufR[(size_t) idx] * keep + wg * (inR + fR)));
        }
        if (loop)
        {
            overdubPos += 1.0;
            if (overdubPos >= L) overdubPos -= L;
        }
        fbSampleL = wetL;
        fbSampleR = wetR;

        // ---- tone, mix, output ----
        wetL = softLimit (wetL, 1.0f, 2.0f);
        wetR = softLimit (wetR, 1.0f, 2.0f);
        const float lpL = toneL.process (wetL), lpR = toneR.process (wetR);
        wetL = lpL * lowGain + (wetL - lpL) * highGain;
        wetR = lpR * lowGain + (wetR - lpR) * highGain;
        const float d = dryS.next(), w = wetS.next(), o = outS.next();
        const float oL = (inL * d + wetL * w) * o;
        const float oR = (inR * d + wetR * w) * o;
        outPeak = std::max (outPeak, std::max (std::abs (oL), std::abs (oR)));
        if (numOut > 0) out[0][i] = oL;
        if (numOut > 1) out[1][i] = oR;
    }
    for (int c = 2; c < numOut; ++c)
        std::fill (out[c], out[c] + numSamples, 0.0f);

    internalPpq = ppq + numSamples / spb;
    publishStatus();
}

void Engine::publishStatus()
{
    const bool loop = isLoopMode (params.loopMode);
    double shown;
    if (loop) shown = loopLengthSamples();
    else shown = std::max (1.0, std::min (params.loopMode == LoopMode::Record ? loopLengthSamples() : kScanSeconds * sr, (double) bufLen - 16));

    int count = 0;
    for (int v = 0; v < kMaxGrains; ++v)
    {
        const auto& g = grains[(size_t) v];
        auto& view = status.grains[(size_t) v];
        view.active.store (g.active, std::memory_order_relaxed);
        if (! g.active) continue;
        ++count;
        double x;
        if (g.circular)
        {
            double p = std::fmod (g.pos, g.regionLen);
            if (p < 0) p += g.regionLen;
            x = p / g.regionLen;
        }
        else
        {
            const int moved = wrap (wpos - g.regionEnd);
            const double delay = moved + (g.regionLen - g.pos);
            x = 1.0 - delay / shown;
        }
        view.x.store ((float) std::clamp (x, 0.0, 1.0), std::memory_order_relaxed);
        view.semis.store (g.semis, std::memory_order_relaxed);
        view.amp.store (g.amp, std::memory_order_relaxed);   // window level 0..1
        view.pan.store ((g.gainR - g.gainL) / std::max (1.0e-6f, g.gainL + g.gainR), std::memory_order_relaxed);
        view.reversed.store (g.reversed, std::memory_order_relaxed);
    }
    if (ovCurBin >= 0) status.overview[(size_t) ovCurBin].store (ovAccum, std::memory_order_relaxed);
    status.activeGrains.store (count);
    status.writePos.store (wpos);
    status.regionLen.store ((int) shown);
    status.filled.store (filled);
    const float scanX = (float) std::clamp (params.scan + driftValue * params.drift * 0.5f, 0.0f, 1.0f);
    status.scanPos.store (1.0f - scanX);
    status.sprayWidth.store (params.spray);
    status.loopPhase.store (loop ? (float) (loopPos / std::max (1.0, loopLengthSamples())) : 0.0f);
    status.writeGain.store (writeGain.value);
    status.frozen.store (params.freeze);
    status.loopMode.store ((int) params.loopMode);
    status.inLevel.store (inPeak);
    status.outLevel.store (outPeak);
    inPeak = outPeak = 0;
}
} // namespace dm

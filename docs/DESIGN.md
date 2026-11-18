# Dust Mote: granular micro-looper & cloud generator (design, v0.1)

Stack: JUCE 8.0.4, C++17, CMake, VST3 + AU (macOS) + Standalone, same layout as Comma and
Driftglass (JUCE-free engine with its own tests, thin plugin wrapper). Scalar DSP only, so it
runs on macOS 11 Intel machines without AVX2. Family: psychedelic and shoegaze; pairs with
driftglass, penumbra, emerald-haze and fretwire.

## Signal flow

```
input -+-------------------------------------------------------------- dry -+
       |                                                                    |
       +-> write head (Freeze ramps it out) -> 16 s stereo buffer           |-> mix -> output
                ^                                  |                        |
                |                    grains (up to 64) + loop playhead      |
                |                                  |                        |
                +-- feedback (5 kHz low-pass, soft limit) <-- wet -> tone --+
```

- **Buffer:** stereo, 16 s at the session rate (about 12 MB at 96 kHz), allocated in
  `prepareToPlay`, never on the audio thread. A test counts heap allocations inside `process()`
  and fails if there are any.
- **Grains:** a fixed pool of 64 voices. Each grain has its own read position, rate (from the
  pitch set, detune and Speed), direction and window (Hann morphing to a flat Tukey with Shape).
  Hermite (cubic) interpolation. Grains are placed so their whole path stays inside recorded
  audio, so they never read across the write head.
- **Quality** caps the pool: Eco 16 grains with linear interpolation, Normal 32, High 64.
  Measured at 48 kHz with every grain at 1 s, all pitches: Eco 1.8%, Normal 7%, High 12% of one
  core (Linux, x86-64, one thread).
- **Level:** each grain is scaled by the expected overlap (density x size, capped at the voice
  count), and the wet sum goes through a soft limiter so dense clouds stay in range.

## Controls

| Section | Controls |
|---|---|
| Grains | Density (1 to 64 grains/s), Size (10 ms to 1 s), Spray (position and timing jitter), Scan (0 = what you just played, 100 = oldest audio in the region), Shape (Hann to Tukey) |
| Pitch set | Chips: -12, -7, -5, 0, +5, +7, +12, +19, +24. Random: 0 = every grain plays the enabled pitch nearest 0, higher = more grains pick a random enabled pitch. Detune: random cents per grain |
| Motion | Reverse probability, Stereo spread, Drift (slow random wander of Scan), Speed (0.25x to 2x, for grains and the loop) |
| Micro-loop | Mode Off / Rec / Dub / Play, Length (free 20 ms to 8 s, or a note value with Sync), Decay, Loop level |
| Freeze | Latch, crossfade time (5 ms to 2 s), MIDI Off / Toggle / Hold, note (any or one note) |
| Global | Feedback, Tone (tilt), Mix, Output, Sync, Quality |

## Micro-looper

- **Off:** rolling capture. Grains come from the last 8 seconds.
- **Rec:** rolling capture, but grains stay inside the last loop length, ready to be caught.
- **Dub:** the write head stops and the last loop length becomes the loop. It plays
  continuously and new input is layered in, with Decay as the feedback on each pass.
- **Play:** the loop plays and nothing new is recorded.

The loop is anchored at its end (the moment you left Rec or Off), so changing Length always
takes audio that was really recorded. The wrap point is crossfaded (10 ms, equal power) with the
audio just before the loop start, so loops of any length are click-free. A test checks this with
a sine whose period does not divide the loop.

## Freeze

Freeze ramps the write gain to zero over the crossfade time; once silent, the write head stops
and the buffer is a static source for the grains (and the loop, in Dub). Release ramps it back
in from the same point. Because the recorded audio tapers to silence and resumes from silence,
there is no step in the buffer. Tests check the taper and the resume are no steeper than the
input itself, and that nothing is written while frozen.

MIDI: **Toggle** flips Freeze on each matching note-on; **Hold** freezes while a matching note is
held. The latch shows the combined state, and clicking it always flips what you hear.

## Sync

With Sync on, Density snaps to the nearest note value (1/1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T,
1/32, 1/64) at the host tempo and grains fire on the host's beat grid; the loop length is a note
value from 1/16 to 4 bars. Without a playing host it runs from an internal clock at the host's
tempo (or 120 bpm).

## Feedback

Grains (and the loop) can be written back into the buffer through a 5 kHz one-pole low-pass and a
soft limiter. Buffer writes are also soft-limited, so even Dub at Decay 100% with full feedback
stays bounded (tested over 20 s).

## UI

A live **dust view**: each grain is a particle whose height is its pitch (-12 at the bottom, +24
at the top) and brightness its window level, with fading trails, floating over the waveform of
the region grains draw from (newest on the right). It shows the scan position and spray window,
the write head (dims as Freeze fades it out) or the loop playhead. The Freeze button is a big
glowing latch with a bar showing the crossfade. Four themes: Lantern, Moth (light), Frost, Neon
Dust; each changes the knob style too. The theme is saved with the session.

## Presets (31)

Clouds: Cloud Chamber, Fog Bank, Dust Storm, Slow Motes, Ambient Wall, Halo Thickener.
Pitch: Octave Dust, Shimmer Mist, Power Chord Cloud, Sub Swell, Fifths & Fourths, Chorus Haze,
Glass Sparkle. Motion: Reverse Rain, Backwards Bloom, Half-Speed Ghost, Wandering Scan,
Double-Time Flutter. Loops: Micro Stutter, Eighth-Note Chop, Overdub Haze, Tape Shards,
One-Bar Looper, Beat Repeat Octaves. Freeze: Drone Bed (Freeze on), Frozen Chord Pad, Frozen
Shimmer, Deep Freeze. Glitch: Glitch Halo, Bit Swarm, Sparse Blips.

Drone Bed loads with Freeze latched, so it plays whatever is already in the buffer. To capture a
new chord, release the latch, play, and latch again.

## Decisions

- One buffer serves the cloud, the micro-loop and Freeze, rather than a separate loop buffer.
  That keeps memory at 12 MB and makes Freeze and the loop behave the same way.
- Loop level was added (not in the plan) so the loop and the grain cloud can be balanced.
- Described by function only; no hardware unit's names or layout are used.

## Not in v0.1

- Resizable UI, user preset save/load.
- Tempo-synced Scan/Drift LFO, per-grain filter, MIDI-played pitch set.

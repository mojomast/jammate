# Adaptive Guitar Jam Companion — Product & Technical Specification

**Document:** SPEC.md  
**Status:** Draft v0.1  
**Research date:** 2026-10-07  
**Target:** Windows-first desktop MVP, architecture kept portable  
**Primary implementation candidate:** `raphaelfukuda/Guitar-Companion` at `88f7e7c805c9c5e17388154a678c2c6a3633ff23`  
**License assumption:** An AGPLv3/open-source product is acceptable. If closed-source distribution is required, stop before forking and replace the base architecture.

---

## 1. Product definition

Build a low-latency practice and creative-jamming application for guitarists.

The player connects a guitar through an audio interface, line input, or microphone. The application monitors the guitar to headphones with minimal latency while a virtual drummer listens to the performance and joins in. The drummer must behave like a musical partner rather than a backing-track player: it should establish a pulse, hold a stable groove, follow intentional tempo changes, react to dynamics, choose musically sensible fills, and recover gracefully from silence, syncopation, mistakes, and restarts.

The first production target is **an excellent adaptive drummer**. The architecture must allow bass, keys, rhythm guitar, or other accompaniment voices to be added later without changing the real-time audio core.

The application must work fully offline for the MVP. No cloud model, LLM, or network service may be required for playing.

---

## 2. Research-backed implementation decision

### 2.1 Primary base: Guitar-Companion

Use `raphaelfukuda/Guitar-Companion` as the first fork candidate rather than beginning from a blank JUCE application or reshaping a general-purpose DAW.

At the researched revision, Guitar-Companion already provides:

- JUCE 8-based standalone and VST3 targets.
- Real guitar input and low-latency audio-device support.
- WASAPI support and optional ASIO.
- A guitar DSP chain and Neural Amp Modeler integration.
- A separate drum bus.
- A sample-accurate drum sequencer.
- An internal drum sampler and support for hosted drum VST3 instruments.
- Hundreds of existing grooves/fills and procedural groove generation.
- Swing and velocity/timing/round-robin humanization.
- Song/scene concepts.
- Recording of guitar, drum, and mix stems.
- Audio-device UI.
- Existing real-guitar testing on 48 kHz / 128-sample ASIO hardware.
- An explicit project rule against allocation, blocking, locks, network, and I/O in `processBlock`.

This is much closer to the desired product than Giada or Hydrogen alone.

**Important:** this is a beta codebase, not a trusted foundation. Its own July 2026 audit identified previous real-time safety and test-coverage concerns. The current revision has improved its model-retirement path, but inspection still found `triggerAsyncUpdate()` reachable from `processBlock`; current JUCE documentation explicitly warns that `AsyncUpdater::triggerAsyncUpdate()` may block when called from a real-time thread. Therefore all feature work is gated behind a stabilization and callback-reachability audit.

### 2.2 Alternatives and references

| Project | Role in this project | Decision |
|---|---|---|
| Guitar-Companion | Application/audio/drum foundation | **Primary fork candidate** |
| Giada 1.6.0 | Mature live-loop/audio-engine reference | Fallback if Guitar-Companion fails stabilization/licensing gate |
| BTrack 1.0.7 | C++ causal real-time beat tracker | **Primary algorithm baseline; must earn selection in guitar-specific benchmark** |
| aubio | C/C++ onset, tempo, beat and pitch analysis | Benchmark/secondary backend |
| BeatNet | Neural online beat/downbeat/tempo/meter tracker | Research benchmark only until latency, robustness, packaging, and guitar-specific accuracy justify shipping |
| Hydrogen | Drum workflow, pattern, kit and humanization reference | Reference/content source where licensing permits; no runtime dependency required |
| JJazzLab 5.2.1 / Toolkit | Generated bass/keys/guitar from chord/rhythm information | Phase-2 full-band candidate |
| Spotify Basic Pitch | Polyphonic transcription/key/chord research | Phase-2 analysis candidate, never on the hard real-time audio thread |

### 2.3 2026 accompaniment design principle

The drummer must **not directly chase the output of a beat detector**.

Recent live-accompaniment research reinforces separating rhythmic perception from the stable rhythmic reference used by the accompaniment engine. The architecture therefore contains a first-class **Musical Clock / Entrainment Engine** that receives noisy beat observations and emits a stable tempo, beat phase, bar phase, meter, and confidence state.

The tracker supplies evidence.  
The Musical Clock decides what the band believes.  
The drummer follows the Musical Clock.

This separation is mandatory.

---

## 3. Product principles

1. **The guitar path wins every deadline.** Analysis and accompaniment intelligence may degrade or drop data; monitored guitar audio must not glitch because analysis is late.
2. **Stable is more musical than reactive.** One odd strum must not jerk the drummer to a new tempo.
3. **Confidence is a first-class value.** Every inference used to change performance behavior must have a confidence state and a fallback.
4. **Musical changes happen at musical boundaries.** Groove changes, fills, scene changes, and most tempo corrections are quantized to beats/bars unless explicitly resynchronizing.
5. **Deterministic before generative.** The MVP uses deterministic rules and existing grooves. Neural/generated accompaniment is future work.
6. **User correction is cheap.** Tap, half-time, double-time, resync, and Fixed/Follow controls must be immediate.
7. **No hidden cloud dependency.** Plug in and jam offline.
8. **No giant refactor before proof.** Introduce narrow new modules around the existing engine. Move old code only when required by a tested seam.
9. **All real-time contracts are testable.** “Lock-free” is not enough; operations reachable from the callback must be bounded.
10. **Future instruments share one clock.** Drums, bass, keys, and any future model must consume the same Musical Clock rather than independently estimating time.

---

## 4. MVP scope

### 4.1 Required

- Standalone Windows application.
- Guitar input from line input, instrument interface, or microphone.
- Headphone/output monitoring.
- ASIO when installed; WASAPI fallback.
- 44.1 kHz and 48 kHz support; 48 kHz is the preferred reference configuration.
- User-selectable input/output device, channels, sample rate, and buffer size where the backend exposes them.
- Manual/FIXED tempo mode.
- Count-in mode.
- Free Jam / FOLLOW mode.
- LOOSE FOLLOW mode.
- Tap tempo.
- Explicit RESYNC control.
- Half-time / double-time correction.
- Style selection.
- Intensity control.
- Complexity control.
- Fill-frequency control.
- Swing/feel control where style supports it.
- Drummer dynamics reacting to guitar energy.
- Drummer auto-join after sufficient rhythmic confidence.
- Stable holdover through short silences.
- Graceful loss/reacquisition after longer silence.
- Existing drum sample engine plus optional hosted drum VST3.
- Session/preset persistence.
- Guitar/drum/mix recording.
- Diagnostics for input level, detected BPM, clock BPM, confidence, analysis overruns, callback load, and estimated/reported device latency.

### 4.2 Explicit MVP non-goals

- Automatic chord recognition.
- Bass/keys/rhythm-guitar generation.
- Neural audio generation.
- LLM-controlled playing.
- Automatic song-form inference.
- Fully automatic arbitrary-meter detection.
- Mobile release.
- Network collaboration.
- Cloud preset/store dependency.
- “Perfect” transcription of guitar.
- Replacing the existing amp/effects subsystem.

These may be added after the drummer passes the musical-quality gates.

---

## 5. User-facing jam modes

### 5.1 Fixed

The drummer plays at the user-selected BPM and does not follow guitar timing.

Useful for strict practice and as the baseline mode for debugging.

### 5.2 Count Me In

The user taps or plays a configurable count-in, normally 4 or 8 beats.

The system establishes initial tempo/phase before drums start. This is the most deterministic adaptive mode and should ship before Free Jam if necessary.

### 5.3 Follow

The system listens continuously and follows meaningful tempo drift while rejecting isolated rhythmic deviations.

Use a moderately responsive Musical Clock.

### 5.4 Loose Follow

The drummer behaves as the rhythm anchor.

It accepts sustained tempo change but resists short-term push/pull, syncopation, fills, missed attacks, and expressive timing.

This should become the default once tuned.

### 5.5 Free Jam

The user simply begins playing.

The system:

1. remains silent while acquiring pulse;
2. exposes confidence/BPM visually;
3. establishes a stable clock;
4. predicts a musically safe join boundary;
5. optionally provides a subtle count cue;
6. enters with drums on a downbeat;
7. transitions into Follow or Loose Follow behavior.

### 5.6 Manual correction controls

Always available:

- Tap tempo
- Resync next beat
- Resync next bar
- Half-time
- Double-time
- Freeze tempo
- Resume follow

These controls are not failure modes; they are intentional human-in-the-loop features.

---

## 6. High-level architecture

```text
 AUDIO DEVICE / GUITAR
          |
          v
 +---------------------+
 | JUCE audio callback |
 +---------------------+
       |          |
       |          +--------------------------------------+
       |                                                 |
       v                                                 v
 Guitar monitoring/DSP                         AnalysisTap (bounded copy)
       |                                                 |
       |                                      fixed-capacity SPSC ring
       |                                                 |
       |                                                 v
       |                                       RhythmAnalyzer thread
       |                                  +---------------------------+
       |                                  | tracker backend(s)        |
       |                                  | onset/energy features     |
       |                                  +---------------------------+
       |                                                 |
       |                                      RhythmObservation
       |                                                 |
       |                                                 v
       |                                       +------------------+
       |                                       | Musical Clock    |
       |                                       | / Entrainment    |
       |                                       +------------------+
       |                                                 |
       |                                           ClockSnapshot
       |                                                 |
       |                                                 v
       |                                       +------------------+
       |                                       | Jam Director     |
       |                                       +------------------+
       |                                          |           |
       |                                   style state     dynamics
       |                                          |           |
       |                                          v           v
       |                                      Drum Adapter / Scheduler
       |                                                 |
       |                                                 v
       +-------------------------------------------> DrumEngine
                                                         |
                                                         v
                                                   master mix
                                                         |
                                                         v
                                                    headphones
```

Future accompaniment voices consume the same `ClockSnapshot` and `JamIntent`.

---

## 7. Thread model

### 7.1 Audio thread

Responsibilities:

- Read input/output buffers.
- Run the existing guitar DSP.
- Make a bounded copy of the chosen analysis tap into a preallocated queue.
- Render/schedule drums.
- Apply already-prepared control snapshots.
- Publish bounded atomic telemetry.

Forbidden:

- Heap allocation/deallocation.
- Blocking lock/mutex.
- File or network I/O.
- Logging/console output.
- JSON/XML parsing.
- Device enumeration.
- Plugin scanning/loading.
- Waiting for analysis.
- Waiting for UI.
- Waiting for background threads.
- System-message posting.
- `juce::AsyncUpdater::triggerAsyncUpdate()` or equivalent callback-reachable message posting.
- Unbounded retry/spin loops.
- Runtime model loading.
- Any new third-party call not audited for bounded real-time behavior.

### 7.2 Rhythm-analysis worker

Responsibilities:

- Drain the SPSC audio-analysis queue.
- Resample/downmix if required.
- Compute onset/energy features.
- Run one or more beat-tracker backends.
- Publish `RhythmObservation`.
- Never block the audio thread.

### 7.3 Musical-clock worker/control thread

Can be the rhythm-analysis thread initially if deterministic and cheap.

Responsibilities:

- Fuse observations.
- Resolve half/double-time ambiguity.
- Smooth tempo.
- Maintain beat/bar phase.
- Enter Acquiring / Locked / Holdover / Lost states.
- Publish immutable/latest-value `ClockSnapshot`.

### 7.4 Message/UI thread

Responsibilities:

- UI.
- Presets.
- File operations.
- Style/catalog loading.
- Plugin loading.
- Session state.
- Background task control.
- Draining diagnostic snapshots.

---

## 8. Real-time exchange contracts

### 8.1 Analysis audio queue

A fixed-capacity single-producer/single-consumer structure.

Requirements:

- Storage fully allocated in `prepareToPlay`.
- Producer operation is bounded.
- Consumer operation is bounded.
- No callback allocation.
- Explicit overrun counter.
- No waiting.

Initial policy: if the queue is full, the callback **drops the incoming analysis block, increments `analysisOverrunCount`, and continues audio immediately**.

The analysis consumer must drain faster than real time under nominal load. The queue is a safety buffer, not a latency reservoir.

### 8.2 Latest snapshots

Coalescible state such as:

- latest `RhythmObservation`
- latest `ClockSnapshot`
- input energy
- confidence
- diagnostics

should use a bounded generation-counter/double-buffer pattern or atomics where the data is naturally atomic.

The callback must never wait for a coherent snapshot. If a new snapshot is unavailable it uses the previous valid one.

### 8.3 Commands

Non-coalescible commands such as:

- start
- stop
- resync
- next style
- explicit fill request

use a fixed-capacity SPSC command queue with an explicit full policy.

---

## 9. Core interfaces

The exact C++ spelling may evolve, but these semantic contracts are frozen for the MVP.

### 9.1 RhythmObservation

```cpp
struct RhythmObservation
{
    uint64_t inputSampleTime;
    double sourceSampleRate;

    float bpmCandidate;
    float beatPhase01;
    float beatConfidence01;

    float onsetStrength01;
    float energyRmsDbfs;
    float transientDensity01;

    bool beatEvent;
    bool silence;
};
```

A tracker backend does **not** directly change drum tempo.

### 9.2 Tracker backend

```cpp
class IRhythmTracker
{
public:
    virtual ~IRhythmTracker() = default;
    virtual void reset(double sampleRate) = 0;
    virtual RhythmObservation process(const AnalysisFrame&) = 0;
    virtual const char* id() const noexcept = 0;
};
```

Initial candidates:

- `BTrackBackend`
- `AubioBackend`
- optional non-shipping `BeatNetBridge` used by the evaluation harness

Selection is based on recorded evidence, not preference.

### 9.3 Musical clock

```cpp
enum class ClockLockState
{
    Acquiring,
    Locked,
    Holdover,
    Lost
};

struct ClockSnapshot
{
    uint64_t generation;

    double bpm;
    double beatPhase01;
    double barPhase01;

    int beatInBar;
    int beatsPerBar;
    int beatUnit;

    float confidence01;
    ClockLockState lockState;
    bool tempoFrozen;
};
```

### 9.4 Jam intent

```cpp
struct JamIntent
{
    float intensity01;
    float complexity01;
    float fillAmount01;
    float swing01;

    bool requestFill;
    bool requestBreak;
    bool requestCrash;
    bool requestStop;

    int sectionIndex;
};
```

### 9.5 Accompaniment provider

Defined in the MVP even though only drums implement it.

```cpp
class IAccompanimentProvider
{
public:
    virtual ~IAccompanimentProvider() = default;
    virtual void prepare(const PrepareContext&) = 0;
    virtual void applyClock(const ClockSnapshot&) = 0;
    virtual void applyIntent(const JamIntent&) = 0;
    virtual void render(const RenderContext&) = 0;
};
```

Future bass/keys implementations must not create independent clocks.

---

## 10. Musical Clock / Entrainment Engine

### 10.1 State machine

#### Acquiring

- Collect candidate tempo and phase.
- Track confidence history.
- Maintain top tempo hypotheses including likely half/double alternatives.
- Do not start drums until the lock policy passes.

#### Locked

- Publish a stable tempo and beat phase.
- Treat tracker values as observations, not commands.
- Correct phase gradually.
- Limit tempo slew according to Follow mode.
- Reject isolated large jumps.

#### Holdover

Entered when rhythmic evidence becomes weak or silent.

- Continue the established internal clock.
- Stop making aggressive tempo corrections.
- Keep drums stable for a configurable musical interval.
- Reduce confidence over time.
- Avoid filling aggressively while confidence is low.

#### Lost

- Stop trusting phase.
- Either keep a user-frozen drummer or gracefully stop at a musical boundary according to mode.
- Return to Acquiring.

### 10.2 Initial tunable policies

These are defaults to be tuned by the evaluation harness, not magic constants:

- supported auto-follow range: 50–220 BPM
- confidence lock threshold: approximately 0.65
- acquire stability window: approximately 1–2 bars
- half/double candidates explicitly modeled
- sudden single-observation changes above approximately 10–12% rejected unless reinforced
- short silence: holdover
- long silence: Lost
- explicit Tap/Resync overrides normal slew limits

All thresholds must live in one configuration structure and be covered by tests.

### 10.3 Tempo modes

**Fixed:** slew = zero.  
**Follow:** moderate allowable tempo slew.  
**Loose:** lower allowable tempo slew and stronger phase damping.

Never write raw detector BPM directly into `DrumEngine::bpm` every callback.

---

## 11. Input analysis

### 11.1 Tap point

Default analysis tap is:

**post input-gain, pre gate/amp/effects**

Rationale:

- preserves pick/strum transients;
- avoids amp-model latency;
- avoids delay/reverb creating false onsets;
- avoids the gate erasing weak rhythmic information.

Allow an advanced debug option to compare pre/post gate later.

### 11.2 Features

MVP analysis produces at minimum:

- RMS/energy envelope
- onset/transient strength
- beat candidate
- tempo candidate
- phase candidate
- confidence
- silence state

Future:

- downbeat probability
- meter probability
- spectral density
- pitch/chord evidence
- phrase cues

---

## 12. Beat-tracker selection process

No tracker is chosen permanently until tested on guitar-specific material.

### 12.1 Required candidates

1. BTrack
2. aubio
3. BeatNet as a research benchmark if practical

### 12.2 Evaluation corpus

Use owned/created or clearly redistributable audio only.

Required cases:

- clean eighth-note strumming
- clean sixteenth-note strumming
- distorted power-chord riff
- palm-muted metal riff
- blues shuffle
- sparse single-note riff
- syncopated funk
- arpeggios
- sustained chords
- intentionally missing downbeats
- stop/start
- accelerando
- ritardando
- 3/4
- 6/8
- noisy microphone capture
- line input with low level
- line input with clipping
- tapping/muting only

Every clip receives ground-truth beat timestamps and nominal/local BPM where practical.

### 12.3 Metrics

- acquisition time in beats/bars
- BPM relative error
- beat-event F-measure with a documented tolerance
- phase error
- half/double-time error rate
- false beat rate in silence
- recovery after stop/start
- stability during syncopation
- CPU time
- allocation count
- analysis queue overrun count
- platform/build complexity

The production backend is selected by an ADR containing the numbers.

---

## 13. Drum performance model

### 13.1 Style is not one loop

A style defines:

- supported meters
- preferred BPM range
- kit
- groove families
- intensity tiers
- complexity tiers
- short fills
- long fills
- transition fills
- intro patterns
- break patterns
- ending patterns
- crash rules
- ride/hat behavior
- swing range
- humanization defaults
- minimum repetition distance
- dynamic-response curves

### 13.2 Versioned style descriptor

Initial descriptor may overlay the existing compiled groove library rather than migrate all content immediately.

Example:

```json
{
  "schemaVersion": 1,
  "id": "rock.hard",
  "name": "Hard Rock",
  "meters": ["4/4"],
  "bpm": {"min": 80, "ideal": 120, "max": 175},
  "grooves": {
    "low": ["rock.basic.01", "rock.basic.02"],
    "medium": ["rock.drive.01"],
    "high": ["rock.drive.03"]
  },
  "fills": {
    "short": ["rock.fill.short.01"],
    "transition": ["rock.fill.trans.01"]
  },
  "humanize": {
    "velocity": 0.25,
    "timing": 0.15,
    "roundRobin": 0.4
  }
}
```

### 13.3 Pattern selection

The Jam Director must:

- use seeded deterministic randomness for testability;
- avoid immediate repetition;
- prefer the current groove family until a musical reason changes it;
- quantize normal changes to bar boundaries;
- place fills primarily at phrase/section boundaries;
- suppress fills when clock confidence is low;
- allow explicit user fill requests;
- map intensity gradually rather than switch randomly.

---

## 14. Dynamic response

The drummer may react to guitar energy without interpreting harmony.

Initial mappings:

- guitar RMS/attack density -> drum velocity envelope
- sustained high energy -> move to higher intensity tier
- reduced energy -> simplify pattern / reduce velocity
- strong phrase-boundary rise -> increased fill/crash probability
- silence -> holdover/break logic, not random fill

All mappings require hysteresis and attack/release smoothing.

---

## 15. Jam Director state machine

Suggested states:

```text
Idle
  -> Listening
  -> ReadyToJoin
  -> Playing
  -> Holdover
  -> Reacquiring
  -> Stopping
  -> Idle
```

The director owns **musical decisions** but not sample rendering.

It consumes:

- `ClockSnapshot`
- guitar energy/onset features
- selected style
- user controls
- explicit commands

It emits:

- `JamIntent`
- pending pattern changes
- fill requests
- stop/join boundary

---

## 16. UI specification

Create a dedicated **Jam** screen/overlay. Do not force the user to operate the full drum editor while playing.

Primary controls:

- Start Listening / Stop
- Style
- Jam Mode
- Intensity
- Complexity
- Fill Amount
- Feel / Follow Tightness
- Swing where applicable
- Kit/source
- Guitar/Drums balance

Primary status:

- input level
- detected tempo candidate
- drummer clock BPM
- confidence
- Acquiring / Locked / Holdover / Lost
- current bar/beat
- next planned event: “Join next bar”, “Fill next bar”, etc.

Large performance actions:

- Tap
- Resync
- Half
- Double
- Fill
- Break
- Stop on next bar

Advanced diagnostics are behind a disclosure panel.

---

## 17. Audio-device and latency UX

On Windows:

- Prefer ASIO when a suitable manufacturer driver is installed.
- Support low-latency WASAPI fallback.
- Do not assume exclusive WASAPI is always superior; current Windows supports low-period shared streams where drivers expose them.
- Expose actual available buffer sizes rather than inventing choices.

Display:

- selected backend
- input/output device
- sample rate
- buffer frames
- block duration
- reported input/output latency where available
- estimated application DSP latency
- xrun/underrun indicator where detectable
- callback load
- analysis-overrun count

Reference validation configuration:

- 48 kHz
- 128 frames
- ASIO-capable USB interface
- headphones connected to the same interface

64-frame operation is desirable but not a release requirement.

---

## 18. Real-time performance gates

### 18.1 Hard callback-safety gate

Under instrumented test:

- zero callback heap allocations
- zero callback deallocations
- zero callback blocking locks
- zero callback file/network I/O
- zero callback system-message posting
- zero callback waits
- bounded queues only

Any violation is a release blocker.

### 18.2 Timing gate

At 48 kHz / 128 frames on the reference machine:

- no callback deadline misses in the standard stress scenario
- p99 callback duration target <= 70% of block duration
- no analysis-induced change to monitored guitar latency
- no analysis queue overrun during nominal 30-minute run

### 18.3 Monitoring latency target

Target measured end-to-end round-trip monitoring latency of <= approximately 12 ms on the reference ASIO setup at 48 kHz / 128 frames, excluding intentionally latency-heavy user-loaded plugins that declare/report their own latency.

The exact measured baseline is recorded before new work and used for regression comparison.

---

## 19. Rhythm quality gates

Initial release targets on the guitar evaluation corpus:

- steady 4/4 material: acquire useful lock within 2 bars for >= 95% of core fixtures
- locked BPM relative error <= 2% on steady-tempo core fixtures
- half/double-time errors < 5% on core fixtures
- no tempo jump from one isolated syncopated event
- silence does not create false acceleration
- explicit resync establishes new phase within the requested beat/bar boundary
- Follow handles controlled gradual tempo ramps without abrupt audible discontinuities
- Loose Follow is measurably less reactive than Follow
- stop/start recovery succeeds without restarting the audio device

Targets may be revised only through an ADR backed by evaluation evidence.

---

## 20. Musical-quality gate

Automated metrics are necessary but insufficient.

A release candidate requires repeated real-guitar play tests using at least:

- clean strumming
- distorted rhythm
- palm-muted metal
- blues/shuffle
- syncopated funk
- sparse single-note playing

Each session records:

- “Did the drummer join at a sensible moment?”
- “Did it stay stable?”
- “Did it overreact?”
- “Were fills predictable enough to feel musical but not repetitive?”
- “Could the player intentionally push/pull it?”
- “Could the player recover it using Tap/Resync without stopping?”

Store qualitative notes beside the corresponding build SHA and config.

---

## 21. Testing strategy

### 21.1 Unit

- queue wrap/overflow
- tracker adapters
- clock acquisition
- clock half/double resolution
- clock holdover/loss
- tempo slew
- phase correction
- Jam Director transitions
- style parsing/schema
- deterministic pattern selection
- fill suppression during low confidence
- drum adapter boundary scheduling

### 21.2 Golden/simulation

Drive clock/director with synthetic observations:

- perfect 120 BPM
- 120 -> 130 ramp
- random syncopated extra onsets
- missing beats
- half-time ambiguity
- two bars silence
- sudden explicit resync
- noisy confidence

The same input sequence must produce deterministic expected output.

### 21.3 Offline audio evaluation

Run tracker backends against the labelled guitar corpus.

Produce JSON/CSV metrics and a Markdown summary.

### 21.4 Integration

Headless processing where possible:

- generated audio input -> analysis -> clock -> director -> drum events
- assert event timestamps
- assert no impossible state transitions
- assert no unbounded queue growth

### 21.5 Hardware

Real interface + headphones:

- ASIO 48k/64
- ASIO 48k/128
- ASIO 48k/256
- WASAPI supported low-latency configuration
- device disconnect/reconnect
- input channel change
- silent input
- clipped input

---

## 22. Diagnostics and traceability

Provide a developer-only trace recorder outside the audio thread.

It may capture:

- sample time
- tracker BPM/confidence
- clock BPM/phase/state
- onset strength
- RMS
- style/intensity
- selected pattern/fill
- queue drops
- callback load

Export CSV/JSON after the session.

Never log directly from the audio callback.

This trace is essential for debugging “the drummer felt wrong” reports.

---

## 23. Persistence

Session state includes:

- input/output device preferences where safe
- style
- jam mode
- intensity
- complexity
- fill amount
- follow tightness
- meter
- kit/source
- audio mix
- clock configuration defaults

Do **not** persist an old live lock as if it were valid next launch.

Version all new persistent structures.

Writes must be atomic and recoverable.

---

## 24. Security and privacy

- Guitar audio remains local by default.
- No recording unless explicitly armed.
- No telemetry by default.
- No network required for jam mode.
- Existing store/network features remain isolated from the real-time engine.
- Plugin loading/scanning remains off the audio thread.
- Treat third-party plugins as crash-risking untrusted code; preserve or improve safe-start behavior.

---

## 25. Licensing/provenance gate

Before the fork becomes the permanent product base:

1. Confirm AGPLv3 is acceptable for the intended distribution.
2. Preserve Guitar-Companion notices and history required by its license.
3. Preserve third-party sample/font notices.
4. Record every new dependency, version/SHA, license, source URL, and whether code is linked, vendored, studied, or only benchmarked.
5. Do not import BeatNet models or datasets into release artifacts until their exact redistribution terms are reviewed.
6. BTrack/aubio GPL compatibility is acceptable only inside the already-AGPL/open-source path.
7. JJazzLab Toolkit integration is a future, separately reviewed dependency.
8. If closed-source commercial distribution becomes a requirement, stop and perform a clean architecture/license review rather than trying to “remove the license later.”

---

## 26. Future full-band architecture

Do not add bass/keys until the drummer meets the MVP musical-quality gate.

Phase 2 adds a harmonic context service:

```text
guitar analysis
    |
    +--> rhythm features --> Musical Clock
    |
    +--> note/chord features --> HarmonicContext
                                   |
                                   v
                             Accompaniment providers
                             - Bass
                             - Keys
                             - Rhythm guitar
```

Candidate approach:

- manual chord/song input first;
- JJazzLab Toolkit or a purpose-built MIDI-pattern provider generates upcoming phrases off the audio thread;
- schedule at least one musical unit ahead;
- later evaluate Basic Pitch/Chordino/other harmony analyzers;
- never make harmony inference a dependency of the drum clock.

---

## 27. Source/research references

Research performed 2026-10-07.

### Primary codebases

- Guitar-Companion: https://github.com/raphaelfukuda/Guitar-Companion
- Giada: https://github.com/monocasual/giada
- BTrack: https://github.com/adamstark/BTrack
- aubio: https://github.com/aubio/aubio
- BeatNet: https://github.com/hashimkarim/beatnet
- Hydrogen: https://github.com/hydrogen-music/hydrogen
- JJazzLab: https://github.com/jjazzboss/JJazzLab
- Basic Pitch: https://github.com/spotify/basic-pitch

### Platform / real-time references

- JUCE AsyncUpdater: https://docs.juce.com/master/classjuce_1_1AsyncUpdater.html
- JUCE AudioDeviceManager: https://juce.com/tutorials/tutorial_audio_device_manager/
- JUCE ThreadedWriter: https://docs.juce.com/master/classjuce_1_1AudioFormatWriter_1_1ThreadedWriter.html
- Microsoft Exclusive-Mode Streams: https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams
- Microsoft Low Latency Audio: https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
- PipeWire latency model: https://docs.pipewire.org/devel/page_latency.html
- PipeWire real-time module: https://docs.pipewire.org/page_module_rt.html

### Accompaniment / synchronization research

- Silent Metronome: Rhythmic Grounding for Live Music Accompaniment, 2026: https://arxiv.org/abs/2609.07688
- Human-Guided Real-Time Beat Tracking Based on Predominant Local Pulse, DAGA 2026: https://pub.dega-akustik.de/DAGA_2026/konferenz-1957.html?article=149
- ACCompanion, IJCAI 2023: https://www.ijcai.org/proceedings/2023/641
- Cyborg Philharmonic synchronization work: https://www.nature.com/articles/s41599-021-00751-8

---

## 28. Definition of MVP done

The MVP is done only when:

1. the stabilized fork passes the hard real-time gate;
2. the selected rhythm tracker has a written guitar-specific benchmark;
3. the Musical Clock passes deterministic simulation tests;
4. a guitarist can plug in, select a style, begin playing, and have the drummer join;
5. the drummer remains musically stable through syncopation and short silence;
6. Follow and Loose Follow are observably different;
7. Tap/Half/Double/Resync recover wrong interpretations without restarting;
8. style/intensity/complexity/fill controls work at musical boundaries;
9. 30-minute reference stress runs have no audio deadline misses attributable to the new system;
10. real-guitar play-test notes say the drummer feels usable as a practice partner, not merely technically synchronized;
11. tests and build run in CI;
12. the exact dependency/license manifest is current.

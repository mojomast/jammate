// AubioBackend implementation. See AubioBackend.h for the design rationale, the
// sample-rate decision, the phase decision and the licence boundary.
//
// Threading: IRhythmTracker::process runs on the rhythm-analysis worker, not the
// audio callback. Allocation there is permitted, but this backend preallocates
// everything in reset() anyway; process() performs no heap traffic. aubio's
// online path (aubio_tempo_do and the getters) is allocation-free; all of its
// buffers and FFT scratch are created in new_aubio_tempo().

#include "aubio/AubioBackend.h"

#include "aubio.h"   // GPL boundary: this .cpp is the only place that includes it

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace jam
{

namespace
{

// aubio's example default (examples/aubiotrack.c) at every input rate. The
// estimator normalises by samplerate / hop_size, so a fixed hop is unbiased;
// see the header. Kept fixed so the detection-function frame rate scales with
// the device rate rather than being resampled.
constexpr int kHop    = 512;
constexpr int kWindow = 1024;

constexpr double kMinSupportedRate = 8000.0;
constexpr double kMaxSupportedRate = 192000.0;

constexpr float kFloorDbfs = -120.0f;

// aubio's default internal silence gate (tempo.c:193). Left at its default so
// the internal beat grid keeps running through quiet-but-real passages; the
// adapter's held-silence state is what suppresses beats during true silence.
constexpr double kAubioInternalSilenceDbfs = -90.0;

double clampRate (double rate) noexcept
{
    if (! (rate > 0.0) || ! std::isfinite (rate))
        return 48000.0;
    return std::clamp (rate, kMinSupportedRate, kMaxSupportedRate);
}

float clamp01 (double v) noexcept
{
    return static_cast<float> (std::clamp (v, 0.0, 1.0));
}

} // namespace

//==============================================================================
struct AubioBackend::Impl
{
    double rate   = 48000.0;
    int    hop    = kHop;
    int    window = kWindow;

    aubio_tempo_t* tempo = nullptr;
    fvec_t*        in    = nullptr;   // one hop of mono audio
    fvec_t*        out   = nullptr;   // aubio_tempo_do output (length 2)

    // Hop accumulator. A partial hop is carried across blocks so the aubio
    // stream is an exact, framing-independent subsample of the device stream.
    int      fill = 0;
    uint64_t captureStart = 0;        // device sample time of the first fed sample
    bool     haveCaptureStart = false;

    // aubio_tempo_get_last() is a 32-bit sample counter; track its wraps so the
    // device timeline stays monotone for sessions longer than 2^32 samples
    // (~24.8 h at 48 kHz).
    bool     haveRawBeat = false;
    uint32_t lastRawBeat = 0;
    uint64_t wrapBase    = 0;         // multiple of 2^32 samples

    // --- audio-derived evidence ---------------------------------------------
    double   prevEnv = 0.0;
    float    transientEma = 0.0f;
    uint64_t activityHoldoff = 0;
    uint64_t holdoffSamples = 0;

    // --- tempo / phase / confidence -----------------------------------------
    bool     haveTempo = false;
    double   lastTempo = 0.0;
    bool     haveAnchor = false;      // a beat has established a phase reference
    uint64_t anchorDevice = 0;
    double   periodSamples = 0.0;

    ~Impl() { release(); }

    void release() noexcept
    {
        if (tempo) { del_aubio_tempo (tempo); tempo = nullptr; }
        if (in)    { del_fvec (in);            in    = nullptr; }
        if (out)   { del_fvec (out);           out   = nullptr; }
    }

    void reset (double r)
    {
        rate   = r;
        hop    = kHop;
        window = kWindow;

        release();

        // The only place that touches the heap: aubio allocates all of its
        // buffers, FFT scratch and the beat tracker state here.
        tempo = new_aubio_tempo ("default", static_cast<uint_t> (window),
                                 static_cast<uint_t> (hop),
                                 static_cast<uint_t> (std::lround (r)));
        in    = new_fvec (static_cast<uint_t> (hop));
        out   = new_fvec (2);
        if (tempo == nullptr || in == nullptr || out == nullptr)
        {
            release();
            throw std::runtime_error ("AubioBackend: new_aubio_tempo failed");
        }
        aubio_tempo_set_silence (tempo, static_cast<smpl_t> (kAubioInternalSilenceDbfs));

        fill = 0;
        captureStart = 0;
        haveCaptureStart = false;

        haveRawBeat = false;
        lastRawBeat = 0;
        wrapBase    = 0;

        prevEnv = 0.0;
        transientEma = 0.0f;
        activityHoldoff = 0;
        holdoffSamples = static_cast<uint64_t> (std::llround (0.05 * r));

        haveTempo = false;
        lastTempo = 0.0;
        haveAnchor = false;
        anchorDevice = 0;
        periodSamples = 0.0;
    }
};

//==============================================================================
AubioBackend::AubioBackend (const AubioBackendConfig& config)
    : impl_ (std::make_unique<Impl>()), config_ (config)
{
}

AubioBackend::~AubioBackend() = default;

void AubioBackend::reset (double sampleRate)
{
    resetRate_ = clampRate (sampleRate);
    impl_->reset (resetRate_);
}

int AubioBackend::deviceHopSamples() const noexcept
{
    return impl_->hop;
}

//==============================================================================
RhythmObservation AubioBackend::process (const AnalysisFrame& frame)
{
    RhythmObservation obs;
    obs.sourceSampleRate = resetRate_;
    obs.inputSampleTime = frame.sampleTime;

    const uint32_t n = std::min<uint32_t> (frame.numSamples,
                                           static_cast<uint32_t> (kMaxAnalysisBlock));
    if (n == 0)
    {
        // A zero-length block carries no audio and therefore no evidence; report
        // silence rather than advancing or fabricating state.
        obs.silence = true;
        obs.energyRmsDbfs = kFloorDbfs;
        return obs;
    }

    // --- energy + silence ---------------------------------------------------
    double sumSquares = 0.0;
    for (uint32_t i = 0; i < n; ++i)
    {
        const double s = frame.samples[i];
        sumSquares += s * s;
    }
    const double rms = std::sqrt (sumSquares / static_cast<double> (n));
    const float dbfs = (rms > 1.0e-12) ? static_cast<float> (20.0 * std::log10 (rms))
                                       : kFloorDbfs;
    obs.energyRmsDbfs = std::max (dbfs, kFloorDbfs);

    const bool blockSilent = obs.energyRmsDbfs < config_.silenceRmsDbfs;

    // Silence is a *held* state: aubio emits a beat roughly one hop after the
    // audio that caused it, so the block carrying the beat is often already
    // below threshold. Latching "recently had audio" for 50 ms keeps real beats
    // while suppressing aubio's predicted grid once a gap is genuinely silent
    // (SPEC.md section 19: silence must not create false beats or acceleration).
    if (! blockSilent)
        impl_->activityHoldoff = impl_->holdoffSamples;

    const bool silent = impl_->activityHoldoff == 0;
    obs.silence = silent;

    if (blockSilent && impl_->activityHoldoff > 0)
        impl_->activityHoldoff =
            (impl_->activityHoldoff > n) ? impl_->activityHoldoff - n : 0;

    // --- onset strength -----------------------------------------------------
    // Derived from the audio we were handed, with the same definition
    // BTrackBackend uses, so the candidates report a comparable
    // onsetStrength01. SPEC.md section 11.2 requires onset strength; this is
    // evidence about the audio, not about aubio.
    const double env   = rms;
    const double denom = env + impl_->prevEnv + static_cast<double> (config_.onsetFluxFloor);
    const double flux  = std::max (0.0, env - impl_->prevEnv) / denom;
    impl_->prevEnv = env;
    obs.onsetStrength01 = clamp01 (flux);

    impl_->transientEma = 0.9f * impl_->transientEma + 0.1f * obs.onsetStrength01;
    obs.transientDensity01 = impl_->transientEma;

    // --- accumulate into hops, feed aubio, collect beats --------------------
    if (! impl_->haveCaptureStart)
    {
        impl_->captureStart = frame.sampleTime;
        impl_->haveCaptureStart = true;
    }

    bool     anyBeat = false;
    uint64_t firstBeatDeviceTime = frame.sampleTime;

    for (uint32_t i = 0; i < n; ++i)
    {
        impl_->in->data[impl_->fill++] = frame.samples[i];
        if (impl_->fill < impl_->hop)
            continue;

        impl_->fill = 0;
        aubio_tempo_do (impl_->tempo, impl_->in, impl_->out);

        // `out->data[0]` is aubio's causally-emitted tactus: non-zero only on
        // the hop that contains a beat, and equal to the sub-hop fractional
        // position. This is the canonical beat signal (examples/aubiotrack.c).
        if (impl_->out->data[0] != 0.0f)
        {
            const uint32_t raw = static_cast<uint32_t> (aubio_tempo_get_last (impl_->tempo));
            if (impl_->haveRawBeat && raw < impl_->lastRawBeat)
                impl_->wrapBase += (1ull << 32);
            impl_->lastRawBeat = raw;
            impl_->haveRawBeat = true;

            const uint64_t beatDevice =
                impl_->captureStart + impl_->wrapBase + static_cast<uint64_t> (raw);

            if (! anyBeat)
                firstBeatDeviceTime = beatDevice;
            anyBeat = true;

            if (! silent)
            {
                // aubio exposes the beat position (get_last, with its sub-hop
                // offset) and the period (get_period = hop * bp). Normalise them
                // to a phase reference; do not free-run a phase.
                const double period = static_cast<double> (aubio_tempo_get_period (impl_->tempo));
                if (period > 0.0)
                {
                    impl_->anchorDevice  = beatDevice;
                    impl_->periodSamples = period;
                    impl_->haveAnchor    = true;
                }
            }
        }
    }

    // --- tempo / confidence -------------------------------------------------
    // aubio reports a tempo as soon as it has a period estimate, independent of
    // whether a beat has fired; report that continuously rather than holding a
    // stale value.
    const double bpmRaw = static_cast<double> (aubio_tempo_get_bpm (impl_->tempo));
    if (bpmRaw > 0.0)
    {
        impl_->lastTempo = std::clamp (bpmRaw, static_cast<double> (config_.minBpm),
                                                static_cast<double> (config_.maxBpm));
        impl_->haveTempo = true;
    }

    obs.bpmCandidate = impl_->haveTempo ? static_cast<float> (impl_->lastTempo) : 0.0f;

    const bool phaseValid = impl_->haveAnchor && impl_->periodSamples > 0.0 && ! silent;
    obs.phaseValid = phaseValid;

    if (phaseValid)
    {
        // Phase at the observation's sample time, so the Musical Clock's
        // applyPhaseCorrection() compares like with like (MusicalClock.cpp:372).
        const double elapsed = static_cast<double> (frame.sampleTime)
                             - static_cast<double> (impl_->anchorDevice);
        double phase = std::fmod (elapsed / impl_->periodSamples, 1.0);
        if (phase < 0.0)
            phase += 1.0;
        obs.beatPhase01 = static_cast<float> (phase);
    }

    if (phaseValid)
        obs.beatConfidence01 = clamp01 (static_cast<double> (aubio_tempo_get_confidence (impl_->tempo)));
    else
        obs.beatConfidence01 = 0.0f;

    // Beats are suppressed during held silence: aubio's predicted grid happily
    // runs through a gap, and the SPEC.md section 19 gate is that silence must
    // not manufacture beats or acceleration.
    obs.beatEvent = anyBeat && ! silent;
    if (obs.beatEvent)
    {
        obs.inputSampleTime = firstBeatDeviceTime;
        obs.beatPhase01 = 0.0f;   // a beat is the phase reference
    }

    return obs;
}

const char* AubioBackend::id() const noexcept
{
    return "aubio";
}

} // namespace jam

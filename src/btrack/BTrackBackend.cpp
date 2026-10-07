// BTrackBackend implementation. See BTrackBackend.h for the design rationale,
// the sample-rate decision and the licence boundary.
//
// Threading: IRhythmTracker::process runs on the rhythm-analysis worker, not the
// audio callback. Allocation there is permitted, but this backend preallocates
// everything in reset() anyway; process() performs no heap traffic. (BTrack's
// causal path uses only stack arrays; see third_party/BTrack/src/BTrack.cpp,
// updateCumulativeScore()/predictBeat() use variable-length stack buffers.)

#include "btrack/BTrackBackend.h"

#include "BTrack.h"   // GPL boundary: this .cpp is the only place that includes it

#include <algorithm>
#include <cmath>

namespace jam
{

namespace
{

// BTrack 1.0.7's own reference rate. The tempo->lag mapping in
// BTrack.cpp::calculateTempo() hard-codes 44100.0 (line 394), so the onset
// detection function must be presented at this rate to be unbiased. See the
// header comment and VENDORED-PATCHES.md.
constexpr double kAnalysisRate = 44100.0;
constexpr int    kHop          = 512;    // BTrack::BTrack() default
constexpr int    kFrame        = 1024;   // BTrack::BTrack() default (2 * hop)

// Supported device rates. Outside this window the resampler's per-block output
// bound is either meaningless or enormous; the caller is told by clamping.
constexpr double kMinSupportedRate = 8000.0;
constexpr double kMaxSupportedRate = 192000.0;

constexpr float kFloorDbfs = -120.0f;

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
struct BTrackBackend::Impl
{
    // --- linear streaming resampler: device rate -> 44.1 kHz ----------------
    // step = device samples advanced per analysis sample.
    double step = 1.0;
    double t = 0.0;         // position within [prevIn, x); emits while t < 1
    double prevIn = 0.0;
    bool   havePrev = false;

    // --- BTrack hop accumulator ---------------------------------------------
    double hopBuf[kHop] = {};
    int    hopFill = 0;

    // Global count of analysis samples emitted since reset (a monotone clock,
    // not a per-block counter), and the device sample time of the first raw
    // sample after reset. The device time of analysis sample k is
    // captureStart + round(k * step), which is framing-independent.
    uint64_t analysisIndex = 0;
    uint64_t captureStart = 0;
    bool     haveCaptureStart = false;
    uint64_t hopStartDeviceTime = 0;

    // --- audio-derived evidence ---------------------------------------------
    double prevEnv = 0.0;
    float  transientEma = 0.0f;
    uint64_t activityHoldoff = 0;   // device samples of "recently had audio" left
    uint64_t holdoffSamples = 0;    // full hold-off, set in reset()

    // --- tempo / phase / confidence -----------------------------------------
    bool   haveTempo = false;
    double lastTempo = 0.0;
    double tempoChangeEma = 0.0;
    float  beatPhase = 0.0f;

    std::unique_ptr<BTrack> btrack;

    void reset (double rate)
    {
        step = rate / kAnalysisRate;
        t = 0.0;
        prevIn = 0.0;
        havePrev = false;

        hopFill = 0;

        analysisIndex = 0;
        captureStart = 0;
        haveCaptureStart = false;
        hopStartDeviceTime = 0;

        prevEnv = 0.0;
        transientEma = 0.0f;
        activityHoldoff = 0;
        // 50 ms is longer than the one-frame (23 ms) analysis latency, so a
        // beat emitted just after its onset is never misread as a beat in
        // silence, while a real gap still suppresses BTrack's free-running
        // predictions. SPEC.md section 19.
        holdoffSamples = static_cast<uint64_t> (std::llround (0.05 * rate));

        haveTempo = false;
        lastTempo = 0.0;
        tempoChangeEma = 0.0;
        beatPhase = 0.0f;

        // BTrack allocates its buffers and FFT plans here; reset() is the only
        // place that is allowed to (and does) touch the heap.
        btrack = std::make_unique<BTrack> (kHop, kFrame);
    }

    /** Feed one 44.1 kHz analysis sample into BTrack, one hop at a time.
        Returns true when BTrack emitted a beat on the hop completed by this
        sample, and writes the hop's device sample time to outDeviceTime. */
    bool pushAnalysisSample (double y, uint64_t& outDeviceTime)
    {
        if (hopFill == 0)
            hopStartDeviceTime = captureStart + static_cast<uint64_t> (std::llround (static_cast<double> (analysisIndex) * step));

        hopBuf[hopFill++] = y;
        ++analysisIndex;

        if (hopFill < kHop)
            return false;

        hopFill = 0;
        btrack->processAudioFrame (hopBuf);

        if (btrack->beatDueInCurrentFrame())
        {
            outDeviceTime = hopStartDeviceTime;
            return true;
        }
        return false;
    }
};

//==============================================================================
BTrackBackend::BTrackBackend (const BTrackBackendConfig& config)
    : impl_ (std::make_unique<Impl>()), config_ (config)
{
}

BTrackBackend::~BTrackBackend() = default;

void BTrackBackend::reset (double sampleRate)
{
    resetRate_ = clampRate (sampleRate);
    impl_->reset (resetRate_);
}

double BTrackBackend::deviceHopSamples() const noexcept
{
    return static_cast<double> (kHop) * resetRate_ / kAnalysisRate;
}

//==============================================================================
RhythmObservation BTrackBackend::process (const AnalysisFrame& frame)
{
    RhythmObservation obs;
    // The caller guarantees the feed rate via reset(); report that rate so the
    // observation's timeline is unambiguous (RhythmTypes.h, inputSampleTime).
    obs.sourceSampleRate = resetRate_;
    obs.inputSampleTime = frame.sampleTime;

    const uint32_t n = std::min<uint32_t> (frame.numSamples, static_cast<uint32_t> (kMaxAnalysisBlock));
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
    const float dbfs = (rms > 1.0e-12) ? static_cast<float> (20.0 * std::log10 (rms)) : kFloorDbfs;
    obs.energyRmsDbfs = std::max (dbfs, kFloorDbfs);

    const bool blockSilent = obs.energyRmsDbfs < config_.silenceRmsDbfs;

    // Silence is a *held* state, not an instantaneous one. BTrack reports a beat
    // roughly one analysis hop after the audio that caused it, so the block that
    // carries the beat is often already below the threshold. Latching "recently
    // had audio" for 50 ms keeps real beats while still suppressing BTrack's
    // cumulative-score predictor once a gap is genuinely silent (SPEC.md
    // section 19: silence must not create false beats or acceleration).
    if (! blockSilent)
        impl_->activityHoldoff = impl_->holdoffSamples;

    const bool silent = impl_->activityHoldoff == 0;
    obs.silence = silent;

    if (blockSilent && impl_->activityHoldoff > 0)
        impl_->activityHoldoff = (impl_->activityHoldoff > n) ? impl_->activityHoldoff - n : 0;

    // --- onset strength -----------------------------------------------------
    // BTrack does not expose its onset detection function (the ODF is private
    // state in BTrack.h, lines 177/182); it only exposes the cumulative score.
    // SPEC.md section 11.2 requires onset strength, so it is derived here
    // directly from the audio we were handed: the positive, normalised
    // energy-envelope flux between consecutive blocks. This is evidence about
    // the audio, not about BTrack, which is exactly what the clock consumes.
    const double env = rms;
    const double denom = env + impl_->prevEnv + 1.0e-9;
    const double flux = std::max (0.0, env - impl_->prevEnv) / denom;
    impl_->prevEnv = env;
    obs.onsetStrength01 = clamp01 (flux);

    // Recent transient activity, as a simple EMA of onset strength.
    impl_->transientEma = 0.9f * impl_->transientEma + 0.1f * obs.onsetStrength01;
    obs.transientDensity01 = impl_->transientEma;

    // --- resample, feed BTrack, collect beats -------------------------------
    bool     anyBeat = false;
    uint64_t firstBeatDeviceTime = frame.sampleTime;

    if (! impl_->haveCaptureStart)
    {
        impl_->captureStart = frame.sampleTime;
        impl_->haveCaptureStart = true;
    }

    for (uint32_t i = 0; i < n; ++i)
    {
        const double x = frame.samples[i];

        if (! impl_->havePrev)
        {
            impl_->prevIn = x;
            impl_->havePrev = true;
            continue;
        }

        while (impl_->t < 1.0)
        {
            const double y = impl_->prevIn + (x - impl_->prevIn) * impl_->t;

            uint64_t beatDeviceTime = 0;
            if (impl_->pushAnalysisSample (y, beatDeviceTime))
            {
                if (! anyBeat)
                    firstBeatDeviceTime = beatDeviceTime;
                anyBeat = true;

                if (! silent)
                {
                    double tempo = impl_->btrack->getCurrentTempoEstimate();
                    tempo = std::clamp (tempo, static_cast<double> (config_.minBpm),
                                        static_cast<double> (config_.maxBpm));

                    if (impl_->haveTempo && impl_->lastTempo > 0.0)
                    {
                        const double rel = std::fabs (tempo - impl_->lastTempo) / impl_->lastTempo;
                        impl_->tempoChangeEma = 0.8 * impl_->tempoChangeEma + 0.2 * rel;
                    }
                    impl_->lastTempo = tempo;
                    impl_->haveTempo = true;

                    // A beat re-arms the phase.
                    impl_->beatPhase = 0.0f;
                }
            }

            impl_->t += impl_->step;
        }

        impl_->t -= 1.0;
        impl_->prevIn = x;
    }

    // --- phase --------------------------------------------------------------
    // BTrack exposes no phase at all: VENDORED-PATCHES.md's "NOT patched, but
    // load-bearing" section. It only says *that* a beat is due. The adapter
    // therefore derives phase from the timing of BTrack's beat events relative
    // to the estimated beat period, which is a real measurement of where the
    // clock is in the beat, not a fabricated one. During silence the phase is
    // not trustworthy (the underlying beats are suppressed), so phaseValid is
    // false there.
    if (! silent && ! anyBeat && impl_->haveTempo && impl_->lastTempo > 0.0)
    {
        const double blockSeconds = static_cast<double> (n) / resetRate_;
        const double beatSeconds = 60.0 / impl_->lastTempo;
        impl_->beatPhase = static_cast<float> (impl_->beatPhase + blockSeconds / beatSeconds);
        impl_->beatPhase -= std::floor (impl_->beatPhase);
    }

    // --- assemble the evidence ----------------------------------------------
    obs.bpmCandidate = impl_->haveTempo ? static_cast<float> (impl_->lastTempo) : 0.0f;
    obs.beatPhase01  = impl_->beatPhase;
    obs.phaseValid   = impl_->haveTempo && ! silent;

    if (! impl_->haveTempo || silent)
        obs.beatConfidence01 = 0.0f;
    else
        obs.beatConfidence01 = clamp01 (1.0 - impl_->tempoChangeEma
                                               / std::max (1.0e-6, static_cast<double> (config_.confidenceFullScaleRelTempoChange)));

    // Beats are suppressed during silence: BTrack's cumulative-score predictor
    // happily free-runs through a gap, and the SPEC.md section 19 gate is that
    // silence must not manufacture beats or acceleration.
    obs.beatEvent = anyBeat && ! silent;
    if (obs.beatEvent)
        obs.inputSampleTime = firstBeatDeviceTime;

    return obs;
}

const char* BTrackBackend::id() const noexcept
{
    return "btrack";
}

} // namespace jam

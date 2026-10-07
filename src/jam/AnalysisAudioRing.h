// Bounded single-producer / single-consumer analysis audio queue.
//
// Contract frozen by SPEC.md section 8.1. This type is written by the audio
// callback and read by the rhythm-analysis worker. It is therefore the single
// most safety-critical structure in the new system.
//
// Guarantees:
//   - all storage is allocated in the constructor; push()/pop() never allocate,
//     never block, never retry, and never touch the heap.
//   - capacity is fixed and capacity == 0 is legal (the ring then behaves as a
//     permanent-overrun sink, which is the safe direction to fail).
//   - overflow policy: the producer's incoming block is DROPPED, a counter is
//     incremented, and the producer returns immediately. Audio never waits for
//     analysis. (SPEC.md 8.1)
//   - exactly one producer thread and one consumer thread. Using the ring from
//     two producers or two consumers is a defect, not a supported mode.
//
// Implementation note: indices are monotonic uint64_t counters rather than
// wrapped uint32_t indices. Monotonic counters make the wraparound arithmetic
// exact and keep the "empty" and "full" cases unambiguous without wasting a
// slot. Overflow of the 64-bit counter is unreachable in practice (at 48 kHz it
// takes ~1.5 million years), and the code degrades safely if it ever happens
// because all ordering comparisons use signed-safe modular differences.

#pragma once

#include "RhythmTypes.h"

#include <atomic>
#include <cstring>
#include <memory>

namespace jam
{

class AnalysisAudioRing
{
public:
    explicit AnalysisAudioRing (std::size_t capacity)
    {
        slots_ = capacity != 0
                     ? std::make_unique<Slot[]>(capacity)
                     : nullptr;
        capacity_ = slots_ != nullptr ? capacity : 0;
    }

    AnalysisAudioRing (const AnalysisAudioRing&) = delete;
    AnalysisAudioRing& operator= (const AnalysisAudioRing&) = delete;

    /** Producer side, audio thread only.
        Copies at most min(numSamples, kMaxAnalysisBlock) samples.
        Returns false (and increments the overrun counter) when the ring is
        full or disabled. Never blocks, never allocates. */
    bool push (const float* samples, uint32_t numSamples, uint64_t sampleTime, double sampleRate) noexcept
    {
        // A disabled ring (capacity 0) has room for nothing, so every block is a
        // dropped block and MUST be counted: otherwise analysisOverrunCount reads
        // a permanent 0 for a device with no analysis buffer, which is exactly
        // the diagnostic blind spot SPEC 22 exists to prevent. The counters are
        // cache-line-aligned atomics, so this stays allocation- and block-free.
        if (slots_ == nullptr)
        {
            overrunCount_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        // An empty block or a null pointer is a caller error, not an overrun:
        // there is no data to drop, so it is not counted as one.
        if (samples == nullptr || numSamples == 0)
            return false;

        if (numSamples > kMaxAnalysisBlock)
            numSamples = static_cast<uint32_t> (kMaxAnalysisBlock);

        const uint64_t w = writePos_.load (std::memory_order_relaxed);

        // Full when the consumer is exactly capacityBehind behind.
        if (w - readPos_.load (std::memory_order_acquire) >= capacity_)
        {
            overrunCount_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        Slot& s = slots_[static_cast<std::size_t> (w % capacity_)];
        s.sampleTime = sampleTime;
        s.sourceSampleRate = sampleRate;
        s.numSamples = numSamples;
        std::memcpy (s.samples, samples, numSamples * sizeof (float));

        // Release: the payload above must be visible before the index is.
        writePos_.store (w + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side, analysis worker only. Bounded, non-blocking.
        Returns false when empty. */
    bool pop (AnalysisFrame& out) noexcept
    {
        if (slots_ == nullptr)
            return false;

        const uint64_t r = readPos_.load (std::memory_order_relaxed);

        if (r == writePos_.load (std::memory_order_acquire))
            return false;

        Slot& s = slots_[static_cast<std::size_t> (r % capacity_)];
        out.sampleTime = s.sampleTime;
        out.sourceSampleRate = s.sourceSampleRate;
        out.numSamples = s.numSamples;
        std::memcpy (out.samples, s.samples, s.numSamples * sizeof (float));

        readPos_.store (r + 1, std::memory_order_release);
        return true;
    }

    /** Producer and consumer may both read these; safe from any thread. */
    uint64_t overrunCount() const noexcept
    {
        return overrunCount_.load (std::memory_order_relaxed);
    }

    uint64_t droppedBlocks() const noexcept { return overrunCount(); }

    uint64_t pushedBlocks() const noexcept
    {
        return writePos_.load (std::memory_order_acquire)
             - readPos_.load (std::memory_order_acquire);
    }

    std::size_t capacity() const noexcept { return capacity_; }

    bool valid() const noexcept { return slots_ != nullptr; }

    /** Drops any queued blocks and restarts both indices at zero. Used at device
        shutdown. Any in-flight blocks are abandoned, which is safe: analysis data
        is disposable.

        This is a *positions* reset, not a disable. The ring keeps accepting
        pushes afterwards — which is what the audio thread needs, because
        prepareToPlay runs again after every device change and the callback must
        not have to be told to resume. To stop accepting data, destroy the ring or
        let it go out of scope in prepareToPlay/releaseResources; there is no
        separate disable() because a stale flag is the kind of thing that stays
        set after a device change and silently starves analysis forever. */
    void reset() noexcept
    {
        writePos_.store (0, std::memory_order_relaxed);
        readPos_.store (0, std::memory_order_relaxed);
    }

private:
    struct Slot
    {
        uint64_t sampleTime = 0;
        double   sourceSampleRate = 48000.0;
        uint32_t numSamples = 0;
        float    samples[kMaxAnalysisBlock] = {};
    };

    std::unique_ptr<Slot[]> slots_ = nullptr;
    std::size_t capacity_ = 0;

    alignas (64) std::atomic<uint64_t> writePos_ { 0 };
    alignas (64) std::atomic<uint64_t> readPos_ { 0 };
    alignas (64) std::atomic<uint64_t> overrunCount_ { 0 };
};

} // namespace jam
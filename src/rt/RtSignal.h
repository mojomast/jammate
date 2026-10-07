// Bounded, header-only signalling across the audio-thread boundary.
//
// SPEC.md 7.1 / 18.1: no allocation, locks, waits, message posting or unbounded
// retry. SPEC 8.2 coalesces notifications and snapshots; SPEC 8.3 preserves
// commands in a fixed-capacity SPSC queue. All storage is inline, including at
// construction. Native lock-free atomics are a compile-time requirement rather
// than silently falling back to a library lock on an unsupported target.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace jam::rt
{

static_assert (std::atomic<bool>::is_always_lock_free,
               "RT signalling requires native lock-free bool atomics");
static_assert (std::atomic<unsigned char>::is_always_lock_free,
               "RT snapshots require native lock-free byte atomics");
static_assert (std::atomic<uint64_t>::is_always_lock_free,
               "RT generations require native lock-free uint64_t atomics");

/** Coalescible edge, polled by a message-thread timer (SPEC 7.4 / 8.2).
    A signal is a hint to inspect separately synchronised state, not a queue or
    permission to read non-atomic data that its owner might still be changing. */
class SignalFlag
{
public:
    SignalFlag() noexcept = default;
    SignalFlag (const SignalFlag&) = delete;
    SignalFlag& operator= (const SignalFlag&) = delete;

    /** Audio thread: one store, no post, no retry. Repeated stores coalesce. */
    void signal() noexcept
    {
        // Release publishes preceding state updates to a successful consuming
        // acquire. A relaxed store would suffice for an edge with no associated
        // state, but release also supports the scene-ready handoff. This does
        // not protect subsequent writes to unrelated non-atomic payloads.
        pending_.store (true, std::memory_order_release);
    }

    /** Message thread: atomically take and clear the outstanding edge. */
    bool consume() noexcept
    {
        // The exchange is the clearing linearisation point: a racing signal is
        // either consumed here or remains for the next poll, never erased by a
        // separate load/store pair. Acquire pairs with signal's release; the
        // release half orders this consumer's preceding work before the clear.
        return pending_.exchange (false, std::memory_order_acq_rel);
    }

    /** Any thread. Observes an edge without claiming it. */
    bool pending() const noexcept
    {
        return pending_.load (std::memory_order_acquire);
    }

private:
    alignas (64) std::atomic<bool> pending_ { false };
};

/** One logical latest-value slot for a small trivially-copyable payload.

    Exactly one publisher and one reader; invalidate() may be called from any
    thread. Construction/destruction require those callers to be quiescent.
    Generations are monotonic uint64_t counters, not wrapped buffer indices.
    The object must be replaced before 2^63 publications or 2^64 invalidations;
    even at 48 kHz those limits take millions of years to reach.

    Data-race question: a conventional seqlock around a plain T copy is NOT
    sufficient in C++. A writer overlapping that copy creates a data race even
    if the reader later rejects it. Here every shared payload byte is atomic.
    A speculative mixed representation stays in a local byte array, never in a
    T, and is installed in out only after the generation check succeeds.

    All sequence and payload operations are sequentially consistent. Their
    single total order includes the writer's odd marker, each byte store, and
    its even marker. Equal nonzero even markers bracketing the reader's byte
    loads exclude ALL intervening writer stores (absent generation wrap). Thus
    a successful read contains exactly one completed publication. SC includes
    the release/acquire publication pairing and also orders the odd marker
    before the bytes; release/acquire on the markers alone would not establish
    this total-order argument for speculative concurrent byte loads.

    SPEC 8.2: the reader attempts ONE snapshot and never retries. A raced read
    returns false without modifying out or the remembered generation, so the
    caller keeps its previous valid value and can poll again on a later tick.
    publish() always replaces the pending value, without consulting the reader.
    Both byte loops have the compile-time extent sizeof(T); no CAS/spin loop. */
template <typename T>
class LatestValue
{
    static_assert (std::is_trivially_copyable_v<T>,
                   "LatestValue payloads must be trivially copyable");
    static_assert (! std::is_const_v<T> && ! std::is_volatile_v<T>,
                   "LatestValue payloads must be mutable unqualified values");

public:
    // Algorithm invariant, not a tuning knob: there is no retry path.
    static constexpr std::size_t readAttemptLimit = 1;

    LatestValue() noexcept = default;
    LatestValue (const LatestValue&) = delete;
    LatestValue& operator= (const LatestValue&) = delete;

    /** Publisher only, audio-thread safe. The source must remain stable for
        this call, as with any ordinary value copy. No reader acknowledgement. */
    void publish (const T& value) noexcept
    {
        // Only the publisher changes sequence_, so its own prior value can be
        // loaded relaxed. The SC stores below order the entire publication.
        const uint64_t previous = sequence_.load (std::memory_order_relaxed);
        sequence_.store (previous + 1, std::memory_order_seq_cst);

        const auto* source = reinterpret_cast<const unsigned char*> (&value);
        for (std::size_t i = 0; i < sizeof (T); ++i)
            payload_[i].store (source[i], std::memory_order_seq_cst);

        sequence_.store (previous + 2, std::memory_order_seq_cst);
    }

    /** Reader only, even through a const reference. False means uninitialised,
        unchanged, or raced; out is untouched in all three cases. */
    bool tryRead (T& out) const noexcept
    {
        // This counter only requests re-reading; it does not publish payload or
        // external lifecycle state, so relaxed access is sufficient. A change
        // racing this call remains visible to a subsequent poll.
        const uint64_t invalidation = invalidation_.load (std::memory_order_relaxed);
        const uint64_t before = sequence_.load (std::memory_order_seq_cst);
        if (before == 0 || (before & uint64_t { 1 }) != 0)
            return false;
        if (before == lastReadSequence_ && invalidation == lastReadInvalidation_)
            return false;

        unsigned char candidate[sizeof (T)];
        for (std::size_t i = 0; i < sizeof (T); ++i)
            candidate[i] = payload_[i].load (std::memory_order_seq_cst);

        if (sequence_.load (std::memory_order_seq_cst) != before)
            return false;

        // Copying the object representation of a trivially-copyable value is
        // legal, including padding, and invokes no user-defined copy operation.
        auto* destination = reinterpret_cast<unsigned char*> (&out);
        for (std::size_t i = 0; i < sizeof (T); ++i)
            destination[i] = candidate[i];

        lastReadSequence_ = before;
        lastReadInvalidation_ = invalidation;
        return true;
    }

    /** Force one re-read of the latest completed value after a lifecycle
        change. Retains the payload; cannot invent a value before publication.
        Repeated invalidations coalesce. Lifecycle state needs its own thread
        synchronisation; this relaxed token is only a cache-invalidation hint. */
    void invalidate() noexcept
    {
        invalidation_.fetch_add (1, std::memory_order_relaxed);
    }

private:
    // Bytes are not read until a nonzero completed generation exists, by which
    // time publish() has initialised every atomic byte (also on C++17 targets).
    alignas (64) std::atomic<unsigned char> payload_[sizeof (T)] {};
    alignas (64) std::atomic<uint64_t> sequence_ { 0 };
    alignas (64) std::atomic<uint64_t> invalidation_ { 0 };

    // Reader-owned bookkeeping; const tryRead does not imply multiple readers.
    alignas (64) mutable uint64_t lastReadSequence_ = 0;
    mutable uint64_t lastReadInvalidation_ = 0;
};

/** Fixed-capacity SPSC queue for non-coalescible commands (SPEC 8.3).
    On full, drop the INCOMING command, count it, and immediately return false
    (same policy as AnalysisAudioRing / SPEC 8.1). Capacity 0 is a legal sink.
    One producer and one consumer; no lifecycle reset while either is active. */
template <typename Command, std::size_t N>
class CommandQueue
{
    static_assert (std::is_trivially_copyable_v<Command>,
                   "CommandQueue commands must be trivially copyable");
    static_assert (! std::is_const_v<Command> && ! std::is_volatile_v<Command>,
                   "CommandQueue commands must be mutable unqualified values");
    static_assert (N < (uint64_t { 1 } << 63),
                   "CommandQueue capacity must fit monotonic index arithmetic");

public:
    CommandQueue() noexcept = default;
    CommandQueue (const CommandQueue&) = delete;
    CommandQueue& operator= (const CommandQueue&) = delete;

    /** Producer only. Bounded, no allocation, no retry. */
    bool push (const Command& command) noexcept
    {
        if constexpr (N == 0)
        {
            countDrop();
            return false;
        }
        else
        {
            const uint64_t w = writePos_.load (std::memory_order_relaxed);
            // Acquire pairs with the consumer's release: a reclaimed slot must
            // not be overwritten until the previous command copy has finished.
            if (w - readPos_.load (std::memory_order_acquire) >= N)
            {
                countDrop();
                return false;
            }

            auto& slot = slots_[writeSlot_];
            const auto* source = reinterpret_cast<const unsigned char*> (&command);
            for (std::size_t i = 0; i < sizeof (Command); ++i)
                slot.bytes[i] = source[i];

            if (++writeSlot_ == N)
                writeSlot_ = 0;

            // Release publishes every payload byte before the nonempty index.
            writePos_.store (w + 1, std::memory_order_release);
            return true;
        }
    }

    /** Consumer only. A failed pop leaves out unchanged. */
    bool pop (Command& out) noexcept
    {
        if constexpr (N == 0)
        {
            return false;
        }
        else
        {
            const uint64_t r = readPos_.load (std::memory_order_relaxed);
            // Acquire makes the producer's completed copy visible. Unlike a
            // latest-value slot, this slot cannot be reused before our release.
            if (r == writePos_.load (std::memory_order_acquire))
                return false;

            const auto& slot = slots_[readSlot_];
            auto* destination = reinterpret_cast<unsigned char*> (&out);
            for (std::size_t i = 0; i < sizeof (Command); ++i)
                destination[i] = slot.bytes[i];

            if (++readSlot_ == N)
                readSlot_ = 0;

            readPos_.store (r + 1, std::memory_order_release);
            return true;
        }
    }

    /** Diagnostic only: no payload is published by this relaxed counter. */
    uint64_t droppedCount() const noexcept
    {
        return dropped_.load (std::memory_order_relaxed);
    }

    static constexpr std::size_t capacity() noexcept { return N; }

private:
    void countDrop() noexcept
    {
        // Only the producer increments this diagnostic, so a relaxed load /
        // store is sufficient and avoids even an atomic RMW on the audio side.
        dropped_.store (dropped_.load (std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
    }

    // Raw representations avoid constructing Command or invoking user code.
    // The capacity-0 placeholder avoids nonstandard zero-length arrays; no
    // operation in that instantiation reads or writes the placeholder.
    struct Slot { unsigned char bytes[sizeof (Command)]; };
    alignas (64) Slot slots_[N != 0 ? N : 1] {};

    // Modular subtraction is exact across uint64_t wrap: the producer never
    // gets more than N ahead, and N is less than half the counter range. Keep
    // thread-owned storage cursors separately: w % N would skip a slot when
    // uint64_t wraps for a capacity that does not divide 2^64 (e.g. N == 3).
    alignas (64) std::atomic<uint64_t> writePos_ { 0 };
    std::size_t writeSlot_ = 0; // Producer only.
    alignas (64) std::atomic<uint64_t> readPos_ { 0 };
    std::size_t readSlot_ = 0;  // Consumer only.
    alignas (64) std::atomic<uint64_t> dropped_ { 0 };
};

} // namespace jam::rt

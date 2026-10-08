// Single definition of the shared callback-allocation probe (see DrumHeapProbe.h).
#include "DrumHeapProbe.h"

#if defined(DRUM_MIDI_HEAP_PROBE)

#include <cstdlib>
#include <new>

namespace
{
thread_local bool gMeasuring = false;
thread_local std::size_t gAllocations = 0;
thread_local std::size_t gDeallocations = 0;
} // namespace

extern "C" void* __real_malloc (std::size_t);
extern "C" void* __real_realloc (void*, std::size_t);
extern "C" void __real_free (void*);

extern "C" void* __wrap_malloc (std::size_t bytes)
{
    if (gMeasuring) ++gAllocations;
    return __real_malloc (bytes);
}

extern "C" void* __wrap_realloc (void* p, std::size_t bytes)
{
    if (gMeasuring) ++gAllocations;
    return __real_realloc (p, bytes);
}

extern "C" void __wrap_free (void* p)
{
    if (gMeasuring && p != nullptr) ++gDeallocations;
    __real_free (p);
}

// C++ new/delete are routed through the same wrapped C heap.
void* operator new (std::size_t bytes)
{
    if (void* p = std::malloc (bytes > 0 ? bytes : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#endif // DRUM_MIDI_HEAP_PROBE

namespace drumprobe
{
#if defined(DRUM_MIDI_HEAP_PROBE)
void beginMeasure() noexcept
{
    gAllocations = 0;
    gDeallocations = 0;
    gMeasuring = true;
}

void endMeasure() noexcept
{
    gMeasuring = false;
}

std::size_t allocations() noexcept { return gAllocations; }
std::size_t deallocations() noexcept { return gDeallocations; }
#endif
} // namespace drumprobe

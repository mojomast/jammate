# RT-REACHABILITY — Callback reachability audit of the Guitar-Companion fork

**Task:** FND-003
**Base commit:** `48f301c8fd046429fe55519ec3a2d20a2fccaa44`
**Audit type:** read-only. Zero files under `src/` were modified.
**Contract audited against:** SPEC.md §7.1 (audio-thread forbidden list), §7.4 (message/UI thread), §18.1 (hard callback-safety gate), §18.2 (timing gate), §22 (diagnostics never log from the callback); DEVPLAN.md `FND-003`; `CONTRIBUTING.md` golden rule 1.

All line references are to the base commit above. Every claim in this document was
derived by reading the code at those lines; the key greps used to spot-check it are
reproduced at the end.

---

## 0. Executive summary

* **`triggerAsyncUpdate()` IS reachable from `processBlock`.** It is called directly
  from `processBlock` at `src/PluginProcessor.cpp:1227`, inside the scene-change
  envelope's `fadeOut` branch. The second site, `src/PluginProcessor.cpp:1070`, lives
  in `prepareToPlay` and is **not** callback-reachable.
  This is a **P0** finding and is the exact hazard DEVPLAN `RT-001` was created to fix.
* No `new`/`delete`, `malloc`, `free`, `std::vector`/`std::string` construction, lock,
  file I/O, JSON/XML parse, or logger call occurs on a directly-traced callback path.
* The remaining callback hazards are: **probable-but-not-proven-zero allocation** via
  `juce::MidiBuffer` growth in the drum path (P0, conditional on a hosted drum VST) and
  via the guest-controlled buffer handed to hosted VST3s (P1); **unaudited third-party
  `processBlock`** calls (P1); and a **lock-free CAS retry loop** (P2).
* `jam/` (jam-core) is **not linked into the callback at all** today.

---

## 1. The two `triggerAsyncUpdate()` call sites

`git grep -n triggerAsyncUpdate HEAD -- src/` returns exactly:

```
HEAD:src/PluginProcessor.cpp:1070:            triggerAsyncUpdate();
HEAD:src/PluginProcessor.cpp:1227:                        triggerAsyncUpdate();
HEAD:src/PluginProcessor.cpp:2443:    // calls back (triggerAsyncUpdate) once the bus is actually silent, and
```

Line 2443 is a comment. The two code sites are analysed below.

### 1.1 Site A — `PluginProcessor.cpp:1070` (inside `prepareToPlay`)

Enclosing function: `GuitarCompanionProcessor::prepareToPlay(double, int)`, defined at
`src/PluginProcessor.cpp:896`.

Verbatim guard (`src/PluginProcessor.cpp:1065-1075`):

```cpp
    // A device change must never leave the guitar stuck inside a scene fade:
    // still apply whatever was queued, then start again at unity gain.
    if (sceneEnvState.load() != (int) SceneEnv::idle)
    {
        if (scenePendingSection.load() >= 0)
            triggerAsyncUpdate();
        sceneEnvState.store ((int) SceneEnv::idle);
    }
    sceneEnvGain = 1.0f;
    sceneLastSection = -1;
```

**Reachable from `processBlock`? NO.**

Reason: `prepareToPlay` is a JUCE `AudioProcessor` lifecycle callback invoked by the
host / audio-device setup path, not by the audio callback, and JUCE guarantees it is
not called concurrently with `processBlock` (the fork itself relies on this: the
comment at `src/PluginProcessor.cpp:1049-1050` says *"prepareToPlay is not concurrent
with processBlock; it can allocate/touch the active and pending models"*). The
`triggerAsyncUpdate()` here still posts a system message, but it is on a control
thread where that is permitted. It is **not** a §7.1 violation.

### 1.2 Site B — `PluginProcessor.cpp:1227` (inside `processBlock`) — **REACHABLE**

Enclosing function: `GuitarCompanionProcessor::processBlock(juce::AudioBuffer<float>&,
juce::MidiBuffer&)`, defined at `src/PluginProcessor.cpp:1099`.

Verbatim guard (`src/PluginProcessor.cpp:1218-1229`):

```cpp
            switch (st)
            {
                case SceneEnv::fadeOut:
                    g1 = juce::jmax (0.0f, g0 - (float) (n / (sr * sceneFadeOutSec)));
                    if (g1 <= 0.0f)
                    {
                        sceneHoldLeft.store ((int) (sr * sceneHoldMaxSec));
                        sceneEnvState.store ((int) SceneEnv::hold);
                        // silent now: safe for the message thread to swap the rig
                        triggerAsyncUpdate();
                    }
                    break;
```

The whole scene block is entered only when `st != SceneEnv::idle || sceneEnvGain < 1.0f`
(`src/PluginProcessor.cpp:1211-1212`).

**Reachable from `processBlock`? YES — conditional.**

`processBlock` is the audio callback. Line 1227 is textually inside it, in the
`SceneEnv::fadeOut` branch, and executes when `g1 <= 0.0f`, i.e. the output fade has
reached silence. The precise runtime condition is:

> **Condition:** a scene envelope is in the `fadeOut` state and the per-block fade ramp
> in the current `processBlock` call reaches `g1 <= 0.0f`.

`sceneEnvState` is set to `fadeOut` **only** by `armSceneEnvelope(int sec)` at
`src/PluginProcessor.cpp:2440-2451`:

```cpp
void GuitarCompanionProcessor::armSceneEnvelope (int sec)
{
    // Any thread. Queues the section and starts the fade out; the audio thread
    // calls back (triggerAsyncUpdate) once the bus is actually silent, and
    // handleAsyncUpdate does the real work from there.
    if (sec < 0 || sec >= drum::maxSections)
        return;

    sceneGen.fetch_add (1);          // invalidates any apply still in progress
    scenePendingSection.store (sec);
    sceneEnvState.store ((int) SceneEnv::fadeOut);
}
```

`armSceneEnvelope` has exactly two callers:

1. `applySceneForSection(int sec)` at `src/PluginProcessor.cpp:2461` — message thread
   (UI: Song "APPLY", RIG menu, "NEXT SCENE").
2. `processDrums(...)` at `src/PluginProcessor.cpp:2648` — **audio thread**, reached
   from `processBlock` at `src/PluginProcessor.cpp:1270`
   (`processDrums (buffer, numOut, n);`). The guard there is:

   ```cpp
   if (scenesOn.load())
   {
       const int bar = drumEngine.uiBar.load();
       const int sec = bar >= 0 ? bar / drum::barsPerSection : -1;
       if (sec != sceneLastSection)
       {
           sceneLastSection = sec;
           if (sec >= 0 && sec < drum::maxSections && sceneArmed[sec].load())
               armSceneEnvelope (sec);
       }
   }
   ```
   (`src/PluginProcessor.cpp:2638-2650`)

So the full audio-thread trigger is:

`processBlock` (`:1270`) → `processDrums` (`:2648`, when `scenesOn` is true and a section
with a stored scene is entered) → `armSceneEnvelope` sets `sceneEnvState = fadeOut`
→ the next block's scene envelope (`:1220-1228`) fades for `sceneFadeOutSec` (12 ms)
→ when the ramp hits 0, `triggerAsyncUpdate()` at `:1227`.

**Conclusion:** Site B is reachable from `processBlock` whenever a scene change is
armed — i.e. **only while a scene envelope is playing**. It cannot fire on a normal
block with no scene armed (`st == idle`, `sceneEnvGain == 1`). An honest statement of
reachability is therefore *"conditionally reachable: on the first audio block in which
an armed scene envelope's fade-out completes"*.

### 1.3 What `triggerAsyncUpdate()` actually does on a non-message thread (JUCE 8)

Verified against JUCE 8.0.4 source and the official class reference (URLs in §6).

`AsyncUpdater::triggerAsyncUpdate()` (`juce_events/broadcasters/juce_AsyncUpdater.cpp`):

```cpp
void AsyncUpdater::triggerAsyncUpdate()
{
    JUCE_ASSERT_MESSAGE_MANAGER_EXISTS
    if (activeMessage->shouldDeliver.compareAndSetBool (1, 0))
        if (! activeMessage->post())
            cancelPendingUpdate();
}
```

1. `shouldDeliver` CAS is lock-free (one byte/atomic).
2. On success it calls `MessageManager::MessageBase::post()`
   (`juce_events/messages/juce_MessageManager.cpp`), which calls the platform
   `postMessageToSystemQueue`.
3. On the shipping Windows target the post path is
   `juce_events/native/juce_Messaging_windows.cpp`:
   `MessageManager::postMessageToSystemQueue` → `InternalMessageQueue::postMessage`,
   which does:

   ```cpp
   {
       const ScopedLock sl (lock);          // <-- a blocking CriticalSection
       shouldTriggerMessageQueueDispatch = messageQueue.isEmpty();
       messageQueue.add (message);          // <-- ReferenceCountedArray: may reallocate
   }
   if (! shouldTriggerMessageQueueDispatch)
       return;
   ...
   PostMessage (juce_messageWindowHandle, customMessageID, 0, 0);   // <-- system post
   ```

4. `handleAsyncUpdate()` is later invoked by the message thread when the posted
   message is delivered.

The official reference is explicit about the real-time hazard; verbatim from
<https://docs.juce.com/master/classjuce_1_1AsyncUpdater.html>:

> It's thread-safe to call this method from any thread, **BUT beware of calling it from a
> real-time (e.g. audio) thread, because it involves posting a message to the system
> queue, which means it may block (and in general will do on most OSes).**

So calling it from `processBlock`:

* posts a **system message** — forbidden outright by SPEC §7.1 ("System-message
  posting" and "`juce::AsyncUpdater::triggerAsyncUpdate()` or equivalent");
* takes a **blocking `CriticalSection`** (`InternalMessageQueue::lock`) shared with the
  message thread — a blocking lock, forbidden by SPEC §7.1 and §18.1;
* **may heap-allocate** inside `ReferenceCountedArray::add` when the internal queue
  grows — forbidden by SPEC §7.1/§18.1;
* **may block** in `PostMessage`/the lock, per the JUCE documentation above — forbidden
  by SPEC §7.1 ("waiting for UI") and §18.1 ("zero callback blocking locks", "zero
  callback waits").

Note: in JUCE 8.0.4 the `AsyncUpdaterMessage` object itself is allocated once in the
`AsyncUpdater` constructor, so the *message object* is not newly allocated per call.
The allocation risk is the `ReferenceCountedArray` growth on the queue side, and the
blocking/locking risk is independent of that. This distinction matters for the
instrumentation plan in §5.

### 1.4 Why this is the single most important open question — and the answer

SPEC.md §2.1 and DEVPLAN `RT-001`/`FND-003` call this out. The answer is a qualified
**yes**: the callback can reach `triggerAsyncUpdate()` at `:1227`, conditionally on an
armed scene envelope. It is therefore a genuine P0 that must be replaced (see F1 in
§3), not a theoretical one.

---

## 2. Callback-reachable subsystem table

"Reaches `processBlock`?" = can a `processBlock` call reach this subsystem's entry
function. Allocation evidence is from code reading; JUCE primitives are cited by their
documented contract. "Third-party" = code outside `src/` that the callback calls and
whose internal real-time behaviour this fork does not control.

| Subsystem | Entry function (file:line) | Reaches `processBlock`? | alloc/free | locks | message posting | file/network I/O | unbounded loop | third-party calls | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| `processBlock` itself (scene envelope) | `GuitarCompanionProcessor::processBlock` `src/PluginProcessor.cpp:1099` | yes (it is the callback) | none directly | none directly | **`triggerAsyncUpdate()` @ `:1227`** | no | no (switch + bounded for) | no | **P0 (F1)** |
| Guitar DSP chain (gate/OD/EQ/delay/reverb/mod/pitch/looper/limiter/wah/harm/oct/ringmod/bitcrush/slowgear/exciter/deesser/tape/console/analyzer) | dispatch at `:1158-1194`; modules `:1356-2311` | yes | none (all arithmetic + preallocated buffers) | none (atomics only) | no | no | no (all `for i<n` / bounded) | JUCE `dsp::Chorus/Phaser/Limiter/Compressor`, `Reverb`, `DelayLine`, `AudioBlock` | OK; F7 for `processHarmFx` CPU |
| NeuralAmpModeler model path | `processAmpAndCabs` `src/PluginProcessor.cpp:2919`; model `:2947`, resampler `:2945` | yes | none in fork code; model/resampler prepared off-thread (`prepareLoadedModel` `:868`, called in `prepareToPlay` `:1056/:1061` and in the loader job `:3240`) | none | no | no | no (chunk loop `:2939`, bounded) | **`nam::DSP::process`, `dsp::ResamplingContainer::ProcessBlock`** | Functionally RT-safe by design, but third-party ⇒ **P1 (F6)** |
| `DrumEngine` | `processDrums` `src/PluginProcessor.cpp:2604` → `DrumEngine::process` `src/DrumEngine.cpp:265` | yes | **`MidiBuffer::addEvent` @ `DrumEngine.cpp:233-234, :383`** can grow `Array<uint8>` (reserved 256 B @ `PluginProcessor.cpp:1019`) ⇒ **P0 (F2)**. Internal sampler path allocates nothing. | none (atomics) | no | no | no (schedule/audition loops advance by ≥1 sample; `:299-331`, `:360-373`) | hosted drum VST `processBlock` @ `DrumEngine.cpp:392` ⇒ **P1 (F6)**; `getPlayHead()` `PluginProcessor.cpp:2608` ⇒ P2 (F5) | **P0**, see F2 |
| `DrumLibrary` / `DrumGenerator` | `drum::library()` `src/DrumLibrary.cpp:15`, `drum::parseSpec` `src/DrumEngine.cpp:36` | **no** | allocates (`std::vector`, `juce::String`) but only off-thread | none | no | no | n/a | n/a | **Not reachable.** Only `src/DrumOverlay.cpp:1074,2546,…` (UI) calls `library()`; the callback reads `DrumEngine`'s atomics. |
| Recording path | `startRecording` `src/PluginProcessor.cpp:2324`, `stopRecording` `:2366`; callback writes at `:1259-1265`, `:1272-1278`, `:1287-1293` | writes yes; control no | none in callback | none | no | no (disk I/O done by `ThreadedWriter` on `recThread`, `:2345`) | no | JUCE `AudioFormatWriter::ThreadedWriter` (RT-safe FIFO), contract at SPEC ref | OK. Delete deferred with `Timer::callAfterDelay` at `:2374-2378`. |
| Scene-change path | `armSceneEnvelope` `src/PluginProcessor.cpp:2440`; `handleAsyncUpdate` `:2538`; `applySceneNow` `:2484`; `applyState` `:3783` | **conditional yes** via `:1227`; the apply itself is message-thread only | apply allocates (`ValueTree`, `JSON`, `String`) but off-thread | `modelInfoLock` in apply (message/loader only) | **`triggerAsyncUpdate` @ `:1227`** | file I/O in `loadModelAsync` (`:3266`,`:3314`) — loader thread only | no | `nam::get_dsp` on loader thread | **P0 (F1)** for `:1227`; the rest is correctly off-thread |
| Analyser / spectrum display path | `processAnalyzerFx` `src/PluginProcessor.cpp:2298` (callback write to `anRing`); `readAnalyzerBlock` `:2313` (UI read) | **write: yes; display: no** | none | none | no | no | no (bounded ring) | no | OK. F5 notes the benign-but-formal data race on `anRing`/`tunerRing`. |
| Tuner path | tap `src/PluginProcessor.cpp:1142-1151` (callback write to `tunerRing`); `readTunerBlock` `:282` (UI read) | **write: yes; read: no** | none | none | no | no | no (bounded ring) | no | OK. See F5 (formal data race). |
| Plugin-catalog path | `src/PluginCatalog.cpp` (`plugcat`) | **no** | allocates (`Array`, `File`, network) | none | no | file/network | n/a | download/install | **Not reachable.** Referenced only from UI/editor (`src/PluginEditor.cpp`) and `StoreOverlay.cpp`. |
| External VST3 slots (8) | `processExtFx` `src/PluginProcessor.cpp:2563`; guest call `:2591` | yes (when slot loaded+on) | fork side: none; **`extMidi` growth up to 64 B reserved (`:1013`) is guest-controlled ⇒ P1 (F2b)** | none | no | no | no (`for i<n`) | **guest `AudioPluginInstance::processBlock`** | **P1 (F6)**; also F2b |
| Looper | `processLooperFx` `src/PluginProcessor.cpp:1881` | yes | none (buffer preallocated `:999`) | none | no | no | no (bounded `for i<n`, bounds-checked `pos<maxLen`) | no | OK |
| Limiter | `processLimiterFx` `src/PluginProcessor.cpp:1949` | yes | none | none | no | no | no | JUCE `dsp::Limiter` | OK |
| CPU meter | `src/PluginProcessor.cpp:1300-1307` | yes | none | none | no | no | no | `juce::Time::getHighResolutionTicks` | OK |
| Host transport read | `getPlayHead()->getPosition()` `src/PluginProcessor.cpp:2608-2611` | yes | unknown (host virtual) | unknown (host) | no | no | unknown (host) | host implementation | **P2 (F5b)** |

---

## 3. Findings, ranked

### F1 — P0 — `triggerAsyncUpdate()` reachable from `processBlock`

* **What:** `triggerAsyncUpdate()` is called on the audio thread from inside
  `processBlock` (`src/PluginProcessor.cpp:1227`, scene `fadeOut` completion), posting a
  system message and taking JUCE's internal message-queue `CriticalSection`.
* **Evidence:** `src/PluginProcessor.cpp:1227`; guard `:1218-1229`; reachable via
  `processBlock:1270 → processDrums:2604 → armSceneEnvelope:2648 → armSceneEnvelope:2440`;
  JUCE 8.0.4 `juce_AsyncUpdater.cpp`, `juce_MessageManager.cpp`
  (`MessageManager::MessageBase::post`), `juce_Messaging_windows.cpp`
  (`InternalMessageQueue::postMessage`); upstream docs quote in §1.3.
* **Why it violates SPEC §7.1:** §7.1 forbids "System-message posting" and names
  `juce::AsyncUpdater::triggerAsyncUpdate()` explicitly; §18.1 forbids callback
  blocking locks, waits and allocations. The post path has all three risks
  (system post, blocking `CriticalSection`, possible `ReferenceCountedArray` growth).
* **Proposed bounded RT-safe replacement (no behaviour change):**
  1. Remove `private juce::AsyncUpdater` from `GuitarCompanionProcessor`
     (`src/PluginProcessor.h:25`) and the `handleAsyncUpdate()` override.
  2. At the point where the fade reaches silence (`:1222-1228`), replace the call with a
     single relaxed atomic store — e.g. an existing field already carries the section
     (`scenePendingSection`), so add `std::atomic<bool> sceneReadyToApply { false }` and
     store `true`, or simply flip `sceneEnvState` to `hold` (already done) and let the
     message thread poll it.
  3. The message thread polls on a `juce::Timer` owned by the processor (or the existing
     editor timer) at ~20–30 Hz: when `sceneEnvState == hold && scenePendingSection >= 0`,
     call the current `handleAsyncUpdate()` body (extracted to a plain method,
     `applyPendingScene()`), which runs `applySceneNow`/`applyState` off the audio
     thread.
  4. Keep the existing message-thread safety net (`applySceneForSection`'s
     `Timer::callAfterDelay`, `:2468-2481`) for the no-device case; it can simply call
     the same `applyPendingScene()`.
  This is exactly the "atomic pending flag consumed by a message-thread timer" option
  named in DEVPLAN `RT-001`, and it also removes the second site's dependence on
  `AsyncUpdater` (Site A at `:1070` would become an atomic flag too).
* **Not implemented** (read-only audit).

### F2 — P0 — `juce::MidiBuffer` growth on the callback path (conditional allocation)

* **What:** `DrumEngine` adds note events to a JUCE `MidiBuffer` from the audio thread.
  `MidiBuffer` stores events in a `juce::Array<uint8>` (`MidiBuffer` has a public
  `Array<uint8> data;`); `addEvent` appends and can **reallocate** (malloc/free) when the
  byte budget is exceeded. The buffers are only *pre-sized*, never *capped*:
  `extMidi.ensureSize(64)` (`src/PluginProcessor.cpp:1013`) and
  `drumMidi.ensureSize(256)` (`:1019`).
* **Evidence:** `src/DrumEngine.cpp:233-234` (`midi.addEvent(noteOn…)`) and
  `:383` (`midi.addEvent(noteOff…)`); `:265` entry; called from
  `src/PluginProcessor.cpp:2657`; reservation at `:1019`. JUCE 8.0.4 `MidiBuffer`
  declaration (`Array<uint8> data;`, `void ensureSize(size_t)`, `bool addEvent(...)`).
* **Why it violates SPEC §7.1/§18.1:** §18.1 requires **zero callback heap allocations**
  and §7.1 forbids heap allocation/deallocation outright. A `MidiBuffer` that outgrows
  its reserved capacity will allocate on the audio thread.
* **Condition / honest scope:** reachable only when a hosted drum VST3 is the drum
  source (`DrumEngine::fireHit` calls `addEvent` only when `vst != nullptr`;
  `src/DrumEngine.cpp:230`). With the internal sampler the callback adds no MIDI events
  at all. With the typical 9 voices and the prepared block sizes, ≤ ~18 events/block
  (~126 B) normally fits the 256 B reservation — but nothing enforces that, and it is
  not proven for every block size / pattern / count-in combination. This is a
  *conditional* allocation, not an observed one.
* **Proposed bounded replacement:**
  1. Compute a hard worst-case byte budget in `prepareToPlay`: `(maxStepsPerBlock *
     numVoices * 2 * bytesPerEvent)`, where `maxStepsPerBlock =
     ceil(preparedBlockSize / minStepLenSamples)`, and reserve that with `ensureSize`.
  2. Add a hard cap: before each `addEvent`, check free capacity; if the cap would be
     exceeded, drop the event and increment an atomic `midiDropCount`. Dropping an
     inaudible scheduled hit is strictly better than allocating in the callback.
  3. Expose `midiDropCount` for the diagnostics view.
  (Alternatively, replace the JUCE `MidiBuffer` on this path with a fixed-capacity
  POD event array.)

### F2b — P1 — guest-controlled `MidiBuffer` handed to hosted VST3 effects

* **What:** `processExtFx` clears `extMidi` and passes it to a hosted effect's
  `processBlock` (`src/PluginProcessor.cpp:2590-2591`). The guest can add an arbitrary
  number of events, growing the 64-byte reservation (`:1013`) on the audio thread.
* **Evidence:** `src/PluginProcessor.cpp:2591`; reservation `:1013`.
* **Why:** same §7.1/§18.1 allocation rule, but the trigger is inside unaudited guest
  code, so it overlaps F6.
* **Proposed bounded replacement:** `ensureSize` to a conservative worst case in
  `prepareToPlay`; wrap the guest call with a capacity check (or pass a
  fixed-capacity buffer) and count drops. Combined with F6's isolation policy.

### F6 — P1 — unaudited third-party `processBlock` on the callback path

* **What:** the callback invokes guest binaries/extensions: NAM model
  (`src/PluginProcessor.cpp:2947`), the resampler (`:2945`), the hosted drum VST3
  (`src/DrumEngine.cpp:392`), and up to 8 hosted effect VST3s
  (`src/PluginProcessor.cpp:2591`).
* **Evidence:** as listed.
* **Why it violates SPEC §7.1:** §7.1 forbids "Any new third-party call not audited for
  bounded real-time behavior" and "Runtime model loading". These calls are inherent to
  the product (SPEC §4.1 explicitly wants an optional hosted drum VST3; NAM is core),
  but their internal RT behaviour cannot be audited from this repository.
* **Honest scope:** NAM and the resampler are purpose-built RT DSP and are prepared
  off-thread; the concrete risk is hosted *third-party* plugins, which the user installs.
  SPEC §24 already accepts that third-party plugins are untrusted; the correct output of
  this audit is to *name them as an accepted, bounded risk* and to keep them behind the
  swap protocol, not to claim they are proven safe.
* **Proposed bounded replacement / mitigation (policy, not a code fix):**
  keep the pending/retired swap protocol (already present: `:1108-1127`,
  `:2565-2578`, `:2620-2632`); never construct/destroy a guest on the audio thread
  (already true); keep guest MIDI buffers capped (F2b); surface a per-slot
  callback-overrun/latency diagnostic; and document in THIRD_PARTY.md that hosted
  plugins are RT-unaudited. No fully bounded replacement is possible for arbitrary guest
  code — that is a product-level accepted risk, stated as such.

### F5 — P2 — host transport call and a lock-free CAS retry loop

* **F5a (P2):** `getPlayHead()->getPosition()` on the callback
  (`src/PluginProcessor.cpp:2608-2611`) is a virtual call into the host. On the
  standalone it is a no-op; in a DAW it executes host code whose allocation/blocking
  behaviour this fork does not control. Proposed: keep (it is the standard JUCE way to
  read host transport) but document it as host-dependent; optionally read BPM on a
  message-thread timer via `AudioPlayHead` where available.
* **F5b (P2):** `publishPeak` (`src/DrumEngine.cpp:88-94`) is a relaxed
  `compare_exchange_weak` retry loop. SPEC §7.1 forbids "unbounded retry/spin loops".
  In practice there is exactly one audio writer and one UI drainer, so it settles in one
  iteration, but it is not *formally* bounded. Proposed: cap retries and fall back to a
  relaxed store (the value is a cosmetic meter), or publish with a plain relaxed store
  and let the UI read-and-clear (the current UI contract already consumes with
  `exchange(0)`).

### F7 — P2 — CPU-heavy bounded loop in `processHarmFx`

* **What:** every ~1024 input samples, `processHarmFx` runs a normalised
  autocorrelation over 512 decimated samples for all lags in ~[6,85]
  (`src/PluginProcessor.cpp:2159-2173`), i.e. tens of thousands of multiply-adds inside
  the callback.
* **Why:** not a §7.1 violation (the loop count is fixed and bounded), but a §18.2
  timing-gate risk at small buffer sizes. Proposed: keep, but measure under the timing
  gate; if p99 load regresses, throttle detection or move it to the analysis thread
  later. No code change now.

### F3 — P2 (negative-result note, not a violation) — ring-buffer data races

* **What:** `tunerRing` (`src/PluginProcessor.h:347`) and `anRing` (`:351`) are plain
  `float[]` written by the audio thread (`src/PluginProcessor.cpp:1147`, `:2307`) and
  read by the message thread (`:288`, `:2319`) without atomics or fences.
* **Why it matters:** formally a C++ data race (UB); the code comments call the read
  "benign". It is not a §7.1 forbidden operation (no lock/allocation), so it is ranked
  below the callback-safety findings, but it belongs in the audit.
* **Proposed:** mark the arrays `std::atomic<float>` (or use a release/acquire double
  buffer) so the read is well-defined; the cost is negligible and the behaviour is
  unchanged.

### Non-findings (explicitly clean)

* No `new`/`delete`/`malloc`/`free`/`push_back`/`resize`/`std::vector`/`std::string`
  construction on the traced callback path. (`delete` appears only in
  `releaseResources` `:1081`, the destructor `:845-856`, loader jobs `:3291-3293`,
  message-thread setters `:3392`, and `collectExternalRetired` `:243-244` — none callable
  from `processBlock`.)
* No `ScopedLock`/`CriticalSection` on the callback path: every `modelInfoLock` use is
  in a message/loader-thread function (`:2688, :2710, :2725, :2747, … , :4175`).
* No `DBG`/`Logger::`/`std::cout`/`printf` anywhere under `src/`.
* No file/network I/O on the callback path: all `File`, `JSON::parse`,
  `toXmlString`, `fromXml`, `createOutputStream` occurrences are in message/loader
  functions (`startRecording`, `loadModelAsync`, `applyState`, presets, `sceneSummary`).
* No `std::thread`/`sleep_for`/`wait(` in `src/*.cpp`/`src/*.h` (verified by grep: no
  matches). Background work uses `juce::ThreadPool loaderPool` (`PluginProcessor.h:463`)
  and `juce::TimeSliceThread recThread` (`:355`), both off the callback.

---

## 4. What is NOT reachable, and why (trustworthy negatives)

A reviewer must be able to trust these. Each was checked by reading the function body and
grepping its callers.

1. **`prepareToPlay`'s `triggerAsyncUpdate()` (`:1070`).** `prepareToPlay` is a host/device
   lifecycle callback; the fork itself documents in the same function that it is not
   concurrent with `processBlock` (`:1049-1050`). Not reachable from the callback.
2. **All GUI files** — `PluginEditor.*`, `DrumOverlay.*`, `SongOverlay.*`,
   `StoreOverlay.*`, `AudioOverlay.*`, `LookAndFeel.h`, `ToneWebView.*`. None are called
   from `processBlock` or `DrumEngine::process`. `PluginProcessor.cpp` includes
   `PluginEditor.h` only to build the editor; the callback calls no editor method.
3. **`DrumLibrary.cpp` / `DrumGenerator.*`.** The callback reads only `DrumEngine`'s
   atomics. `drum::library()`/`drum::parseSpec()` are referenced from
   `src/DrumOverlay.cpp` (UI) and `src/DrumLibrary.cpp` internals; the results are written
   into `DrumEngine` atomic patterns on the message thread.
4. **`PluginCatalog.*`.** Scan/download/install logic; referenced only from UI code.
5. **`Tone3000Client.*` / `ToneWebView.*`.** Network/UI; never referenced by the
   processor's callback.
6. **`jam/` (jam-core).** No translation unit in `src/` outside `src/jam/` includes any
   jam header (verified by grep: zero matches for `AnalysisAudioRing|MusicalClock|
   JamDirector|IRhythmTracker|RhythmTypes|JamConfig` in `src/*.{cpp,h}`). Per ADR-0003 §4
   the analysis tap is deliberately not wired yet. So the callback cannot reach jam-core
   today; when INT-ANALYSIS-001 wires `AnalysisAudioRing::push`, that becomes a *new*
   callback-reachable site and must be re-audited (the ring itself is allocation-free and
   bounded — `src/jam/AnalysisAudioRing.h:54-80`).
7. **Scene apply / model / IR / preset / state work.** `armSceneEnvelope`,
   `applySceneForSection`, `handleAsyncUpdate`, `applySceneNow`, `applyState`,
   `loadModelAsync`, `loadIrAsync`, `loadDrumPluginAsync`, `loadExternalPluginAsync`,
   `unloadModelLane`, `unloadIrSlot`, `startRecording`, `stopRecording`,
   `exportLoopToWav`, `captureState`, `getStateInformation`, `setStateInformation`,
   and all preset functions run on the message thread, the `loaderPool`, or the
   `recThread`. The only callback-side effect of the model swap is the allocation-free
   atomic exchange at `:1108-1127` (`reset(p)` after `release()`, no `delete`).
8. **File/JSON/XML I/O.** `loadFileAsString`/`JSON::parse` (`:3266-3267`, `:3314`),
   `toXmlString`/`fromXml` (`:2428-2429`, `:2489`, `:2507`, `:4107`),
   `createOutputStream` (`:2341`, `:2906`) are all off the callback.
9. **`MessageManager::callAsync` (`:3300`, `:3340`).** Both are inside the
   `loaderPool.addJob` lambda (`:3209`), i.e. loader thread, not the callback.
10. **`juce::Timer::callAfterDelay` (`:2374-2378`, `:2468`).** Message-thread stop/apply
    paths; not the callback.
11. **`juce::Convolution::loadImpulseResponse` (`:3569`).** Message thread
    (`loadIrAsync`); the Convolution swaps its IR internally RT-safely, and the callback
    only reads `getCurrentIRSize()`/`process`.

---

## 5. Instrumentation plan (propose only — do NOT implement here)

Goal: let a future task *prove* SPEC §18.1 (zero allocations, zero deallocations, zero
message posts, zero blocking locks) on the real callback without changing behaviour.
All items are debug-build-only and must be inert in Release.

1. **Allocation counter.** In the test binary only, override global `operator new`,
   `operator new[]`, `operator delete`, `operator delete[]` to increment relaxed
   atomics and record the current thread id. `processBlock` sets a
   `thread_local bool gInAudioCallback` on entry (a tiny RAII guard, no allocation) and
   clears it on exit. A single `processBlock` that finishes with a non-zero delta fails
   the gate. This directly tests F2/F2b and catches anything missed.
2. **Message-post counter.** Wrap the (post-F1) scene-signalling path; assert the
   callback never calls `triggerAsyncUpdate`/`MessageManager::postMessage`. Before F1 is
   fixed, a debug-only counter incremented at `:1227` *proves* the reachability claim
   under a scene-change stress scenario; after F1 it must stay at zero. The existing
   `handleAsyncUpdate` body can be instrumented to record which thread invoked it
   (must be the message thread).
3. **Lock-depth probe.** In debug, replace `juce::ScopedLock` on `modelInfoLock`
   with a wrapper that maintains a `thread_local` depth. Assert the audio-callback flag
   is false whenever the depth > 0. This proves no callback reaches a lock.
4. **`MidiBuffer` high-water.** In `DrumEngine::process`, after scheduling, compare the
   block's event bytes against the reserved capacity (`drumMidi`/`extMidi`) and publish
   the high-water mark + a drop counter to diagnostics. This turns F2's "conditionally
   possible" into an observable number.
5. **Third-party boundary timers.** Wrap the guest `processBlock` calls (NAM, resampler,
   ext VST, drum VST) with `juce::Time::getHighResolutionTicks` deltas published to
   atomics, so the timing gate (§18.2) can attribute overruns to a guest.
6. **Callback load already exists** (`cpuLoad`, `src/PluginProcessor.cpp:1300-1307`);
   the above should be reported alongside it in the diagnostics view (§22), never logged
   from the callback.

A stress scenario to exercise the hazards: `scenesOn = true`, alternating scenes with a
hosted drum VST3 and at least one hosted effect slot, plus an aggressive pattern; run for
30 minutes at 48 kHz/128 and assert every counter above is zero (except the drop counters).

---

## 6. Sources used

* Repository code at `48f301c`: `src/PluginProcessor.{h,cpp}`, `src/DrumEngine.{h,cpp}`,
  `src/PluginProcessor.h`, `src/jam/*`, `SPEC.md` §7.1/§7.4/§18.1/§18.2/§22,
  `DEVPLAN.md` §FND-003/§RT-001, `CONTRIBUTING.md`, `docs/adr/0003-*.md`.
* JUCE 8.0.4 (the framework version this fork pins):
  * `modules/juce_events/broadcasters/juce_AsyncUpdater.{h,cpp}`
  * `modules/juce_events/messages/juce_MessageManager.cpp`
  * `modules/juce_events/messages/juce_CallbackMessage.h`
  * `modules/juce_events/native/juce_Messaging_windows.cpp`
  * `modules/juce_audio_basics/midi/juce_MidiBuffer.h`, `juce_MidiMessage.h`
  * Class reference: <https://docs.juce.com/master/classjuce_1_1AsyncUpdater.html>
* SPEC.md §27 already lists the AsyncUpdater doc URL; the verbatim warning quoted in
  §1.3 matches that page as retrieved on 2026-10-07.

### Spot-check commands (reproduce the key claims)

```bash
git grep -n triggerAsyncUpdate HEAD -- src/
# -> :1070 and :1227 (code), :2443 (comment)

sed -n '1218,1229p' src/PluginProcessor.cpp     # the reachable site
sed -n '1065,1075p' src/PluginProcessor.cpp     # the prepareToPlay site
sed -n '2638,2657p' src/PluginProcessor.cpp     # armSceneEnvelope reachable from audio

grep -nE "\bnew \b|\bdelete \b|malloc|free\(|ScopedLock|CriticalSection" src/PluginProcessor.cpp
grep -nE "\bDBG\b|Logger::|std::cout|std::cerr|printf" src/*.cpp src/*.h   # -> no matches
grep -rn "drum::library()\|parseSpec" src/       # -> UI only
grep -rn "AnalysisAudioRing\|MusicalClock\|JamDirector" src/*.cpp src/*.h  # -> no matches
```

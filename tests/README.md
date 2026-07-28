# Guitar Companion - unit tests

Small, dependency-free test suite for the **Drums** module: the compact groove
spec parser (`drum::parseSpec`), the factory library data (~570 grooves in
`src/DrumLibrary.cpp`), the procedural generator (`drum::generateBar`) and the
bar codec used for persistence (`DrumEngine::barToString` / `barFromString`).

The tests are **OFF by default** (`GUITAR_COMPANION_BUILD_TESTS=OFF`): a normal build
of the plugin is not affected in any way and the test target does not even
exist unless you ask for it.

## Running them

Always use a **separate build directory** - `build/` is the plugin's and is
usually busy:

```sh
cmake -B build-tests -DGUITAR_COMPANION_BUILD_TESTS=ON
cmake --build build-tests --config Release --target GuitarCompanionTests
ctest --test-dir build-tests -C Release --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 4` in well under a second
(the first build takes a few minutes because JUCE and the embedded assets have
to be compiled from scratch in the new directory).

You can also run the executable directly - it prints every case and every
`info:` line:

```sh
build-tests/tests/GuitarCompanionTests_artefacts/Release/GuitarCompanionTests.exe
```

An optional argument filters the test cases by substring, which is what the
four `ctest` entries do (`spec_`, `library_`, `generator_`, `codec_`):

```sh
GuitarCompanionTests.exe codec_          # only the bar codec cases
GuitarCompanionTests.exe weckl           # only the "linear drummer" case
```

Exit code 0 means everything passed. A filter that matches nothing is an
error, so a renamed case can never pass silently.

## Adding a test

No external framework (the repo builds offline from submodules, so no
Catch2/FetchContent). The harness is `TestHarness.h`, about 30 lines:

```cpp
#include "TestHarness.h"
#include "DrumEngine.h"

TEST_CASE (spec_my_new_check)
{
    CHECK (2 + 2 == 4);                       // records a failure, keeps going
    CHECK_MSG (a == b, "context for humans"); // same, with a message
    REQUIRE (ptr != nullptr);                 // fails AND aborts this case
    INFO_MSG ("just printed, never fails");   // known issues / statistics
}
```

Name the case with the prefix of its suite (`spec_`, `library_`, `generator_`,
`codec_`) so it is picked up by the matching `ctest` entry, and add the file to
`tests/CMakeLists.txt`. Only link production sources that do **not** pull the
UI in: `PluginProcessor.cpp` includes `PluginEditor.h` and would drag the whole
editor (and the NAM core) into the test binary.

## Known issues reported, not failed

* the library ships **16 duplicated `(genre, name)` pairs** - reported by
  `library_duplicate_names_report_informative`;
* `parseSpec` is very lenient (a non-numeric step becomes step 0, a leading `-`
  is read as a range) - reported by `spec_known_lenient_parsing_quirks`.

Both are documented on purpose: the suite must stay green so that a red run
always means a real regression.

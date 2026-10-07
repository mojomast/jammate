# Local JUCE build without root

Debian 13 x86_64, GCC 14.2; pinned JUCE/NAM checkout. Actual successful JUCE
configuration and test build supersede ADR-0003's earlier root-access assumption.
Windows/ASIO and physical audio-device evidence remain separate requirements.

## Dependencies

Initialize NAM's existing nested pins, without moving any gitlink:

```sh
git -C third_party/NeuralAmpModelerCore submodule update --init --depth 1 \
  Dependencies/eigen Dependencies/AudioDSPTools
```

Eigen: `bc3b39870ecb690a623a3f49149a358b95c5781d`.
AudioDSPTools: `0827c6c2fc0deced568536142ea86f189e0b98a1`.

Development packages were downloaded with `apt-get download` (no installation)
and extracted using `dpkg-deb -x` into
`/home/mojo/projects/guitars-build-resume/sysroot`:

| package | downloaded version |
|---|---|
| libasound2-dev | 1.2.14-1+deb13u1 |
| libfontconfig-dev | 2.15.0-2.3 |
| libfreetype-dev | 2.13.3+dfsg-1+deb13u1 |
| libxcomposite-dev | 1:0.4.6-1 |
| libxcursor-dev | 1:1.2.3-1 |
| libxinerama-dev | 2:1.1.4-3+b4 |
| libxrandr-dev | 2:1.5.4-1+b3 |
| libxrender-dev | 1:0.9.12-1 |
| libbz2-dev | 1.0.8-6 |
| libpng-dev | 1.6.48-1+deb13u6 |
| libbrotli-dev | 1.1.0-2+b7 |

Rewrite extracted `.pc` file `prefix=/usr` values to the prefix's absolute `usr`
path. Include both `usr/lib/x86_64-linux-gnu/pkgconfig` and `usr/share/pkgconfig`:
bzip2 metadata is in the latter; omitting it caused helper link failures.
Redirect dangling unversioned library symlinks to existing runtime libraries
in `/usr/lib/x86_64-linux-gnu` by matching filename. All of these changes are
outside the repository, and no system packages were installed.

## Actual environment and commands

```sh
export PATH=/tmp/opencode/venv/bin:$PATH
base=/home/mojo/projects/guitars-build-resume
export TMPDIR="$base/tmp"
export CMAKE_BUILD_PARALLEL_LEVEL=2
export PKG_CONFIG_PATH="$base/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$base/sysroot/usr/share/pkgconfig"
export CPATH="$base/sysroot/usr/include:$base/sysroot/usr/include/freetype2"
export LIBRARY_PATH="$base/sysroot/usr/lib/x86_64-linux-gnu"
cmake -S . -B "$base/plugin" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGUITAR_COMPANION_BUILD_TESTS=ON -DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF \
  -DCMAKE_PREFIX_PATH="$base/sysroot/usr"
cmake --build "$base/plugin" --target GuitarCompanionTests -j 2
ctest --test-dir "$base/plugin" -R '^drums\.' --output-on-failure
cmake --build "$base/plugin" --target GuitarCompanion_Standalone GuitarCompanion_VST3 -j 2
```

Configure built/exported/tested `juceaide`; all five drum ctest suites passed.
The application exposed two upstream Linux portability defects: the IR-length
`juce::jmin<int64>` instantiates an incomplete JUCE SIMD integer overload under
GCC, and `PluginCatalog.cpp` uses Windows-only filesystem enums unconditionally.
Narrow repairs use `std::min<int64>` and platform-specific VST3 locations while
retaining existing Windows paths. The VST3 shared-module link additionally
required `POSITION_INDEPENDENT_CODE=ON` on `nam_core` (the initial link failed
with `R_X86_64_TPOFF32` against NAM's thread-local prewarm state).

Both **Standalone and VST3** targets built successfully; VST3 metadata generation
also completed. Upstream warnings are retained; this is not a warning-free app
claim. Logs live under the above base (`configure.log`, `tests-build.log`,
`app-build.log`). `/tmp` is a full 7.9 GB tmpfs, so compiler scratch and build
outputs use the disk-backed path for this run.

## Artifact record

Release artifacts under `plugin/GuitarCompanion_artefacts/Release/`:

| Artifact | SHA-256 |
|---|---|
| `Standalone/Guitar Companion` | `affa0b31039bfcc4f821d3114d935829e43340e0ee3ff502a533c4611f5b020c` |
| `VST3/Guitar Companion.vst3/Contents/x86_64-linux/Guitar Companion.so` | `2b61a5e0ba272bb06027fc6dcf8b463374f6bfd915a9339f23c0c93a295214aa` |

These are the current fork plus the resumption portability/MIDI fixes, not an
unchanged frozen-upstream build. NAM remains at its original pinned revision.
No physical-device round-trip, Windows/ASIO or audible-playtest measurement was
performed by compiling these artifacts; FND-002 and G0 retain those open items.

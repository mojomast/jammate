#!/usr/bin/env python3
"""Fresh-link adaptive processor sources against a read-only product closure.

This is a new integration lane. The first-live-slice replay protocol and pins
remain historical evidence. JUCE/NAM/assets and unchanged product objects are
reused; every adaptive source, the processor and the editor are fresh compiled.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--product-build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, product, out = args.source.resolve(), args.product_build.resolve(), args.out.resolve()
    if out.exists() and any(out.iterdir()):
        raise SystemExit('Output directory must be empty: preserve every failed and successful run')
    out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('replay_recipe', root / 'tools/live-jam-replay/replay_lib.py')
    recipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recipe)
    vals = recipe.production_flags(str(product))
    product_source = recipe.product_source_dir(str(product), vals)
    if product_source is None or Path(product_source).resolve() != root:
        raise SystemExit('Product source must be the exact --source tree; ABI-mixed archives are rejected')
    dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True)
    if dirty:
        raise SystemExit('Commit the source before measurement; dirty source is rejected')
    cache = (product / 'CMakeCache.txt').read_text()
    suppress = next((line.split('=', 1)[1].strip().upper() for line in cache.splitlines()
                     if line.startswith('CMAKE_SUPPRESS_REGENERATION:')), '')
    if suppress not in ('ON', 'TRUE', 'YES', '1'):
        raise SystemExit('Configure the product with -DCMAKE_SUPPRESS_REGENERATION=ON and build it; '
                         'Ninja dry-run otherwise stops at the always-dirty CMake glob check')
    pending = subprocess.check_output(['ninja', '-n', 'GuitarCompanion_Standalone',
                                       'GuitarCompanion_VST3', 'GuitarCompanionTests'],
                                      cwd=product, text=True)
    if any(word in pending for word in ('Building ', 'Linking ', 'Re-running CMake')):
        raise SystemExit('Product targets are not up-to-date; build them before reusing their closure')
    closure = recipe.extract_link_closure(str(product))
    if not closure['ok']:
        raise SystemExit('No complete product link closure')
    flags = [f'-I{root / "src"}', f'-I{root / "tools/live-jam-replay/src"}']
    for key in ('DEFINES', 'INCLUDES', 'FLAGS'):
        flags += shlex.split(vals[key])
    sources = [root / 'src' / name for name in
               ('PluginProcessor.cpp', 'PluginEditor.cpp', 'DrumEngine.cpp',
                'ui/JamOverlay.cpp', 'ui/JamLivePresenter.cpp')]
    sources += sorted((root / 'src/jam').glob('*.cpp'))
    sources += [root / 'tools/live-jam-replay/src/RtProbeInstrumentation.cpp',
                root / 'tools/live-jam-replay/src/InstrumentSelfCheck.cpp',
                root / 'tools/adaptive-jam-replay/main.cpp']
    # The self-check has its own main and is linked separately.
    commands, objects = [], []
    manifest = {'schema': 'adaptive-jam-replay/build/1', 'source_head': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'fresh_sources': {str(p.relative_to(root)): sha(p) for p in sources},
        'headers': {str(p.relative_to(root)): sha(p) for directory in
                    (root / 'src', root / 'tools/live-jam-replay/src')
                    for p in directory.rglob('*.h')},
        'tools': {str(p.relative_to(root)): sha(p) for p in
                  (root / 'tools/adaptive-jam-replay/build.py',
                   root / 'tools/live-jam-replay/replay_lib.py',
                   root / 'docs/research/ADAPTIVE-REPLAY-PROTOCOL.md')},
        'product_source': recipe.git_info(product_source), 'source_dirty': bool(dirty),
        'product_freshness_check': pending,
        'reused_archives': {p: sha(p) for p in closure['archives']},
        'commands': commands, 'scope': 'actual processor with injected observations; callback-thread probe only'}
    try:
        with (out / 'build.log').open('w') as log:
            for i, source in enumerate(sources):
                obj = out / f'{i}-{source.stem}.o'
                command = ['c++', *flags, '-fvisibility=default', '-Wno-frame-address',
                           '-c', str(source), '-o', str(obj)]
                commands.append(command)
                subprocess.run(command, cwd=product, stdout=log, stderr=log, check=True)
                objects.append(str(obj))
            wraps = [f'-Wl,--wrap={name}' for name in
                     ('malloc', 'calloc', 'realloc', 'free', 'pthread_mutex_lock',
                      'pthread_mutex_trylock', 'pthread_mutex_unlock', 'pthread_cond_clockwait')]
            instrument, selfcheck, driver = objects[-3:]
            check_command = ['c++', instrument, selfcheck, *wraps, '-lpthread', '-ldl',
                             '-o', str(out / 'instrument_selfcheck')]
            commands.append(check_command)
            subprocess.run(check_command, cwd=product, stdout=log, stderr=log, check=True)
            subprocess.run([str(out / 'instrument_selfcheck')], stdout=log, stderr=log, check=True)
            command = ['c++', *objects[:-2], driver, *wraps, '-Wl,--start-group',
                       *closure['inputs'], '-Wl,--end-group', '-o', str(out / 'adaptive_jam_replay')]
            commands.append(command)
            subprocess.run(command, cwd=product, stdout=log, stderr=log, check=True)
        manifest['binary_sha256'] = sha(out / 'adaptive_jam_replay')
        manifest['instrument_selfcheck_sha256'] = sha(out / 'instrument_selfcheck')
        print(out / 'adaptive_jam_replay')
    finally:
        (out / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()

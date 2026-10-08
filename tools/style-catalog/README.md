# tools/style-catalog

Provenance tooling for `src/jam/StyleCatalog.{h,cpp}` (STYLE-DIRECTOR-002).

`src/jam` is JUCE-free and cannot link the compiled drum library, so the catalog
is validated by **source-hash regression + full re-resolution against the
actual library source**.

## Files

| File | Purpose |
|---|---|
| `style_catalog_provenance.py` | Verify (`--check`) / regenerate (`--emit`) every shipped reference against `src/DrumLibrary.cpp`; enforces the sha256 fingerprint. |
| `build_style_catalog.py` | One-shot authoring generator that produced `src/jam/StyleCatalog.cpp`. Kept for review/reproducibility; not run by builds. |
| `generated/StyleCatalogLibraryFixture.h` | Committed snapshot of the ACTUAL entries for the referenced indices. Included by `tests/jam/StyleCatalogTests.cpp`. |
| `library_provenance.json` | Committed manifest: source sha256, all references and their resolved entries. |
| `tests/test_style_catalog_provenance.py` | Unit tests for the tool and the parser. |

## Commands

```sh
# Verify the shipped catalog against the live library and committed snapshot.
python3 tools/style-catalog/style_catalog_provenance.py --check

# Regenerate the fixture + manifest after editing the catalog, then verify.
python3 tools/style-catalog/style_catalog_provenance.py --emit

# Tool/parser unit tests.
python3 -B -m unittest discover tools/style-catalog/tests -p 'test_*.py'
```

`--check` fails loudly if `src/DrumLibrary.cpp` changed, if any catalog index no
longer resolves to the declared genre/name/fill/meter/spec-hash, or if the
committed fixture/manifest is stale.

## Why indices and not names

The library has 570 entries and no string ids; entries are `genre` + `name`, and
that pair is **not unique** (16 collisions, 27 duplicated names). Examples that
would break a name-derived id:

- `("FUNK","Linear funk")` -> groove index 63 **and** fill index 187;
- `("ROCK","Shuffle rock")` -> index 9 (swing 45) and index 535 (swing 33).

The catalog stores positional indices plus full provenance; this tool proves the
indices still mean what the catalog says.

"""Offline research helpers for the TRACK-003 BeatNet feasibility review.

This package contains NO BeatNet code, NO model weights and NO benchmark
numbers. It exists to (a) probe whether a pinned BeatNet could run in the
current environment and (b) score a *declared* BeatNet run against the
repository's rhythm corpus once one exists.

The ``provenance`` block is self-reported metadata: the contract checks that it
is well-formed and the scorer verifies each input WAV hash against the committed
corpus, but neither proves the beats came from BeatNet. Nothing in this package
may synthesise a tracker observation, and no score here is verified BeatNet
evidence without a separately recorded driver run and raw output. See
``io_contract`` and ``README.md``.
"""

__all__ = ["provenance", "corpus", "metrics", "io_contract"]

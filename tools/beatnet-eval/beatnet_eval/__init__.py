"""Offline research helpers for the TRACK-003 BeatNet feasibility review.

This package contains NO BeatNet code, NO model weights and NO benchmark
numbers. It exists to (a) probe whether a pinned BeatNet could run in the
current environment and (b) score a *declared real* BeatNet run against the
repository's rhythm corpus once one exists.

Nothing in this package may be used to synthesise a tracker observation, and
the scorer refuses input that does not carry real-run provenance. See
``io_contract`` and ``README.md``.
"""

__all__ = ["provenance", "corpus", "metrics", "io_contract"]

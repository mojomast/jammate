"""Shared integrity helpers: strict JSON, hashing, safe paths, timestamps.

Every loader here fails closed: non-finite JSON constants, missing files,
unreadable paths and escaping relative paths raise a precise error instead of
silently returning a zero or a default.
"""
from __future__ import annotations

import datetime
import hashlib
import json
import os


class IntegrityError(Exception):
    """An evidence-integrity violation that must fail the run closed."""


def sha256_file(path: str, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    try:
        with open(path, "rb") as fh:
            for block in iter(lambda: fh.read(chunk), b""):
                h.update(block)
    except OSError as exc:
        raise IntegrityError(f"cannot read {path}: {exc}") from exc
    return h.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _reject_constant(name: str) -> None:
    raise IntegrityError(f"non-finite JSON constant not allowed: {name}")


def load_json_strict(path: str):
    """Read JSON with NaN/Infinity rejected and duplicate keys rejected.

    `object_pairs_hook` raising on duplicates stops a manifest that defines the
    same recording twice from being silently collapsed.
    """
    def no_dupes(pairs):
        seen = {}
        for key, value in pairs:
            if key in seen:
                raise IntegrityError(f"duplicate JSON key {key!r} in {path}")
            seen[key] = value
        return seen

    try:
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        raise IntegrityError(f"cannot read {path}: {exc}") from exc
    return json.loads(text, parse_constant=_reject_constant,
                      object_pairs_hook=no_dupes)


def loads_json_strict(text: str, where: str = "<string>"):
    def no_dupes(pairs):
        seen = {}
        for key, value in pairs:
            if key in seen:
                raise IntegrityError(f"duplicate JSON key {key!r} in {where}")
            seen[key] = value
        return seen
    return json.loads(text, parse_constant=_reject_constant,
                      object_pairs_hook=no_dupes)


def dump_json_strict(obj, path: str) -> None:
    """Write deterministic JSON (sorted keys, no NaN, trailing newline)."""
    text = json.dumps(obj, indent=2, sort_keys=True, allow_nan=False)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
        fh.write("\n")


def utc_now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).replace(
        microsecond=0).isoformat().replace("+00:00", "Z")


def safe_resolve(root: str, relpath: str) -> str:
    """Resolve `relpath` under `root`, rejecting absolute paths and escapes.

    Symlinks are resolved before the containment check so a symlink that points
    outside `root` is rejected rather than followed. `root` itself may be a
    symlink; it is resolved once.
    """
    if relpath is None or relpath == "":
        raise IntegrityError("empty path is not allowed")
    if os.path.isabs(relpath):
        raise IntegrityError(f"absolute path not allowed here: {relpath}")
    root_abs = os.path.realpath(root)
    candidate = os.path.realpath(os.path.join(root_abs, relpath))
    if candidate != root_abs and not candidate.startswith(root_abs + os.sep):
        raise IntegrityError(f"path escapes its root: {relpath!r} -> {candidate!r}")
    return candidate


def is_hex64(value) -> bool:
    return (isinstance(value, str) and len(value) == 64
            and all(c in "0123456789abcdef" for c in value))


def is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def finite(value) -> bool:
    import math
    return is_number(value) and math.isfinite(value)

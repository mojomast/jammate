"""Import manifest + annotation model, identity validation and gate eligibility.

The import manifest is the provenance record for one evaluation set. It stores,
per recording: a stable id, the audio location (private paths are allowed and
never copied), the exact audio identity (sha256, rate, channels, frames,
duration), the classification (real / synthetic / derived), ownership and
licence, and an annotation reference. Annotations carry the ground-truth grid,
meter and tempo profile.

Everything is validated against the actual bytes: a mismatched sha256, rate,
duration or frame count is a hard failure, and a recording that claims to be a
real human performance must carry a human-sourced, licensed annotation.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field

from .integrity import (IntegrityError, finite, is_hex64, load_json_strict,
                        safe_resolve, sha256_file)
from .wav import WavInfo, read_wav_info

IMPORT_SCHEMA = "guitar-lock-eval/import-manifest/1.0"
ANNOTATION_SCHEMA = "guitar-lock-eval/annotation/1.0"

CLASSIFICATIONS = ("real", "synthetic", "derived")


@dataclass
class Finding:
    severity: str          # "hard" | "warn"
    code: str
    detail: str

    def as_dict(self) -> dict:
        return {"severity": self.severity, "code": self.code, "detail": self.detail}


@dataclass
class Annotation:
    source: str
    license: str
    tempo_profile: str
    beats: list[float]
    onsets: list[float]
    beats_per_bar: int
    meter_numerator: int
    meter_denominator: int
    true_silence_spans: list[list[float]]
    nominal_bpm: float | None = None
    bpm_start: float | None = None
    bpm_end: float | None = None
    ramp_start_seconds: float = 0.0
    ramp_end_seconds: float = 0.0
    notes: str = ""
    origin: str = ""       # human-readable provenance of the annotation

    def hasNominalBpm(self) -> bool:
        return self.nominal_bpm is not None and self.nominal_bpm > 0.0

    def isSteady(self) -> bool:
        return self.tempo_profile == "constant"

    def isRamp(self) -> bool:
        return self.tempo_profile == "linear-ramp"


@dataclass
class Recording:
    id: str
    audio_path: str
    classification: str
    license: str = ""
    ownership: str = ""
    provenance: str = ""
    representative: bool = True
    parent_id: str | None = None
    tags: list[str] = field(default_factory=list)
    annotation_spec: dict | None = None
    declared: dict = field(default_factory=dict)

    annotation: Annotation | None = None
    audio_path_resolved: str | None = None
    wav: WavInfo | None = None

    def isCore(self) -> bool:
        return "core" in self.tags


@dataclass
class ImportManifest:
    path: str
    schema: str
    task: str
    created_utc: str
    recordings: list[Recording]
    sha256: str
    raw: dict


def _require(condition: bool, code: str, detail: str, findings: list[Finding]):
    if not condition:
        findings.append(Finding("hard", code, detail))


def _number_list(value, where: str) -> list[float]:
    if not isinstance(value, list):
        raise IntegrityError(f"{where} must be a list")
    out = []
    for item in value:
        if not finite(item):
            raise IntegrityError(f"{where} must contain finite numbers")
        out.append(float(item))
    return out


def _spans(value, where: str) -> list[list[float]]:
    if value is None:
        return []
    if not isinstance(value, list):
        raise IntegrityError(f"{where} must be a list of [start,end] spans")
    out = []
    for span in value:
        if (not isinstance(span, list) or len(span) != 2
                or not finite(span[0]) or not finite(span[1])):
            raise IntegrityError(f"{where} span must be [start,end] finite numbers")
        if span[1] < span[0]:
            raise IntegrityError(f"{where} span end < start")
        out.append([float(span[0]), float(span[1])])
    return out


def _annotation_from_obj(obj: dict, where: str) -> Annotation:
    if obj.get("schema") not in (None, ANNOTATION_SCHEMA):
        raise IntegrityError(f"{where}: unexpected annotation schema {obj.get('schema')!r}")
    source = obj.get("source")
    if source not in ("human", "manual", "generated", "derived"):
        raise IntegrityError(f"{where}: annotation.source must be human|manual|generated|derived")
    meter = obj.get("meter")
    if not isinstance(meter, dict):
        raise IntegrityError(f"{where}: annotation.meter required")
    beats = _number_list(obj.get("beats"), f"{where}.beats")
    for i in range(1, len(beats)):
        if not beats[i] > beats[i - 1]:
            raise IntegrityError(f"{where}.beats must be strictly increasing")
    onsets = _number_list(obj.get("onsets", []), f"{where}.onsets")
    tempo = obj.get("tempo_profile")
    if tempo not in ("constant", "linear-ramp"):
        raise IntegrityError(f"{where}: tempo_profile must be constant|linear-ramp")
    nominal = obj.get("nominal_bpm")
    if nominal is not None and not finite(nominal):
        raise IntegrityError(f"{where}: nominal_bpm must be finite")
    if tempo == "constant" and not (nominal is not None and nominal > 0):
        raise IntegrityError(f"{where}: constant tempo requires positive nominal_bpm")
    bpb = meter.get("beats_per_bar")
    if not isinstance(bpb, int) or bpb <= 0:
        raise IntegrityError(f"{where}: meter.beats_per_bar must be a positive integer")
    num = meter.get("numerator", 4)
    den = meter.get("denominator", 4)
    if not isinstance(num, int) or not isinstance(den, int) or num <= 0 or den <= 0:
        raise IntegrityError(f"{where}: meter numerator/denominator must be positive integers")
    return Annotation(
        source=source,
        license=obj.get("license", ""),
        tempo_profile=tempo,
        beats=beats,
        onsets=onsets,
        beats_per_bar=bpb,
        meter_numerator=num,
        meter_denominator=den,
        true_silence_spans=_spans(obj.get("true_silence_spans"), f"{where}.true_silence_spans"),
        nominal_bpm=float(nominal) if nominal is not None else None,
        bpm_start=float(obj["bpm_start"]) if finite(obj.get("bpm_start")) else None,
        bpm_end=float(obj["bpm_end"]) if finite(obj.get("bpm_end")) else None,
        ramp_start_seconds=float(obj.get("ramp_start_seconds", 0.0)),
        ramp_end_seconds=float(obj.get("ramp_end_seconds", 0.0)),
        notes=obj.get("notes", ""),
        origin=where,
    )


def _annotation_from_rhythm_fixture(corpus_manifest: str, fixture_name: str) -> Annotation:
    data = load_json_strict(corpus_manifest)
    fixtures = data.get("fixtures")
    if not isinstance(fixtures, list):
        raise IntegrityError(f"{corpus_manifest}: no fixtures array")
    fx = next((f for f in fixtures if f.get("name") == fixture_name), None)
    if fx is None:
        raise IntegrityError(f"{corpus_manifest}: no fixture named {fixture_name!r}")
    meter = fx.get("meter") or {}
    nominal = fx.get("nominalBpm")
    conv = {
        "source": "generated",
        "license": fx.get("license", ""),
        "tempo_profile": fx.get("tempoProfile"),
        "nominal_bpm": nominal,
        "bpm_start": fx.get("bpmStart"),
        "bpm_end": fx.get("bpmEnd"),
        "ramp_start_seconds": fx.get("rampStartSeconds", 0.0),
        "ramp_end_seconds": fx.get("rampEndSeconds", 0.0),
        "meter": {
            "numerator": meter.get("numerator", 4),
            "denominator": meter.get("denominator", 4),
            "beats_per_bar": meter.get("beatsPerBar", 4),
        },
        "beats": fx.get("beats"),
        "onsets": fx.get("onsets", []),
        "true_silence_spans": fx.get("trueSilenceSpans", []),
        "notes": fx.get("notes", ""),
    }
    return _annotation_from_obj(
        conv, f"{corpus_manifest}:{fixture_name}")


def load_annotation(spec: dict, manifest_dir: str) -> Annotation:
    if not isinstance(spec, dict):
        raise IntegrityError("annotation reference must be an object")
    kind = spec.get("kind")
    if kind == "file":
        path = spec.get("path")
        if not path:
            raise IntegrityError("annotation.kind=file requires a path")
        resolved = safe_resolve(manifest_dir, path)
        if not os.path.isfile(resolved):
            raise IntegrityError(f"annotation file missing: {resolved}")
        declared_sha = spec.get("sha256")
        if declared_sha is not None:
            if not is_hex64(declared_sha):
                raise IntegrityError("annotation.sha256 must be 64-hex")
            actual = sha256_file(resolved)
            if actual != declared_sha:
                raise IntegrityError(
                    f"annotation sha256 mismatch for {resolved}: {actual} != {declared_sha}")
        return _annotation_from_obj(load_json_strict(resolved), resolved)
    if kind == "rhythm-manifest-fixture":
        corpus = spec.get("corpus_manifest")
        fixture = spec.get("fixture")
        if not corpus or not fixture:
            raise IntegrityError("annotation.kind=rhythm-manifest-fixture requires corpus_manifest and fixture")
        resolved = safe_resolve(manifest_dir, corpus) if not os.path.isabs(corpus) else corpus
        if not os.path.isfile(resolved):
            raise IntegrityError(f"corpus manifest missing: {resolved}")
        return _annotation_from_rhythm_fixture(resolved, fixture)
    raise IntegrityError(f"unknown annotation.kind {kind!r}")


def load_import_manifest(path: str, audio_root: str | None = None,
                         annotation_root: str | None = None,
                         verify_files: bool = True) -> ImportManifest:
    data = load_json_strict(path)
    if data.get("schema") != IMPORT_SCHEMA:
        raise IntegrityError(
            f"{path}: schema must be {IMPORT_SCHEMA}, got {data.get('schema')!r}")
    recordings_raw = data.get("recordings")
    if not isinstance(recordings_raw, list) or not recordings_raw:
        raise IntegrityError(f"{path}: recordings must be a non-empty list")

    manifest_dir = os.path.dirname(os.path.abspath(path))
    root_audio = audio_root if audio_root else manifest_dir
    root_ann = annotation_root if annotation_root else (audio_root if audio_root else manifest_dir)

    recordings: list[Recording] = []
    for i, entry in enumerate(recordings_raw):
        if not isinstance(entry, dict):
            raise IntegrityError(f"{path}: recordings[{i}] must be an object")
        rec_id = entry.get("id")
        if not isinstance(rec_id, str) or not rec_id:
            raise IntegrityError(f"{path}: recordings[{i}].id must be a non-empty string")
        classification = entry.get("classification")
        if classification not in CLASSIFICATIONS:
            raise IntegrityError(
                f"{path}: {rec_id}.classification must be one of {CLASSIFICATIONS}")
        rec = Recording(
            id=rec_id,
            audio_path=entry.get("audio_path", ""),
            classification=classification,
            license=entry.get("license", ""),
            ownership=entry.get("ownership", ""),
            provenance=entry.get("provenance", ""),
            representative=bool(entry.get("representative", True)),
            parent_id=entry.get("parent_id"),
            tags=list(entry.get("tags", [])),
            annotation_spec=entry.get("annotation"),
            declared={k: entry.get(k) for k in (
                "audio_sha256", "sample_rate", "channels", "sample_width_bytes",
                "frames", "duration_seconds")},
        )
        if not rec.audio_path:
            raise IntegrityError(f"{path}: {rec_id}.audio_path is required")
        # Audio path: absolute private paths are allowed; relative paths resolve
        # under the audio root with escape rejection.
        if os.path.isabs(rec.audio_path):
            rec.audio_path_resolved = rec.audio_path
        else:
            rec.audio_path_resolved = safe_resolve(root_audio, rec.audio_path)

        if rec.annotation_spec is not None:
            rec.annotation = load_annotation(rec.annotation_spec, root_ann)
        recordings.append(rec)

    manifest = ImportManifest(
        path=os.path.abspath(path),
        schema=data["schema"],
        task=data.get("task", ""),
        created_utc=data.get("created_utc", ""),
        recordings=recordings,
        sha256=sha256_file(path),
        raw=data,
    )
    if verify_files:
        findings = validate_manifest(manifest)
        hard = [f for f in findings if f.severity == "hard"]
        if hard:
            raise IntegrityError("import manifest validation failed:\n  "
                                 + "\n  ".join(f"{f.code}: {f.detail}" for f in hard))
    return manifest


def validate_manifest(manifest: ImportManifest) -> list[Finding]:
    """Validate declared identity against the actual bytes and classify.

    Returns every finding; a caller fails closed on any `hard` finding.
    """
    findings: list[Finding] = []
    ids = [r.id for r in manifest.recordings]
    _require(len(ids) == len(set(ids)), "duplicate_recording_id",
             "recording ids must be unique", findings)
    known = set(ids)

    for rec in manifest.recordings:
        where = f"{manifest.path}:{rec.id}"
        if rec.classification == "derived":
            _require(rec.parent_id in known, "derived_parent_missing",
                     f"{where}: derived recording needs an existing parent_id", findings)
        else:
            _require(rec.parent_id in (None, ""), "parent_on_nondervied",
                     f"{where}: parent_id is only valid for derived recordings", findings)

        # Classification vs annotation source: a real claim needs human truth.
        if rec.classification == "real":
            _require(rec.annotation is not None, "real_without_annotation",
                     f"{where}: a real recording requires an annotation", findings)
            if rec.annotation is not None:
                _require(rec.annotation.source in ("human", "manual"),
                         "synthetic_mislabelled_real",
                         f"{where}: real classification but annotation source is "
                         f"{rec.annotation.source!r} (human/manual required)", findings)
            _require(bool(rec.ownership.strip()), "real_without_ownership",
                     f"{where}: a real recording requires explicit ownership", findings)
            _require(bool(rec.license.strip()), "real_without_license",
                     f"{where}: a real recording requires an explicit license", findings)
        elif rec.classification == "synthetic":
            _require(bool(rec.provenance.strip()), "synthetic_without_provenance",
                     f"{where}: synthetic recording requires a provenance note", findings)
            _require(bool(rec.license.strip()), "synthetic_without_license",
                     f"{where}: synthetic recording requires an explicit license", findings)
        elif rec.classification == "derived":
            _require(bool(rec.provenance.strip()), "derived_without_provenance",
                     f"{where}: derived recording requires a provenance note", findings)

        # Actual bytes.
        try:
            info = read_wav_info(rec.audio_path_resolved)
        except IntegrityError as exc:
            findings.append(Finding("hard", "audio_unreadable", f"{where}: {exc}"))
            continue
        rec.wav = info
        if not info.ok:
            findings.append(Finding("hard", "audio_not_scorable",
                                    f"{where}: {info.error}"))
        _require(is_hex64(rec.declared.get("audio_sha256")),
                 "declared_sha_missing",
                 f"{where}: audio_sha256 must be a 64-hex sha256", findings)
        if info.sha256 and is_hex64(rec.declared.get("audio_sha256")):
            _require(info.sha256 == rec.declared["audio_sha256"],
                     "audio_sha_mismatch",
                     f"{where}: audio sha256 {info.sha256} != declared "
                     f"{rec.declared['audio_sha256']}", findings)
        for key, actual in (("sample_rate", info.sample_rate),
                            ("channels", info.channels),
                            ("sample_width_bytes", info.sample_width_bytes),
                            ("frames", info.frames)):
            declared = rec.declared.get(key)
            if declared is None:
                findings.append(Finding("hard", "declared_field_missing",
                                        f"{where}: declared {key} missing"))
            elif abs(float(declared) - float(actual)) > 1e-6:
                findings.append(Finding("hard", "audio_field_mismatch",
                                        f"{where}: {key} declared {declared} != actual {actual}"))
        if rec.declared.get("duration_seconds") is not None:
            d = abs(float(rec.declared["duration_seconds"]) - info.duration_seconds)
            _require(d <= 0.05, "audio_duration_mismatch",
                     f"{where}: duration declared "
                     f"{rec.declared['duration_seconds']} != actual "
                     f"{info.duration_seconds:.6f}", findings)
    return findings


def gate_eligibility(rec: Recording, criteria: dict) -> tuple[bool, str]:
    """Whether a recording may count toward the useful-lock >=95% gate."""
    if rec.classification != "real":
        return False, f"classification={rec.classification} (gate needs real)"
    if not rec.representative:
        return False, "marked unrepresentative"
    ann = rec.annotation
    if ann is None:
        return False, "no annotation"
    if ann.source not in ("human", "manual"):
        return False, f"annotation source={ann.source}"
    if not rec.ownership.strip() or not rec.license.strip():
        return False, "missing ownership/license"
    if not (ann.tempo_profile == "constant"):
        return False, f"tempo_profile={ann.tempo_profile} (gate needs steady)"
    if not (ann.meter_numerator == 4 and ann.meter_denominator == 4
            and ann.beats_per_bar == 4):
        return False, "meter is not 4/4"
    min_beats = int(criteria.get("min_annotated_beats", 8))
    if len(ann.beats) < min_beats:
        return False, f"only {len(ann.beats)} annotated beats (< {min_beats})"
    return True, "eligible"

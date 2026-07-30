"""Create three disposable Crawl-idle correction candidates in the target.

Only assets below /Game/_Game/Animations/Locomotion/Crawl/Diagnostics are
created. The authoritative migrated animation is hashed before and after.
"""

import hashlib
import json
import os
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
EVIDENCE_DIR = (
    PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719"
)
OUTPUT_FILE = EVIDENCE_DIR / "UE58_CrawlSpineCorrectionCandidates.json"
SOURCE_ASSET = (
    "/Game/_Game/Animations/Locomotion/Crawl/"
    "Anim_KA_Crawling_Baby_Idle"
)
SOURCE_FILE = (
    PROJECT_DIR
    / "Content"
    / "_Game"
    / "Animations"
    / "Locomotion"
    / "Crawl"
    / "Anim_KA_Crawling_Baby_Idle.uasset"
)
DIAGNOSTIC_ROOT = (
    "/Game/_Game/Animations/Locomotion/Crawl/Diagnostics/"
    "Anim_KA_Crawling_Baby_Idle"
)
CANDIDATES = (
    (0.25, DIAGNOSTIC_ROOT + "_Corr25"),
    (0.45, DIAGNOSTIC_ROOT + "_Corr45"),
    (0.65, DIAGNOSTIC_ROOT + "_Corr65"),
)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def fail(message):
    raise RuntimeError(message)


if not SOURCE_FILE.is_file():
    fail("Source Crawl idle package is absent")

source_hash_before = sha256(SOURCE_FILE)
records = []
for factor, destination in CANDIDATES:
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        fail("Candidate path already exists: " + destination)
    duplicated = unreal.EditorAssetLibrary.duplicate_asset(
        SOURCE_ASSET, destination
    )
    if duplicated is None:
        fail("Could not duplicate Crawl idle to " + destination)
    correction_json = (
        unreal.ProjectAnimationDiagnosticsLibrary
        .redistribute_adjacent_bone_rotation(
            duplicated, "spine_01", "spine_02", factor
        )
    )
    correction = json.loads(correction_json)
    if correction.get("success") is not True:
        fail("Correction failed for {}: {}".format(destination, correction_json))
    if not unreal.EditorAssetLibrary.save_loaded_asset(duplicated, False):
        fail("Could not save candidate " + destination)
    records.append(
        {
            "factor": factor,
            "asset": destination + "." + destination.rsplit("/", 1)[-1],
            "correction": correction,
        }
    )

source_hash_after = sha256(SOURCE_FILE)
if source_hash_after != source_hash_before:
    fail("Authoritative Crawl idle changed while creating candidates")

payload = {
    "status": "UE58_CRAWL_SPINE_CORRECTION_CANDIDATES_PASS",
    "source_asset": SOURCE_ASSET,
    "source_sha256_before": source_hash_before,
    "source_sha256_after": source_hash_after,
    "lower_bone": "spine_01",
    "upper_bone": "spine_02",
    "candidates": records,
}
EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
OUTPUT_FILE.write_text(
    json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8",
)
unreal.log("[CrawlSpineCandidates58] " + json.dumps(payload))

"""Update only the DungeonGeneration map row in the migration manifest."""

import csv
import hashlib
import io
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
EVIDENCE = "Docs/Migration/Evidence/Phase4_DungeonGeneration_ContentBuild.json"
CONTENT_COMMIT = "95fcd1b"
PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
TARGET_RELATIVE = "Content\\Procedural\\Maps\\DungeonGeneration.umap"
SOURCE_LENGTH = 58016
SOURCE_SHA256 = (
    "B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F"
)
TARGET_LENGTH = 58272
TARGET_SHA256 = (
    "4262B37586D7626F2C912AD81BE7FFF27EEA2615130919B5B85339E7B77E39E1"
)
EXPECTED_DEPENDENCIES = {
    "/Engine/EngineMaterials/WorldGridMaterial",
    "/Script/NavigationSystem",
    "/Script/PCG",
}
RESAVE_EVIDENCE = (
    ROOT
    / "Saved"
    / "Migration"
    / "Phase4"
    / "DungeonGeneration"
    / "DungeonGeneration58Resave.json"
)


def load(path):
    if not path.is_file():
        raise RuntimeError("Required evidence is absent: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def render_row(row, line_ending):
    stream = io.StringIO(newline="")
    writer = csv.writer(
        stream,
        quoting=csv.QUOTE_ALL,
        lineterminator=line_ending,
    )
    writer.writerow(row)
    return stream.getvalue().encode("utf-8")


resave = load(RESAVE_EVIDENCE)
if (
    resave.get("status") != "UE58_DUNGEON_GENERATION_LOAD_RESAVE_RELOAD_PASS"
    or resave.get("package") != PACKAGE
    or resave.get("length_after_resave") != TARGET_LENGTH
    or resave.get("sha256_after_resave") != TARGET_SHA256
    or set(resave.get("dependencies_after_resave", [])) != EXPECTED_DEPENDENCIES
    or resave.get("game_dependencies")
    or resave.get("target_delta_exact") is not True
):
    raise RuntimeError("UE 5.8 map resave evidence is not the exact PASS")

target_path = ROOT / Path(TARGET_RELATIVE.replace("\\", "/"))
if not target_path.is_file():
    raise RuntimeError("Migrated target map is absent: " + str(target_path))
if target_path.stat().st_size != TARGET_LENGTH or sha256(target_path) != TARGET_SHA256:
    raise RuntimeError("Target map differs from the UE 5.8 resave evidence")

raw_lines = MANIFEST.read_bytes().splitlines(keepends=True)
if not raw_lines:
    raise RuntimeError("Manifest is empty")
header = next(csv.reader([raw_lines[0].decode("utf-8-sig").rstrip("\r\n")]))
indices = {name: index for index, name in enumerate(header)}
required_columns = {
    "PackageName",
    "SourceLength",
    "SourceSHA256",
    "TargetPath",
    "TargetFile",
    "TargetAssetClass",
    "Presence",
    "TargetLength",
    "TargetSHA256",
    "TargetRegistryPresent",
    "TargetDependencyCount",
    "TargetDependencies",
    "TargetReferencers",
    "Classification",
    "Authority",
    "Action",
    "Result",
    "TestEvidence",
    "Commit",
    "Notes",
}
if not required_columns.issubset(indices):
    raise RuntimeError("Manifest schema differs from the audited Phase 2 schema")

seen = 0
for line_index in range(1, len(raw_lines)):
    encoded = raw_lines[line_index]
    line_ending = "\r\n" if encoded.endswith(b"\r\n") else "\n"
    row = next(csv.reader([encoded.decode("utf-8").rstrip("\r\n")]))
    if len(row) != len(header):
        raise RuntimeError("Malformed manifest row at line {}".format(line_index + 1))
    package = row[indices["PackageName"]]
    if package != PACKAGE:
        if row[indices["TestEvidence"]] == EVIDENCE:
            raise RuntimeError("Evidence path is attached outside the exact map row: " + package)
        continue
    seen += 1
    if seen != 1:
        raise RuntimeError("Duplicate DungeonGeneration manifest row")
    if (
        row[indices["SourceLength"]] != str(SOURCE_LENGTH)
        or row[indices["SourceSHA256"]].upper() != SOURCE_SHA256
    ):
        raise RuntimeError("Manifest source map baseline differs from the frozen receipt")
    if row[indices["Presence"]] not in {"SOURCE_ONLY", "BOTH"}:
        raise RuntimeError("Unexpected pre-update map presence")
    if (
        row[indices["Presence"]] == "BOTH"
        and row[indices["TestEvidence"]] != EVIDENCE
    ):
        raise RuntimeError("Refusing to replace a prior BOTH map disposition")

    dependencies = sorted(EXPECTED_DEPENDENCIES)
    row[indices["TargetPath"]] = PACKAGE
    row[indices["TargetFile"]] = TARGET_RELATIVE
    row[indices["TargetAssetClass"]] = "World"
    row[indices["Presence"]] = "BOTH"
    row[indices["TargetLength"]] = str(TARGET_LENGTH)
    row[indices["TargetSHA256"]] = TARGET_SHA256
    row[indices["TargetRegistryPresent"]] = "True"
    row[indices["TargetDependencyCount"]] = str(len(dependencies))
    row[indices["TargetDependencies"]] = ";".join(dependencies)
    row[indices["TargetReferencers"]] = ""
    row[indices["Classification"]] = "MIGRATED_PROJECT_CONTENT"
    row[indices["Authority"]] = "SOURCE_BEHAVIOR"
    row[indices["Action"]] = "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
    row[indices["Result"]] = "PASS"
    row[indices["TestEvidence"]] = EVIDENCE
    row[indices["Commit"]] = CONTENT_COMMIT
    row[indices["Notes"]] = (
        "Phase 2/3 classified this as a source-only inspection candidate "
        "without package loads or saves. Exact single-map UE 5.7 AssetTools "
        "migration; UE 5.8 load/save/reload PASS. PIE, visual, cook, package, "
        "DoorToLevel, BP_MassiveDungeon, and full procedural runtime remain "
        "PENDING."
    )
    raw_lines[line_index] = render_row(row, line_ending)

if seen != 1:
    raise RuntimeError("Expected exactly one DungeonGeneration manifest row")

MANIFEST.write_bytes(b"".join(raw_lines))
print("PHASE4_DUNGEON_GENERATION_MANIFEST_UPDATE_PASS migrated=1")

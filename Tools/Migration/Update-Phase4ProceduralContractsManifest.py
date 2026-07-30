"""Update only the exact twenty procedural-contract rows in the manifest."""

import csv
import hashlib
import io
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
EVIDENCE = (
    "Docs/Migration/Evidence/Phase4_ProceduralContracts_ContentBuild.json"
)
EXPECTED_SOURCE_BYTES = 223283
EXPECTED_SOURCE_FINGERPRINT = (
    "30A8ACBD998EBD41242A3BD850C8CBB3E7E6A19D63D72E632D0BB897917FA006"
)
EXPECTED_CLASS_COUNTS = {
    "Blueprint": 6,
    "UserDefinedStruct": 12,
    "UserDefinedEnum": 2,
}
EXPECTED_PACKAGES = {
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
}

MIGRATION_EVIDENCE = (
    ROOT / "Saved" / "Migration" / "Phase4" / "EFProceduralContracts57Migration.json"
)
RESAVE_EVIDENCE = (
    ROOT / "Saved" / "Migration" / "Phase4" / "EFProceduralContracts58Resave.json"
)


def load(path):
    if not path.is_file():
        raise RuntimeError("Required evidence is absent: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def package_file(package_name):
    relative = package_name.removeprefix("/Game/").replace("/", "\\")
    return "Content\\" + relative + ".uasset"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def source_fingerprint(rows):
    values = [rows[package]["SourceSHA256"].upper() for package in sorted(rows)]
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest().upper()


migration = load(MIGRATION_EVIDENCE)
resave = load(RESAVE_EVIDENCE)
if (
    migration.get("status")
    != "ASSETTOOLS_EXACT_PROCEDURAL_CONTRACTS_MIGRATION_PASS"
    or migration.get("package_count") != 20
    or migration.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
):
    raise RuntimeError("UE 5.7 migration evidence is not the exact 20-package PASS")
if (
    resave.get("status")
    != "UE58_PROCEDURAL_CONTRACTS_LOAD_COMPILE_RESAVE_PASS"
    or resave.get("package_count") != 20
    or resave.get("compiled_blueprint_count") != 6
    or resave.get("class_counts") != EXPECTED_CLASS_COUNTS
    or resave.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or resave.get("unexpected_game_dependencies")
    or resave.get("redirectors")
):
    raise RuntimeError("UE 5.8 resave evidence is not the exact 20-package PASS")

resave_rows = {row["package"]: row for row in resave.get("packages", [])}
if set(resave_rows) != EXPECTED_PACKAGES or len(resave_rows) != 20:
    raise RuntimeError("UE 5.8 resave package set differs from the exact allowlist")
dependencies = resave.get("dependencies", {})
if set(dependencies) != EXPECTED_PACKAGES:
    raise RuntimeError("UE 5.8 dependency evidence is incomplete")

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


def render_row(row, line_ending):
    stream = io.StringIO(newline="")
    writer = csv.writer(
        stream,
        quoting=csv.QUOTE_ALL,
        lineterminator=line_ending,
    )
    writer.writerow(row)
    return stream.getvalue().encode("utf-8")


seen = set()
source_rows = {}
target_class_counts = {}
for line_index in range(1, len(raw_lines)):
    encoded = raw_lines[line_index]
    line_ending = "\r\n" if encoded.endswith(b"\r\n") else "\n"
    row = next(csv.reader([encoded.decode("utf-8").rstrip("\r\n")]))
    if len(row) != len(header):
        raise RuntimeError("Malformed manifest row at line {}".format(line_index + 1))
    package = row[indices["PackageName"]]
    if package not in EXPECTED_PACKAGES:
        if row[indices["TestEvidence"]] == EVIDENCE:
            raise RuntimeError(
                "Evidence path is already attached outside the exact allowlist: "
                + package
            )
        continue
    if package in seen:
        raise RuntimeError("Duplicate exact-allowlist manifest row: " + package)

    source_rows[package] = {
        "SourceLength": row[indices["SourceLength"]],
        "SourceSHA256": row[indices["SourceSHA256"]],
    }
    if row[indices["Presence"]] not in {"SOURCE_ONLY", "BOTH"}:
        raise RuntimeError("Unexpected pre-update presence for " + package)
    if (
        row[indices["Presence"]] == "BOTH"
        and row[indices["TestEvidence"]] != EVIDENCE
    ):
        raise RuntimeError("Refusing to replace a prior BOTH disposition: " + package)

    target_relative = package_file(package)
    target_path = ROOT / Path(target_relative.replace("\\", "/"))
    if not target_path.is_file():
        raise RuntimeError("Migrated target file is absent: " + str(target_path))
    evidence_row = resave_rows[package]
    actual_hash = sha256(target_path)
    if (
        target_path.stat().st_size != evidence_row["length"]
        or actual_hash != evidence_row["sha256_after_resave"]
    ):
        raise RuntimeError("Target differs from UE 5.8 resave evidence: " + package)

    target_class = evidence_row["class"]
    target_class_counts[target_class] = target_class_counts.get(target_class, 0) + 1
    direct_dependencies = sorted(set(dependencies[package]))
    unexpected_game = [
        value
        for value in direct_dependencies
        if value.startswith("/Game/") and value not in EXPECTED_PACKAGES
    ]
    if unexpected_game:
        raise RuntimeError(
            "Unexpected /Game dependency for {}: {!r}".format(
                package, unexpected_game
            )
        )

    row[indices["TargetPath"]] = package
    row[indices["TargetFile"]] = target_relative
    row[indices["TargetAssetClass"]] = target_class
    row[indices["Presence"]] = "BOTH"
    row[indices["TargetLength"]] = str(target_path.stat().st_size)
    row[indices["TargetSHA256"]] = actual_hash
    row[indices["TargetRegistryPresent"]] = "True"
    row[indices["TargetDependencyCount"]] = str(len(direct_dependencies))
    row[indices["TargetDependencies"]] = ";".join(direct_dependencies)
    row[indices["TargetReferencers"]] = ""
    row[indices["Classification"]] = "MIGRATED_PROJECT_CONTENT"
    row[indices["Authority"]] = "SOURCE_BEHAVIOR"
    row[indices["Action"]] = (
        "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
    )
    row[indices["Result"]] = "PASS"
    row[indices["TestEvidence"]] = EVIDENCE
    row[indices["Commit"]] = "259bf42"
    note = row[indices["Notes"]].rstrip()
    migration_note = (
        "Exact 20-package UE 5.7 AssetTools migration with dependencies "
        "disabled after a closed /Game dependency gate; UE 5.8 "
        "load/compile/resave PASS. Procedural runtime, PIE, cook and package PENDING."
    )
    if migration_note not in note:
        note = (note + " " + migration_note).strip()
    row[indices["Notes"]] = note
    raw_lines[line_index] = render_row(row, line_ending)
    seen.add(package)

if seen != EXPECTED_PACKAGES:
    raise RuntimeError(
        "Manifest allowlist mismatch; missing={!r}".format(
            sorted(EXPECTED_PACKAGES - seen)
        )
    )
if sum(int(row["SourceLength"]) for row in source_rows.values()) != EXPECTED_SOURCE_BYTES:
    raise RuntimeError("Manifest source-byte total differs from the frozen baseline")
if source_fingerprint(source_rows) != EXPECTED_SOURCE_FINGERPRINT:
    raise RuntimeError("Manifest source fingerprint differs from the frozen baseline")
if target_class_counts != EXPECTED_CLASS_COUNTS:
    raise RuntimeError(
        "Manifest target class counts differ: " + repr(target_class_counts)
    )

MANIFEST.write_bytes(b"".join(raw_lines))
print("PHASE4_PROCEDURAL_CONTRACTS_MANIFEST_UPDATE_PASS migrated=20")

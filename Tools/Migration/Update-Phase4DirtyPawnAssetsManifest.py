"""Update only the exact fifteen DirtyPawn material-closure manifest rows."""

import csv
import hashlib
import io
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
EVIDENCE = "Docs/Migration/Evidence/Phase4_DirtyPawnAssets_ContentRuntime.json"
COMMIT = "ec10c8e"
SOURCE_BYTES = 70130437
SOURCE_FINGERPRINT = (
    "8F58CFD389DA0D63D2633D372C5DE251496EC53F53EEC15FE51AB160C46AEC36"
)
CLASS_COUNTS = {"Texture2D": 6, "MaterialFunction": 8, "Material": 1}
PACKAGES = {
    "/Game/_Game/Textures/leakage_sfhkcazc_4k/T_Leakage_sfhkcazc_2K_Mask",
    "/Game/DirtyPawnSystem/Demo/Character/Characters/Mannequins/Textures/Manny/T_Manny_01_D",
    "/Game/DirtyPawnSystem/Demo/StarterContent/Textures/T_Perlin_Noise_M",
    "/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Blood/MF_Blood_Mask",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SandSnow_Mask",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SkinnedHeightMask",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Mud/MF_MudHeight_BaseColor",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_BaseColor",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Normal",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Roughness",
    "/Game/DirtyPawnSystem/Materials/Functions/DPS/Smear/MF_Smear_Mask",
    "/Game/DirtyPawnSystem/Textures/T_BloodScratch_Alpha",
    "/Game/DirtyPawnSystem/Textures/T_Noise_Normal",
    "/Game/DirtyPawnSystem/Textures/T_Perlin_Noise_M",
}
BASE = ROOT / "Saved" / "Migration" / "Phase4" / "DirtyPawnAssets"


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


def target_relative(package):
    return "Content\\" + package.removeprefix("/Game/").replace("/", "\\") + ".uasset"


def fingerprint(rows):
    order = sorted(
        rows,
        key=lambda package: (
            0 if package.startswith("/Game/_Game/") else 1,
            package,
        ),
    )
    lines = [
        "{}|{}|{}".format(
            package,
            rows[package]["source_length"],
            rows[package]["source_sha256"].upper(),
        )
        for package in order
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def render_row(row, line_ending):
    stream = io.StringIO(newline="")
    writer = csv.writer(stream, quoting=csv.QUOTE_ALL, lineterminator=line_ending)
    writer.writerow(row)
    return stream.getvalue().encode("utf-8")


migration = load(BASE / "DirtyPawnAssets57Migration.json")
resave = load(BASE / "DirtyPawnAssets58Resave.json")
if (
    migration.get("status") != "ASSETTOOLS_EXACT_DIRTYPAWN_ASSETS_MIGRATION_PASS"
    or migration.get("package_count") != 15
    or migration.get("created_package_count") != 15
    or migration.get("source_bytes") != SOURCE_BYTES
    or migration.get("source_fingerprint") != SOURCE_FINGERPRINT
    or migration.get("target_delta_exact") is not True
    or migration.get("resumed_from_prior_log") is not True
):
    raise RuntimeError("UE 5.7 DirtyPawn migration evidence is not the exact PASS")
if (
    resave.get("status") != "UE58_DIRTYPAWN_ASSETS_LOAD_COMPILE_RESAVE_RELOAD_PASS"
    or resave.get("package_count") != 15
    or resave.get("source_bytes") != SOURCE_BYTES
    or resave.get("source_fingerprint") != SOURCE_FINGERPRINT
    or resave.get("class_counts") != CLASS_COUNTS
    or resave.get("material_compile_count") != 1
    or resave.get("material_function_compile_count") != 8
    or resave.get("target_delta_exact") is not True
    or resave.get("unexpected_game_dependencies")
    or resave.get("redirectors")
):
    raise RuntimeError("UE 5.8 DirtyPawn resave evidence is not the exact PASS")

resave_rows = {row["package"]: row for row in resave.get("packages", [])}
if set(resave_rows) != PACKAGES or len(resave.get("packages", [])) != 15:
    raise RuntimeError("UE 5.8 package set differs from the exact DirtyPawn allowlist")
if fingerprint(resave_rows) != SOURCE_FINGERPRINT:
    raise RuntimeError("UE 5.8 rows differ from the frozen source fingerprint")
dependencies = resave.get("dependencies_after_resave", {})
if set(dependencies) != PACKAGES:
    raise RuntimeError("UE 5.8 dependency evidence is incomplete")
for package, values in dependencies.items():
    unexpected = [
        value for value in values if value.startswith("/Game/") and value not in PACKAGES
    ]
    if unexpected:
        raise RuntimeError(
            "Unexpected /Game dependency for {}: {!r}".format(package, unexpected)
        )

raw_lines = MANIFEST.read_bytes().splitlines(keepends=True)
if not raw_lines:
    raise RuntimeError("Manifest is empty")
header = next(csv.reader([raw_lines[0].decode("utf-8-sig").rstrip("\r\n")]))
indices = {name: index for index, name in enumerate(header)}
required = {
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
if not required.issubset(indices):
    raise RuntimeError("Manifest schema differs from the audited Phase 2 schema")

seen = set()
for line_index in range(1, len(raw_lines)):
    encoded = raw_lines[line_index]
    line_ending = "\r\n" if encoded.endswith(b"\r\n") else "\n"
    row = next(csv.reader([encoded.decode("utf-8").rstrip("\r\n")]))
    if len(row) != len(header):
        raise RuntimeError("Malformed manifest row at line {}".format(line_index + 1))
    package = row[indices["PackageName"]]
    if package not in PACKAGES:
        if row[indices["TestEvidence"]] == EVIDENCE:
            raise RuntimeError("DirtyPawn evidence is attached outside the allowlist: " + package)
        continue
    if package in seen:
        raise RuntimeError("Duplicate DirtyPawn manifest row: " + package)

    evidence_row = resave_rows[package]
    if (
        row[indices["SourceLength"]] != str(evidence_row["source_length"])
        or row[indices["SourceSHA256"]].upper()
        != evidence_row["source_sha256"].upper()
    ):
        raise RuntimeError("Manifest source baseline differs for " + package)
    if row[indices["Presence"]] not in {"SOURCE_ONLY", "BOTH"}:
        raise RuntimeError("Unexpected pre-update presence for " + package)
    if (
        row[indices["Presence"]] == "BOTH"
        and row[indices["TestEvidence"]] != EVIDENCE
    ):
        raise RuntimeError("Refusing to replace a prior BOTH disposition: " + package)

    relative = target_relative(package)
    target = ROOT / Path(relative.replace("\\", "/"))
    if not target.is_file():
        raise RuntimeError("Migrated target asset is absent: " + str(target))
    if (
        target.stat().st_size != evidence_row["length"]
        or sha256(target) != evidence_row["sha256"].upper()
    ):
        raise RuntimeError("Target differs from UE 5.8 evidence: " + package)

    direct_dependencies = sorted(set(dependencies[package]))
    row[indices["TargetPath"]] = package
    row[indices["TargetFile"]] = relative
    row[indices["TargetAssetClass"]] = evidence_row["class"]
    row[indices["Presence"]] = "BOTH"
    row[indices["TargetLength"]] = str(evidence_row["length"])
    row[indices["TargetSHA256"]] = evidence_row["sha256"].upper()
    row[indices["TargetRegistryPresent"]] = "True"
    row[indices["TargetDependencyCount"]] = str(len(direct_dependencies))
    row[indices["TargetDependencies"]] = ";".join(direct_dependencies)
    row[indices["TargetReferencers"]] = ""
    row[indices["Classification"]] = "MIGRATED_PROJECT_CONTENT"
    row[indices["Authority"]] = "SOURCE_BEHAVIOR"
    row[indices["Action"]] = "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
    row[indices["Result"]] = "PASS"
    row[indices["TestEvidence"]] = EVIDENCE
    row[indices["Commit"]] = COMMIT
    row[indices["Notes"]] = (
        "Exact 15-package DirtyPawn UE 5.7 AssetTools migration; UE 5.8 "
        "load/material compile/resave/reload, Editor/Game builds, and focused "
        "runtime wrapper/binding contract PASS. Wet/mud/blood/smear/snow/sand "
        "visual QA, tattoo compatibility, blood-alpha visual/API confirmation, "
        "cook, package, and packaged runtime remain PENDING."
    )
    raw_lines[line_index] = render_row(row, line_ending)
    seen.add(package)

if seen != PACKAGES:
    raise RuntimeError(
        "Manifest DirtyPawn allowlist mismatch; missing={!r}".format(
            sorted(PACKAGES - seen)
        )
    )

MANIFEST.write_bytes(b"".join(raw_lines))
print("PHASE4_DIRTYPAWN_ASSETS_MANIFEST_UPDATE_PASS migrated=15")

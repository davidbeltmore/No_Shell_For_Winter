"""Update only the exact 127-package Phase 4 modern UI manifest rows."""

import csv
import hashlib
import io
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
EVIDENCE = "Docs/Migration/Evidence/Phase4_ModernUI_ConfigBuild.json"
ROOT_COUNTS = {
    "/Game/_Game/Widgets/Chronicle": 23,
    "/Game/_Game/Widgets/InnerState": 24,
    "/Game/_Game/Widgets/Status": 17,
    "/Game/_Game/Widgets/Attributes": 27,
    "/Game/_Game/Widgets/SinfulAscensionAltar": 36,
}
RESAVE_EVIDENCE = (
    ROOT / "Saved" / "Migration" / "Phase4" / "ModernUI58Resave.json"
)
if not RESAVE_EVIDENCE.is_file():
    raise RuntimeError("UE 5.8 modern UI resave evidence is absent")
resave = json.loads(RESAVE_EVIDENCE.read_text(encoding="utf-8-sig"))
TARGET_CLASSES = {
    row["package"]: row["class"] for row in resave.get("packages", [])
}
if len(TARGET_CLASSES) != 127:
    raise RuntimeError("UE 5.8 resave evidence does not contain 127 classes")


def package_file(package_name):
    relative = package_name.removeprefix("/Game/").replace("/", "\\")
    return "Content\\" + relative + ".uasset"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


raw_lines = MANIFEST.read_bytes().splitlines(keepends=True)
if not raw_lines:
    raise RuntimeError("Manifest is empty")
header = next(csv.reader([raw_lines[0].decode("utf-8-sig").rstrip("\r\n")]))
indices = {name: index for index, name in enumerate(header)}
required_columns = {
    "PackageName",
    "SourceAssetClass",
    "TargetPath",
    "TargetFile",
    "TargetAssetClass",
    "Presence",
    "TargetLength",
    "TargetSHA256",
    "TargetRegistryPresent",
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
actual_root_counts = {root: 0 for root in ROOT_COUNTS}
for line_index in range(1, len(raw_lines)):
    encoded = raw_lines[line_index]
    line_ending = "\r\n" if encoded.endswith(b"\r\n") else "\n"
    row = next(csv.reader([encoded.decode("utf-8").rstrip("\r\n")]))
    if len(row) != len(header):
        raise RuntimeError("Malformed manifest row at line {}".format(line_index + 1))
    package = row[indices["PackageName"]]
    matching_roots = [
        root for root in ROOT_COUNTS if package.startswith(root + "/")
    ]
    if not matching_roots:
        continue
    if len(matching_roots) != 1:
        raise RuntimeError("Package matches multiple modern UI roots: " + package)
    root = matching_roots[0]
    actual_root_counts[root] += 1

    target_relative = package_file(package)
    target_path = ROOT / Path(target_relative.replace("\\", "/"))
    if not target_path.is_file():
        raise RuntimeError("Migrated target file is absent: " + str(target_path))
    if row[indices["Presence"]] not in {"SOURCE_ONLY", "BOTH"}:
        raise RuntimeError("Unexpected presence before update: " + package)
    if (
        row[indices["Presence"]] == "BOTH"
        and row[indices["TestEvidence"]] != EVIDENCE
    ):
        raise RuntimeError("Refusing to replace a prior BOTH disposition: " + package)

    row[indices["TargetPath"]] = package
    row[indices["TargetFile"]] = target_relative
    row[indices["TargetAssetClass"]] = TARGET_CLASSES[package]
    row[indices["Presence"]] = "BOTH"
    row[indices["TargetLength"]] = str(target_path.stat().st_size)
    row[indices["TargetSHA256"]] = sha256(target_path)
    row[indices["TargetRegistryPresent"]] = "True"
    row[indices["TargetDependencyCount"]] = ""
    row[indices["TargetDependencies"]] = ""
    row[indices["TargetReferencers"]] = ""
    row[indices["Classification"]] = "MIGRATED_PROJECT_CONTENT"
    row[indices["Authority"]] = "SOURCE_BEHAVIOR"
    row[indices["Action"]] = (
        "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
    )
    row[indices["Result"]] = "PASS"
    row[indices["TestEvidence"]] = EVIDENCE
    row[indices["Commit"]] = "4a66c74"
    note = row[indices["Notes"]].rstrip()
    migration_note = (
        "Exact UE 5.7 AssetTools migration; UE 5.7 read-only compile and "
        "UE 5.8 load/compile/resave PASS."
    )
    if migration_note not in note:
        note = (note + " " + migration_note).strip()
    row[indices["Notes"]] = note
    raw_lines[line_index] = render_row(row, line_ending)
    seen.add(package)

if actual_root_counts != ROOT_COUNTS:
    raise RuntimeError(
        "Modern UI manifest root counts differ: {!r}".format(actual_root_counts)
    )
if len(seen) != 127:
    raise RuntimeError("Expected 127 modern UI manifest rows")

MANIFEST.write_bytes(b"".join(raw_lines))
print("PHASE4_MODERN_UI_MANIFEST_UPDATE_PASS migrated=127")

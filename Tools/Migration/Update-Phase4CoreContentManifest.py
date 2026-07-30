"""Update only the exact Phase 4 core/Defeat rows in the package manifest."""

import csv
import hashlib
import io
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Docs" / "Migration" / "04_Content_Migration_Manifest.csv"
EVIDENCE = "Docs/Migration/Evidence/Phase4_EFProjectCoreContent_ConfigAutomation.json"

MIGRATED_PACKAGES = {
    "/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses",
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry",
    "/Game/Data/CharacterBackground/DT_ProjectBackstories",
    "/Game/Data/CharacterBackground/DT_ProjectProfessions",
    "/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget",
    "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget",
    "/Game/_Game/Icons/Bleeding",
    "/Game/_Game/Icons/Dirt",
    "/Game/_Game/Icons/Dizzy",
    "/Game/_Game/Icons/Exhausted",
    "/Game/_Game/Icons/extremepain_transparent",
    "/Game/_Game/Icons/Fear",
    "/Game/_Game/Icons/frenzy_transparent",
    "/Game/_Game/Icons/gracestep_transparent",
    "/Game/_Game/Icons/Hungry",
    "/Game/_Game/Icons/knockedout_transparent",
    "/Game/_Game/Icons/Orgasm",
    "/Game/_Game/Icons/SleepDeprived",
    "/Game/_Game/Icons/Thirst",
    "/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed",
    "/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium",
    "/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel",
}
GENERATED_PACKAGE = "/Game/_Game/Images/T_ProjectCharacterBackgroundPreview"


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
generated_seen = False
for line_index in range(1, len(raw_lines)):
    encoded = raw_lines[line_index]
    line_ending = "\r\n" if encoded.endswith(b"\r\n") else "\n"
    row = next(csv.reader([encoded.decode("utf-8").rstrip("\r\n")]))
    if len(row) != len(header):
        raise RuntimeError("Malformed manifest row at line {}".format(line_index + 1))
    package = row[indices["PackageName"]]
    if package == GENERATED_PACKAGE:
        generated_seen = True
    if package not in MIGRATED_PACKAGES:
        continue

    target_relative = package_file(package)
    target_path = ROOT / Path(target_relative.replace("\\", "/"))
    if not target_path.is_file():
        raise RuntimeError("Migrated target file is absent: " + str(target_path))
    if row[indices["Presence"]] not in {"SOURCE_ONLY", "BOTH"}:
        raise RuntimeError("Unexpected pre-update presence for " + package)

    row[indices["TargetPath"]] = package
    row[indices["TargetFile"]] = target_relative
    row[indices["TargetAssetClass"]] = row[indices["SourceAssetClass"]]
    row[indices["Presence"]] = "BOTH"
    row[indices["TargetLength"]] = str(target_path.stat().st_size)
    row[indices["TargetSHA256"]] = sha256(target_path)
    row[indices["TargetRegistryPresent"]] = "True"
    row[indices["TargetDependencyCount"]] = ""
    row[indices["TargetDependencies"]] = ""
    row[indices["TargetReferencers"]] = ""
    row[indices["Classification"]] = "MIGRATED_PROJECT_CONTENT"
    row[indices["Authority"]] = "SOURCE_BEHAVIOR"
    row[indices["Action"]] = "MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE"
    row[indices["Result"]] = "PASS"
    row[indices["TestEvidence"]] = EVIDENCE
    row[indices["Commit"]] = "671feda"
    note = row[indices["Notes"]].rstrip()
    migration_note = "Migrated through isolated UE 5.7 AssetTools; UE 5.8 load/resave PASS."
    if migration_note not in note:
        note = (note + " " + migration_note).strip()
    row[indices["Notes"]] = note
    raw_lines[line_index] = render_row(row, line_ending)
    seen.add(package)

if seen != MIGRATED_PACKAGES:
    raise RuntimeError(
        "Manifest allowlist mismatch; missing={!r}".format(
            sorted(MIGRATED_PACKAGES - seen)
        )
    )
if generated_seen:
    raise RuntimeError("Generated preview row already exists; refusing duplicate update")

generated_relative = package_file(GENERATED_PACKAGE)
generated_path = ROOT / Path(generated_relative.replace("\\", "/"))
if not generated_path.is_file():
    raise RuntimeError("Generated preview Texture2D is absent")
generated = [""] * len(header)
values = {
    "PackageName": GENERATED_PACKAGE,
    "TargetPath": GENERATED_PACKAGE,
    "TargetFile": generated_relative,
    "AssetClass": "Texture2D",
    "TargetAssetClass": "Texture2D",
    "Presence": "TARGET_ONLY",
    "TargetLength": str(generated_path.stat().st_size),
    "TargetSHA256": sha256(generated_path),
    "SourceRegistryPresent": "False",
    "TargetRegistryPresent": "True",
    "System": "StorySelection",
    "Classification": "TARGET_GENERATED_PROJECT_CONTENT",
    "Authority": "TARGET",
    "Action": "GENERATE_VIA_UE58_ASSETTOOLS_FROM_VERIFIED_PNG",
    "Result": "PASS",
    "TestEvidence": EVIDENCE,
    "Commit": "671feda",
    "Notes": (
        "Generated from Content/_Game/Images/preview.png, 749769 bytes, "
        "SHA-256 6B4075152BB866EB6B05AB8E24AD68A1138756AA0899681B348FA38F8DE288D3."
    ),
}
for name, value in values.items():
    generated[indices[name]] = value
raw_lines.append(render_row(generated, "\r\n"))

MANIFEST.write_bytes(b"".join(raw_lines))
print(
    "PHASE4_MANIFEST_UPDATE_PASS migrated={} generated=1".format(len(seen))
)

"""Read-only UE 5.8 structural probe for EFProjectSystems.

The probe loads native classes and their CDOs, reads text configuration, and
queries AssetRegistry metadata. It never loads maps or content objects and it
never compiles or saves assets.
"""

import datetime
import json
import os
import re

import unreal


PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
OUTPUT_PATH = os.path.join(
    PROJECT_DIR,
    "Saved",
    "Migration",
    "Phase3",
    "EFProjectSystemsReadOnlyProbe58.json",
)
STRICT_SOFT_ASSETS = os.environ.get(
    "CODEX_EFPS_REQUIRE_SOFT_ASSETS", "0"
).strip() == "1"

checks = []
failures = []


def record(name, passed, actual=None, expected=None):
    passed = bool(passed)
    checks.append(
        {
            "name": name,
            "passed": passed,
            "actual": actual,
            "expected": expected,
        }
    )
    if not passed:
        failures.append(
            "{}: expected {!r}, got {!r}".format(name, expected, actual)
        )


def to_snake(name):
    value = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value).lower()


def get_property(value, name):
    candidates = [name, to_snake(name)]
    if name.startswith("b") and len(name) > 1 and name[1].isupper():
        candidates.append(to_snake(name[1:]))
    errors = []
    for candidate in candidates:
        try:
            return value.get_editor_property(candidate)
        except Exception as exception:
            errors.append("{}: {}".format(candidate, exception))
    raise RuntimeError(
        "Could not read {} from {} ({})".format(
            name, value, "; ".join(errors)
        )
    )


def require_class(path):
    loaded = unreal.load_class(None, path)
    if loaded is None:
        failures.append("Native class did not load: " + path)
    return loaded


def require_cdo(path):
    loaded = require_class(path)
    if loaded is None:
        return None
    cdo = unreal.get_default_object(loaded)
    if cdo is None:
        failures.append("CDO did not load: " + path)
    return cdo


def key_name(value):
    try:
        return str(get_property(value, "KeyName"))
    except Exception:
        return str(value)


def float_record(name, actual, expected, tolerance=0.0001):
    number = float(actual)
    record(name, abs(number - float(expected)) <= tolerance, number, expected)


engine_version = unreal.SystemLibrary.get_engine_version()
record(
    "engine_version",
    engine_version.startswith("5.8."),
    engine_version,
    "5.8.x",
)

# Representative reflected surfaces in all modules that expose UObject types.
class_contracts = (
    (
        "/Script/EFProjectSystemsCore.EFProjectInputSettings",
        "/Script/DeveloperSettings.DeveloperSettings",
    ),
    (
        "/Script/EFProjectSystemsCore.EFProjectSurvivalSettings",
        "/Script/DeveloperSettings.DeveloperSettings",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsComponent",
        "/Script/Engine.ActorComponent",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectSurvivalStatusComponent",
        "/Script/Engine.ActorComponent",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectSinfulAscensionComponent",
        "/Script/Engine.ActorComponent",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectRuntimePerformanceSubsystem",
        "/Script/Engine.GameInstanceSubsystem",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectPerformanceBudgetSubsystem",
        "/Script/Engine.TickableWorldSubsystem",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectEmoteMenuWidget",
        "/Script/UMG.UserWidget",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsWidget",
        "/Script/UMG.UserWidget",
    ),
    (
        "/Script/EFProjectSystemsEditor.ProjectAnimationDiagnosticsLibrary",
        "/Script/Engine.BlueprintFunctionLibrary",
    ),
)

class_rows = []
for class_path, base_path in class_contracts:
    loaded_class = require_class(class_path)
    base_class = require_class(base_path)
    is_child = bool(
        loaded_class
        and base_class
        and unreal.MathLibrary.class_is_child_of(loaded_class, base_class)
    )
    record(
        "class_inheritance:" + class_path,
        is_child,
        bool(is_child),
        True,
    )
    class_rows.append(
        {
            "class": class_path,
            "expected_base": base_path,
            "passed": is_child,
        }
    )

plugin_dir = os.path.join(PROJECT_DIR, "Plugins", "EFProjectSystems")
with open(
    os.path.join(plugin_dir, "EFProjectSystems.uplugin"),
    "r",
    encoding="utf-8-sig",
) as handle:
    descriptor = json.load(handle)

actual_modules = {
    item["Name"]: item["Type"] for item in descriptor.get("Modules", [])
}
expected_modules = {
    "EFProjectSystemsCore": "Runtime",
    "EFProjectSystemsGameplay": "Runtime",
    "EFProjectSystemsUI": "Runtime",
    "EFProjectSystemsEditor": "Editor",
}
record(
    "descriptor_modules",
    actual_modules == expected_modules,
    actual_modules,
    expected_modules,
)
for module_name in expected_modules:
    binary_path = os.path.join(
        plugin_dir,
        "Binaries",
        "Win64",
        "UnrealEditor-{}.dll".format(module_name),
    )
    record(
        "editor_binary:" + module_name,
        os.path.isfile(binary_path),
        os.path.isfile(binary_path),
        True,
    )

# Config-backed input contract.
input_cdo = require_cdo("/Script/EFProjectSystemsCore.EFProjectInputSettings")
input_expected = {
    "ToggleWalkKey": "N",
    "ToggleCrawlKey": "C",
    "ToggleInteractionMenuKey": "Y",
    "ToggleNeedsHudKey": "Comma",
    "ToggleActivityFeedKey": "J",
    "ToggleGameplayDebugMenuKey": "L",
    "ToggleGameplayFreeCameraKey": "O",
    "SurrenderKey": "Down",
}
input_actual = {}
for property_name, expected in input_expected.items():
    actual = key_name(get_property(input_cdo, property_name))
    input_actual[property_name] = actual
    record("input:" + property_name, actual == expected, actual, expected)

intimacy_cdo = require_cdo(
    "/Script/EFProjectSystemsGameplay.ProjectIntimacySettings"
)
for property_name, expected in (
    ("HudToggleKey", "Hyphen"),
    ("HudSecondaryToggleKey", "Subtract"),
):
    actual = key_name(get_property(intimacy_cdo, property_name))
    record("input:" + property_name, actual == expected, actual, expected)

default_input_path = os.path.join(PROJECT_DIR, "Config", "DefaultInput.ini")
with open(default_input_path, "r", encoding="utf-8-sig") as handle:
    default_input_lines = {line.strip() for line in handle}
record(
    "input:TabConsolePreserved",
    "+ConsoleKeys=Tab" in default_input_lines,
    "+ConsoleKeys=Tab" in default_input_lines,
    True,
)
record(
    "input:EqualsConsoleReleased",
    "-ConsoleKeys=Equals" in default_input_lines
    and "+ConsoleKeys=Equals" not in default_input_lines,
    sorted(line for line in default_input_lines if "ConsoleKeys=Equals" in line),
    ["-ConsoleKeys=Equals"],
)

# Native survival defaults and release Sinful Ascension values.
needs_cdo = require_cdo(
    "/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsSettings"
)
record(
    "survival:BarsPerNeed",
    int(get_property(needs_cdo, "BarsPerNeed")) == 10,
    int(get_property(needs_cdo, "BarsPerNeed")),
    10,
)
float_record(
    "survival:PenaltyPerNeedAtZero",
    get_property(needs_cdo, "PenaltyPerNeedAtZero"),
    0.25,
)
record(
    "survival:bEnableAutoDecay",
    bool(get_property(needs_cdo, "bEnableAutoDecay")),
    bool(get_property(needs_cdo, "bEnableAutoDecay")),
    True,
)

need_names = [
    str(get_property(item, "NeedName"))
    for item in list(get_property(needs_cdo, "DefaultNeeds"))
]
sensation_names = [
    str(get_property(item, "SensationName"))
    for item in list(get_property(needs_cdo, "DefaultSensations"))
]
affected_attributes = [
    str(item)
    for item in list(
        get_property(needs_cdo, "AffectedSecondaryAttributes")
    )
]
record(
    "survival:DefaultNeeds",
    need_names == ["Hunger", "Thirst", "Sleep"],
    need_names,
    ["Hunger", "Thirst", "Sleep"],
)
record(
    "survival:DefaultSensations",
    sensation_names == ["Madness", "Pain", "Lust"],
    sensation_names,
    ["Madness", "Pain", "Lust"],
)
record(
    "survival:AffectedSecondaryAttributes",
    affected_attributes
    == [
        "MeleeDamage",
        "RangedDamage",
        "SpellDamage",
        "PhysicalDefense",
        "SpellDefense",
        "CritChance",
    ],
    affected_attributes,
    [
        "MeleeDamage",
        "RangedDamage",
        "SpellDamage",
        "PhysicalDefense",
        "SpellDefense",
        "CritChance",
    ],
)

sinful_cdo = require_cdo(
    "/Script/EFProjectSystemsGameplay.ProjectSinfulAscensionSettings"
)
record(
    "sinful:MaxSinAttributeLevel",
    int(get_property(sinful_cdo, "MaxSinAttributeLevel")) == 100,
    int(get_property(sinful_cdo, "MaxSinAttributeLevel")),
    100,
)
float_record(
    "sinful:SensationMaxHoldSeconds_RELEASE",
    get_property(sinful_cdo, "SensationMaxHoldSeconds"),
    10.0,
)
float_record(
    "sinful:DanceLustPercentOfMaxPerSecond_RELEASE",
    get_property(sinful_cdo, "DanceLustPercentOfMaxPerSecond"),
    0.05,
)
record(
    "sinful:DynamicMaximumRulesCount",
    len(list(get_property(sinful_cdo, "DynamicMaximumRules"))) == 7,
    len(list(get_property(sinful_cdo, "DynamicMaximumRules"))),
    7,
)

defeat_cdo = require_cdo(
    "/Script/EFProjectSystemsGameplay.ProjectDefeatFlowSettings"
)
float_record(
    "defeat:PainPerAppliedDamage_RELEASE",
    get_property(defeat_cdo, "PainPerAppliedDamage"),
    0.01,
)
record(
    "defeat:bEnableAdvancedDefeatFlow",
    bool(get_property(defeat_cdo, "bEnableAdvancedDefeatFlow")),
    bool(get_property(defeat_cdo, "bEnableAdvancedDefeatFlow")),
    True,
)

# Exact project-owned redirects. Resolution of serialized references is deferred
# until the corresponding allowlisted assets are migrated and loaded.
redirect_lines = (
    '+StructRedirects=(OldName="/Script/ACFUltimateSample.ProjectSurvivalConsumableProfile",NewName="/Script/EFProjectSystemsGameplay.ProjectSurvivalConsumableProfile")',
    '+ClassRedirects=(OldName="/Script/ACFUltimateSample.Project...",NewName="/Script/EFProjectSystemsGameplay.Project",MatchWildcard=true)',
    '+StructRedirects=(OldName="/Script/ACFUltimateSample.FProject...",NewName="/Script/EFProjectSystemsGameplay.FProject",MatchWildcard=true)',
    '+EnumRedirects=(OldName="/Script/ACFUltimateSample.EProject...",NewName="/Script/EFProjectSystemsGameplay.EProject",MatchWildcard=true)',
    '+FunctionRedirects=(OldName="/Script/ACFUltimateSample.Project...",NewName="/Script/EFProjectSystemsGameplay.Project",MatchWildcard=true)',
)
with open(
    os.path.join(PROJECT_DIR, "Config", "DefaultEngine.ini"),
    "r",
    encoding="utf-8-sig",
) as handle:
    engine_lines = {re.sub(r"\s+", "", line) for line in handle}
for redirect_line in redirect_lines:
    normalized = re.sub(r"\s+", "", redirect_line)
    record(
        "redirect:" + redirect_line.split("=", 1)[0],
        normalized in engine_lines,
        normalized in engine_lines,
        True,
    )

with open(
    os.path.join(PROJECT_DIR, "Config", "DefaultGameplayTags.ini"),
    "r",
    encoding="utf-8-sig",
) as handle:
    gameplay_tags_text = handle.read()
project_tags = sorted(
    set(
        re.findall(
            r'GameplayTagList=\(Tag="(Project\.[^"]+)"',
            gameplay_tags_text,
        )
    )
)
record(
    "gameplay_tags:ProjectTagCount",
    len(project_tags) == 58,
    len(project_tags),
    58,
)

# The registry is queried by package name only. No content object is loaded.
soft_packages = {
    "/Game/Procedural/Maps/DungeonGeneration": "deferred_world_flow",
    "/Game/Procedural/DoorToLevel": "deferred_world_flow",
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry": "deferred_survival",
    "/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses": "status_catalog",
    "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget": "deferred_defeat",
    "/Game/Data/CharacterBackground/DT_ProjectBackstories": "deferred_background",
    "/Game/Data/CharacterBackground/DT_ProjectProfessions": "deferred_background",
    "/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget": "deferred_background",
    "/Game/FullSample/Blueprints/Characters/Enemies/ACFMeleeEnemyBP": "target_authoritative_enemy",
    "/Game/FullSample/Blueprints/Characters/Enemies/ACFRangedEnemyBP": "target_authoritative_enemy",
    "/Game/FullSample/Blueprints/Characters/Enemies/ACFMageEnemyBP": "target_authoritative_enemy",
}
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], False)
registry.wait_for_completion()
soft_rows = []
for package_name, owner in sorted(soft_packages.items()):
    assets = list(
        registry.get_assets_by_package_name(unreal.Name(package_name))
    )
    soft_rows.append(
        {
            "package": package_name,
            "owner": owner,
            "exists": bool(assets),
            "asset_count": len(assets),
        }
    )

missing_soft = [row for row in soft_rows if not row["exists"]]
preview_path = os.path.join(
    PROJECT_DIR, "Content", "_Game", "Images", "preview.png"
)
filesystem_rows = [
    {
        "path": preview_path,
        "owner": "deferred_character_background_preview",
        "exists": os.path.isfile(preview_path),
    }
]
missing_filesystem = [row for row in filesystem_rows if not row["exists"]]
if STRICT_SOFT_ASSETS and (missing_soft or missing_filesystem):
    failures.append(
        "Strict content gate found {} missing packages and {} missing files".format(
            len(missing_soft), len(missing_filesystem)
        )
    )

if failures:
    status = "READ_ONLY_STRUCTURAL_PROBE_FAIL"
elif missing_soft or missing_filesystem:
    status = "READ_ONLY_STRUCTURAL_PROBE_PASS_SOFT_ASSETS_PENDING"
else:
    status = "READ_ONLY_STRUCTURAL_PROBE_PASS"

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(
        datetime.timezone.utc
    ).isoformat(),
    "status": status,
    "engine_version": engine_version,
    "strict_soft_assets": STRICT_SOFT_ASSETS,
    "classes": class_rows,
    "settings": {
        "input": input_actual,
        "needs": need_names,
        "sensations": sensation_names,
        "affected_secondary_attributes": affected_attributes,
    },
    "checks": checks,
    "redirects": {
        "expected": list(redirect_lines),
        "serialized_reference_resolution": "PENDING_ALLOWLISTED_ASSET_LOAD",
    },
    "gameplay_tags": project_tags,
    "soft_assets": {
        "total": len(soft_rows),
        "existing_count": len(soft_rows) - len(missing_soft),
        "missing_count": len(missing_soft),
        "records": soft_rows,
    },
    "filesystem_artifacts": filesystem_rows,
    "operations": {
        "map_load_requested": False,
        "content_asset_load_requested": False,
        "asset_save_requested": False,
        "blueprint_compile_requested": False,
    },
    "pending_runtime_input_contract": [
        "Period",
        "T",
        "Plus",
        "Minus",
        "H",
        "O",
        "N",
        "C",
        "Y",
        "J",
        "L",
        "Comma",
        "Down",
    ],
    "failures": failures,
}

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

if failures:
    unreal.log_error(
        "CODEX_EFPS_PROBE_FAIL: {} ({})".format(
            "; ".join(failures), OUTPUT_PATH
        )
    )
    raise RuntimeError("; ".join(failures))

unreal.log(
    "CODEX_EFPS_PROBE_PASS: {} ({})".format(status, OUTPUT_PATH)
)

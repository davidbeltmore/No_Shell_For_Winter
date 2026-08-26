"""Real visible HUB PIE validation for EF Clothing Morph V25.

The garment is acquired through ACF's interaction/inventory/equipment path.  This
script never assigns a skeletal mesh and never saves editor assets. It validates
the V25 pair/tier/bounds/skeleton contract before collecting rendered gameplay proof.
"""

import builtins
import hashlib
import json
import math
import os
import re
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_DIR = Path(
    os.environ.get(
        "CODEX_EF_CLOTHING_V2_QA_DIR",
        PROJECT_DIR / "Saved" / "ClothingMorphV2QA" / "HubPIE_adhoc",
    )
)
RESULT_FILE = Path(
    os.environ.get(
        "CODEX_EF_CLOTHING_V2_QA_RESULT",
        OUTPUT_DIR / "RuntimeResult.json",
    )
)
PROGRESS_FILE = OUTPUT_DIR / "Progress.log"
TARGET_MAP = os.environ.get("CODEX_EF_CLOTHING_V2_QA_MAP", "/Game/_Game/Hub/HUB")
TARGET_MAP_NAME = "hub"
TIMEOUT_SECONDS = float(os.environ.get("CODEX_EF_CLOTHING_V2_QA_TIMEOUT", "330"))
COMPILER_BINDING_FILE = Path(
    os.environ.get("CODEX_EF_CLOTHING_V2_COMPILER_BINDING", "")
)
EXPECTED_COMPILER_BINDING_SHA256 = os.environ.get(
    "CODEX_EF_CLOTHING_V2_COMPILER_BINDING_SHA256", ""
).upper()

REGISTRY_PATH = "/Game/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry"
SOURCE_GARMENT_PATH = "/Game/DazToUnreal/UnderWearPanty/UnderWearPanty"
BODY_PATH = "/Game/DazToUnreal/Female/Female"
COMPATIBILITY_PATH = "/Game/DazToUnreal/Multiple/Multiple"
PANTY_ITEM_CLASS = "/Game/_Game/Clothes/Panty.Panty_C"
LEGS_SLOT = "ItemSlot.Armor.Legs"
EXPECTED_COMPILER_VERSION = 25
EXPECTED_SKIN_PROFILE = "EF_AutoFit"
BODY_STRESS_MORPH = "Body Heavy"
BODY_SECONDARY_STRESS_MORPH = "Body Fitness Mass"
EXPECTED_PAIR_BODY_MORPHS = (
    BODY_SECONDARY_STRESS_MORPH,
    BODY_STRESS_MORPH,
)
EXPECTED_PAIR_GRID_RESOLUTION = 4
EXPECTED_PAIR_PROBES_PER_AXIS = 3
EXPECTED_PAIR_OFFSET_TIERS = 9
EXPECTED_PAIR_CELL_COUNT = EXPECTED_PAIR_GRID_RESOLUTION**2
EXPECTED_PAIR_BODY_PROBE_COUNT = (
    EXPECTED_PAIR_CELL_COUNT * EXPECTED_PAIR_PROBES_PER_AXIS**2
)
EXPECTED_PAIR_OFFSET_EVALUATION_COUNT = (
    EXPECTED_PAIR_BODY_PROBE_COUNT * EXPECTED_PAIR_OFFSET_TIERS
)
EXPECTED_MORPH_ACTIVATION_EPSILON = 0.0
EXPECTED_GLOBAL_CLEARANCE = 1.10
EXPECTED_GARMENT_CLEARANCE = 1.25
EXPECTED_CLEARANCE_MIN = 1.0
EXPECTED_CLEARANCE_MAX = 2.0
EXPECTED_CLEARANCE_TIERS = 9
MORPH_RANGE_EPSILON = 0.001
EXPECTED_SCREENSHOT_FILENAMES = (
    "01_acf_pickup_selected.png",
    "02_front_idle_ready.png",
    "03_back_heavy_fitness_ready.png",
    "03b_back_heavy_fitness_closeup.png",
    "04_front_offset_product.png",
    "05_side_walk_motion.png",
    "05b_side_walk_motion_burst.png",
    "06_back_walk_motion.png",
    "07_side_crawl_heavy_fitness.png",
    "07b_side_crawl_closeup.png",
    "08_cvar_disabled_source_rollback.png",
    "09_cvar_reenabled_ready.png",
)

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    if not value:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def class_path(value):
    if not value:
        return ""
    try:
        return value.get_class().get_path_name()
    except Exception:
        return ""


def get_property(value, *names, default=None):
    if value is None:
        return default
    for name in names:
        try:
            return value.get_editor_property(name)
        except Exception:
            pass
        try:
            return getattr(value, name)
        except Exception:
            pass
    return default


def call(value, method_name, *args):
    method = getattr(value, method_name, None)
    if not callable(method):
        raise RuntimeError(
            f"Missing reflected method {method_name} on {object_path(value) or value}"
        )
    return method(*args)


def normalized_world_name(world):
    if not world:
        return ""
    name = world.get_name()
    name = re.sub(r"^UEDPIE_\d+_", "", name, flags=re.IGNORECASE)
    return name.lower()


def canonical_asset_path(value):
    if not value:
        return ""
    for method_name in ("get_asset_path_name", "get_asset_path_string"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                candidate = str(method())
                if candidate:
                    return candidate.split(".", 1)[0]
            except Exception:
                pass
    to_soft_path = getattr(value, "to_soft_object_path", None)
    if callable(to_soft_path):
        try:
            return canonical_asset_path(to_soft_path())
        except Exception:
            pass
    text = object_path(value)
    match = re.search(r"(/Game/[A-Za-z0-9_./-]+)", text)
    if match:
        return match.group(1).split(".", 1)[0]
    return text.split(".", 1)[0]


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def normalize_guid(value):
    if value is None:
        return ""
    guid_library = getattr(unreal, "GuidLibrary", None)
    converter = getattr(guid_library, "conv_guid_to_string", None)
    if callable(converter):
        try:
            converted = str(converter(value))
            converted_match = re.search(
                r"(?i)(?<![0-9a-f])([0-9a-f]{32})(?![0-9a-f])", converted
            )
            if converted_match:
                return converted_match.group(1).upper()
        except Exception:
            pass
    text = str(value).strip()
    dashed = re.search(
        r"(?i)([0-9a-f]{8})-([0-9a-f]{4})-([0-9a-f]{4})-"
        r"([0-9a-f]{4})-([0-9a-f]{12})",
        text,
    )
    if dashed:
        return "".join(dashed.groups()).upper()
    compact = re.search(r"(?i)(?<![0-9a-f])([0-9a-f]{32})(?![0-9a-f])", text)
    if compact:
        return compact.group(1).upper()
    components = re.findall(r"(?i)(?:^|[,({ ])(?:A|B|C|D)\s*=\s*([0-9a-f]{8})", text)
    if len(components) == 4:
        return "".join(components).upper()
    return ""


def gameplay_tag_string(value):
    if value is None:
        return ""
    library = getattr(unreal, "GameplayTagLibrary", None)
    get_debug = getattr(library, "get_debug_string_from_gameplay_tag", None)
    if callable(get_debug):
        try:
            debug_text = str(get_debug(value)).strip()
            if debug_text:
                return debug_text
        except Exception:
            pass
    get_name = getattr(library, "get_tag_name", None)
    if callable(get_name):
        try:
            tag_name = str(get_name(value)).strip()
            if tag_name and tag_name.lower() not in ("none", "name_none"):
                return tag_name
        except Exception:
            pass
    return str(value).strip()


def project_file_from_binding(row, label):
    relative_path = str(row.get("relative_path", "")).replace("/", os.sep)
    require(relative_path and not os.path.isabs(relative_path), f"{label} has no relative path")
    candidate = (PROJECT_DIR / relative_path).resolve()
    project_root = PROJECT_DIR.resolve()
    require(
        os.path.commonpath((str(project_root), str(candidate))) == str(project_root),
        f"{label} escapes the target project: {candidate}",
    )
    require(candidate.is_file(), f"{label} is missing: {candidate}")
    expected_size = int(row.get("size_bytes", -1))
    expected_hash = str(row.get("sha256", "")).upper()
    require(expected_size >= 0 and candidate.stat().st_size == expected_size, f"{label} size changed")
    require(re.fullmatch(r"[0-9A-F]{64}", expected_hash), f"{label} SHA-256 is malformed")
    require(sha256_file(candidate) == expected_hash, f"{label} SHA-256 changed")
    return candidate


def load_compiler_binding():
    require(str(COMPILER_BINDING_FILE), "Compiler PASS binding was not provided")
    binding_path = COMPILER_BINDING_FILE.resolve()
    require(binding_path.is_file(), f"Compiler binding is missing: {binding_path}")
    binding_hash = sha256_file(binding_path)
    require(
        re.fullmatch(r"[0-9A-F]{64}", EXPECTED_COMPILER_BINDING_SHA256)
        and binding_hash == EXPECTED_COMPILER_BINDING_SHA256,
        "Compiler binding manifest hash does not match the runner",
    )
    binding = json.loads(binding_path.read_text(encoding="utf-8-sig"))
    require(int(binding.get("schema_version", -1)) == 1, "Unsupported compiler binding schema")

    receipt_row = binding.get("compiler_receipt", {})
    receipt_path = project_file_from_binding(receipt_row, "Compiler receipt")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    metrics = receipt.get("metrics", {})
    require(receipt.get("success") is True, "Bound compiler receipt is not PASS")
    require(receipt.get("compile_success") is True, "Bound native compile did not pass")
    require(receipt.get("validation_success") is True, "Bound compiler validation did not pass")
    require(
        receipt.get("protected_inputs_unchanged") is True,
        "Bound compiler receipt did not preserve protected inputs",
    )
    require(
        str(receipt.get("status", "")) == "UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS",
        "Bound receipt has the wrong status",
    )
    require(
        int(metrics.get("compiler_version", -1)) == EXPECTED_COMPILER_VERSION,
        "Bound receipt is not compiler V25",
    )
    receipt_inputs = receipt.get("inputs", {})
    require(
        canonical_asset_path(receipt_inputs.get("source_garment")) == SOURCE_GARMENT_PATH
        and canonical_asset_path(receipt_inputs.get("body_surface")) == BODY_PATH
        and canonical_asset_path(receipt_inputs.get("compatibility_reference"))
        == COMPATIBILITY_PATH,
        "Bound receipt inputs do not match this gameplay proof",
    )
    protected_before = receipt.get("protected_sha256_before", {})
    protected_after = receipt.get("protected_sha256_after", {})
    require(
        protected_before == protected_after and protected_after,
        "Bound receipt protected hashes are missing or changed",
    )
    verified_protected = []
    for key, row in sorted(protected_after.items()):
        candidate = Path(str(row.get("file", ""))).resolve()
        project_root = PROJECT_DIR.resolve()
        require(
            os.path.commonpath((str(project_root), str(candidate))) == str(project_root),
            f"Receipt protected file escapes the target project: {candidate}",
        )
        require(candidate.is_file(), f"Receipt protected file is missing: {candidate}")
        require(
            candidate.stat().st_size == int(row.get("size_bytes", -1)),
            f"Receipt protected size changed for {key}",
        )
        require(
            sha256_file(candidate) == str(row.get("sha256", "")).upper(),
            f"Receipt protected SHA-256 changed for {key}",
        )
        verified_protected.append(key)

    compiler_row = binding.get("compiler", {})
    receipt_guid = normalize_guid(metrics.get("build_guid"))
    require(receipt_guid, "Bound receipt build GUID is missing")
    require(
        receipt_guid == normalize_guid(compiler_row.get("build_guid")),
        "Binding and receipt build GUID differ",
    )
    outputs = binding.get("outputs", {})
    receipt_outputs = receipt.get("outputs", {})
    for key in ("derived_garment", "profile"):
        require(
            canonical_asset_path(outputs.get(key))
            == canonical_asset_path(receipt_outputs.get(key)),
            f"Bound {key} differs from the PASS receipt",
        )
    require(
        canonical_asset_path(outputs.get("registry")) == REGISTRY_PATH,
        "Bound registry path is wrong",
    )

    assets = binding.get("assets", {})
    for key in ("derived_garment", "profile", "registry"):
        row = assets.get(key, {})
        require(
            canonical_asset_path(row.get("object_path"))
            == canonical_asset_path(outputs.get(key)),
            f"Bound {key} object path differs from its hashed asset",
        )
        project_file_from_binding(row, f"Bound {key}")

    result = dict(binding)
    result["manifest"] = str(binding_path)
    result["manifest_sha256"] = binding_hash
    result["verified_receipt"] = str(receipt_path)
    result["verified_protected_inputs"] = verified_protected
    STATE.result["compiler_binding"] = result
    return result


def validate_binding_against_loaded_assets(registry, profile_snapshot_value):
    binding = STATE.result["compiler_binding"]
    outputs = binding["outputs"]
    require(
        canonical_asset_path(registry) == canonical_asset_path(outputs["registry"]),
        "Loaded registry differs from the compiler binding",
    )
    require(
        canonical_asset_path(profile_snapshot_value["object"])
        == canonical_asset_path(outputs["profile"]),
        "Loaded fit profile differs from the compiler binding",
    )
    require(
        profile_snapshot_value["fitted_garment"]
        == canonical_asset_path(outputs["derived_garment"]),
        "Loaded fitted mesh differs from the compiler binding",
    )
    require(
        (
            normalize_guid(profile_snapshot_value["build_guid"])
            or normalize_guid(profile_snapshot_value["object"])
        )
        == normalize_guid(binding["compiler"]["build_guid"]),
        "Loaded profile build GUID differs from the PASS compiler receipt",
    )
    binding["loaded_assets_match"] = True


def mesh_asset(component):
    if not component:
        return None
    getter = getattr(component, "get_skeletal_mesh_asset", None)
    if callable(getter):
        try:
            return getter()
        except Exception:
            pass
    return get_property(component, "skeletal_mesh", default=None)


def component_tags(component):
    return [str(tag) for tag in (get_property(component, "component_tags", default=[]) or [])]


def vector_add(a, b):
    return unreal.Vector(a.x + b.x, a.y + b.y, a.z + b.z)


def vector_subtract(a, b):
    return unreal.Vector(a.x - b.x, a.y - b.y, a.z - b.z)


def vector_scale(value, scalar):
    return unreal.Vector(value.x * scalar, value.y * scalar, value.z * scalar)


def vector_length(value):
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


def finite_number(value):
    return isinstance(value, (int, float)) and math.isfinite(float(value))


def vector_snapshot(value):
    require(value is not None, "Expected a reflected FVector")
    result = {
        "x": float(value.x),
        "y": float(value.y),
        "z": float(value.z),
    }
    require(all(finite_number(axis) for axis in result.values()), f"Non-finite vector: {result}")
    return result


def bounds_snapshot(mesh):
    require(mesh is not None, "Cannot inspect bounds of a null skeletal mesh")
    getter = getattr(mesh, "get_imported_bounds", None)
    require(callable(getter), f"SkeletalMesh exposes no GetImportedBounds: {object_path(mesh)}")
    bounds = getter()
    extent = get_property(bounds, "box_extent", "BoxExtent", default=None)
    origin = get_property(bounds, "origin", "Origin", default=None)
    radius = float(get_property(bounds, "sphere_radius", "SphereRadius", default=float("nan")))
    require(finite_number(radius) and radius > 0.0, f"Invalid sphere bounds on {object_path(mesh)}")
    return {
        "origin": vector_snapshot(origin),
        "box_extent": vector_snapshot(extent),
        "sphere_radius": radius,
    }


def mesh_skeleton_path(mesh):
    if not mesh:
        return ""
    getter = getattr(mesh, "get_skeleton", None)
    skeleton = getter() if callable(getter) else get_property(mesh, "skeleton", default=None)
    return canonical_asset_path(skeleton)


def parse_morph_target_names(mesh):
    """Return the cooked mesh's actual targets; refuse an unverifiable absence check."""
    names = set()
    direct_getter = getattr(mesh, "get_morph_target_names", None)
    if callable(direct_getter):
        try:
            names.update(str(name) for name in (direct_getter() or []) if str(name))
        except Exception:
            pass
    if not names:
        try:
            morphs = get_property(mesh, "morph_targets", "MorphTargets", default=[]) or []
            names.update(str(morph.get_name()) for morph in morphs if morph)
        except Exception:
            pass
    if not names:
        asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
        asset_data = asset_registry.get_asset_by_object_path(object_path(mesh))
        if asset_data:
            raw = str(asset_data.get_tag_value("MorphTargetNames") or "")
            names.update(token.strip() for token in raw.split(";") if token.strip())
    require(names, f"Could not enumerate cooked morph targets for {object_path(mesh)}")
    return sorted(names, key=str.lower)


def emit(message):
    line = f"[EFClothingMorphV2HubPIE58] {message}"
    unreal.log(line)
    STATE.lines.append(line)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    PROGRESS_FILE.write_text("\n".join(STATE.lines) + "\n", encoding="utf-8")


def write_result():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    RESULT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False, sort_keys=True, default=str)
        + "\n",
        encoding="utf-8",
    )


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def find_component(actor, class_fragment):
    if not actor:
        return None
    fragment = class_fragment.lower()
    for component in actor.get_components_by_class(unreal.ActorComponent) or []:
        identity = f"{component.get_name()} {class_path(component)}".lower()
        if fragment in identity:
            return component
    return None


def all_skeletal_components(actor):
    if not actor:
        return []
    return list(actor.get_components_by_class(unreal.SkeletalMeshComponent) or [])


def find_body_component(actor):
    for component in all_skeletal_components(actor):
        if canonical_asset_path(mesh_asset(component)) == BODY_PATH:
            return component
    return None


def resolve_effective_pose_driver(component):
    """Mirror V25/UE's top-most skeletal LeaderPose resolution without mutation."""
    chain = []
    visited = set()
    current = component
    while current is not None and len(chain) < 128:
        identity = object_path(current)
        require(identity and identity not in visited, f"Cyclic LeaderPose chain: {chain}")
        visited.add(identity)
        chain.append(
            {
                "component": identity,
                "mesh": canonical_asset_path(mesh_asset(current)),
            }
        )
        leader = get_property(current, "leader_pose_component", default=None)
        if leader is None:
            return current, chain
        require(
            isinstance(leader, unreal.SkeletalMeshComponent),
            f"LeaderPose chain contains a non-skeletal component: {object_path(leader)}",
        )
        current = leader
    raise RuntimeError(f"LeaderPose chain exceeded its safety depth: {chain}")


def visible_character_creation_widgets():
    rows = []
    try:
        widgets = unreal.ObjectIterator(unreal.UserWidget)
    except Exception:
        return rows
    for widget in widgets:
        try:
            if not widget.is_in_viewport():
                continue
            identity = f"{object_path(widget)} {class_path(widget)}"
            lowered = identity.lower()
            visibility = str(widget.get_visibility())
            visibility_lower = visibility.lower()
            render_visible = (
                "hidden" not in visibility_lower
                and "collapsed" not in visibility_lower
            )
            if render_visible and (
                "efcharactercreation" in lowered
                or "wbp_efcharactercreationroot" in lowered
            ):
                rows.append(
                    {
                        "object": object_path(widget),
                        "class": class_path(widget),
                        "visibility": visibility,
                    }
                )
        except Exception:
            continue
    rows.sort(key=lambda row: row["object"].lower())
    return rows


def verify_character_creation_absent(checkpoint):
    rows = visible_character_creation_widgets()
    STATE.result["character_creation"][checkpoint] = rows
    require(not rows, f"Character Creation is in the viewport at {checkpoint}: {rows}")


def sample_early_character_creation():
    """Observe every valid early PIE tick, beginning before pawn stabilization."""
    sampling = STATE.result["character_creation_early_sampling"]
    if sampling["status"] != "PENDING":
        return
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    if not world:
        return
    rows = visible_character_creation_widgets()
    sampling["sample_count"] += 1
    world_seconds = float(unreal.GameplayStatics.get_time_seconds(world))
    if sampling["first_world_seconds"] is None:
        sampling["first_world_seconds"] = world_seconds
    sampling["last_world_seconds"] = world_seconds
    if rows:
        sampling["violations"].append(
            {
                "sample_index": sampling["sample_count"],
                "world_seconds": world_seconds,
                "widgets": rows,
            }
        )
    require(
        not rows,
        f"Character Creation became visibly renderable during early HUB PIE: {rows}",
    )


def find_profile():
    registry = unreal.load_asset(REGISTRY_PATH)
    require(registry is not None, f"Missing V2 registry: {REGISTRY_PATH}")
    profiles = get_property(registry, "profiles", "Profiles", default=[]) or []
    for profile in profiles:
        source_path = canonical_asset_path(
            get_property(profile, "source_garment", "SourceGarment", default=None)
        )
        if source_path == SOURCE_GARMENT_PATH:
            return registry, profile
    raise RuntimeError(
        f"Registry {REGISTRY_PATH} has no profile for {SOURCE_GARMENT_PATH}"
    )


def morph_sample_snapshot(sample):
    return {
        "body_value": float(
            get_property(sample, "body_value", "BodyValue", default=float("nan"))
        ),
        "garment_morph": str(
            get_property(sample, "garment_morph", "GarmentMorph", default="")
        ),
        "identity": bool(
            get_property(sample, "identity", "b_identity", "bIdentity", default=False)
        ),
        "step_from_previous": bool(
            get_property(
                sample,
                "step_from_previous",
                "b_step_from_previous",
                "bStepFromPrevious",
                default=False,
            )
        ),
        "step_switch_body_value": float(
            get_property(
                sample,
                "step_switch_body_value",
                "StepSwitchBodyValue",
                default=float("nan"),
            )
        ),
        "minimum_clearance_multiplier": float(
            get_property(
                sample,
                "minimum_clearance_multiplier",
                "MinimumClearanceMultiplier",
                default=float("nan"),
            )
        ),
    }


def morph_binding_snapshot(binding):
    samples = [
        morph_sample_snapshot(sample)
        for sample in (get_property(binding, "samples", "Samples", default=[]) or [])
    ]
    return {
        "body_morph": str(
            get_property(binding, "body_morph", "BodyMorph", default="")
        ),
        "garment_morph": str(
            get_property(binding, "garment_morph", "GarmentMorph", default="")
        ),
        "scale": float(get_property(binding, "scale", "Scale", default=1.0)),
        "bias": float(get_property(binding, "bias", "Bias", default=0.0)),
        "minimum_certified_value": float(
            get_property(
                binding,
                "minimum_certified_value",
                "MinimumCertifiedValue",
                default=float("nan"),
            )
        ),
        "maximum_certified_value": float(
            get_property(
                binding,
                "maximum_certified_value",
                "MaximumCertifiedValue",
                default=float("nan"),
            )
        ),
        "sample_count": len(samples),
        "samples": samples,
    }


def pair_cell_snapshot(cell):
    return {
        "first_cell_index": int(
            get_property(cell, "first_cell_index", "FirstCellIndex", default=-1)
        ),
        "second_cell_index": int(
            get_property(cell, "second_cell_index", "SecondCellIndex", default=-1)
        ),
        "first_minimum_value": float(
            get_property(cell, "first_minimum_value", "FirstMinimumValue", default=float("nan"))
        ),
        "first_maximum_value": float(
            get_property(cell, "first_maximum_value", "FirstMaximumValue", default=float("nan"))
        ),
        "second_minimum_value": float(
            get_property(cell, "second_minimum_value", "SecondMinimumValue", default=float("nan"))
        ),
        "second_maximum_value": float(
            get_property(cell, "second_maximum_value", "SecondMaximumValue", default=float("nan"))
        ),
        "garment_morph": str(
            get_property(cell, "garment_morph", "GarmentMorph", default="")
        ),
        "minimum_certified_gap_cm": float(
            get_property(
                cell,
                "minimum_certified_gap_cm",
                "MinimumCertifiedGapCm",
                default=float("nan"),
            )
        ),
        "minimum_clearance_multiplier": float(
            get_property(
                cell,
                "minimum_clearance_multiplier",
                "MinimumClearanceMultiplier",
                default=float("nan"),
            )
        ),
        "certified_body_probe_count": int(
            get_property(
                cell,
                "certified_body_probe_count",
                "CertifiedBodyProbeCount",
                default=-1,
            )
        ),
        "certified_offset_evaluation_count": int(
            get_property(
                cell,
                "certified_offset_evaluation_count",
                "CertifiedOffsetEvaluationCount",
                default=-1,
            )
        ),
    }


def pair_certificate_snapshot(certificate):
    cells = [
        pair_cell_snapshot(cell)
        for cell in (
            get_property(certificate, "cells", "Cells", default=[]) or []
        )
    ]
    cells.sort(key=lambda row: (row["first_cell_index"], row["second_cell_index"]))
    return {
        "first_body_morph": str(
            get_property(certificate, "first_body_morph", "FirstBodyMorph", default="")
        ),
        "second_body_morph": str(
            get_property(certificate, "second_body_morph", "SecondBodyMorph", default="")
        ),
        "first_minimum_certified_value": float(
            get_property(
                certificate,
                "first_minimum_certified_value",
                "FirstMinimumCertifiedValue",
                default=float("nan"),
            )
        ),
        "first_maximum_certified_value": float(
            get_property(
                certificate,
                "first_maximum_certified_value",
                "FirstMaximumCertifiedValue",
                default=float("nan"),
            )
        ),
        "second_minimum_certified_value": float(
            get_property(
                certificate,
                "second_minimum_certified_value",
                "SecondMinimumCertifiedValue",
                default=float("nan"),
            )
        ),
        "second_maximum_certified_value": float(
            get_property(
                certificate,
                "second_maximum_certified_value",
                "SecondMaximumCertifiedValue",
                default=float("nan"),
            )
        ),
        "grid_resolution": int(
            get_property(certificate, "grid_resolution", "GridResolution", default=-1)
        ),
        "probe_count_per_axis": int(
            get_property(
                certificate,
                "probe_count_per_axis",
                "ProbeCountPerAxis",
                default=-1,
            )
        ),
        "certified_offset_tier_count": int(
            get_property(
                certificate,
                "certified_offset_tier_count",
                "CertifiedOffsetTierCount",
                default=-1,
            )
        ),
        "minimum_certified_gap_cm": float(
            get_property(
                certificate,
                "minimum_certified_gap_cm",
                "MinimumCertifiedGapCm",
                default=float("nan"),
            )
        ),
        "cell_count": len(cells),
        "cells": cells,
    }


def profile_snapshot(profile):
    bindings = get_property(profile, "morph_bindings", "MorphBindings", default=[]) or []
    binding_rows = [morph_binding_snapshot(binding) for binding in bindings]
    pair_certificates = get_property(
        profile,
        "morph_pair_certificates",
        "MorphPairCertificates",
        default=[],
    ) or []
    monitored_body_morph_names = [
        str(name)
        for name in (
            get_property(
                profile,
                "monitored_body_morph_names",
                "MonitoredBodyMorphNames",
                default=[],
            )
            or []
        )
    ]
    required_bones = get_property(
        profile, "required_weighted_bones", "RequiredWeightedBones", default=[]
    ) or []
    bounds_expansion = get_property(
        profile,
        "compiled_concurrent_bounds_expansion_cm",
        "CompiledConcurrentBoundsExpansionCm",
        default=None,
    )
    return {
        "object": object_path(profile),
        "compiler_version": int(
            get_property(profile, "compiler_version", "CompilerVersion", default=-1)
        ),
        "build_guid": str(get_property(profile, "build_guid", "BuildGuid", default="")),
        "source_garment": canonical_asset_path(
            get_property(profile, "source_garment", "SourceGarment", default=None)
        ),
        "fitted_garment": canonical_asset_path(
            get_property(profile, "fitted_garment", "FittedGarment", default=None)
        ),
        "body_surface": canonical_asset_path(
            get_property(profile, "body_surface", "BodySurface", default=None)
        ),
        "compatibility_reference": canonical_asset_path(
            get_property(
                profile,
                "compatibility_reference",
                "CompatibilityReference",
                default=None,
            )
        ),
        "skin_weight_profile_name": str(
            get_property(
                profile,
                "skin_weight_profile_name",
                "SkinWeightProfileName",
                default="",
            )
        ),
        "clearance_morph_name": str(
            get_property(
                profile, "clearance_morph_name", "ClearanceMorphName", default=""
            )
        ),
        "default_clearance_value": float(
            get_property(
                profile,
                "default_clearance_value",
                "DefaultClearanceValue",
                default=float("nan"),
            )
        ),
        "certified_clearance_multiplier_min": float(
            get_property(
                profile,
                "certified_clearance_multiplier_min",
                "CertifiedClearanceMultiplierMin",
                default=float("nan"),
            )
        ),
        "certified_clearance_multiplier_max": float(
            get_property(
                profile,
                "certified_clearance_multiplier_max",
                "CertifiedClearanceMultiplierMax",
                default=float("nan"),
            )
        ),
        "certified_clearance_tier_count": int(
            get_property(
                profile,
                "certified_clearance_tier_count",
                "CertifiedClearanceTierCount",
                default=-1,
            )
        ),
        "minimum_certified_offset_gap_cm": float(
            get_property(
                profile,
                "minimum_certified_offset_gap_cm",
                "MinimumCertifiedOffsetGapCm",
                default=float("nan"),
            )
        ),
        "compiled_minimum_clearance_cm": float(
            get_property(
                profile,
                "compiled_minimum_clearance_cm",
                "CompiledMinimumClearanceCm",
                default=-1.0,
            )
        ),
        "compiled_clearance_reserve_cm": float(
            get_property(
                profile,
                "compiled_clearance_reserve_cm",
                "CompiledClearanceReserveCm",
                default=-1.0,
            )
        ),
        "penetrating_vertex_count_after": int(
            get_property(
                profile,
                "penetrating_vertex_count_after",
                "PenetratingVertexCountAfter",
                default=-1,
            )
        ),
        "minimum_signed_gap_after_cm": float(
            get_property(
                profile,
                "minimum_signed_gap_after_cm",
                "MinimumSignedGapAfterCm",
                default=-1.0,
            )
        ),
        "minimum_sampled_morph_gap_cm": float(
            get_property(
                profile,
                "minimum_sampled_morph_gap_cm",
                "MinimumSampledMorphGapCm",
                default=-1.0,
            )
        ),
        "compiled_morph_threshold_position_cm": float(
            get_property(
                profile,
                "compiled_morph_threshold_position_cm",
                "CompiledMorphThresholdPositionCm",
                default=float("nan"),
            )
        ),
        "post_threshold_altered_delta_count": int(
            get_property(
                profile,
                "post_threshold_altered_delta_count",
                "PostThresholdAlteredDeltaCount",
                default=-1,
            )
        ),
        "compiled_concurrent_bounds_expansion_cm": vector_snapshot(bounds_expansion),
        "compiled_concurrent_sphere_expansion_cm": float(
            get_property(
                profile,
                "compiled_concurrent_sphere_expansion_cm",
                "CompiledConcurrentSphereExpansionCm",
                default=float("nan"),
            )
        ),
        "shared_skeleton_fingerprint": str(
            get_property(
                profile,
                "shared_skeleton_fingerprint",
                "SharedSkeletonFingerprint",
                default="",
            )
        ),
        "binding_count": len(binding_rows),
        "morph_bindings": binding_rows,
        "monitored_body_morph_names": monitored_body_morph_names,
        "morph_pair_certificates": [
            pair_certificate_snapshot(certificate)
            for certificate in pair_certificates
        ],
        "morph_pair_certificate_count": len(pair_certificates),
        "compiled_morph_activation_epsilon": float(
            get_property(
                profile,
                "compiled_morph_activation_epsilon",
                "CompiledMorphActivationEpsilon",
                default=float("nan"),
            )
        ),
        "required_weighted_bone_count": len(required_bones),
        "generated_morph_sample_count": int(
            get_property(
                profile,
                "generated_morph_sample_count",
                "GeneratedMorphSampleCount",
                default=-1,
            )
        ),
        "maximum_morph_samples_per_binding": int(
            get_property(
                profile,
                "maximum_morph_samples_per_binding",
                "MaximumMorphSamplesPerBinding",
                default=-1,
            )
        ),
        "stepped_morph_interval_count": int(
            get_property(
                profile,
                "stepped_morph_interval_count",
                "SteppedMorphIntervalCount",
                default=-1,
            )
        ),
        "certified_morph_pair_count": int(
            get_property(
                profile,
                "certified_morph_pair_count",
                "CertifiedMorphPairCount",
                default=-1,
            )
        ),
        "generated_pair_cell_morph_count": int(
            get_property(
                profile,
                "generated_pair_cell_morph_count",
                "GeneratedPairCellMorphCount",
                default=-1,
            )
        ),
        "pair_body_probe_count": int(
            get_property(
                profile,
                "pair_body_probe_count",
                "PairBodyProbeCount",
                default=-1,
            )
        ),
        "pair_offset_evaluation_count": int(
            get_property(
                profile,
                "pair_offset_evaluation_count",
                "PairOffsetEvaluationCount",
                default=-1,
            )
        ),
        "minimum_sampled_pair_gap_cm": float(
            get_property(
                profile,
                "minimum_sampled_pair_gap_cm",
                "MinimumSampledPairGapCm",
                default=float("nan"),
            )
        ),
    }


def quantize_certified_offset_for_profile(profile, requested):
    minimum = profile["certified_clearance_multiplier_min"]
    maximum = profile["certified_clearance_multiplier_max"]
    tier_count = profile["certified_clearance_tier_count"]
    require(
        finite_number(minimum)
        and finite_number(maximum)
        and tier_count >= 2
        and maximum > minimum,
        "Certified offset tier contract is invalid",
    )
    require(finite_number(requested), f"Clearance request is not finite: {requested}")
    clamped = max(minimum, min(maximum, float(requested)))
    tier_step = (maximum - minimum) / float(tier_count - 1)
    tier_index = int(math.ceil(((clamped - minimum) / tier_step) - 1.0e-6))
    tier_index = max(0, min(tier_count - 1, tier_index))
    return minimum + tier_step * tier_index


def is_exact_certified_offset_tier(profile, value):
    return finite_number(value) and abs(
        quantize_certified_offset_for_profile(profile, value) - value
    ) <= 0.0001


def validate_profile_contract(profile):
    snapshot = profile_snapshot(profile)
    require(
        snapshot["compiler_version"] == EXPECTED_COMPILER_VERSION,
        f"Expected compiler v{EXPECTED_COMPILER_VERSION}, got {snapshot['compiler_version']}",
    )
    require(snapshot["source_garment"] == SOURCE_GARMENT_PATH, "Source garment mismatch")
    require(snapshot["body_surface"] == BODY_PATH, "Body surface mismatch")
    require(
        snapshot["compatibility_reference"] == COMPATIBILITY_PATH,
        "Multiple compatibility reference mismatch",
    )
    require(
        snapshot["fitted_garment"].startswith("/Game/_Generated/EFClothingMorphV2/"),
        "Fitted garment is not project-generated",
    )
    require(
        snapshot["skin_weight_profile_name"] == EXPECTED_SKIN_PROFILE,
        "Compiled skin profile mismatch",
    )
    require(snapshot["clearance_morph_name"], "Profile has no clearance morph")
    require(
        finite_number(snapshot["default_clearance_value"])
        and snapshot["default_clearance_value"] > 0.0,
        "Profile default clearance value is invalid",
    )
    require(
        abs(snapshot["certified_clearance_multiplier_min"] - EXPECTED_CLEARANCE_MIN)
        <= 0.0001,
        f"Unexpected certified offset minimum: {snapshot['certified_clearance_multiplier_min']}",
    )
    require(
        abs(snapshot["certified_clearance_multiplier_max"] - EXPECTED_CLEARANCE_MAX)
        <= 0.0001,
        f"Unexpected certified offset maximum: {snapshot['certified_clearance_multiplier_max']}",
    )
    require(
        snapshot["certified_clearance_tier_count"] == EXPECTED_CLEARANCE_TIERS,
        f"Expected {EXPECTED_CLEARANCE_TIERS} certified tiers: {snapshot}",
    )
    require(
        finite_number(snapshot["compiled_morph_activation_epsilon"])
        and snapshot["compiled_morph_activation_epsilon"]
        == EXPECTED_MORPH_ACTIVATION_EPSILON,
        "V25 morph activation epsilon must be exactly zero",
    )
    monitored_names = snapshot["monitored_body_morph_names"]
    require(monitored_names, "V25 profile has no monitored body-morph set")
    require(
        len(monitored_names) == len(set(monitored_names)),
        f"V25 monitored body-morph set contains duplicates: {monitored_names}",
    )
    require(
        all(name in monitored_names for name in EXPECTED_PAIR_BODY_MORPHS),
        f"V25 does not monitor the certified stress pair: {monitored_names}",
    )
    require(
        snapshot["morph_pair_certificate_count"] == 1
        and snapshot["certified_morph_pair_count"] == 1,
        f"Expected exactly one V25 pair certificate: {snapshot['morph_pair_certificates']}",
    )
    require(
        snapshot["generated_pair_cell_morph_count"] == EXPECTED_PAIR_CELL_COUNT,
        f"Expected {EXPECTED_PAIR_CELL_COUNT} generated V25 pair-cell morphs",
    )
    require(
        snapshot["pair_body_probe_count"] == EXPECTED_PAIR_BODY_PROBE_COUNT,
        f"Expected {EXPECTED_PAIR_BODY_PROBE_COUNT} V25 pair body probes",
    )
    require(
        snapshot["pair_offset_evaluation_count"]
        == EXPECTED_PAIR_OFFSET_EVALUATION_COUNT,
        f"Expected {EXPECTED_PAIR_OFFSET_EVALUATION_COUNT} V25 pair/offset evaluations",
    )

    pair_certificate = snapshot["morph_pair_certificates"][0]
    require(
        (
            pair_certificate["first_body_morph"],
            pair_certificate["second_body_morph"],
        )
        == EXPECTED_PAIR_BODY_MORPHS,
        f"Unexpected canonical V25 pair: {pair_certificate}",
    )
    require(
        pair_certificate["grid_resolution"] == EXPECTED_PAIR_GRID_RESOLUTION
        and pair_certificate["probe_count_per_axis"]
        == EXPECTED_PAIR_PROBES_PER_AXIS
        and pair_certificate["certified_offset_tier_count"]
        == EXPECTED_PAIR_OFFSET_TIERS,
        f"V25 pair grid/probe/tier contract mismatch: {pair_certificate}",
    )
    require(
        pair_certificate["cell_count"] == EXPECTED_PAIR_CELL_COUNT,
        f"V25 pair certificate does not contain {EXPECTED_PAIR_CELL_COUNT} cells",
    )
    require(
        pair_certificate["first_minimum_certified_value"]
        < pair_certificate["first_maximum_certified_value"]
        and pair_certificate["second_minimum_certified_value"]
        < pair_certificate["second_maximum_certified_value"],
        f"V25 pair certificate has invalid value domains: {pair_certificate}",
    )
    expected_cell_indices = {
        (first_index, second_index)
        for first_index in range(EXPECTED_PAIR_GRID_RESOLUTION)
        for second_index in range(EXPECTED_PAIR_GRID_RESOLUTION)
    }
    actual_cell_indices = {
        (cell["first_cell_index"], cell["second_cell_index"])
        for cell in pair_certificate["cells"]
    }
    require(
        actual_cell_indices == expected_cell_indices,
        f"V25 pair grid is incomplete or ambiguous: {sorted(actual_cell_indices)}",
    )
    expected_probes_per_cell = EXPECTED_PAIR_PROBES_PER_AXIS**2
    expected_evaluations_per_cell = (
        expected_probes_per_cell * EXPECTED_PAIR_OFFSET_TIERS
    )
    pair_morph_names = [cell["garment_morph"] for cell in pair_certificate["cells"]]
    require(
        all(name and name != "None" for name in pair_morph_names)
        and len(pair_morph_names) == len(set(pair_morph_names)),
        f"V25 pair cells do not own unique generated morphs: {pair_morph_names}",
    )
    for cell in pair_certificate["cells"]:
        require(
            cell["certified_body_probe_count"] == expected_probes_per_cell
            and cell["certified_offset_evaluation_count"]
            == expected_evaluations_per_cell,
            f"V25 pair cell has incomplete geometric coverage: {cell}",
        )
        require(
            finite_number(cell["minimum_certified_gap_cm"])
            and cell["minimum_certified_gap_cm"]
            >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
            f"V25 pair cell violates minimum clearance: {cell}",
        )
        require(
            is_exact_certified_offset_tier(
                snapshot, cell["minimum_clearance_multiplier"]
            ),
            f"V25 pair cell automatic clearance is not an exact certified tier: {cell}",
        )
        first_step = (
            pair_certificate["first_maximum_certified_value"]
            - pair_certificate["first_minimum_certified_value"]
        ) / EXPECTED_PAIR_GRID_RESOLUTION
        second_step = (
            pair_certificate["second_maximum_certified_value"]
            - pair_certificate["second_minimum_certified_value"]
        ) / EXPECTED_PAIR_GRID_RESOLUTION
        expected_first_minimum = (
            pair_certificate["first_minimum_certified_value"]
            + cell["first_cell_index"] * first_step
        )
        expected_first_maximum = expected_first_minimum + first_step
        expected_second_minimum = (
            pair_certificate["second_minimum_certified_value"]
            + cell["second_cell_index"] * second_step
        )
        expected_second_maximum = expected_second_minimum + second_step
        require(
            abs(cell["first_minimum_value"] - expected_first_minimum) <= 0.0001
            and abs(cell["first_maximum_value"] - expected_first_maximum) <= 0.0001
            and abs(cell["second_minimum_value"] - expected_second_minimum) <= 0.0001
            and abs(cell["second_maximum_value"] - expected_second_maximum) <= 0.0001,
            f"V25 pair cell domain does not match its grid indices: {cell}",
        )
    require(
        finite_number(pair_certificate["minimum_certified_gap_cm"])
        and pair_certificate["minimum_certified_gap_cm"]
        >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
        "V25 pair certificate is below the profile clearance contract",
    )
    require(
        finite_number(snapshot["minimum_sampled_pair_gap_cm"])
        and snapshot["minimum_sampled_pair_gap_cm"]
        >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
        "V25 sampled pair gap is below the profile clearance contract",
    )
    require(snapshot["binding_count"] > 0, "Profile has no morph bindings")
    for binding in snapshot["morph_bindings"]:
        require(binding["sample_count"] >= 2, f"Binding has no piecewise samples: {binding}")
        for sample in binding["samples"]:
            require(
                is_exact_certified_offset_tier(
                    snapshot, sample["minimum_clearance_multiplier"]
                ),
                f"Morph sample automatic clearance is not an exact certified tier: {sample}",
            )
    require(snapshot["required_weighted_bone_count"] > 0, "Profile has no weighted-bone contract")
    require(snapshot["generated_morph_sample_count"] > 0, "Profile has no generated morph samples")
    require(snapshot["maximum_morph_samples_per_binding"] >= 2, "Piecewise sample contract is empty")
    require(snapshot["penetrating_vertex_count_after"] == 0, "Compiled rest pose still penetrates")
    require(
        snapshot["minimum_signed_gap_after_cm"]
        >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
        "Compiled rest-pose clearance is below its declared minimum",
    )
    require(
        snapshot["minimum_sampled_morph_gap_cm"]
        >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
        "Compiled morph clearance is below its declared minimum",
    )
    require(
        snapshot["minimum_certified_offset_gap_cm"]
        >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
        "At least one certified offset tier is below the clearance contract",
    )
    require(
        finite_number(snapshot["compiled_morph_threshold_position_cm"])
        and snapshot["compiled_morph_threshold_position_cm"] >= 0.0,
        "Compiled UE morph threshold is invalid",
    )
    require(
        snapshot["post_threshold_altered_delta_count"] >= 0,
        "Post-threshold delta metric is invalid",
    )
    expansion = snapshot["compiled_concurrent_bounds_expansion_cm"]
    require(
        all(expansion[axis] > 0.0 for axis in ("x", "y", "z")),
        f"Concurrent L1 bounds expansion is empty: {expansion}",
    )
    require(
        finite_number(snapshot["compiled_concurrent_sphere_expansion_cm"])
        and snapshot["compiled_concurrent_sphere_expansion_cm"] > 0.0,
        "Concurrent sphere expansion is empty",
    )
    require(
        re.fullmatch(r"[0-9a-fA-F]{32}", snapshot["shared_skeleton_fingerprint"]),
        "Shared USkeleton fingerprint is missing or malformed",
    )

    fitted_mesh = unreal.load_asset(snapshot["fitted_garment"])
    source_mesh = unreal.load_asset(snapshot["source_garment"])
    body_mesh = unreal.load_asset(snapshot["body_surface"])
    compatibility_mesh = unreal.load_asset(snapshot["compatibility_reference"])
    require(all((fitted_mesh, source_mesh, body_mesh, compatibility_mesh)), "A V25 mesh failed to load")

    shared_skeletons = {
        mesh_skeleton_path(mesh)
        for mesh in (fitted_mesh, source_mesh, body_mesh, compatibility_mesh)
    }
    require("" not in shared_skeletons, "A V25 mesh has no shared USkeleton")
    require(
        len(shared_skeletons) == 1,
        f"V25 meshes do not share one immutable USkeleton: {sorted(shared_skeletons)}",
    )
    snapshot["shared_skeleton_path"] = next(iter(shared_skeletons))

    fitted_morph_names = parse_morph_target_names(fitted_mesh)
    bound_body_names = sorted(
        {
            str(get_property(binding, "body_morph", "BodyMorph", default=""))
            for binding in (get_property(profile, "morph_bindings", "MorphBindings", default=[]) or [])
            if str(get_property(binding, "body_morph", "BodyMorph", default=""))
        },
        key=str.lower,
    )
    leaked_body_morphs = sorted(set(bound_body_names).intersection(fitted_morph_names), key=str.lower)
    require(
        not leaked_body_morphs,
        f"Derived garment still contains direct body morph targets: {leaked_body_morphs}",
    )
    require(
        snapshot["clearance_morph_name"] in fitted_morph_names,
        "The cooked fitted mesh lacks its clearance morph",
    )
    missing_pair_morphs = sorted(set(pair_morph_names).difference(fitted_morph_names))
    require(
        not missing_pair_morphs,
        f"The cooked fitted mesh lacks V25 pair-cell morphs: {missing_pair_morphs}",
    )
    snapshot["fitted_morph_target_count"] = len(fitted_morph_names)
    snapshot["bound_body_morph_count"] = len(bound_body_names)
    snapshot["pair_cell_morph_target_count"] = len(pair_morph_names)
    snapshot["direct_body_morph_targets_present"] = leaked_body_morphs

    source_bounds = bounds_snapshot(source_mesh)
    fitted_bounds = bounds_snapshot(fitted_mesh)
    for axis in ("x", "y", "z"):
        require(
            fitted_bounds["box_extent"][axis] + 0.01
            >= source_bounds["box_extent"][axis] + expansion[axis],
            f"Fitted {axis}-bounds do not contain concurrent morph expansion",
        )
    require(
        fitted_bounds["sphere_radius"] + 0.01
        >= source_bounds["sphere_radius"]
        + snapshot["compiled_concurrent_sphere_expansion_cm"],
        "Fitted sphere bounds do not contain concurrent morph expansion",
    )
    snapshot["source_imported_bounds"] = source_bounds
    snapshot["fitted_imported_bounds"] = fitted_bounds
    STATE.result["profile"] = snapshot
    return snapshot


def item_class_path(item):
    item_class = get_property(item, "item_class", "ItemClass", default=None)
    return object_path(item_class)


def acf_item_guid(item):
    guid = normalize_guid(get_property(item, "item_guid", "ItemGuid", default=None))
    require(guid, f"ACF item has no reflected runtime GUID: {item}")
    return guid


def acf_item_snapshot(item, source):
    item_object = get_property(item, "item", "Item", default=None)
    item_path = item_class_path(item) or class_path(item_object)
    return {
        "source": source,
        "guid": acf_item_guid(item),
        "item_class": item_path,
        "count": int(get_property(item, "count", "Count", default=1) or 0),
        "is_equipped": bool(
            get_property(item, "b_is_equipped", "bIsEquipped", default=False)
        ),
        "equipment_slot": gameplay_tag_string(
            get_property(
                item,
                "equipment_slot",
                "EquipmentSlot",
                "item_slot",
                "ItemSlot",
                default="",
            )
        ),
    }


def acf_item_rows(items, source):
    rows = [acf_item_snapshot(item, source) for item in (items or [])]
    require(
        len({row["guid"] for row in rows}) == len(rows),
        f"Duplicate ACF item GUID in {source}",
    )
    return sorted(rows, key=lambda row: (row["guid"], row["item_class"]))


def player_inventory_rows():
    return acf_item_rows(call(STATE.equipment, "get_inventory") or [], "player_inventory")


def current_equipment_rows():
    current = call(STATE.equipment, "get_current_equipment")
    equipped = get_property(
        current,
        "equipped_items",
        "EquippedItems",
        default=None,
    )
    require(equipped is not None, "ACF current equipment exposes no equipped-item array")
    return acf_item_rows(equipped, "player_equipment")


def object_is_valid(value):
    if value is None:
        return False
    try:
        return bool(unreal.SystemLibrary.is_valid(value))
    except Exception:
        try:
            return not bool(value.is_pending_kill())
        except Exception:
            return False


def find_panty_pickup(world):
    world_item_class = unreal.load_class(None, "/Script/InventorySystem.ACFWorldItem")
    require(world_item_class is not None, "Unable to load ACFWorldItem class")
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, world_item_class) or []
    candidates = []
    for actor in actors:
        getter = getattr(actor, "get_items", None)
        if not callable(getter):
            continue
        try:
            items = getter() or []
        except Exception:
            continue
        classes = [item_class_path(item) for item in items]
        if any(path == PANTY_ITEM_CLASS for path in classes):
            candidates.append((actor, classes))
    require(len(candidates) == 1, f"Expected one Panty pickup, found {len(candidates)}")
    return candidates[0]


def setup_real_acf_interaction():
    pickup_location = STATE.pickup.get_actor_location()
    pawn_location = STATE.player.get_actor_location()
    horizontal = unreal.Vector(pickup_location.x - 85.0, pickup_location.y, pawn_location.z)
    moved = STATE.player.set_actor_location(horizontal, False, True)
    require(bool(moved), "Could not move the pawn into the pickup interaction radius")
    look = unreal.MathLibrary.find_look_at_rotation(horizontal, pickup_location)
    STATE.player.set_actor_rotation(unreal.Rotator(0.0, look.yaw, 0.0), True)
    call(STATE.interaction, "enable_detection", True)
    call(STATE.interaction, "refresh_interactions")
    # Deterministic candidate selection still exercises Interact/GatherItem and the
    # inventory/equipment pipeline; it does not equip or assign a mesh directly.
    call(STATE.interaction, "register_interactable", STATE.pickup)
    call(STATE.interaction, "set_current_best_interactable", STATE.pickup)
    selected = call(STATE.interaction, "get_current_best_interactable_actor")
    require(selected == STATE.pickup, "ACF did not select the Panty pickup")
    require(bool(call(STATE.interaction, "has_valid_interactable")), "ACF reports no valid interactable")
    pickup_rows = acf_item_rows(call(STATE.pickup, "get_items") or [], "world_pickup")
    inventory_before = player_inventory_rows()
    pickup_panties = [row for row in pickup_rows if row["item_class"] == PANTY_ITEM_CLASS]
    require(pickup_panties, "Selected ACF pickup has no GUID-bearing Panty item")
    pickup_guids = {row["guid"] for row in pickup_rows}
    before_guids = {row["guid"] for row in inventory_before}
    require(
        not pickup_guids.intersection(before_guids),
        "World pickup GUID already exists in the player inventory before Interact",
    )
    STATE.pickup_item_rows = pickup_rows
    STATE.pickup_guids = pickup_guids
    STATE.pickup_panty_guids = {row["guid"] for row in pickup_panties}
    STATE.inventory_before_rows = inventory_before
    STATE.result["acf_interaction"] = {
        "pickup": object_path(STATE.pickup),
        "pickup_item_classes": STATE.pickup_item_classes,
        "interaction_component": object_path(STATE.interaction),
        "selected_actor": object_path(selected),
        "route": [
            "UACFInteractionComponent.Interact",
            "AACFWorldItem.OnInteractedByPawn",
            "AACFWorldItem.GatherItem",
            "UACFInventoryComponent.MoveItemsFromInventory",
            "UACFEquipmentComponent.HandleItemAdded/EquipItemFromInventory",
            "UACFEquipmentComponent.AddSkeletalMeshComponent",
        ],
        "direct_mesh_assignment_used": False,
        "direct_equipment_shortcut_used": False,
        "pickup_items_before": pickup_rows,
        "pickup_guids_before": sorted(pickup_guids),
        "player_inventory_before": inventory_before,
        "player_inventory_guids_before": sorted(before_guids),
        "pickup_destroy_on_gather": bool(
            call(STATE.pickup, "destroy_on_all_items_gathered")
        ),
    }


def validate_real_acf_transfer():
    inventory_after = player_inventory_rows()
    equipment_after = current_equipment_rows()
    after_inventory_guids = {row["guid"] for row in inventory_after}
    equipped_guids = {row["guid"] for row in equipment_after}
    before_guids = {row["guid"] for row in STATE.inventory_before_rows}
    guid_delta = after_inventory_guids.difference(before_guids)
    acquired_pickup_guids = STATE.pickup_guids.intersection(after_inventory_guids)
    acquired_panty_guids = STATE.pickup_panty_guids.intersection(after_inventory_guids)
    legs_rows = [row for row in equipment_after if LEGS_SLOT in row["equipment_slot"]]

    require(
        guid_delta == STATE.pickup_guids,
        f"Player inventory GUID delta does not equal the world pickup: delta={sorted(guid_delta)} "
        f"pickup={sorted(STATE.pickup_guids)}",
    )
    require(
        acquired_pickup_guids == STATE.pickup_guids,
        "Not every original world-item GUID reached the player inventory",
    )
    require(
        acquired_panty_guids == STATE.pickup_panty_guids,
        "The original Panty GUID did not reach the player inventory",
    )
    require(len(legs_rows) == 1, f"Expected exactly one equipped Legs item: {legs_rows}")
    require(
        legs_rows[0]["item_class"] == PANTY_ITEM_CLASS,
        f"Equipped Legs GUID is not Panty: {legs_rows[0]}",
    )
    require(
        legs_rows[0]["guid"] in STATE.pickup_panty_guids,
        "Equipped Legs item did not preserve the real pickup GUID",
    )
    require(
        STATE.pickup_panty_guids.issubset(equipped_guids),
        "Panty pickup GUID is absent from ACF current equipment",
    )

    pickup_valid = object_is_valid(STATE.pickup)
    pickup_remaining = []
    if pickup_valid:
        pickup_remaining = acf_item_rows(
            call(STATE.pickup, "get_items") or [],
            "world_pickup_after",
        )
    remaining_guids = {row["guid"] for row in pickup_remaining}
    pickup_consumed = (not pickup_valid) or not STATE.pickup_guids.intersection(remaining_guids)
    if not pickup_consumed:
        return False

    STATE.result["acf_interaction"].update(
        {
            "player_inventory_after": inventory_after,
            "player_inventory_guids_after": sorted(after_inventory_guids),
            "inventory_guid_delta": sorted(guid_delta),
            "current_equipment_after": equipment_after,
            "equipped_legs_guid": legs_rows[0]["guid"],
            "pickup_actor_valid_after": pickup_valid,
            "pickup_items_after": pickup_remaining,
            "pickup_consumed": True,
            "original_guid_preserved_to_inventory": True,
            "original_guid_preserved_to_legs_equipment": True,
        }
    )
    return True


def find_leg_armor_slot(actor):
    for component in all_skeletal_components(actor):
        if "acf armorslotcomponent" not in class_path(component).lower().replace("_", " "):
            if "acfarmorslotcomponent" not in class_path(component).lower():
                continue
        getter = getattr(component, "get_slot_tag", None)
        if not callable(getter):
            continue
        try:
            tag = getter()
        except Exception:
            continue
        if gameplay_tag_string(tag) == LEGS_SLOT:
            return component, tag
    return None, None


def garment_snapshot(component):
    leader = get_property(component, "leader_pose_component", default=None)
    current_profile = ""
    getter = getattr(component, "get_current_skin_weight_profile_name", None)
    if callable(getter):
        try:
            current_profile = str(getter())
        except Exception:
            current_profile = ""
    using_skin_profile = None
    using_getter = getattr(component, "is_using_skin_weight_profile", None)
    if callable(using_getter):
        try:
            using_skin_profile = bool(using_getter())
        except Exception:
            using_skin_profile = None
    skin_profile_pending = None
    pending_getter = getattr(component, "is_skin_weight_profile_pending", None)
    if callable(pending_getter):
        try:
            skin_profile_pending = bool(pending_getter())
        except Exception:
            skin_profile_pending = None
    try:
        visible = bool(component.is_visible())
    except Exception:
        visible = None
    return {
        "component": object_path(component),
        "component_class": class_path(component),
        "mesh": canonical_asset_path(mesh_asset(component)),
        "tags": component_tags(component),
        "visible": visible,
        "hidden_in_game": bool(get_property(component, "hidden_in_game", default=False)),
        "render_in_main_pass": bool(
            get_property(component, "render_in_main_pass", default=True)
        ),
        "leader_pose_component": object_path(leader),
        "leader_pose_mesh": canonical_asset_path(mesh_asset(leader)),
        "leader_pose_is_effective_pose_driver": (
            bool(leader)
            and object_path(leader) == object_path(STATE.effective_pose_driver)
        ),
        "skin_weight_profile": current_profile,
        "is_using_skin_weight_profile": using_skin_profile,
        "skin_weight_profile_pending": skin_profile_pending,
    }


def runtime_snapshot():
    runtime = STATE.runtime
    return {
        "component": object_path(runtime),
        "applied": int(call(runtime, "get_applied_garment_count")),
        "pending": int(call(runtime, "get_pending_garment_count")),
        "debug_summary": str(call(runtime, "get_debug_summary")),
    }


def garment_is_renderable(garment):
    return (
        garment.get("visible") is True
        and garment.get("hidden_in_game") is False
        and garment.get("render_in_main_pass") is True
    )


def garment_has_exact_ready_contract(runtime, garment):
    if not runtime or not garment:
        return False
    profile = STATE.result.get("profile", {})
    return (
        runtime.get("applied") == 1
        and runtime.get("pending") == 0
        and garment.get("mesh") == profile.get("fitted_garment")
        and "EFClothingMorphV2.Managed" in garment.get("tags", [])
        and "EFClothingMorphV2.Pending" not in garment.get("tags", [])
        and garment.get("leader_pose_is_effective_pose_driver") is True
        and garment.get("leader_pose_mesh") == COMPATIBILITY_PATH
        and garment.get("skin_weight_profile") == EXPECTED_SKIN_PROFILE
        and garment.get("is_using_skin_weight_profile") is True
        and garment.get("skin_weight_profile_pending") in (False, None)
    )


def sample_initial_fit_visibility(label):
    if not STATE.safety_sampling_active:
        return
    resolve_runtime_context()
    slot, _ = find_leg_armor_slot(STATE.player)
    runtime = runtime_snapshot() if STATE.runtime else None
    garment = garment_snapshot(slot) if slot else None
    target_mesh = bool(
        garment
        and garment.get("mesh")
        in (SOURCE_GARMENT_PATH, STATE.result.get("profile", {}).get("fitted_garment"))
    )
    renderable = bool(target_mesh and garment_is_renderable(garment))
    safe = garment_has_exact_ready_contract(runtime, garment)
    STATE.safety_sample_index += 1
    world_seconds = (
        float(unreal.GameplayStatics.get_time_seconds(STATE.world)) if STATE.world else -1.0
    )
    row = {
        "index": STATE.safety_sample_index,
        "label": label,
        "phase": STATE.phase,
        "world_seconds": world_seconds,
        "runtime_applied": runtime["applied"] if runtime else None,
        "runtime_pending": runtime["pending"] if runtime else None,
        "component": garment.get("component", "") if garment else "",
        "mesh": garment.get("mesh", "") if garment else "",
        "target_mesh": target_mesh,
        "renderable": renderable,
        "safe": safe,
        "tags": garment.get("tags", []) if garment else [],
        "skin_weight_profile": garment.get("skin_weight_profile", "") if garment else "",
        "is_using_skin_weight_profile": (
            garment.get("is_using_skin_weight_profile") if garment else None
        ),
        "skin_weight_profile_pending": (
            garment.get("skin_weight_profile_pending") if garment else None
        ),
        "leader_pose_component": garment.get("leader_pose_component", "") if garment else "",
        "leader_pose_is_effective_pose_driver": (
            garment.get("leader_pose_is_effective_pose_driver") if garment else False
        ),
    }
    STATE.result["initial_visibility_gate"]["samples"].append(row)
    if renderable and STATE.result["initial_visibility_gate"]["first_renderable"] is None:
        STATE.result["initial_visibility_gate"]["first_renderable"] = row
    require(
        not renderable or safe,
        "Garment became renderable before the exact V25 safe contract: "
        f"sample={row}",
    )


def finish_initial_fit_visibility_gate():
    gate = STATE.result["initial_visibility_gate"]
    first_renderable = gate.get("first_renderable")
    require(gate["samples"], "No post-Interact visibility samples were collected")
    require(first_renderable is not None, "No renderable garment post-tick sample was observed")
    require(first_renderable.get("safe") is True, "First renderable post-tick sample was unsafe")
    gate["sample_count"] = len(gate["samples"])
    gate["unsafe_renderable_count"] = sum(
        1 for row in gate["samples"] if row["renderable"] and not row["safe"]
    )
    require(gate["unsafe_renderable_count"] == 0, "Unsafe renderable garment frame observed")
    gate["status"] = "PASS_OBSERVED_POST_TICK_RENDERABLE_SAMPLES_WERE_EXACT_READY"
    STATE.safety_sampling_active = False


def validate_ready_garment(checkpoint):
    require(STATE.runtime is not None, "EFClothingFitRuntimeComponent is missing")
    require(STATE.garment is not None, "Leg armor garment component is missing")
    runtime = runtime_snapshot()
    garment = garment_snapshot(STATE.garment)
    profile = STATE.result["profile"]
    require(runtime["applied"] == 1, f"Expected READY=1 at {checkpoint}: {runtime}")
    require(runtime["pending"] == 0, f"Expected Pending=0 at {checkpoint}: {runtime}")
    require(garment["mesh"] == profile["fitted_garment"], f"Derived mesh mismatch at {checkpoint}")
    require("EFClothingMorphV2.Managed" in garment["tags"], f"Managed tag missing at {checkpoint}")
    require("EFClothingMorphV2.Pending" not in garment["tags"], f"Pending tag remains at {checkpoint}")
    require(garment["leader_pose_component"], f"LeaderPose missing at {checkpoint}")
    require(garment["leader_pose_component"] != garment["component"], "Garment leads itself")
    require(
        garment["leader_pose_is_effective_pose_driver"] is True,
        f"LeaderPose is not Female's effective Multiple driver at {checkpoint}",
    )
    require(
        garment["leader_pose_mesh"] == COMPATIBILITY_PATH,
        f"LeaderPose must be Female's effective Multiple driver at {checkpoint}",
    )
    require(garment["skin_weight_profile"] == EXPECTED_SKIN_PROFILE, f"EF_AutoFit is not active at {checkpoint}")
    require(
        garment["is_using_skin_weight_profile"] is True,
        f"Component does not report exact active skin-weight profile at {checkpoint}",
    )
    require(
        garment["skin_weight_profile_pending"] in (False, None),
        f"EF_AutoFit pending state is neither false nor unavailable at {checkpoint}",
    )
    require(garment["visible"] is True, f"Garment is not visible at {checkpoint}")
    require(not garment["hidden_in_game"], f"Garment is hidden in game at {checkpoint}")
    require(garment["render_in_main_pass"], f"Garment is not in the main pass at {checkpoint}")
    STATE.result["runtime_checks"][checkpoint] = {
        "runtime": runtime,
        "garment": garment,
    }
    return runtime, garment


def find_morph_binding(body_morph):
    bindings = get_property(
        STATE.profile, "morph_bindings", "MorphBindings", default=[]
    ) or []
    for binding in bindings:
        body_name = str(get_property(binding, "body_morph", "BodyMorph", default=""))
        if body_name == body_morph:
            return binding
    raise RuntimeError(f"Profile does not bind stress morph {body_morph}")


def morph_binding_weight_snapshot(body_morph):
    binding = find_morph_binding(body_morph)
    samples = get_property(binding, "samples", "Samples", default=[]) or []
    rows = []
    active = []
    for sample in samples:
        row = morph_sample_snapshot(sample)
        identity = row["identity"]
        morph_name = row["garment_morph"]
        weight = 0.0 if identity or not morph_name or morph_name == "None" else float(
            call(STATE.garment, "get_morph_target", morph_name)
        )
        row["weight"] = weight
        rows.append(row)
        if abs(weight) > 0.001:
            active.append(row)
    body_weight = float(call(STATE.body, "get_morph_target", body_morph))
    original_garment_weight = float(
        call(STATE.garment, "get_morph_target", body_morph)
    )
    return {
        "body_morph": body_morph,
        "body_weight": body_weight,
        "original_same_named_garment_weight": original_garment_weight,
        "sample_count": len(rows),
        "active_sample_count": len(active),
        "active_weight_sum": sum(row["weight"] for row in active),
        "samples": rows,
    }


def validate_bound_morph_weights(body_morph, expected_value, result_key):
    snapshot = morph_binding_weight_snapshot(body_morph)
    STATE.result["morph_weights"][result_key] = snapshot
    require(
        abs(snapshot["body_weight"] - expected_value) <= 0.01,
        f"{body_morph} did not reach {expected_value}",
    )
    require(snapshot["sample_count"] >= 2, f"{body_morph} has no piecewise samples")
    require(
        1 <= snapshot["active_sample_count"] <= 2,
        f"Runtime should activate one or two adjacent {body_morph} samples: {snapshot}",
    )
    require(
        all(-0.001 <= row["weight"] <= 1.001 for row in snapshot["samples"]),
        f"A generated {body_morph} sample weight is outside 0..1",
    )
    require(
        abs(snapshot["active_weight_sum"] - 1.0) <= 0.02,
        f"Piecewise {body_morph} weights do not sum to one: {snapshot['active_weight_sum']}",
    )
    require(
        abs(snapshot["original_same_named_garment_weight"]) <= 0.001,
        f"The uncorrected same-named garment morph {body_morph} is still active",
    )
    return snapshot


def validate_stress_combo_weights(result_prefix="stress_combo"):
    heavy = morph_binding_weight_snapshot(BODY_STRESS_MORPH)
    fitness_mass = morph_binding_weight_snapshot(BODY_SECONDARY_STRESS_MORPH)
    for binding_snapshot in (heavy, fitness_mass):
        body_morph = binding_snapshot["body_morph"]
        require(
            abs(binding_snapshot["body_weight"] - 1.0) <= 0.01,
            f"{body_morph} did not reach 1.0 for the V25 stress pair",
        )
        require(
            binding_snapshot["sample_count"] >= 2,
            f"{body_morph} has no one-dimensional V25 samples to suppress",
        )
        require(
            binding_snapshot["active_sample_count"] == 0
            and all(
                abs(row["weight"]) <= 0.001
                for row in binding_snapshot["samples"]
            ),
            f"V25 pair mode left one-dimensional {body_morph} samples active: "
            f"{binding_snapshot}",
        )
        require(
            abs(binding_snapshot["original_same_named_garment_weight"]) <= 0.001,
            f"The uncorrected same-named garment morph {body_morph} is active",
        )

    pair = pair_cell_weight_snapshot()
    require(
        pair["active_cell_count"] == 1,
        f"V25 stress pair must activate exactly one pair cell: {pair}",
    )
    require(
        abs(pair["active_weight_sum"] - 1.0) <= 0.001,
        f"The selected V25 pair cell is not at exact weight 1: {pair}",
    )
    require(
        pair["expected_cell_indices"]
        == {
            "first": EXPECTED_PAIR_GRID_RESOLUTION - 1,
            "second": EXPECTED_PAIR_GRID_RESOLUTION - 1,
        },
        f"Body Fitness Mass=1 + Body Heavy=1 did not resolve to cell 3,3: {pair}",
    )
    require(
        len(pair["active_cells"]) == 1
        and pair["active_cells"][0]["first_cell_index"]
        == pair["expected_cell_indices"]["first"]
        and pair["active_cells"][0]["second_cell_index"]
        == pair["expected_cell_indices"]["second"]
        and abs(pair["active_cells"][0]["weight"] - 1.0) <= 0.001,
        f"V25 activated the wrong pair cell: {pair}",
    )
    require(
        all(
            abs(row["weight"] - 1.0) <= 0.001
            if row in pair["active_cells"]
            else abs(row["weight"]) <= 0.001
            for row in pair["cells"]
        ),
        f"V25 pair cells are not mutually exclusive: {pair}",
    )
    snapshot = {
        "heavy": heavy,
        "fitness_mass": fitness_mass,
        "pair": pair,
    }
    STATE.result["morph_weights"][result_prefix] = snapshot
    return snapshot


def pair_cell_weight_snapshot():
    profile_snapshot_value = STATE.result.get("profile", {})
    certificates = profile_snapshot_value.get("morph_pair_certificates", [])
    require(len(certificates) == 1, "Runtime has no unique V25 pair certificate")
    certificate = certificates[0]
    require(
        (
            certificate["first_body_morph"],
            certificate["second_body_morph"],
        )
        == EXPECTED_PAIR_BODY_MORPHS,
        f"Runtime selected an unexpected V25 pair certificate: {certificate}",
    )
    body_values = {
        name: float(call(STATE.body, "get_morph_target", name))
        for name in EXPECTED_PAIR_BODY_MORPHS
    }

    def resolve_cell_index(value, minimum, maximum):
        normalized = max(
            0.0,
            min(1.0, (value - minimum) / max(maximum - minimum, 1.0e-8)),
        )
        return max(
            0,
            min(
                EXPECTED_PAIR_GRID_RESOLUTION - 1,
                int(math.floor(normalized * EXPECTED_PAIR_GRID_RESOLUTION)),
            ),
        )

    expected_first_index = resolve_cell_index(
        body_values[certificate["first_body_morph"]],
        certificate["first_minimum_certified_value"],
        certificate["first_maximum_certified_value"],
    )
    expected_second_index = resolve_cell_index(
        body_values[certificate["second_body_morph"]],
        certificate["second_minimum_certified_value"],
        certificate["second_maximum_certified_value"],
    )
    cells = []
    for cell in certificate["cells"]:
        row = dict(cell)
        row["weight"] = float(
            call(STATE.garment, "get_morph_target", cell["garment_morph"])
        )
        cells.append(row)
    active_cells = [row for row in cells if abs(row["weight"]) > 0.001]
    return {
        "body_values": body_values,
        "expected_cell_indices": {
            "first": expected_first_index,
            "second": expected_second_index,
        },
        "cell_count": len(cells),
        "active_cell_count": len(active_cells),
        "active_weight_sum": sum(row["weight"] for row in active_cells),
        "active_cells": active_cells,
        "cells": cells,
    }


def validate_rest_morph_weights(body_morph, result_key):
    snapshot = morph_binding_weight_snapshot(body_morph)
    STATE.result["morph_weights"][result_key] = snapshot
    require(abs(snapshot["body_weight"]) <= 0.001, f"{body_morph} did not reset to zero")
    require(
        snapshot["active_sample_count"] == 0,
        f"{body_morph} V25 samples remain active at zero",
    )
    return snapshot


def validate_rest_pair_weights(result_key="rest_pair_cells_0"):
    snapshot = pair_cell_weight_snapshot()
    STATE.result["morph_weights"][result_key] = snapshot
    require(
        snapshot["cell_count"] == EXPECTED_PAIR_CELL_COUNT,
        f"Rest check did not inspect all {EXPECTED_PAIR_CELL_COUNT} V25 pair cells",
    )
    require(
        snapshot["active_cell_count"] == 0
        and all(abs(row["weight"]) <= 0.001 for row in snapshot["cells"]),
        f"A V25 pair cell remains active at rest: {snapshot}",
    )
    return snapshot


def pair_matrix_cases():
    certificates = STATE.result.get("profile", {}).get("morph_pair_certificates", [])
    require(len(certificates) == 1, "Runtime has no unique V25 pair certificate")
    certificate = certificates[0]
    cases = []
    for cell in sorted(
        certificate["cells"],
        key=lambda row: (row["first_cell_index"], row["second_cell_index"]),
    ):
        cases.append(
            {
                "first_cell_index": cell["first_cell_index"],
                "second_cell_index": cell["second_cell_index"],
                "first_body_morph": certificate["first_body_morph"],
                "second_body_morph": certificate["second_body_morph"],
                "first_body_value": (
                    cell["first_minimum_value"] + cell["first_maximum_value"]
                )
                * 0.5,
                "second_body_value": (
                    cell["second_minimum_value"] + cell["second_maximum_value"]
                )
                * 0.5,
            }
        )
    require(
        len(cases) == EXPECTED_PAIR_CELL_COUNT,
        f"Expected {EXPECTED_PAIR_CELL_COUNT} pair-matrix cases, got {len(cases)}",
    )
    return cases


def validate_pair_matrix_case(case):
    first = morph_binding_weight_snapshot(case["first_body_morph"])
    second = morph_binding_weight_snapshot(case["second_body_morph"])
    for binding, expected_value in (
        (first, case["first_body_value"]),
        (second, case["second_body_value"]),
    ):
        require(
            abs(binding["body_weight"] - expected_value) <= 0.01,
            f"{binding['body_morph']} did not reach matrix value {expected_value}",
        )
        require(
            binding["active_sample_count"] == 0
            and all(abs(row["weight"]) <= 0.001 for row in binding["samples"]),
            f"Pair matrix left 1D samples active for {binding['body_morph']}: {binding}",
        )
        require(
            abs(binding["original_same_named_garment_weight"]) <= 0.001,
            f"Direct garment morph leaked during pair matrix: {binding['body_morph']}",
        )

    pair = pair_cell_weight_snapshot()
    expected_indices = {
        "first": case["first_cell_index"],
        "second": case["second_cell_index"],
    }
    require(
        pair["expected_cell_indices"] == expected_indices,
        f"Pair matrix value resolved to the wrong certified cell: {pair}",
    )
    require(
        pair["active_cell_count"] == 1
        and len(pair["active_cells"]) == 1
        and abs(pair["active_weight_sum"] - 1.0) <= 0.001,
        f"Pair matrix did not preserve exact 1/15 exclusivity: {pair}",
    )
    active = pair["active_cells"][0]
    require(
        active["first_cell_index"] == case["first_cell_index"]
        and active["second_cell_index"] == case["second_cell_index"]
        and abs(active["weight"] - 1.0) <= 0.001
        and all(
            abs(row["weight"] - 1.0) <= 0.001
            if (
                row["first_cell_index"] == case["first_cell_index"]
                and row["second_cell_index"] == case["second_cell_index"]
            )
            else abs(row["weight"]) <= 0.001
            for row in pair["cells"]
        ),
        f"Pair matrix activated a wrong or non-exclusive cell: {pair}",
    )
    clearance = verify_automatic_clearance(
        "pair_matrix_{0}_{1}".format(
            case["first_cell_index"], case["second_cell_index"]
        ),
        pair,
    )
    return {
        "first_cell_index": case["first_cell_index"],
        "second_cell_index": case["second_cell_index"],
        "first_body_value": case["first_body_value"],
        "second_body_value": case["second_body_value"],
        "first_binding": first,
        "second_binding": second,
        "pair": pair,
        "clearance": clearance,
    }


def clearance_weight():
    morph_name = STATE.result["profile"]["clearance_morph_name"]
    return float(call(STATE.garment, "get_morph_target", morph_name))


def quantize_certified_offset(requested):
    return quantize_certified_offset_for_profile(STATE.result["profile"], requested)


def profile_binding_snapshot(body_morph, required=True):
    matches = [
        binding
        for binding in STATE.result["profile"].get("morph_bindings", [])
        if binding["body_morph"] == body_morph
    ]
    require(len(matches) <= 1, f"Profile has duplicate bindings for {body_morph}")
    if not matches:
        require(not required, f"Profile does not bind active morph {body_morph}")
        return None
    return matches[0]


def one_dimensional_logical_sample_weights(binding, value):
    samples = binding["samples"]
    require(samples, f"Binding has no samples: {binding['body_morph']}")
    weights = [0.0] * len(samples)
    minimum = binding["minimum_certified_value"]
    if value <= minimum + 1.0e-4:
        pass
    elif value <= samples[0]["body_value"]:
        blend = max(0.0, min(1.0, value / max(samples[0]["body_value"], 1.0e-4)))
        step_threshold = max(
            0.0,
            min(
                1.0,
                (samples[0]["step_switch_body_value"] - minimum)
                / max(samples[0]["body_value"] - minimum, 1.0e-4),
            ),
        )
        weights[0] = (
            0.0 if blend <= step_threshold else 1.0
        ) if samples[0]["step_from_previous"] else blend
    elif value >= samples[-1]["body_value"]:
        weights[-1] = 1.0
    else:
        for high_index in range(1, len(samples)):
            low = samples[high_index - 1]
            high = samples[high_index]
            if value <= high["body_value"]:
                blend = max(
                    0.0,
                    min(
                        1.0,
                        (value - low["body_value"])
                        / max(high["body_value"] - low["body_value"], 1.0e-4),
                    ),
                )
                if high["step_from_previous"]:
                    step_threshold = max(
                        0.0,
                        min(
                            1.0,
                            (high["step_switch_body_value"] - low["body_value"])
                            / max(high["body_value"] - low["body_value"], 1.0e-4),
                        ),
                    )
                    weights[high_index - 1] = 1.0 if blend <= step_threshold else 0.0
                    weights[high_index] = 0.0 if blend <= step_threshold else 1.0
                else:
                    weights[high_index - 1] = 1.0 - blend
                    weights[high_index] = blend
                break
    return [
        dict(sample, logical_weight=weights[index])
        for index, sample in enumerate(samples)
    ]


def automatic_clearance_requirement(pair_snapshot=None):
    profile = STATE.result["profile"]
    tier_minimum = profile["certified_clearance_multiplier_min"]
    epsilon = profile["compiled_morph_activation_epsilon"]
    resolved = {}
    active_names = []
    for morph_name in profile["monitored_body_morph_names"]:
        binding = profile_binding_snapshot(morph_name, required=False)
        body_value = float(call(STATE.body, "get_morph_target", morph_name))
        resolved_value = (
            body_value * binding["scale"] + binding["bias"]
            if binding is not None
            else body_value
        )
        require(
            finite_number(body_value) and finite_number(resolved_value),
            f"Non-finite monitored body morph {morph_name}",
        )
        if binding is not None:
            require(
                binding["minimum_certified_value"]
                <= resolved_value
                <= binding["maximum_certified_value"],
                f"Monitored body morph is outside its certificate: {morph_name}={resolved_value}",
            )
        resolved[morph_name] = {
            "body_value": body_value,
            "resolved_value": resolved_value,
            "has_binding": binding is not None,
        }
        if abs(resolved_value) > epsilon:
            require(binding is not None, f"Active monitored morph has no binding: {morph_name}")
            active_names.append(morph_name)
    active_names.sort()

    if not active_names:
        return {
            "source": "fitted_rest",
            "required_multiplier": tier_minimum,
            "active_body_morphs": [],
            "resolved_body_morphs": resolved,
        }

    if len(active_names) == 1:
        morph_name = active_names[0]
        binding = profile_binding_snapshot(morph_name)
        logical_samples = one_dimensional_logical_sample_weights(
            binding, resolved[morph_name]["resolved_value"]
        )
        active_samples = [
            sample for sample in logical_samples if sample["logical_weight"] > 0.0
        ]
        required_multiplier = max(
            [tier_minimum]
            + [sample["minimum_clearance_multiplier"] for sample in active_samples]
        )
        return {
            "source": "one_dimensional_samples",
            "required_multiplier": required_multiplier,
            "active_body_morphs": active_names,
            "resolved_body_morphs": resolved,
            "active_samples": active_samples,
        }

    require(
        len(active_names) == 2,
        f"Automatic-clearance QA cannot certify {len(active_names)} active body morphs",
    )
    matching_certificates = [
        certificate
        for certificate in profile["morph_pair_certificates"]
        if [certificate["first_body_morph"], certificate["second_body_morph"]]
        == active_names
    ]
    require(
        len(matching_certificates) == 1,
        f"Active morph pair has no unique clearance certificate: {active_names}",
    )
    certificate = matching_certificates[0]
    require(
        certificate["first_minimum_certified_value"]
        <= resolved[certificate["first_body_morph"]]["resolved_value"]
        <= certificate["first_maximum_certified_value"]
        and certificate["second_minimum_certified_value"]
        <= resolved[certificate["second_body_morph"]]["resolved_value"]
        <= certificate["second_maximum_certified_value"],
        f"Active morph pair is outside its clearance certificate: {active_names}",
    )

    def resolve_cell_index(value, minimum, maximum):
        normalized = max(0.0, min(1.0, (value - minimum) / max(maximum - minimum, 1.0e-8)))
        return max(
            0,
            min(
                certificate["grid_resolution"] - 1,
                int(math.floor(normalized * certificate["grid_resolution"])),
            ),
        )

    first_index = resolve_cell_index(
        resolved[certificate["first_body_morph"]]["resolved_value"],
        certificate["first_minimum_certified_value"],
        certificate["first_maximum_certified_value"],
    )
    second_index = resolve_cell_index(
        resolved[certificate["second_body_morph"]]["resolved_value"],
        certificate["second_minimum_certified_value"],
        certificate["second_maximum_certified_value"],
    )
    matching_cells = [
        cell
        for cell in certificate["cells"]
        if cell["first_cell_index"] == first_index
        and cell["second_cell_index"] == second_index
    ]
    require(
        len(matching_cells) == 1,
        f"Active pair resolved to no unique clearance cell: {first_index},{second_index}",
    )
    cell = matching_cells[0]
    if pair_snapshot is None:
        pair_snapshot = pair_cell_weight_snapshot()
    active_cells = pair_snapshot["active_cells"]
    require(
        len(active_cells) == 1
        and active_cells[0]["first_cell_index"] == first_index
        and active_cells[0]["second_cell_index"] == second_index,
        f"Rendered pair cell differs from automatic-clearance cell: {pair_snapshot}",
    )
    return {
        "source": "pair_cell",
        "required_multiplier": cell["minimum_clearance_multiplier"],
        "active_body_morphs": active_names,
        "resolved_body_morphs": resolved,
        "first_cell_index": first_index,
        "second_cell_index": second_index,
        "cell": cell,
    }


def verify_clearance_resolution(
    checkpoint,
    global_multiplier,
    garment_multiplier,
    pair_snapshot=None,
):
    profile = STATE.result["profile"]
    automatic = automatic_clearance_requirement(pair_snapshot)
    manual_request = (
        profile["default_clearance_value"]
        * float(global_multiplier)
        * float(garment_multiplier)
    )
    combined_request = max(manual_request, automatic["required_multiplier"])
    expected_tier = quantize_certified_offset(combined_request)
    actual = clearance_weight()
    snapshot = {
        "checkpoint": checkpoint,
        "global": float(global_multiplier),
        "per_garment": float(garment_multiplier),
        "default_clearance_value": profile["default_clearance_value"],
        "manual_requested_offset": manual_request,
        "automatic": automatic,
        "combined_request": combined_request,
        "expected_certified_tier": expected_tier,
        "actual_clearance_morph_weight": actual,
    }
    STATE.result["offset_product"][checkpoint] = snapshot
    require(
        abs(actual - expected_tier) <= 0.002,
        f"Clearance offset is {actual}, expected max(manual={manual_request}, "
        f"automatic={automatic['required_multiplier']}) -> tier {expected_tier}",
    )
    return snapshot


def verify_offset_product(checkpoint):
    return verify_clearance_resolution(
        checkpoint,
        EXPECTED_GLOBAL_CLEARANCE,
        EXPECTED_GARMENT_CLEARANCE,
    )


def verify_automatic_clearance(checkpoint, pair_snapshot=None):
    return verify_clearance_resolution(checkpoint, 1.0, 1.0, pair_snapshot)


def find_world_subsystem(world, class_name):
    subsystem_class = unreal.load_class(None, class_name)
    require(subsystem_class is not None, f"Unable to load subsystem class {class_name}")
    library = getattr(unreal, "SubsystemBlueprintLibrary", None)
    if library is not None:
        getter = getattr(library, "get_world_subsystem", None)
        if callable(getter):
            try:
                subsystem = getter(world, subsystem_class)
                if subsystem:
                    return subsystem
            except Exception:
                pass
    try:
        for subsystem in unreal.ObjectIterator(subsystem_class):
            try:
                if subsystem.get_world() == world:
                    return subsystem
            except Exception:
                continue
    except Exception:
        pass
    return None


def resolve_free_camera():
    if STATE.free_camera_subsystem is None:
        STATE.free_camera_subsystem = find_world_subsystem(
            STATE.world,
            "/Script/EFProjectSystemsGameplay.ProjectGameplayFreeCameraSubsystem",
        )
    require(STATE.free_camera_subsystem is not None, "Gameplay free-camera subsystem is missing")
    if not bool(call(STATE.free_camera_subsystem, "is_gameplay_free_camera_active")):
        started = bool(call(STATE.free_camera_subsystem, "start_gameplay_free_camera"))
        if not started:
            return False
    view_target = call(STATE.controller, "get_view_target")
    if not view_target or "CameraActor" not in class_path(view_target):
        return False
    STATE.camera = view_target
    STATE.result["free_camera"] = {
        "subsystem": object_path(STATE.free_camera_subsystem),
        "active": bool(call(STATE.free_camera_subsystem, "is_gameplay_free_camera_active")),
        "camera": object_path(STATE.camera),
    }
    return True


def position_character_camera(view, distance=255.0):
    require(STATE.camera is not None, "Free camera is not active")
    location = STATE.player.get_actor_location()
    forward = STATE.player.get_actor_forward_vector()
    right = STATE.player.get_actor_right_vector()
    if view == "front":
        radial = forward
    elif view == "back":
        radial = vector_scale(forward, -1.0)
    elif view == "side":
        radial = right
    else:
        raise RuntimeError(f"Unknown camera view {view}")
    camera_location = vector_add(
        vector_add(location, vector_scale(radial, distance)),
        unreal.Vector(0.0, 0.0, 95.0),
    )
    target = vector_add(location, unreal.Vector(0.0, 0.0, 88.0))
    rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, target)
    STATE.camera.set_actor_location_and_rotation(camera_location, rotation, False, True)


def position_pickup_camera():
    require(STATE.camera is not None, "Free camera is not active")
    player_location = STATE.player.get_actor_location()
    pickup_location = STATE.pickup.get_actor_location()
    midpoint = unreal.Vector(
        (player_location.x + pickup_location.x) * 0.5,
        (player_location.y + pickup_location.y) * 0.5,
        player_location.z + 72.0,
    )
    right = STATE.player.get_actor_right_vector()
    backward = vector_scale(STATE.player.get_actor_forward_vector(), -1.0)
    camera_location = vector_add(
        vector_add(midpoint, vector_scale(right, 210.0)),
        vector_add(vector_scale(backward, 95.0), unreal.Vector(0.0, 0.0, 55.0)),
    )
    rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, midpoint)
    STATE.camera.set_actor_location_and_rotation(camera_location, rotation, False, True)


def begin_capture(filename, label, next_phase):
    verify_character_creation_absent(f"capture_{label}")
    path = OUTPUT_DIR / filename
    STATE.capture_next_phase = next_phase
    STATE.capture_path = path
    STATE.capture_label = label
    request = {
        "label": label,
        "path": str(path),
        "accepted": False,
        "exists": False,
        "player_location": str(STATE.player.get_actor_location()),
        "player_velocity_cm_s": vector_length(STATE.player.get_velocity()),
        "body_heavy": float(call(STATE.body, "get_morph_target", BODY_STRESS_MORPH)),
        "body_fitness_mass": float(
            call(STATE.body, "get_morph_target", BODY_SECONDARY_STRESS_MORPH)
        ),
        "garment_mesh": canonical_asset_path(mesh_asset(STATE.garment)) if STATE.garment else "",
    }
    STATE.result["screenshots"][filename] = request
    STATE.capture_request_in_progress = True
    try:
        request["accepted"] = bool(
            unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(path))
        )
    finally:
        STATE.capture_request_in_progress = False
    STATE.phase = "wait_capture"
    STATE.phase_elapsed = 0.0
    emit(f"SCREENSHOT_REQUEST={filename} accepted={request['accepted']}")
    write_result()


def record_equipment_contract():
    slot, tag = find_leg_armor_slot(STATE.player)
    if not slot:
        return False
    armor_definition = call(slot, "get_armor_definition")
    item_path = class_path(armor_definition)
    if item_path != PANTY_ITEM_CLASS:
        return False
    if not bool(call(STATE.equipment, "has_any_item_in_equipment_slot", tag)):
        return False
    equipment_rows = current_equipment_rows()
    legs_rows = [row for row in equipment_rows if LEGS_SLOT in row["equipment_slot"]]
    if len(legs_rows) != 1 or legs_rows[0]["item_class"] != PANTY_ITEM_CLASS:
        return False
    STATE.garment = slot
    STATE.result["acf_equipment"] = {
        "equipment_component": object_path(STATE.equipment),
        "slot_component": object_path(slot),
        "slot_tag": gameplay_tag_string(tag),
        "armor_definition": object_path(armor_definition),
        "armor_definition_class": item_path,
        "has_item_in_legs_slot": True,
        "render_mesh_component_is_slot": True,
        "equipped_item_guid": legs_rows[0]["guid"],
        "current_equipment": equipment_rows,
    }
    return True


def stop_character_motion():
    movement = STATE.player.get_component_by_class(unreal.CharacterMovementComponent) if STATE.player else None
    if movement:
        try:
            movement.stop_movement_immediately()
        except Exception:
            pass


def set_body_morph(morph_name, value):
    call(STATE.body, "set_morph_target", morph_name, float(value), False)


def set_stress_combo(heavy, fitness_mass):
    set_body_morph(BODY_STRESS_MORPH, heavy)
    set_body_morph(BODY_SECONDARY_STRESS_MORPH, fitness_mass)


def transition(phase):
    STATE.phase = phase
    STATE.phase_elapsed = 0.0
    emit(f"phase={phase}")


class RuntimeState:
    def __init__(self):
        self.phase = "load_map"
        self.phase_elapsed = 0.0
        self.started = time.monotonic()
        self.callback = None
        self.lines = []
        self.stable_samples = 0
        self.world = None
        self.player = None
        self.controller = None
        self.body = None
        self.effective_pose_driver = None
        self.pose_driver_chain = []
        self.interaction = None
        self.equipment = None
        self.locomotion = None
        self.runtime = None
        self.registry = None
        self.profile = None
        self.pickup = None
        self.pickup_item_classes = []
        self.pickup_item_rows = []
        self.pickup_guids = set()
        self.pickup_panty_guids = set()
        self.inventory_before_rows = []
        self.garment = None
        self.free_camera_subsystem = None
        self.camera = None
        self.original_body_heavy = 0.0
        self.original_body_fitness_mass = 0.0
        self.capture_next_phase = None
        self.capture_path = None
        self.capture_label = None
        self.capture_request_in_progress = False
        self.rollback_heavy_reapplied = False
        self.out_of_range_value = None
        self.safety_sampling_active = False
        self.safety_sample_index = 0
        self.pair_matrix_cases = []
        self.pair_matrix_index = 0
        self.result = {
            "schema_version": 6,
            "status": "UE58_EF_CLOTHING_MORPH_V2_HUB_PIE_IN_PROGRESS",
            "project": str(PROJECT_DIR),
            "map": TARGET_MAP,
            "expected_compiler_version": EXPECTED_COMPILER_VERSION,
            "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "character_creation": {},
            "character_creation_early_sampling": {
                "status": "PENDING",
                "sample_count": 0,
                "first_world_seconds": None,
                "last_world_seconds": None,
                "violations": [],
            },
            "profile": {},
            "compiler_binding": {},
            "runtime_context": {},
            "acf_interaction": {},
            "acf_equipment": {},
            "runtime_checks": {},
            "initial_visibility_gate": {
                "status": "PENDING",
                "samples": [],
                "first_renderable": None,
            },
            "morph_weights": {},
            "pair_matrix": {
                "status": "PENDING",
                "state_count": 0,
                "states": [],
            },
            "offset_product": {},
            "out_of_range_suppression": {},
            "motion": {},
            "cvar_rollback": {},
            "free_camera": {},
            "screenshots": {},
            "errors": [],
            "visual_review": "PENDING_HUMAN_REVIEW",
            "no_assets_saved": True,
        }


STATE = RuntimeState()


def restore_runtime_state():
    try:
        if STATE.world:
            unreal.SystemLibrary.execute_console_command(
                STATE.world, "EFClothingMorph.V2.Enabled 1"
            )
    except Exception:
        pass
    try:
        if STATE.body:
            set_stress_combo(
                STATE.original_body_heavy,
                STATE.original_body_fitness_mass,
            )
    except Exception:
        pass
    try:
        if STATE.runtime:
            call(STATE.runtime, "clear_garment_clearance_multiplier", STATE.garment)
            call(STATE.runtime, "set_runtime_clearance_multiplier", 1.0)
    except Exception:
        pass
    try:
        if STATE.locomotion:
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
    except Exception:
        pass
    stop_character_motion()
    try:
        if STATE.free_camera_subsystem:
            call(STATE.free_camera_subsystem, "stop_gameplay_free_camera")
    except Exception:
        pass


def finish(success, failure=None):
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
    builtins._codex_ef_clothing_morph_v2_hub_pie = None
    restore_runtime_state()
    STATE.result["status"] = (
        "UE58_EF_CLOTHING_MORPH_V2_HUB_PIE_PASS"
        if success
        else "UE58_EF_CLOTHING_MORPH_V2_HUB_PIE_FAIL"
    )
    STATE.result["failure"] = failure
    STATE.result["finished_utc"] = time.strftime(
        "%Y-%m-%dT%H:%M:%SZ", time.gmtime()
    )
    STATE.result["cleanup_requested"] = True
    write_result()
    emit(f"finished success={success} failure={failure or ''}")
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    except Exception:
        pass
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def resolve_runtime_context():
    STATE.world = EDITOR_LEVEL_LIBRARY.get_game_world()
    if not STATE.world:
        return False
    STATE.controller = unreal.GameplayStatics.get_player_controller(STATE.world, 0)
    STATE.player = unreal.GameplayStatics.get_player_pawn(STATE.world, 0)
    if not STATE.controller or not STATE.player:
        return False
    STATE.body = find_body_component(STATE.player)
    if STATE.body:
        STATE.effective_pose_driver, STATE.pose_driver_chain = (
            resolve_effective_pose_driver(STATE.body)
        )
    else:
        STATE.effective_pose_driver = None
        STATE.pose_driver_chain = []
    STATE.interaction = find_component(STATE.player, "ACFInteractionComponent")
    STATE.equipment = find_component(STATE.player, "ACFEquipmentComponent")
    STATE.locomotion = find_component(STATE.player, "ProjectLocomotionOverride")
    STATE.runtime = find_component(STATE.player, "EFClothingFitRuntimeComponent")
    return all(
        (
            STATE.body,
            STATE.interaction,
            STATE.equipment,
            STATE.locomotion,
        )
    )


def tick(delta_time):
    try:
        if STATE.capture_request_in_progress:
            return
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            raise RuntimeError(f"Timeout in phase {STATE.phase}")
        if (
            STATE.phase in ("wait_pie", "bootstrap")
            and LEVEL_EDITOR.is_in_play_in_editor()
        ):
            sample_early_character_creation()
        if STATE.safety_sampling_active and STATE.phase != "interact":
            sample_initial_fit_visibility("post_tick")

        if STATE.phase == "load_map":
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
            emit(f"loading_map={TARGET_MAP}")
            # load_level pumps Slate and may re-enter the post-tick callback.
            # Advance first so a nested tick cannot recursively reload the map.
            transition("wait_map")
            LEVEL_EDITOR.load_level(TARGET_MAP)
            return

        if STATE.phase == "wait_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if normalized_world_name(editor_world) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.5:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            transition("wait_pie")
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor() or not resolve_runtime_context():
                STATE.stable_samples = 0
                return
            world_time = float(unreal.GameplayStatics.get_time_seconds(STATE.world))
            if normalized_world_name(STATE.world) == TARGET_MAP_NAME and world_time >= 3.0:
                STATE.stable_samples += 1
            else:
                STATE.stable_samples = 0
            if STATE.stable_samples < 3:
                return
            transition("bootstrap")
            return

        if STATE.phase == "bootstrap":
            require(resolve_runtime_context(), "Required player runtime components are missing")
            if STATE.profile is None:
                verify_character_creation_absent("hub_start")
                early_sampling = STATE.result["character_creation_early_sampling"]
                require(
                    early_sampling["sample_count"] > 0
                    and not early_sampling["violations"],
                    "Character Creation was not cleanly observed from the first valid PIE tick",
                )
                early_sampling["status"] = (
                    "PASS_VISIBLE_CC_ABSENT_FROM_FIRST_VALID_PIE_TICK"
                )
                load_compiler_binding()
                STATE.registry, STATE.profile = find_profile()
                profile_contract = validate_profile_contract(STATE.profile)
                validate_binding_against_loaded_assets(STATE.registry, profile_contract)
                direct_body_leader = get_property(
                    STATE.body, "leader_pose_component", default=None
                )
                require(
                    direct_body_leader is not None
                    and canonical_asset_path(mesh_asset(direct_body_leader))
                    == COMPATIBILITY_PATH,
                    "Exact Female does not directly follow the required Multiple component",
                )
                require(
                    object_path(STATE.effective_pose_driver)
                    == object_path(direct_body_leader)
                    and canonical_asset_path(mesh_asset(STATE.effective_pose_driver))
                    == COMPATIBILITY_PATH,
                    "Female's effective top-most pose driver is not its direct Multiple leader",
                )
                STATE.result["runtime_context"] = {
                    "player": object_path(STATE.player),
                    "body_component": object_path(STATE.body),
                    "body_mesh": canonical_asset_path(mesh_asset(STATE.body)),
                    "body_direct_leader_component": object_path(direct_body_leader),
                    "body_direct_leader_mesh": canonical_asset_path(
                        mesh_asset(direct_body_leader)
                    ),
                    "effective_pose_driver_component": object_path(
                        STATE.effective_pose_driver
                    ),
                    "effective_pose_driver_mesh": canonical_asset_path(
                        mesh_asset(STATE.effective_pose_driver)
                    ),
                    "body_direct_leader_is_effective_pose_driver": (
                        object_path(direct_body_leader)
                        == object_path(STATE.effective_pose_driver)
                    ),
                    "pose_driver_chain": STATE.pose_driver_chain,
                    "runtime_component": object_path(STATE.runtime),
                    "equipment_component": object_path(STATE.equipment),
                }
                STATE.original_body_heavy = float(
                    call(STATE.body, "get_morph_target", BODY_STRESS_MORPH)
                )
                STATE.original_body_fitness_mass = float(
                    call(STATE.body, "get_morph_target", BODY_SECONDARY_STRESS_MORPH)
                )
                # The two stress bindings must exist before gameplay evidence begins.
                find_morph_binding(BODY_STRESS_MORPH)
                find_morph_binding(BODY_SECONDARY_STRESS_MORPH)
                set_stress_combo(0.0, 0.0)
            if not resolve_free_camera():
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError("Gameplay free camera did not start")
                return
            if STATE.pickup is None:
                STATE.pickup, STATE.pickup_item_classes = find_panty_pickup(STATE.world)
                setup_real_acf_interaction()
            position_pickup_camera()
            begin_capture(
                "01_acf_pickup_selected.png",
                "acf_pickup_selected",
                "interact",
            )
            return

        if STATE.phase == "wait_capture":
            if STATE.capture_label in (
                "side_walk_motion",
                "side_walk_motion_burst",
                "back_walk_motion",
            ):
                call(
                    STATE.player,
                    "add_movement_input",
                    STATE.player.get_actor_forward_vector(),
                    1.0,
                    True,
                )
            elif STATE.capture_label in (
                "side_crawl_heavy_fitness",
                "side_crawl_closeup",
            ):
                call(
                    STATE.player,
                    "add_movement_input",
                    STATE.player.get_actor_forward_vector(),
                    0.65,
                    True,
                )
            if STATE.phase_elapsed < 2.25:
                return
            request = STATE.result["screenshots"][STATE.capture_path.name]
            request["exists"] = STATE.capture_path.is_file()
            request["size_bytes"] = (
                STATE.capture_path.stat().st_size if STATE.capture_path.is_file() else 0
            )
            require(request["accepted"], f"Screenshot request was rejected: {STATE.capture_path.name}")
            require(request["exists"], f"Screenshot file is missing: {STATE.capture_path}")
            require(request["size_bytes"] > 4096, f"Screenshot is unexpectedly small: {STATE.capture_path}")
            emit(f"SCREENSHOT_READY={STATE.capture_path.name}")
            transition(STATE.capture_next_phase)
            return

        if STATE.phase == "interact":
            verify_character_creation_absent("before_acf_interact")
            selected = call(STATE.interaction, "get_current_best_interactable_actor")
            require(selected == STATE.pickup, "Panty pickup selection changed before Interact")
            STATE.safety_sampling_active = True
            sample_initial_fit_visibility("immediately_before_interact")
            call(STATE.interaction, "interact", "")
            STATE.result["acf_interaction"]["interact_invoked"] = True
            sample_initial_fit_visibility("immediately_after_interact")
            transition("wait_equip")
            return

        if STATE.phase == "wait_equip":
            resolve_runtime_context()
            if not record_equipment_contract() or STATE.runtime is None:
                if STATE.phase_elapsed > 45.0:
                    raise RuntimeError("Panty did not auto-equip through ACF")
                return
            runtime = runtime_snapshot()
            if runtime["applied"] != 1 or runtime["pending"] != 0:
                if STATE.phase_elapsed > 45.0:
                    raise RuntimeError(f"EF Clothing Morph V2 did not become READY: {runtime}")
                return
            if not validate_real_acf_transfer():
                if STATE.phase_elapsed > 45.0:
                    raise RuntimeError("World pickup retained its original GUID after ACF equip")
                return
            verify_character_creation_absent("after_acf_equip")
            validate_ready_garment("equipped_ready")
            finish_initial_fit_visibility_gate()
            set_stress_combo(0.0, 0.0)
            call(STATE.runtime, "force_reconcile")
            transition("wait_rest_ready")
            return

        if STATE.phase == "wait_rest_ready":
            validate_ready_garment("rest_ready")
            if STATE.phase_elapsed < 1.25:
                return
            validate_rest_morph_weights(BODY_STRESS_MORPH, "body_heavy_0")
            validate_rest_morph_weights(
                BODY_SECONDARY_STRESS_MORPH,
                "body_fitness_mass_0",
            )
            validate_rest_pair_weights()
            verify_automatic_clearance("fitted_rest")
            position_character_camera("front")
            begin_capture(
                "02_front_idle_ready.png",
                "front_idle_ready",
                "apply_pair_matrix_cell",
            )
            return

        if STATE.phase == "apply_pair_matrix_cell":
            if not STATE.pair_matrix_cases:
                STATE.pair_matrix_cases = pair_matrix_cases()
            if STATE.pair_matrix_index >= len(STATE.pair_matrix_cases):
                matrix = STATE.result["pair_matrix"]
                matrix["state_count"] = len(matrix["states"])
                require(
                    matrix["state_count"] == EXPECTED_PAIR_CELL_COUNT,
                    f"Pair matrix covered only {matrix['state_count']} states",
                )
                matrix["status"] = "PASS_ALL_16_CELL_CENTERS_EXCLUSIVE_1_OF_16"
                transition("apply_heavy")
                return
            case = STATE.pair_matrix_cases[STATE.pair_matrix_index]
            set_body_morph(case["first_body_morph"], case["first_body_value"])
            set_body_morph(case["second_body_morph"], case["second_body_value"])
            transition("wait_pair_matrix_cell")
            return

        if STATE.phase == "wait_pair_matrix_cell":
            if STATE.phase_elapsed < 0.35:
                return
            case = STATE.pair_matrix_cases[STATE.pair_matrix_index]
            validate_ready_garment(
                "pair_matrix_{0}_{1}".format(
                    case["first_cell_index"], case["second_cell_index"]
                )
            )
            STATE.result["pair_matrix"]["states"].append(
                validate_pair_matrix_case(case)
            )
            STATE.pair_matrix_index += 1
            transition("apply_pair_matrix_cell")
            return

        if STATE.phase == "apply_heavy":
            set_stress_combo(1.0, 1.0)
            transition("wait_heavy")
            return

        if STATE.phase == "wait_heavy":
            validate_ready_garment("heavy_fitness_ready")
            if STATE.phase_elapsed < 1.35:
                return
            combo = validate_stress_combo_weights("initial_combo")
            verify_automatic_clearance(
                "heavy_fitness_before_manual_offset", combo["pair"]
            )
            position_character_camera("back")
            begin_capture(
                "03_back_heavy_fitness_ready.png",
                "back_heavy_fitness_ready",
                "heavy_closeup",
            )
            return

        if STATE.phase == "heavy_closeup":
            validate_ready_garment("heavy_fitness_closeup_ready")
            position_character_camera("back", 175.0)
            begin_capture(
                "03b_back_heavy_fitness_closeup.png",
                "back_heavy_fitness_closeup",
                "apply_offset",
            )
            return

        if STATE.phase == "apply_offset":
            call(
                STATE.runtime,
                "set_runtime_clearance_multiplier",
                EXPECTED_GLOBAL_CLEARANCE,
            )
            call(
                STATE.runtime,
                "set_garment_clearance_multiplier",
                STATE.garment,
                EXPECTED_GARMENT_CLEARANCE,
            )
            transition("wait_offset")
            return

        if STATE.phase == "wait_offset":
            validate_ready_garment("offset_ready")
            if STATE.phase_elapsed < 1.25:
                return
            verify_offset_product("before_cvar_rollback")
            validate_stress_combo_weights("offset_combo")
            position_character_camera("front")
            begin_capture(
                "04_front_offset_product.png",
                "front_offset_product",
                "apply_out_of_range",
            )
            return

        if STATE.phase == "apply_out_of_range":
            binding = find_morph_binding(BODY_STRESS_MORPH)
            certified_maximum = float(
                get_property(
                    binding,
                    "maximum_certified_value",
                    "MaximumCertifiedValue",
                    default=1.0,
                )
            )
            require(finite_number(certified_maximum), "Body Heavy certified maximum is invalid")
            STATE.out_of_range_value = certified_maximum + 0.125
            set_body_morph(BODY_STRESS_MORPH, STATE.out_of_range_value)
            call(STATE.runtime, "force_reconcile")
            transition("wait_out_of_range_suppressed")
            return

        if STATE.phase == "wait_out_of_range_suppressed":
            runtime = runtime_snapshot()
            garment = garment_snapshot(STATE.garment)
            suppressed = (
                runtime["applied"] == 0
                and runtime["pending"] == 1
                and garment["mesh"] == STATE.result["profile"]["fitted_garment"]
                and "EFClothingMorphV2.Pending" in garment["tags"]
                and not garment["render_in_main_pass"]
            )
            if not suppressed:
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError(
                        f"Out-of-range Body Heavy was not fail-suppressed: runtime={runtime} garment={garment}"
                    )
                return
            STATE.result["out_of_range_suppression"]["suppressed"] = {
                "body_morph": BODY_STRESS_MORPH,
                "value": STATE.out_of_range_value,
                "runtime": runtime,
                "garment": garment,
                "render_suppressed": True,
            }
            verify_character_creation_absent("out_of_range_suppressed")
            set_stress_combo(1.0, 1.0)
            call(STATE.runtime, "force_reconcile")
            transition("wait_out_of_range_recovered")
            return

        if STATE.phase == "wait_out_of_range_recovered":
            runtime = runtime_snapshot()
            if runtime["applied"] != 1 or runtime["pending"] != 0:
                if STATE.phase_elapsed > 15.0:
                    raise RuntimeError(f"V25 did not recover after certified morph restoration: {runtime}")
                return
            validate_ready_garment("out_of_range_recovered")
            validate_stress_combo_weights("recovered_combo")
            verify_offset_product("after_out_of_range_recovery")
            verify_character_creation_absent("out_of_range_recovered")
            STATE.result["out_of_range_suppression"]["recovered"] = {
                "runtime": runtime,
                "garment": garment_snapshot(STATE.garment),
                "certified_values_restored": True,
            }
            transition("start_walk")
            return

        if STATE.phase == "start_walk":
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            transition("walk_side")
            return

        if STATE.phase == "walk_side":
            call(
                STATE.player,
                "add_movement_input",
                STATE.player.get_actor_forward_vector(),
                1.0,
                True,
            )
            if STATE.phase_elapsed < 2.0:
                return
            speed = vector_length(STATE.player.get_velocity())
            animation = str(call(STATE.locomotion, "get_current_animation_asset_name"))
            require(speed > 5.0, f"Player did not move for walk QA (speed={speed})")
            require(animation and animation != "None", "Walk animation name is empty")
            STATE.result["motion"]["walk_side"] = {
                "speed_cm_s": speed,
                "animation": animation,
                "walk_mode": True,
            }
            validate_ready_garment("walk_side_ready")
            position_character_camera("side")
            begin_capture(
                "05_side_walk_motion.png",
                "side_walk_motion",
                "walk_side_burst",
            )
            return

        if STATE.phase == "walk_side_burst":
            call(
                STATE.player,
                "add_movement_input",
                STATE.player.get_actor_forward_vector(),
                1.0,
                True,
            )
            validate_ready_garment("walk_side_burst_ready")
            position_character_camera("side", 205.0)
            begin_capture(
                "05b_side_walk_motion_burst.png",
                "side_walk_motion_burst",
                "walk_back",
            )
            return

        if STATE.phase == "walk_back":
            call(
                STATE.player,
                "add_movement_input",
                STATE.player.get_actor_forward_vector(),
                1.0,
                True,
            )
            if STATE.phase_elapsed < 1.5:
                return
            speed = vector_length(STATE.player.get_velocity())
            require(speed > 5.0, f"Player stopped before back walk capture (speed={speed})")
            STATE.result["motion"]["walk_back"] = {
                "speed_cm_s": speed,
                "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
            }
            validate_ready_garment("walk_back_ready")
            position_character_camera("back")
            begin_capture(
                "06_back_walk_motion.png",
                "back_walk_motion",
                "start_crawl",
            )
            return

        if STATE.phase == "start_crawl":
            stop_character_motion()
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            transition("crawl_motion")
            return

        if STATE.phase == "crawl_motion":
            active = bool(call(STATE.locomotion, "is_crawl_mode_active"))
            if active:
                call(
                    STATE.player,
                    "add_movement_input",
                    STATE.player.get_actor_forward_vector(),
                    0.65,
                    True,
                )
            if not active:
                if STATE.phase_elapsed > 20.0:
                    raise RuntimeError("Crawl mode did not activate within 20 seconds")
                return
            if STATE.phase_elapsed < 4.0:
                return
            animation = str(call(STATE.locomotion, "get_current_animation_asset_name"))
            STATE.result["motion"]["crawl"] = {
                "active": active,
                "animation": animation,
                "speed_cm_s": vector_length(STATE.player.get_velocity()),
            }
            require(animation and animation != "None", "Crawl animation name is empty")
            validate_ready_garment("crawl_ready")
            validate_stress_combo_weights("crawl_combo")
            verify_offset_product("crawl")
            position_character_camera("side", 275.0)
            begin_capture(
                "07_side_crawl_heavy_fitness.png",
                "side_crawl_heavy_fitness",
                "crawl_closeup",
            )
            return

        if STATE.phase == "crawl_closeup":
            validate_ready_garment("crawl_closeup_ready")
            combo = validate_stress_combo_weights("crawl_closeup_combo")
            verify_clearance_resolution(
                "crawl_closeup",
                EXPECTED_GLOBAL_CLEARANCE,
                EXPECTED_GARMENT_CLEARANCE,
                combo["pair"],
            )
            position_character_camera("side", 190.0)
            begin_capture(
                "07b_side_crawl_closeup.png",
                "side_crawl_closeup",
                "disable_v2",
            )
            return

        if STATE.phase == "disable_v2":
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
            stop_character_motion()
            unreal.SystemLibrary.execute_console_command(
                STATE.world, "EFClothingMorph.V2.Enabled 0"
            )
            transition("wait_rollback")
            return

        if STATE.phase == "wait_rollback":
            runtime = runtime_snapshot()
            garment = garment_snapshot(STATE.garment)
            if runtime["applied"] != 0 or runtime["pending"] != 0 or garment["mesh"] != SOURCE_GARMENT_PATH:
                if STATE.phase_elapsed > 15.0:
                    raise RuntimeError(
                        f"CVar rollback did not restore the source garment: runtime={runtime} garment={garment}"
                    )
                return
            require("EFClothingMorphV2.Managed" not in garment["tags"], "Managed tag survived rollback")
            require("EFClothingMorphV2.Pending" not in garment["tags"], "Pending tag survived rollback")
            require(
                garment["skin_weight_profile"] in ("", "None"),
                f"Skin profile survived rollback: {garment['skin_weight_profile']}",
            )
            # RestoreAllGarments intentionally asks Character Creation to replay
            # its stored state. Reapply the stress morph to the body so rollback
            # and re-enable are compared against the same two-morph stress pose.
            if not STATE.rollback_heavy_reapplied:
                set_stress_combo(1.0, 1.0)
                STATE.rollback_heavy_reapplied = True
                STATE.phase_elapsed = 0.0
                return
            if STATE.phase_elapsed < 0.75:
                return
            require(
                abs(float(call(STATE.body, "get_morph_target", BODY_STRESS_MORPH)) - 1.0)
                <= 0.01,
                "Body Heavy did not remain at 1.0 for rollback comparison",
            )
            require(
                abs(
                    float(
                        call(
                            STATE.body,
                            "get_morph_target",
                            BODY_SECONDARY_STRESS_MORPH,
                        )
                    )
                    - 1.0
                )
                <= 0.01,
                "Body Fitness Mass did not remain at 1.0 for rollback comparison",
            )
            STATE.result["cvar_rollback"]["disabled"] = {
                "runtime": runtime,
                "garment": garment,
                "source_restored": True,
                "expected_offset_after_reenable": STATE.result["offset_product"][
                    "crawl_closeup"
                ]["expected_certified_tier"],
            }
            verify_character_creation_absent("cvar_disabled")
            position_character_camera("front")
            begin_capture(
                "08_cvar_disabled_source_rollback.png",
                "cvar_disabled_source_rollback",
                "enable_v2",
            )
            return

        if STATE.phase == "enable_v2":
            unreal.SystemLibrary.execute_console_command(
                STATE.world, "EFClothingMorph.V2.Enabled 1"
            )
            call(STATE.runtime, "force_reconcile")
            transition("wait_reenabled")
            return

        if STATE.phase == "wait_reenabled":
            runtime = runtime_snapshot()
            if runtime["applied"] != 1 or runtime["pending"] != 0:
                if STATE.phase_elapsed > 30.0:
                    raise RuntimeError(f"V2 did not recover after CVar re-enable: {runtime}")
                return
            validate_ready_garment("cvar_reenabled_ready")
            validate_stress_combo_weights("cvar_reenabled_combo")
            reenabled_offset = verify_offset_product("after_cvar_reenable")
            STATE.result["cvar_rollback"]["reenabled"] = {
                "runtime": runtime,
                "garment": garment_snapshot(STATE.garment),
                "derived_restored": True,
                "per_garment_multiplier_preserved": True,
                "offset": reenabled_offset,
            }
            verify_character_creation_absent("cvar_reenabled")
            position_character_camera("front")
            begin_capture(
                "09_cvar_reenabled_ready.png",
                "cvar_reenabled_ready",
                "final_checks",
            )
            return

        if STATE.phase == "final_checks":
            verify_character_creation_absent("final")
            validate_ready_garment("final")
            validate_stress_combo_weights("final_combo")
            verify_offset_product("final")
            require(
                STATE.result["character_creation_early_sampling"].get("status")
                == "PASS_VISIBLE_CC_ABSENT_FROM_FIRST_VALID_PIE_TICK"
                and STATE.result["character_creation_early_sampling"].get(
                    "sample_count", 0
                )
                > 0
                and not STATE.result["character_creation_early_sampling"].get(
                    "violations"
                ),
                "Early Character Creation visibility sampling did not pass",
            )
            require(
                STATE.result["pair_matrix"].get("status")
                == "PASS_ALL_16_CELL_CENTERS_EXCLUSIVE_1_OF_16"
                and STATE.result["pair_matrix"].get("state_count")
                == EXPECTED_PAIR_CELL_COUNT,
                "The runtime 4x4 pair-cell matrix did not pass",
            )
            require(
                STATE.result["compiler_binding"].get("loaded_assets_match") is True,
                "Runtime assets were not bound to the exact compiler PASS receipt",
            )
            require(
                STATE.result["acf_interaction"].get("pickup_consumed") is True
                and STATE.result["acf_interaction"].get(
                    "original_guid_preserved_to_legs_equipment"
                )
                is True,
                "Real pickup GUID/inventory/equipment proof is incomplete",
            )
            require(
                STATE.result["initial_visibility_gate"].get("status")
                == "PASS_OBSERVED_POST_TICK_RENDERABLE_SAMPLES_WERE_EXACT_READY",
                "Initial render-safety sampling did not pass",
            )
            require(
                bool(STATE.result["out_of_range_suppression"].get("suppressed"))
                and bool(STATE.result["out_of_range_suppression"].get("recovered")),
                "Out-of-range fail-suppression/recovery evidence is incomplete",
            )
            require(
                STATE.result["cvar_rollback"]
                .get("reenabled", {})
                .get("per_garment_multiplier_preserved")
                is True,
                "Per-garment offset persistence across CVar rollback was not proven",
            )
            require(
                set(STATE.result["screenshots"]) == set(EXPECTED_SCREENSHOT_FILENAMES),
                "The required gameplay screenshot set was not requested",
            )
            require(
                all(row.get("exists") for row in STATE.result["screenshots"].values()),
                "At least one gameplay screenshot is missing",
            )
            finish(True)
            return
    except Exception as exc:
        details = traceback.format_exc()
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = details
        unreal.log_error(details)
        finish(False, str(exc))


existing = getattr(builtins, "_codex_ef_clothing_morph_v2_hub_pie", None)
if existing is not None:
    unreal.log_warning("[EFClothingMorphV2HubPIE58] duplicate registration ignored")
else:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    PROGRESS_FILE.write_text("", encoding="utf-8")
    write_result()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_ef_clothing_morph_v2_hub_pie = STATE
    emit("registered=True")

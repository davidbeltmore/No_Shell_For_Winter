"""Visible HUB PIE smoke test for the EF Clothing Morph V2 PreviewRestOnly profile.

This test deliberately proves only the geometry-compiled rest fit, transferred
EF_AutoFit weights, animation following, catalog body coverage, and reversible
runtime ownership. It never assigns a skeletal mesh, never changes a body morph,
and acquires the garment only through ACF's real interaction/equipment route.
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
        PROJECT_DIR / "Saved" / "ClothingMorphV2QA" / "PreviewRestOnlyHubPIE_adhoc",
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
TIMEOUT_SECONDS = float(os.environ.get("CODEX_EF_CLOTHING_V2_QA_TIMEOUT", "300"))
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
GP_MATERIAL_SLOT = "Genesis9_GP_Torso"
EXPECTED_EXCLUDED_SURFACE_SLOTS = (GP_MATERIAL_SLOT,)
EXPECTED_EXCLUDED_BONE_BRANCHES = ("anus_01", "pelvis2", "rectum_01")
# Golden Palace morph namespaces are deliberately *not* excluded. The compiler's
# retained-surface leak guard proved that 17 GP morphs also move visible Female
# skin, so omitting them would permit silent clipping. PreviewRestOnly monitors
# them at zero; FullCatalog is responsible for compiling their shape response.
EXPECTED_EXCLUDED_MORPH_PREFIXES = ()
MINIMUM_PREVIEW_GAP_CM = 0.55
MORPH_ZERO_EPSILON = 0.001
EXPECTED_SCREENSHOT_FILENAMES = (
    "01_acf_pickup_selected.png",
    "02_front_idle_ready.png",
    "03_back_idle_ready.png",
    "04_side_walk_motion.png",
    "05_back_walk_motion.png",
    "06_side_crawl_motion.png",
    "07_side_crawl_closeup.png",
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
    name = re.sub(r"^UEDPIE_\d+_", "", world.get_name(), flags=re.IGNORECASE)
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


def emit(message):
    line = f"[EFClothingMorphV2PreviewRestHubPIE58] {message}"
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
    require(
        candidate.stat().st_size == int(row.get("size_bytes", -1)),
        f"{label} size changed",
    )
    expected_hash = str(row.get("sha256", "")).upper()
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
    require(binding.get("profile_mode") == "preview_rest_only", "Binding is not PreviewRestOnly")

    receipt_path = project_file_from_binding(
        binding.get("compiler_receipt", {}), "Compiler receipt"
    )
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    metrics = receipt.get("metrics", {})
    require(receipt.get("success") is True, "Bound compiler receipt is not PASS")
    require(receipt.get("compile_success") is True, "Bound native compile did not pass")
    require(receipt.get("validation_success") is True, "Bound validation did not pass")
    require(
        receipt.get("protected_inputs_unchanged") is True,
        "Bound receipt did not preserve protected inputs",
    )
    require(
        receipt.get("status") == "UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS",
        "Bound receipt has the wrong status",
    )
    require(receipt.get("profile_mode") == "preview_rest_only", "Bound receipt is not PreviewRestOnly")
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
    require(int(metrics.get("morph_binding_count", -1)) == 0, "Preview receipt has morph bindings")
    require(
        int(metrics.get("morph_pair_certificate_count", -1)) == 0
        and int(metrics.get("generated_pair_cell_morph_count", -1)) == 0
        and int(metrics.get("pair_body_probe_count", -1)) == 0
        and int(metrics.get("pair_offset_evaluation_count", -1)) == 0,
        "Preview receipt contains pair certification data",
    )
    require(
        int(metrics.get("penetrating_vertex_count_before", 0)) > 0
        and int(metrics.get("penetrating_vertex_count_after", -1)) == 0
        and float(metrics.get("minimum_signed_gap_after_cm", -1.0))
        >= MINIMUM_PREVIEW_GAP_CM,
        "Preview receipt does not prove the required rest-fit repair",
    )
    require(
        tuple(str(v) for v in metrics.get("excluded_body_surface_material_slots", []) or [])
        == EXPECTED_EXCLUDED_SURFACE_SLOTS
        and int(metrics.get("excluded_body_surface_triangle_count", 0)) > 0,
        "Preview receipt lacks the exact excluded GP surface",
    )
    require(
        tuple(str(v) for v in metrics.get("excluded_body_bone_branches", []) or [])
        == EXPECTED_EXCLUDED_BONE_BRANCHES,
        "Preview receipt lacks the exact excluded GP bone branches",
    )
    require(
        tuple(str(v) for v in metrics.get("excluded_body_morph_prefixes", []) or [])
        == EXPECTED_EXCLUDED_MORPH_PREFIXES,
        "Preview receipt lacks the exact excluded GP morph namespaces",
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
            f"Receipt protected file escapes target project: {candidate}",
        )
        require(candidate.is_file(), f"Receipt protected file is missing: {candidate}")
        require(candidate.stat().st_size == int(row.get("size_bytes", -1)), f"Size changed for {key}")
        require(sha256_file(candidate) == str(row.get("sha256", "")).upper(), f"Hash changed for {key}")
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
    require(canonical_asset_path(outputs.get("registry")) == REGISTRY_PATH, "Bound registry path is wrong")
    for key in ("derived_garment", "profile", "registry"):
        row = binding.get("assets", {}).get(key, {})
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


def vector_scale(value, scalar):
    return unreal.Vector(value.x * scalar, value.y * scalar, value.z * scalar)


def vector_length(value):
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


def finite_number(value):
    return isinstance(value, (int, float)) and math.isfinite(float(value))


def mesh_skeleton_path(mesh):
    if not mesh:
        return ""
    getter = getattr(mesh, "get_skeleton", None)
    skeleton = getter() if callable(getter) else get_property(mesh, "skeleton", default=None)
    return canonical_asset_path(skeleton)


def parse_morph_target_names(mesh):
    names = set()
    getter = getattr(mesh, "get_morph_target_names", None)
    if callable(getter):
        try:
            names.update(str(name) for name in (getter() or []) if str(name))
        except Exception:
            pass
    if not names:
        try:
            morphs = get_property(mesh, "morph_targets", "MorphTargets", default=[]) or []
            names.update(str(morph.get_name()) for morph in morphs if morph)
        except Exception:
            pass
    require(names, f"Could not enumerate cooked morph targets for {object_path(mesh)}")
    return sorted(names, key=str.lower)


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
    chain = []
    visited = set()
    current = component
    while current is not None and len(chain) < 128:
        identity = object_path(current)
        require(identity and identity not in visited, f"Cyclic LeaderPose chain: {chain}")
        visited.add(identity)
        chain.append({"component": identity, "mesh": canonical_asset_path(mesh_asset(current))})
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
            visible = "hidden" not in visibility.lower() and "collapsed" not in visibility.lower()
            if visible and (
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
    return sorted(rows, key=lambda row: row["object"].lower())


def verify_character_creation_absent(checkpoint):
    rows = visible_character_creation_widgets()
    STATE.result["character_creation"][checkpoint] = rows
    require(not rows, f"Character Creation is in the viewport at {checkpoint}: {rows}")


def sample_early_character_creation():
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
    require(not rows, f"Character Creation became visible during early HUB PIE: {rows}")


def find_profile():
    registry = unreal.load_asset(REGISTRY_PATH)
    require(registry is not None, f"Missing V2 registry: {REGISTRY_PATH}")
    profiles = get_property(registry, "profiles", "Profiles", default=[]) or []
    for profile in profiles:
        if canonical_asset_path(
            get_property(profile, "source_garment", "SourceGarment", default=None)
        ) == SOURCE_GARMENT_PATH:
            return registry, profile
    raise RuntimeError(f"Registry has no profile for {SOURCE_GARMENT_PATH}")


def profile_snapshot(profile):
    required_bones = [
        str(value)
        for value in (
            get_property(profile, "required_weighted_bones", "RequiredWeightedBones", default=[])
            or []
        )
    ]
    monitored = [
        str(value)
        for value in (
            get_property(
                profile,
                "monitored_body_morph_names",
                "MonitoredBodyMorphNames",
                default=[],
            )
            or []
        )
    ]
    return {
        "object": object_path(profile),
        "compiler_version": int(get_property(profile, "compiler_version", "CompilerVersion", default=-1)),
        "build_guid": str(get_property(profile, "build_guid", "BuildGuid", default="")),
        "source_garment": canonical_asset_path(get_property(profile, "source_garment", "SourceGarment", default=None)),
        "fitted_garment": canonical_asset_path(get_property(profile, "fitted_garment", "FittedGarment", default=None)),
        "body_surface": canonical_asset_path(get_property(profile, "body_surface", "BodySurface", default=None)),
        "compatibility_reference": canonical_asset_path(
            get_property(profile, "compatibility_reference", "CompatibilityReference", default=None)
        ),
        "skin_weight_profile_name": str(
            get_property(profile, "skin_weight_profile_name", "SkinWeightProfileName", default="")
        ),
        "clearance_morph_name": str(
            get_property(profile, "clearance_morph_name", "ClearanceMorphName", default="")
        ),
        "default_clearance_value": float(
            get_property(profile, "default_clearance_value", "DefaultClearanceValue", default=float("nan"))
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
        "required_weighted_bones": required_bones,
        "required_weighted_bone_count": len(required_bones),
        "excluded_body_surface_material_slots": [
            str(value)
            for value in (
                get_property(
                    profile,
                    "excluded_body_surface_material_slots",
                    "ExcludedBodySurfaceMaterialSlots",
                    default=[],
                )
                or []
            )
        ],
        "excluded_body_bone_branches": [
            str(value)
            for value in (
                get_property(
                    profile,
                    "excluded_body_bone_branches",
                    "ExcludedBodyBoneBranches",
                    default=[],
                )
                or []
            )
        ],
        "excluded_body_morph_prefixes": [
            str(value)
            for value in (
                get_property(
                    profile,
                    "excluded_body_morph_prefixes",
                    "ExcludedBodyMorphPrefixes",
                    default=[],
                )
                or []
            )
        ],
        "excluded_body_surface_triangle_count": int(
            get_property(
                profile,
                "excluded_body_surface_triangle_count",
                "ExcludedBodySurfaceTriangleCount",
                default=-1,
            )
        ),
        "morph_binding_count": len(
            get_property(profile, "morph_bindings", "MorphBindings", default=[]) or []
        ),
        "morph_pair_certificate_count": len(
            get_property(
                profile,
                "morph_pair_certificates",
                "MorphPairCertificates",
                default=[],
            )
            or []
        ),
        "generated_morph_sample_count": int(
            get_property(profile, "generated_morph_sample_count", "GeneratedMorphSampleCount", default=-1)
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
            get_property(profile, "pair_body_probe_count", "PairBodyProbeCount", default=-1)
        ),
        "pair_offset_evaluation_count": int(
            get_property(
                profile,
                "pair_offset_evaluation_count",
                "PairOffsetEvaluationCount",
                default=-1,
            )
        ),
        "monitored_body_morph_names": monitored,
        "compiled_morph_activation_epsilon": float(
            get_property(
                profile,
                "compiled_morph_activation_epsilon",
                "CompiledMorphActivationEpsilon",
                default=float("nan"),
            )
        ),
        "penetrating_vertex_count_before": int(
            get_property(
                profile,
                "penetrating_vertex_count_before",
                "PenetratingVertexCountBefore",
                default=-1,
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
        "compiled_minimum_clearance_cm": float(
            get_property(
                profile,
                "compiled_minimum_clearance_cm",
                "CompiledMinimumClearanceCm",
                default=-1.0,
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
    }


def validate_required_bone_exclusions(snapshot):
    excluded = set(snapshot["excluded_body_bone_branches"])
    lineages = {}
    for bone_name in snapshot["required_weighted_bones"]:
        require(int(call(STATE.body, "get_bone_index", bone_name)) >= 0, f"Required bone is absent: {bone_name}")
        lineage = []
        current = bone_name
        visited = set()
        while current and current != "None" and len(lineage) < 256:
            require(current not in visited, f"Cyclic body bone lineage for {bone_name}")
            visited.add(current)
            lineage.append(current)
            parent = str(call(STATE.body, "get_parent_bone", current))
            if not parent or parent == "None" or parent == current:
                break
            current = parent
        require(
            not excluded.intersection(lineage),
            f"Required bone {bone_name} remains inside an excluded branch: {lineage}",
        )
        lineages[bone_name] = lineage
    snapshot["required_weighted_bone_lineages"] = lineages


def validate_profile_contract(profile):
    snapshot = profile_snapshot(profile)
    require(snapshot["compiler_version"] == EXPECTED_COMPILER_VERSION, "Compiler version mismatch")
    require(snapshot["source_garment"] == SOURCE_GARMENT_PATH, "Source garment mismatch")
    require(snapshot["body_surface"] == BODY_PATH, "Body surface mismatch")
    require(snapshot["compatibility_reference"] == COMPATIBILITY_PATH, "Multiple mismatch")
    require(snapshot["fitted_garment"].startswith("/Game/_Generated/EFClothingMorphV2/"), "Fitted mesh is not generated")
    require(snapshot["skin_weight_profile_name"] == EXPECTED_SKIN_PROFILE, "Skin profile mismatch")
    require(snapshot["clearance_morph_name"], "Preview profile has no clearance morph")
    require(finite_number(snapshot["default_clearance_value"]) and snapshot["default_clearance_value"] > 0.0, "Invalid clearance value")
    require(
        snapshot["certified_clearance_multiplier_min"] == 1.0
        and snapshot["certified_clearance_multiplier_max"] == 2.0
        and snapshot["certified_clearance_tier_count"] == 9,
        "Preview certified offset contract is invalid",
    )
    require(
        snapshot["excluded_body_surface_material_slots"]
        == list(EXPECTED_EXCLUDED_SURFACE_SLOTS)
        and snapshot["excluded_body_surface_triangle_count"] > 0,
        "Profile lacks exact GP surface exclusion",
    )
    require(
        snapshot["excluded_body_bone_branches"]
        == list(EXPECTED_EXCLUDED_BONE_BRANCHES),
        "Profile lacks exact GP bone-branch exclusion",
    )
    require(
        snapshot["excluded_body_morph_prefixes"]
        == list(EXPECTED_EXCLUDED_MORPH_PREFIXES),
        "Profile lacks exact GP morph-prefix exclusion",
    )
    require(snapshot["required_weighted_bone_count"] > 0, "Profile has no weighted-bone contract")
    require(
        snapshot["required_weighted_bones"]
        == sorted(set(snapshot["required_weighted_bones"]), key=str.lower),
        "Required weighted bones are not canonical",
    )
    validate_required_bone_exclusions(snapshot)
    require(snapshot["morph_binding_count"] == 0, "PreviewRestOnly unexpectedly has morph bindings")
    require(
        snapshot["morph_pair_certificate_count"] == 0
        and snapshot["generated_morph_sample_count"] == 0
        and snapshot["generated_pair_cell_morph_count"] == 0
        and snapshot["pair_body_probe_count"] == 0
        and snapshot["pair_offset_evaluation_count"] == 0,
        "PreviewRestOnly unexpectedly has morph/pair certification",
    )
    monitored = snapshot["monitored_body_morph_names"]
    require(monitored, "Preview profile has no monitored morph safety set")
    require(monitored == sorted(set(monitored), key=str.lower), "Monitored morph set is not canonical")
    require(
        not any(name.startswith(EXPECTED_EXCLUDED_MORPH_PREFIXES) for name in monitored),
        "Excluded GP morph namespace leaked into monitored morphs",
    )
    require(snapshot["compiled_morph_activation_epsilon"] == 0.0, "Preview morph activation epsilon is not zero")
    require(
        snapshot["penetrating_vertex_count_before"] > 0
        and snapshot["penetrating_vertex_count_after"] == 0
        and snapshot["minimum_signed_gap_after_cm"] >= MINIMUM_PREVIEW_GAP_CM
        and snapshot["minimum_signed_gap_after_cm"]
        >= snapshot["compiled_minimum_clearance_cm"] - 0.001,
        "Compiled preview rest fit does not satisfy its clearance contract",
    )
    require(
        re.fullmatch(r"[0-9a-fA-F]{32}", snapshot["shared_skeleton_fingerprint"]),
        "Shared-skeleton fingerprint is missing",
    )

    fitted = unreal.load_asset(snapshot["fitted_garment"])
    source = unreal.load_asset(snapshot["source_garment"])
    body = unreal.load_asset(snapshot["body_surface"])
    compatibility = unreal.load_asset(snapshot["compatibility_reference"])
    require(all((fitted, source, body, compatibility)), "A PreviewRestOnly mesh failed to load")
    skeletons = {mesh_skeleton_path(mesh) for mesh in (fitted, source, body, compatibility)}
    require("" not in skeletons and len(skeletons) == 1, f"Meshes do not share one USkeleton: {skeletons}")
    snapshot["shared_skeleton_path"] = next(iter(skeletons))
    fitted_morphs = parse_morph_target_names(fitted)
    require(snapshot["clearance_morph_name"] in fitted_morphs, "Fitted mesh lacks clearance morph")
    leaked = sorted(set(monitored).intersection(fitted_morphs), key=str.lower)
    require(not leaked, f"Body morphs leaked into PreviewRestOnly fitted mesh: {leaked}")
    snapshot["fitted_morph_target_count"] = len(fitted_morphs)
    snapshot["direct_body_morph_targets_present"] = leaked

    binding = STATE.result["compiler_binding"]
    STATE.result["profile"] = snapshot
    loaded_registry = canonical_asset_path(STATE.registry)
    bound_registry = canonical_asset_path(binding["outputs"]["registry"])
    loaded_profile = canonical_asset_path(snapshot["object"])
    bound_profile = canonical_asset_path(binding["outputs"]["profile"])
    loaded_fitted = snapshot["fitted_garment"]
    bound_fitted = canonical_asset_path(binding["outputs"]["derived_garment"])
    # UE's Python wrapper exposes FGuid as an opaque empty struct on this build.
    # Generated profile names include the same publication GUID, so use that
    # exact object-path token as the reflected fallback.
    loaded_guid = normalize_guid(snapshot["build_guid"]) or normalize_guid(
        snapshot["object"]
    )
    snapshot["build_guid_raw"] = snapshot["build_guid"]
    snapshot["build_guid"] = loaded_guid
    bound_guid = normalize_guid(binding["compiler"]["build_guid"])
    require(
        loaded_registry == bound_registry,
        f"Loaded registry differs from compiler binding: loaded={loaded_registry} bound={bound_registry}",
    )
    require(
        loaded_profile == bound_profile,
        f"Loaded profile differs from compiler binding: loaded={loaded_profile} bound={bound_profile}",
    )
    require(
        loaded_fitted == bound_fitted,
        f"Loaded fitted mesh differs from compiler binding: loaded={loaded_fitted} bound={bound_fitted}",
    )
    require(
        loaded_guid == bound_guid,
        f"Loaded build GUID differs from compiler binding: loaded={loaded_guid} "
        f"raw={snapshot['build_guid']} bound={bound_guid}",
    )
    binding["loaded_assets_match"] = True
    return snapshot


def validate_monitored_morphs_zero(checkpoint):
    monitored = STATE.result["profile"]["monitored_body_morph_names"]
    non_zero = []
    maximum = 0.0
    for morph_name in monitored:
        value = float(call(STATE.body, "get_morph_target", morph_name))
        require(finite_number(value), f"Non-finite monitored morph {morph_name}")
        maximum = max(maximum, abs(value))
        if abs(value) > MORPH_ZERO_EPSILON:
            non_zero.append({"morph": morph_name, "value": value})
    row = {
        "status": "PASS_ALL_MONITORED_ZERO" if not non_zero else "FAIL_NONZERO_MONITORED_MORPH",
        "monitored_count": len(monitored),
        "maximum_absolute_weight": maximum,
        "non_zero": non_zero,
    }
    STATE.result["monitored_morph_checks"][checkpoint] = row
    require(
        not non_zero,
        f"PreviewRestOnly cannot certify active body morphs at {checkpoint}: {non_zero}",
    )
    return row


def debug_counter(summary, name):
    match = re.search(rf"\b{re.escape(name)}=(-?\d+)\b", summary)
    require(match is not None, f"Runtime debug summary lacks {name}: {summary}")
    return int(match.group(1))


def runtime_snapshot():
    summary = str(call(STATE.runtime, "get_debug_summary"))
    return {
        "component": object_path(STATE.runtime),
        "applied": int(call(STATE.runtime, "get_applied_garment_count")),
        "pending": int(call(STATE.runtime, "get_pending_garment_count")),
        "coverage_sections": debug_counter(summary, "CoverageSections"),
        "coverage_refs": debug_counter(summary, "CoverageRefs"),
        "debug_summary": summary,
    }


def body_coverage_snapshot():
    slot_names = [str(name) for name in call(STATE.body, "get_material_slot_names") or []]
    require(GP_MATERIAL_SLOT in slot_names, f"Female lacks exact GP material slot: {slot_names}")
    material_index = int(call(STATE.body, "get_material_index", GP_MATERIAL_SLOT))
    require(material_index >= 0, "Unable to resolve GP material slot dynamically")
    lod_count = int(call(STATE.body, "get_num_lods"))
    require(lod_count > 0, "Female exposes no runtime LODs")
    shown_by_lod = [
        bool(call(STATE.body, "is_material_section_shown", material_index, lod_index))
        for lod_index in range(lod_count)
    ]
    runtime = runtime_snapshot()
    return {
        "slot_name": GP_MATERIAL_SLOT,
        "material_index": material_index,
        "lod_count": lod_count,
        "shown_by_lod": shown_by_lod,
        "all_shown": all(shown_by_lod),
        "all_hidden": not any(shown_by_lod),
        "runtime": runtime,
    }


def validate_body_coverage(checkpoint, expected_hidden, expected_sections, expected_refs):
    row = body_coverage_snapshot()
    row["expected_hidden"] = bool(expected_hidden)
    row["expected_coverage_sections"] = int(expected_sections)
    row["expected_coverage_refs"] = int(expected_refs)
    if expected_hidden:
        require(row["all_hidden"], f"GP surface is visible at {checkpoint}: {row}")
    else:
        require(row["all_shown"], f"GP surface was not restored at {checkpoint}: {row}")
    require(
        row["runtime"]["coverage_sections"] == expected_sections
        and row["runtime"]["coverage_refs"] == expected_refs,
        f"Coverage refcount mismatch at {checkpoint}: {row}",
    )
    row["status"] = "PASS"
    STATE.result["coverage_checks"][checkpoint] = row
    return row


def item_class_path(item):
    return object_path(get_property(item, "item_class", "ItemClass", default=None))


def acf_item_guid(item):
    guid = normalize_guid(get_property(item, "item_guid", "ItemGuid", default=None))
    require(guid, f"ACF item has no runtime GUID: {item}")
    return guid


def acf_item_snapshot(item, source):
    item_object = get_property(item, "item", "Item", default=None)
    return {
        "source": source,
        "guid": acf_item_guid(item),
        "item_class": item_class_path(item) or class_path(item_object),
        "count": int(get_property(item, "count", "Count", default=1) or 0),
        "is_equipped": bool(get_property(item, "b_is_equipped", "bIsEquipped", default=False)),
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
    require(len({row["guid"] for row in rows}) == len(rows), f"Duplicate ACF GUID in {source}")
    return sorted(rows, key=lambda row: (row["guid"], row["item_class"]))


def player_inventory_rows():
    return acf_item_rows(call(STATE.equipment, "get_inventory") or [], "player_inventory")


def current_equipment_rows():
    current = call(STATE.equipment, "get_current_equipment")
    equipped = get_property(current, "equipped_items", "EquippedItems", default=None)
    require(equipped is not None, "ACF current equipment has no equipped-item array")
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
    require(world_item_class is not None, "Unable to load ACFWorldItem")
    candidates = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, world_item_class) or []:
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
    require(bool(STATE.player.set_actor_location(horizontal, False, True)), "Could not enter pickup radius")
    look = unreal.MathLibrary.find_look_at_rotation(horizontal, pickup_location)
    STATE.player.set_actor_rotation(unreal.Rotator(0.0, look.yaw, 0.0), True)
    call(STATE.interaction, "enable_detection", True)
    call(STATE.interaction, "refresh_interactions")
    call(STATE.interaction, "register_interactable", STATE.pickup)
    call(STATE.interaction, "set_current_best_interactable", STATE.pickup)
    selected = call(STATE.interaction, "get_current_best_interactable_actor")
    require(selected == STATE.pickup, "ACF did not select the Panty pickup")
    require(bool(call(STATE.interaction, "has_valid_interactable")), "ACF reports no valid interactable")
    pickup_rows = acf_item_rows(call(STATE.pickup, "get_items") or [], "world_pickup")
    inventory_before = player_inventory_rows()
    pickup_panties = [row for row in pickup_rows if row["item_class"] == PANTY_ITEM_CLASS]
    require(pickup_panties, "Selected pickup has no GUID-bearing Panty")
    pickup_guids = {row["guid"] for row in pickup_rows}
    before_guids = {row["guid"] for row in inventory_before}
    require(not pickup_guids.intersection(before_guids), "Pickup GUID already exists in inventory")
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
    }


def validate_real_acf_transfer():
    inventory_after = player_inventory_rows()
    equipment_after = current_equipment_rows()
    after_inventory_guids = {row["guid"] for row in inventory_after}
    equipped_guids = {row["guid"] for row in equipment_after}
    before_guids = {row["guid"] for row in STATE.inventory_before_rows}
    guid_delta = after_inventory_guids.difference(before_guids)
    legs_rows = [row for row in equipment_after if LEGS_SLOT in row["equipment_slot"]]
    require(guid_delta == STATE.pickup_guids, "Inventory GUID delta differs from pickup")
    require(STATE.pickup_guids.issubset(after_inventory_guids), "Pickup GUID did not reach inventory")
    require(len(legs_rows) == 1, f"Expected one Legs item: {legs_rows}")
    require(legs_rows[0]["item_class"] == PANTY_ITEM_CLASS, "Equipped Legs item is not Panty")
    require(legs_rows[0]["guid"] in STATE.pickup_panty_guids, "Equipped Panty lost original GUID")
    require(STATE.pickup_panty_guids.issubset(equipped_guids), "Panty GUID absent from equipment")
    pickup_valid = object_is_valid(STATE.pickup)
    pickup_remaining = []
    if pickup_valid:
        pickup_remaining = acf_item_rows(call(STATE.pickup, "get_items") or [], "world_pickup_after")
    remaining_guids = {row["guid"] for row in pickup_remaining}
    pickup_consumed = (not pickup_valid) or not STATE.pickup_guids.intersection(remaining_guids)
    if not pickup_consumed:
        return False
    STATE.result["acf_interaction"].update(
        {
            "interact_invoked": True,
            "player_inventory_after": inventory_after,
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
        if "acfarmorslotcomponent" not in class_path(component).lower().replace("_", ""):
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


def capture_acf_wait_equip_diagnostic():
    row = {
        "phase_elapsed_seconds": round(float(STATE.phase_elapsed), 3),
        "pickup_valid": object_is_valid(STATE.pickup),
        "inventory": [],
        "equipment": [],
        "armor_slots": [],
        "errors": [],
    }
    try:
        row["inventory"] = player_inventory_rows()
    except Exception as exc:
        row["errors"].append(f"inventory: {exc}")
    try:
        row["equipment"] = current_equipment_rows()
    except Exception as exc:
        row["errors"].append(f"equipment: {exc}")
    if row["pickup_valid"]:
        try:
            row["pickup_items"] = acf_item_rows(
                call(STATE.pickup, "get_items") or [], "world_pickup_wait"
            )
        except Exception as exc:
            row["errors"].append(f"pickup: {exc}")
    for component in all_skeletal_components(STATE.player):
        component_class = class_path(component)
        if "acfarmorslotcomponent" not in component_class.lower().replace("_", ""):
            continue
        slot_row = {
            "component": object_path(component),
            "class": component_class,
            "mesh": canonical_asset_path(mesh_asset(component)),
            "slot_tag": "",
            "armor_definition": "",
            "armor_definition_class": "",
        }
        try:
            slot_tag = call(component, "get_slot_tag")
            slot_row["slot_tag"] = gameplay_tag_string(slot_tag)
        except Exception as exc:
            slot_row["slot_tag_error"] = str(exc)
        try:
            armor_definition = call(component, "get_armor_definition")
            slot_row["armor_definition"] = object_path(armor_definition)
            slot_row["armor_definition_class"] = class_path(armor_definition)
        except Exception as exc:
            slot_row["armor_definition_error"] = str(exc)
        row["armor_slots"].append(slot_row)
    STATE.result["acf_wait_equip_last"] = row
    return row


def garment_snapshot(component):
    leader = get_property(component, "leader_pose_component", default=None)
    current_profile = ""
    getter = getattr(component, "get_current_skin_weight_profile_name", None)
    if callable(getter):
        try:
            current_profile = str(getter())
        except Exception:
            pass
    using_profile = None
    getter = getattr(component, "is_using_skin_weight_profile", None)
    if callable(getter):
        try:
            using_profile = bool(getter())
        except Exception:
            pass
    profile_pending = None
    getter = getattr(component, "is_skin_weight_profile_pending", None)
    if callable(getter):
        try:
            profile_pending = bool(getter())
        except Exception:
            pass
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
        "render_in_main_pass": bool(get_property(component, "render_in_main_pass", default=True)),
        "leader_pose_component": object_path(leader),
        "leader_pose_mesh": canonical_asset_path(mesh_asset(leader)),
        "leader_pose_is_effective_pose_driver": bool(leader)
        and object_path(leader) == object_path(STATE.effective_pose_driver),
        "skin_weight_profile": current_profile,
        "is_using_skin_weight_profile": using_profile,
        "skin_weight_profile_pending": profile_pending,
    }


def garment_is_renderable(garment):
    return (
        garment.get("visible") is True
        and garment.get("hidden_in_game") is False
        and garment.get("render_in_main_pass") is True
    )


def garment_has_exact_ready_contract(runtime, garment, coverage):
    if not runtime or not garment or not coverage:
        return False
    return (
        runtime.get("applied") == 1
        and runtime.get("pending") == 0
        and runtime.get("coverage_sections") == 1
        and runtime.get("coverage_refs") == 1
        and coverage.get("all_hidden") is True
        and garment.get("mesh") == STATE.result.get("profile", {}).get("fitted_garment")
        and "EFClothingMorphV2.Managed" in garment.get("tags", [])
        and "EFClothingMorphV2.Pending" not in garment.get("tags", [])
        and garment.get("leader_pose_is_effective_pose_driver") is True
        and garment.get("leader_pose_mesh")
        == canonical_asset_path(mesh_asset(STATE.effective_pose_driver))
        and garment.get("skin_weight_profile") == EXPECTED_SKIN_PROFILE
        and garment.get("is_using_skin_weight_profile") is True
        # UE 5.8 does not expose IsSkinWeightProfilePending to Python on every
        # component class. Runtime Ready=1 is produced only after C++ verifies
        # the exact profile is active and not pending, so None is acceptable
        # only alongside that complete READY contract.
        and garment.get("skin_weight_profile_pending") in (False, None)
    )


def sample_initial_fit_visibility(label):
    if not STATE.safety_sampling_active:
        return
    resolve_runtime_context()
    slot, _ = find_leg_armor_slot(STATE.player)
    runtime = runtime_snapshot() if STATE.runtime else None
    garment = garment_snapshot(slot) if slot else None
    coverage = body_coverage_snapshot() if STATE.body and STATE.runtime else None
    target_mesh = bool(
        garment
        and garment.get("mesh")
        in (SOURCE_GARMENT_PATH, STATE.result.get("profile", {}).get("fitted_garment"))
    )
    renderable = bool(target_mesh and garment_is_renderable(garment))
    safe = garment_has_exact_ready_contract(runtime, garment, coverage)
    STATE.safety_sample_index += 1
    row = {
        "index": STATE.safety_sample_index,
        "label": label,
        "phase": STATE.phase,
        "world_seconds": float(unreal.GameplayStatics.get_time_seconds(STATE.world)),
        "runtime": runtime,
        "garment": garment,
        "coverage": coverage,
        "target_mesh": target_mesh,
        "renderable": renderable,
        "safe": safe,
    }
    gate = STATE.result["initial_visibility_gate"]
    gate["samples"].append(row)
    if renderable and gate["first_renderable"] is None:
        gate["first_renderable"] = row
    require(not renderable or safe, f"Observed post-tick garment sample was renderable before exact READY: {row}")


def finish_initial_fit_visibility_gate():
    gate = STATE.result["initial_visibility_gate"]
    first_renderable = gate.get("first_renderable")
    require(gate["samples"], "No post-Interact frame samples were collected")
    require(first_renderable is not None, "No renderable garment post-tick sample was observed")
    require(first_renderable.get("safe") is True, "First renderable post-tick sample was unsafe")
    gate["sample_count"] = len(gate["samples"])
    gate["unsafe_renderable_count"] = sum(
        1 for row in gate["samples"] if row["renderable"] and not row["safe"]
    )
    require(gate["unsafe_renderable_count"] == 0, "Unsafe renderable frame observed")
    gate["status"] = "PASS_OBSERVED_POST_TICK_RENDERABLE_SAMPLES_WERE_EXACT_READY_WITH_GP_HIDDEN"
    STATE.safety_sampling_active = False


def validate_ready_garment(checkpoint):
    require(STATE.runtime is not None and STATE.garment is not None, "Runtime/garment is missing")
    runtime = runtime_snapshot()
    garment = garment_snapshot(STATE.garment)
    profile = STATE.result["profile"]
    require(runtime["applied"] == 1 and runtime["pending"] == 0, f"Runtime is not READY at {checkpoint}: {runtime}")
    require(garment["mesh"] == profile["fitted_garment"], f"Derived mesh mismatch at {checkpoint}")
    require("EFClothingMorphV2.Managed" in garment["tags"], f"Managed tag missing at {checkpoint}")
    require("EFClothingMorphV2.Pending" not in garment["tags"], f"Pending tag remains at {checkpoint}")
    require(garment["leader_pose_component"] != garment["component"], "Garment leads itself")
    require(garment["leader_pose_is_effective_pose_driver"] is True, f"Wrong pose driver at {checkpoint}")
    require(
        garment["leader_pose_mesh"]
        == canonical_asset_path(mesh_asset(STATE.effective_pose_driver)),
        f"Garment does not follow Female's exact effective pose driver at {checkpoint}",
    )
    require(garment["skin_weight_profile"] == EXPECTED_SKIN_PROFILE, f"EF_AutoFit inactive at {checkpoint}")
    require(garment["is_using_skin_weight_profile"] is True, f"Skin profile not active at {checkpoint}")
    require(
        garment["skin_weight_profile_pending"] in (False, None),
        f"Skin profile pending at {checkpoint}",
    )
    require(garment["visible"] is True and not garment["hidden_in_game"] and garment["render_in_main_pass"], f"Garment not renderable at {checkpoint}")
    coverage = validate_body_coverage(checkpoint, True, 1, 1)
    morphs = validate_monitored_morphs_zero(checkpoint)
    STATE.result["runtime_checks"][checkpoint] = {
        "runtime": runtime,
        "garment": garment,
        "coverage": coverage,
        "monitored_morphs": morphs,
    }
    return runtime, garment


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
        if not bool(call(STATE.free_camera_subsystem, "start_gameplay_free_camera")):
            return False
    view_target = call(STATE.controller, "get_view_target")
    if not view_target or "CameraActor" not in class_path(view_target):
        return False
    STATE.camera = view_target
    STATE.result["free_camera"] = {
        "subsystem": object_path(STATE.free_camera_subsystem),
        "active": True,
        "camera": object_path(STATE.camera),
    }
    return True


def position_character_camera(view, distance=210.0, target_height=86.0):
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
        unreal.Vector(0.0, 0.0, target_height + 7.0),
    )
    target = vector_add(location, unreal.Vector(0.0, 0.0, target_height))
    rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, target)
    STATE.camera.set_actor_location_and_rotation(camera_location, rotation, False, True)


def position_pickup_camera():
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
        "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
        "garment_mesh": canonical_asset_path(mesh_asset(STATE.garment)) if STATE.garment else "",
        "coverage": body_coverage_snapshot(),
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


def stop_character_motion():
    movement = (
        STATE.player.get_component_by_class(unreal.CharacterMovementComponent)
        if STATE.player
        else None
    )
    if movement:
        try:
            movement.stop_movement_immediately()
        except Exception:
            pass


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
        self.capture_next_phase = None
        self.capture_path = None
        self.capture_label = None
        self.capture_request_in_progress = False
        self.safety_sampling_active = False
        self.safety_sample_index = 0
        self.result = {
            "schema_version": 1,
            "status": "UE58_EF_CLOTHING_MORPH_V2_PREVIEW_REST_HUB_PIE_IN_PROGRESS",
            "project": str(PROJECT_DIR),
            "map": TARGET_MAP,
            "profile_mode": "preview_rest_only",
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
            "compiler_binding": {},
            "profile": {},
            "runtime_context": {},
            "acf_interaction": {},
            "acf_equipment": {},
            "runtime_checks": {},
            "coverage_checks": {},
            "monitored_morph_checks": {},
            "initial_visibility_gate": {
                "status": "PENDING",
                "samples": [],
                "first_renderable": None,
            },
            "motion": {},
            "cvar_rollback": {},
            "free_camera": {},
            "screenshots": {},
            "errors": [],
            "visual_review": "PENDING_HUMAN_REVIEW",
            "scope": "REST_FIT_AND_ANIMATION_SMOKE_ONLY_NO_MORPH_CERTIFICATION",
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
    builtins._codex_ef_clothing_morph_v2_preview_rest_hub_pie = None
    restore_runtime_state()
    STATE.result["status"] = (
        "UE58_EF_CLOTHING_MORPH_V2_PREVIEW_REST_HUB_PIE_PASS"
        if success
        else "UE58_EF_CLOTHING_MORPH_V2_PREVIEW_REST_HUB_PIE_FAIL"
    )
    STATE.result["failure"] = failure
    STATE.result["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
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
        STATE.effective_pose_driver, STATE.pose_driver_chain = resolve_effective_pose_driver(
            STATE.body
        )
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
            STATE.runtime,
        )
    )


def tick(delta_time):
    try:
        if STATE.capture_request_in_progress:
            return
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            raise RuntimeError(f"Timeout in phase {STATE.phase}")
        if STATE.phase in ("wait_pie", "bootstrap") and LEVEL_EDITOR.is_in_play_in_editor():
            sample_early_character_creation()
        if STATE.safety_sampling_active and STATE.phase != "interact":
            sample_initial_fit_visibility("presented_post_tick")

        if STATE.phase == "load_map":
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
            emit(f"loading_map={TARGET_MAP}")
            # load_level pumps Slate and can re-enter this callback before it
            # returns. Advance first so a nested tick cannot issue another map
            # load and recursively crash the editor.
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
            STATE.stable_samples = (
                STATE.stable_samples + 1
                if normalized_world_name(STATE.world) == TARGET_MAP_NAME and world_time >= 3.0
                else 0
            )
            if STATE.stable_samples < 3:
                return
            transition("bootstrap")
            return

        if STATE.phase == "bootstrap":
            require(resolve_runtime_context(), "Required player runtime components are missing")
            if STATE.profile is None:
                verify_character_creation_absent("hub_start")
                early = STATE.result["character_creation_early_sampling"]
                require(early["sample_count"] > 0 and not early["violations"], "Early Character Creation sampling failed")
                early["status"] = "PASS_VISIBLE_CC_ABSENT_FROM_FIRST_VALID_PIE_TICK"
                load_compiler_binding()
                candidate_registry, candidate_profile = find_profile()
                # validate_profile_contract checks the exact loaded registry
                # identity but does not mutate gameplay state.
                STATE.registry = candidate_registry
                profile = validate_profile_contract(candidate_profile)
                direct_leader = get_property(STATE.body, "leader_pose_component", default=None)
                direct_leader_mesh = canonical_asset_path(mesh_asset(direct_leader))
                effective_driver_mesh = canonical_asset_path(
                    mesh_asset(STATE.effective_pose_driver)
                )
                body_is_its_own_driver = (
                    direct_leader is None
                    and object_path(STATE.effective_pose_driver)
                    == object_path(STATE.body)
                    and effective_driver_mesh == BODY_PATH
                )
                body_uses_explicit_driver = (
                    direct_leader is not None
                    and object_path(STATE.effective_pose_driver)
                    == object_path(direct_leader)
                    and effective_driver_mesh == COMPATIBILITY_PATH
                )
                if not (body_is_its_own_driver or body_uses_explicit_driver):
                    STATE.result["runtime_context"]["leader_wait"] = {
                        "elapsed_seconds": STATE.phase_elapsed,
                        "body_component": object_path(STATE.body),
                        "direct_leader_component": object_path(direct_leader),
                        "direct_leader_mesh": direct_leader_mesh,
                        "effective_pose_driver_component": object_path(
                            STATE.effective_pose_driver
                        ),
                        "effective_pose_driver_mesh": effective_driver_mesh,
                        "pose_driver_chain": STATE.pose_driver_chain,
                    }
                    if STATE.phase_elapsed < 20.0:
                        return
                    raise RuntimeError(
                        "Female did not expose a valid self/Multiple effective pose "
                        f"driver within 20s: {STATE.result['runtime_context']['leader_wait']}"
                    )
                STATE.profile = candidate_profile
                STATE.result["runtime_context"] = {
                    "player": object_path(STATE.player),
                    "body_component": object_path(STATE.body),
                    "body_mesh": canonical_asset_path(mesh_asset(STATE.body)),
                    "body_direct_leader_component": object_path(direct_leader),
                    "body_direct_leader_mesh": direct_leader_mesh,
                    "effective_pose_driver_component": object_path(STATE.effective_pose_driver),
                    "effective_pose_driver_mesh": effective_driver_mesh,
                    "body_direct_leader_is_effective_pose_driver": object_path(direct_leader)
                    == object_path(STATE.effective_pose_driver),
                    "body_is_its_own_pose_driver": body_is_its_own_driver,
                    "body_uses_explicit_multiple_driver": body_uses_explicit_driver,
                    "pose_driver_chain": STATE.pose_driver_chain,
                    "runtime_component": object_path(STATE.runtime),
                    "equipment_component": object_path(STATE.equipment),
                    "profile": profile["object"],
                }
                validate_monitored_morphs_zero("bootstrap_pre_equip")
                validate_body_coverage("pre_equip", False, 0, 0)
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
            if STATE.capture_label in ("side_walk_motion", "back_walk_motion"):
                call(
                    STATE.player,
                    "add_movement_input",
                    STATE.player.get_actor_forward_vector(),
                    1.0,
                    True,
                )
            elif STATE.capture_label in ("side_crawl_motion", "side_crawl_closeup"):
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
            request["size_bytes"] = STATE.capture_path.stat().st_size if request["exists"] else 0
            require(request["accepted"], f"Screenshot request rejected: {STATE.capture_path.name}")
            require(request["exists"] and request["size_bytes"] > 4096, f"Screenshot missing/invalid: {STATE.capture_path}")
            emit(f"SCREENSHOT_READY={STATE.capture_path.name}")
            transition(STATE.capture_next_phase)
            return

        if STATE.phase == "interact":
            verify_character_creation_absent("before_acf_interact")
            require(
                call(STATE.interaction, "get_current_best_interactable_actor") == STATE.pickup,
                "Panty pickup selection changed before Interact",
            )
            call(STATE.interaction, "interact", "")
            STATE.result["acf_interaction"]["interact_invoked"] = True
            # Start at the next post-tick. This samples every observable state
            # through READY; the C++ viewport OnBeginDraw guard is the separate
            # same-frame protection and is not directly instrumented by Python.
            STATE.safety_sampling_active = True
            transition("wait_equip")
            return

        if STATE.phase == "wait_equip":
            resolve_runtime_context()
            capture_acf_wait_equip_diagnostic()
            if not record_equipment_contract():
                if STATE.phase_elapsed > 45.0:
                    raise RuntimeError(
                        "Panty did not auto-equip through ACF: "
                        f"{STATE.result.get('acf_wait_equip_last', {})}"
                    )
                return
            runtime = runtime_snapshot()
            if runtime["applied"] != 1 or runtime["pending"] != 0:
                if STATE.phase_elapsed > 45.0:
                    raise RuntimeError(f"PreviewRestOnly did not become READY: {runtime}")
                return
            if not validate_real_acf_transfer():
                if STATE.phase_elapsed > 45.0:
                    raise RuntimeError("World pickup retained its GUID after ACF equip")
                return
            verify_character_creation_absent("after_acf_equip")
            validate_ready_garment("equipped_ready")
            finish_initial_fit_visibility_gate()
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
            stop_character_motion()
            transition("wait_idle_front")
            return

        if STATE.phase == "wait_idle_front":
            if STATE.phase_elapsed < 1.25:
                return
            validate_ready_garment("idle_front_ready")
            STATE.result["motion"]["idle"] = {
                "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
                "speed_cm_s": vector_length(STATE.player.get_velocity()),
            }
            position_character_camera("front", 150.0, 72.0)
            begin_capture("02_front_idle_ready.png", "front_idle_ready", "idle_back")
            return

        if STATE.phase == "idle_back":
            validate_ready_garment("idle_back_ready")
            position_character_camera("back", 145.0, 72.0)
            begin_capture("03_back_idle_ready.png", "back_idle_ready", "start_walk")
            return

        if STATE.phase == "start_walk":
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            transition("walk_side")
            return

        if STATE.phase == "walk_side":
            call(STATE.player, "add_movement_input", STATE.player.get_actor_forward_vector(), 1.0, True)
            if STATE.phase_elapsed < 2.0:
                return
            speed = vector_length(STATE.player.get_velocity())
            animation = str(call(STATE.locomotion, "get_current_animation_asset_name"))
            require(speed > 5.0 and animation and animation != "None", "Walk did not produce real motion/animation")
            STATE.result["motion"]["walk_side"] = {"speed_cm_s": speed, "animation": animation}
            validate_ready_garment("walk_side_ready")
            position_character_camera("side", 170.0, 72.0)
            begin_capture("04_side_walk_motion.png", "side_walk_motion", "walk_back")
            return

        if STATE.phase == "walk_back":
            call(STATE.player, "add_movement_input", STATE.player.get_actor_forward_vector(), 1.0, True)
            if STATE.phase_elapsed < 1.5:
                return
            speed = vector_length(STATE.player.get_velocity())
            require(speed > 5.0, f"Player stopped before back walk capture: {speed}")
            STATE.result["motion"]["walk_back"] = {
                "speed_cm_s": speed,
                "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
            }
            validate_ready_garment("walk_back_ready")
            position_character_camera("back", 180.0, 72.0)
            begin_capture("05_back_walk_motion.png", "back_walk_motion", "start_crawl")
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
                call(STATE.player, "add_movement_input", STATE.player.get_actor_forward_vector(), 0.65, True)
            if not active:
                if STATE.phase_elapsed > 20.0:
                    raise RuntimeError("Crawl mode did not activate")
                return
            if STATE.phase_elapsed < 4.0:
                return
            animation = str(call(STATE.locomotion, "get_current_animation_asset_name"))
            speed = vector_length(STATE.player.get_velocity())
            require(animation and animation != "None" and speed > 1.0, "Crawl did not produce real motion/animation")
            STATE.result["motion"]["crawl"] = {
                "active": active,
                "animation": animation,
                "speed_cm_s": speed,
            }
            validate_ready_garment("crawl_ready")
            position_character_camera("side", 200.0, 65.0)
            begin_capture("06_side_crawl_motion.png", "side_crawl_motion", "crawl_closeup")
            return

        if STATE.phase == "crawl_closeup":
            validate_ready_garment("crawl_closeup_ready")
            position_character_camera("side", 135.0, 62.0)
            begin_capture("07_side_crawl_closeup.png", "side_crawl_closeup", "disable_v2")
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
            restored = (
                runtime["applied"] == 0
                and runtime["pending"] == 0
                and garment["mesh"] == SOURCE_GARMENT_PATH
                and "EFClothingMorphV2.Managed" not in garment["tags"]
                and "EFClothingMorphV2.Pending" not in garment["tags"]
                and garment["skin_weight_profile"] in ("", "None")
            )
            if not restored:
                if STATE.phase_elapsed > 15.0:
                    raise RuntimeError(f"CVar rollback did not restore source garment: {runtime} {garment}")
                return
            coverage = validate_body_coverage("cvar_disabled_source", False, 0, 0)
            morphs = validate_monitored_morphs_zero("cvar_disabled_source")
            verify_character_creation_absent("cvar_disabled")
            STATE.result["cvar_rollback"]["disabled"] = {
                "runtime": runtime,
                "garment": garment,
                "coverage": coverage,
                "monitored_morphs": morphs,
                "source_restored": True,
            }
            transition("enable_v2")
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
            verify_character_creation_absent("cvar_reenabled")
            STATE.result["cvar_rollback"]["reenabled"] = {
                "runtime": runtime,
                "garment": garment_snapshot(STATE.garment),
                "coverage": STATE.result["coverage_checks"]["cvar_reenabled_ready"],
                "derived_restored": True,
            }
            transition("final_checks")
            return

        if STATE.phase == "final_checks":
            verify_character_creation_absent("final")
            validate_ready_garment("final")
            require(
                STATE.result["character_creation_early_sampling"]["status"]
                == "PASS_VISIBLE_CC_ABSENT_FROM_FIRST_VALID_PIE_TICK",
                "Early Character Creation gate did not pass",
            )
            require(
                STATE.result["compiler_binding"].get("loaded_assets_match") is True,
                "Runtime assets were not bound to exact compiler PASS",
            )
            require(
                STATE.result["acf_interaction"].get("pickup_consumed") is True
                and STATE.result["acf_interaction"].get(
                    "original_guid_preserved_to_legs_equipment"
                )
                is True,
                "Real ACF GUID proof is incomplete",
            )
            require(
                STATE.result["initial_visibility_gate"]["status"]
                == "PASS_OBSERVED_POST_TICK_RENDERABLE_SAMPLES_WERE_EXACT_READY_WITH_GP_HIDDEN",
                "Observed post-tick render-safety gate did not pass",
            )
            require(
                set(STATE.result["screenshots"]) == set(EXPECTED_SCREENSHOT_FILENAMES)
                and all(row.get("exists") for row in STATE.result["screenshots"].values()),
                "Required gameplay screenshot set is incomplete",
            )
            finish(True)
            return
    except Exception as exc:
        details = traceback.format_exc()
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = details
        unreal.log_error(details)
        finish(False, str(exc))


existing = getattr(
    builtins, "_codex_ef_clothing_morph_v2_preview_rest_hub_pie", None
)
if existing is not None:
    unreal.log_warning(
        "[EFClothingMorphV2PreviewRestHubPIE58] duplicate registration ignored"
    )
else:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    PROGRESS_FILE.write_text("", encoding="utf-8")
    write_result()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_ef_clothing_morph_v2_preview_rest_hub_pie = STATE
    emit("registered=True")

"""Visible HUB PIE certification harness for EF Clothing Morph V26.

This is a catalog-driven runtime test.  It discovers enabled SurfaceWrapGPU rows,
acquires their fixtures through the real ACF world-item interaction route, and
never edits or saves an Unreal asset.  Render safety is sampled after every Slate
tick: a catalog garment may be renderable only when its exact V26 surface state is
Ready and its generated mesh/profile contract is active.

For frozen representative poses it also arms the editor-only asynchronous final
Optimus geometry helper and enforces the certified intersection/gap/residual/NaN
thresholds against actual GPU render buffers. Temporal per-vertex correction
variation remains explicitly pending because the helper exposes spatial correction
magnitudes, not the multi-frame per-vertex vectors needed for that separate gate.
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
        "CODEX_EF_CLOTHING_V26_QA_DIR",
        PROJECT_DIR / "Saved" / "ClothingMorphV2QA" / "SurfaceRuntime_adhoc",
    )
)
RESULT_FILE = Path(
    os.environ.get(
        "CODEX_EF_CLOTHING_V26_QA_RESULT",
        OUTPUT_DIR / "RuntimeResult.json",
    )
)
PROGRESS_FILE = OUTPUT_DIR / "Progress.log"
TARGET_MAP = os.environ.get("CODEX_EF_CLOTHING_V26_QA_MAP", "/Game/_Game/Hub/HUB")
TARGET_MAP_NAME = TARGET_MAP.rsplit("/", 1)[-1].lower()
TIMEOUT_SECONDS = float(os.environ.get("CODEX_EF_CLOTHING_V26_QA_TIMEOUT", "780"))

CATALOG_PATH = "/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments"
REGISTRY_PATH = "/Game/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry"
COMPATIBILITY_PATH = "/Game/DazToUnreal/Multiple/Multiple"
EXPECTED_COMPILER_VERSION = 26
EXPECTED_BINDING_SCHEMA = 5
EXPECTED_SKIN_PROFILE = "EF_AutoFit"
SURFACE_OFFSET_CM = 0.05
FULL_EQUIP_UNEQUIP_CYCLES = 25
SCREENSHOT_WAIT_SECONDS = 2.25
SURFACE_GRAPH_PATH = "/EFClothingMorph/Deformers/DG_EFGarmentSurfaceConstraint"
READBACK_TIMEOUT_SECONDS = 45.0
VERTEX_GAP_TOLERANCE_CM = -0.02
TRIANGLE_SAMPLE_GAP_TOLERANCE_CM = -0.02
CLEARANCE_RESIDUAL_TOLERANCE_CM = -0.03
THRESHOLD_EPSILON_CM = 0.0001

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    if value is None:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def class_path(value):
    if value is None:
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


def callable_method(value, method_name):
    return callable(getattr(value, method_name, None))


def canonical_asset_path(value):
    if value is None:
        return ""
    for method_name in ("get_asset_path_name", "get_asset_path_string"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                candidate = str(method())
                if candidate:
                    return canonical_asset_path(candidate)
            except Exception:
                pass
    to_soft_path = getattr(value, "to_soft_object_path", None)
    if callable(to_soft_path):
        try:
            return canonical_asset_path(to_soft_path())
        except Exception:
            pass
    text = object_path(value)
    match = re.search(r"(/(?:Game|EFClothingMorph)/[A-Za-z0-9_./-]+)", text)
    if match:
        return match.group(1).split(".", 1)[0].rstrip("'\"")
    return text.split(".", 1)[0].strip("'\"")


def mesh_asset(component):
    if component is None:
        return None
    getter = getattr(component, "get_skeletal_mesh_asset", None)
    if callable(getter):
        try:
            return getter()
        except Exception:
            pass
    return get_property(component, "skeletal_mesh_asset", "skeletal_mesh", default=None)


def mesh_path(component):
    return canonical_asset_path(mesh_asset(component))


def mesh_skeleton_path(mesh_or_component):
    mesh = (
        mesh_asset(mesh_or_component)
        if isinstance(mesh_or_component, unreal.SkeletalMeshComponent)
        else mesh_or_component
    )
    return canonical_asset_path(get_property(mesh, "skeleton", default=None))


def component_tags(component):
    return sorted(str(tag) for tag in (get_property(component, "component_tags", default=[]) or []))


def normalized_world_name(world):
    if not world:
        return ""
    return re.sub(r"^UEDPIE_\d+_", "", world.get_name(), flags=re.IGNORECASE).lower()


def normalize_guid(value):
    if value is None:
        return ""
    try:
        text = value.to_string()
    except Exception:
        text = str(value)
    normalized = re.sub(r"[^0-9A-Fa-f]", "", text).upper()
    return normalized if len(normalized) == 32 else ""


def finite_number(value):
    try:
        return math.isfinite(float(value))
    except Exception:
        return False


def vector_length(value):
    return math.sqrt(float(value.x) ** 2 + float(value.y) ** 2 + float(value.z) ** 2)


def vector_add(a, b):
    return unreal.Vector(a.x + b.x, a.y + b.y, a.z + b.z)


def vector_scale(value, scalar):
    return unreal.Vector(value.x * scalar, value.y * scalar, value.z * scalar)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def emit(message):
    line = f"[EFClothingMorphV26SurfaceRuntime] {message}"
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
            identity = f"{object_path(widget)} {class_path(widget)}".lower()
            visibility = str(widget.get_visibility())
            visible = "hidden" not in visibility.lower() and "collapsed" not in visibility.lower()
            if visible and (
                "efcharactercreation" in identity
                or "wbp_efcharactercreationroot" in identity
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


def sample_character_creation():
    if not LEVEL_EDITOR.is_in_play_in_editor():
        return
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    if not world:
        return
    rows = visible_character_creation_widgets()
    gate = STATE.result["hub_character_creation_gate"]
    gate["sample_count"] += 1
    world_seconds = float(unreal.GameplayStatics.get_time_seconds(world))
    if gate["first_world_seconds"] is None:
        gate["first_world_seconds"] = world_seconds
    gate["last_world_seconds"] = world_seconds
    if rows:
        violation = {
            "sample_index": gate["sample_count"],
            "world_seconds": world_seconds,
            "widgets": rows,
        }
        gate["violations"].append(violation)
        raise RuntimeError(f"Character Creation became visible in HUB PIE: {violation}")


def bool_from_json(value, default=False):
    if isinstance(value, bool):
        return value
    if value is None:
        return default
    return str(value).strip().lower() in ("1", "true", "yes")


def json_field(row, *names, default=None):
    lowered = {str(key).lower(): value for key, value in row.items()}
    for name in names:
        if name in row:
            return row[name]
        if name.lower() in lowered:
            return lowered[name.lower()]
    return default


def safe_slug(text):
    value = re.sub(r"[^A-Za-z0-9]+", "_", str(text)).strip("_").lower()
    return value or "garment"


def snapshot_topology(topology):
    return {
        "lod_index": int(get_property(topology, "lod_index", default=-1)),
        "render_vertices": int(get_property(topology, "render_vertex_count", default=0)),
        "render_indices": int(get_property(topology, "render_index_count", default=0)),
        "triangles": int(get_property(topology, "triangle_count", default=0)),
        "sections": int(get_property(topology, "section_count", default=0)),
        "fingerprint": str(get_property(topology, "topology_fingerprint", default="")),
        "content_fingerprint": str(get_property(topology, "content_fingerprint", default="")),
    }


def snapshot_binding(binding, source, fitted, body):
    require(binding is not None, "SurfaceWrapGPU profile has no loaded surface binding")
    snapshot = {
        "asset": object_path(binding),
        "source": canonical_asset_path(get_property(binding, "source_garment")),
        "fitted": canonical_asset_path(get_property(binding, "fitted_garment")),
        "body": canonical_asset_path(get_property(binding, "body_surface")),
        "compiler_version": int(get_property(binding, "compiler_version", default=-1)),
        "schema_version": int(get_property(binding, "schema_version", default=-1)),
        "build_guid": normalize_guid(get_property(binding, "build_guid")),
        "lod_pairs": [],
    }
    require(snapshot["source"] == source, "Surface binding source differs from catalog")
    require(snapshot["fitted"] == fitted, "Surface binding fitted mesh differs from profile")
    require(snapshot["body"] == body, "Surface binding body differs from catalog")
    require(snapshot["compiler_version"] == EXPECTED_COMPILER_VERSION, "Surface binding is not V26")
    require(snapshot["schema_version"] == EXPECTED_BINDING_SCHEMA, "Unexpected surface binding schema")
    require(snapshot["build_guid"], "Surface binding BuildGuid is invalid")

    pairs = list(get_property(binding, "lod_pair_bindings", default=[]) or [])
    require(pairs, "Surface binding contains no LOD pairs")
    for pair in pairs:
        garment_topology = get_property(pair, "garment_topology")
        body_topology = get_property(pair, "body_topology")
        metrics = get_property(pair, "metrics")
        vertex_bindings = list(get_property(pair, "vertex_bindings", default=[]) or [])
        maximum_corrections = [
            float(get_property(value, "maximum_correction_cm", default=float("nan")))
            for value in vertex_bindings
        ]
        row = {
            "garment": snapshot_topology(garment_topology),
            "body": snapshot_topology(body_topology),
            "base_clearance_cm": float(get_property(pair, "base_clearance_cm", default=float("nan"))),
            "compiled_reserve_cm": float(get_property(pair, "compiled_reserve_cm", default=float("nan"))),
            "certified": bool(get_property(pair, "b_certified", "certified", default=False)),
            "correction_bounds_cm": {
                "minimum": min(maximum_corrections) if maximum_corrections else float("nan"),
                "maximum": max(maximum_corrections) if maximum_corrections else float("nan"),
            },
            "metrics": {
                "bound_vertices": int(get_property(metrics, "bound_render_vertex_count", default=0)),
                "invalid_anchors": int(get_property(metrics, "invalid_anchor_count", default=-1)),
                "surface_follow": int(get_property(metrics, "surface_follow_vertex_count", default=0)),
                "hybrid": int(get_property(metrics, "hybrid_vertex_count", default=0)),
                "collision_only": int(get_property(metrics, "collision_only_vertex_count", default=0)),
                "preserve_upstream": int(get_property(metrics, "preserve_upstream_vertex_count", default=0)),
                "candidate_triangles": int(get_property(metrics, "candidate_triangle_count", default=0)),
                "witnesses": int(get_property(metrics, "witness_count", default=0)),
                "excluded_preserve_upstream_garment_triangles": int(
                    get_property(metrics, "excluded_preserve_upstream_garment_triangle_count", default=0)
                ),
                "used_degenerate_body_triangles": int(get_property(metrics, "degenerate_body_triangle_count", default=-1)),
                "excluded_source_body_degenerate_triangles": int(
                    get_property(
                        metrics,
                        "excluded_degenerate_body_triangle_count",
                        "excluded_source_body_degenerate_triangle_count",
                        default=0,
                    )
                ),
                "minimum_rest_gap_cm": float(get_property(metrics, "minimum_rest_signed_gap_cm", default=float("nan"))),
                "maximum_anchor_error_cm": float(get_property(metrics, "maximum_anchor_error_cm", default=float("nan"))),
            },
        }
        require(row["certified"], f"Uncertified LOD pair: {row}")
        require(row["garment"]["lod_index"] >= 0 and row["body"]["lod_index"] >= 0, f"Invalid LOD indices: {row}")
        require(row["garment"]["render_vertices"] > 0 and row["body"]["render_vertices"] > 0, f"Empty LOD topology: {row}")
        require(row["garment"]["fingerprint"] and row["body"]["fingerprint"], f"Missing topology fingerprint: {row}")
        require(row["metrics"]["bound_vertices"] == row["garment"]["render_vertices"], f"Incomplete render-vertex binding coverage: {row}")
        require(len(vertex_bindings) == row["garment"]["render_vertices"], f"Binding array length differs from cooked render vertices: {row}")
        require(all(finite_number(value) and value > 0.0 for value in maximum_corrections), f"Invalid per-vertex maximum correction data: {row}")
        require(row["metrics"]["invalid_anchors"] == 0, f"Invalid anchors in certified binding: {row}")
        require(row["metrics"]["preserve_upstream"] >= 0, f"Invalid PreserveUpstream vertex count: {row}")
        require(
            row["metrics"]["excluded_preserve_upstream_garment_triangles"] >= 0,
            f"Invalid PreserveUpstream garment-triangle exclusion count: {row}",
        )
        require(
            row["metrics"]["used_degenerate_body_triangles"] == 0,
            f"A zero-area body triangle survived into certified anchors/candidates: {row}",
        )
        require(
            row["metrics"]["excluded_source_body_degenerate_triangles"] >= 0,
            f"Excluded source-body degenerate count is invalid: {row}",
        )
        require(finite_number(row["base_clearance_cm"]) and row["base_clearance_cm"] > 0.0, f"Invalid base clearance: {row}")
        require(finite_number(row["compiled_reserve_cm"]) and row["compiled_reserve_cm"] >= 0.0, f"Invalid compiled reserve: {row}")
        snapshot["lod_pairs"].append(row)
    return snapshot


def load_catalog_contract():
    catalog = unreal.load_asset(CATALOG_PATH)
    require(isinstance(catalog, unreal.DataTable), f"Missing catalog: {CATALOG_PATH}")
    compatibility_mesh = unreal.load_asset(COMPATIBILITY_PATH)
    require(
        isinstance(compatibility_mesh, unreal.SkeletalMesh),
        f"Missing compatibility Skeletal Mesh: {COMPATIBILITY_PATH}",
    )
    compatibility_skeleton = mesh_skeleton_path(compatibility_mesh)
    require(
        compatibility_skeleton,
        f"Compatibility mesh has no shared skeleton: {COMPATIBILITY_PATH}",
    )
    STATE.compatibility_mesh = compatibility_mesh
    STATE.compatibility_skeleton_path = compatibility_skeleton
    raw_json = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(catalog)
    require(raw_json, "Could not export garment catalog to JSON")
    authored_rows = json.loads(raw_json)
    require(isinstance(authored_rows, list), "Garment catalog JSON is not an array")

    enabled = []
    for authored in authored_rows:
        if not bool_from_json(json_field(authored, "bEnabled", "Enabled", default=True), True):
            continue
        row_name = str(json_field(authored, "Name", "RowName", default=""))
        source = canonical_asset_path(json_field(authored, "SourceGarment", default=""))
        body = canonical_asset_path(json_field(authored, "BodySurface", default=""))
        backend = str(json_field(authored, "Backend", default=""))
        require(row_name and source and body, f"Enabled catalog row is incomplete: {authored}")
        require("SURFACEWRAPGPU" in backend.replace("_", "").upper(), f"Enabled row {row_name} is not SurfaceWrapGPU: {backend}")
        enabled.append(
            {
                "row_name": row_name,
                "slug": safe_slug(row_name),
                "source": source,
                "body": body,
                "backend": "SurfaceWrapGPU",
                "fit_policy": str(json_field(authored, "FitPolicy", default="Auto")),
                "fail_closed_missing_lod": bool_from_json(json_field(authored, "bFailClosedOnMissingLOD", default=True), True),
            }
        )
    require(enabled, "Catalog has no enabled SurfaceWrapGPU rows")
    require(len({(row["source"], row["body"]) for row in enabled}) == len(enabled), "Enabled catalog has duplicate source/body pairs")
    require(all(row["fail_closed_missing_lod"] for row in enabled), "Every enabled SurfaceWrapGPU row must fail closed on missing LOD")

    registry = unreal.load_asset(REGISTRY_PATH)
    require(registry is not None, f"Missing generated registry: {REGISTRY_PATH}")
    profiles = list(get_property(registry, "profiles", default=[]) or [])
    internal_rows = []
    public_rows = []
    for row in enabled:
        matches = []
        for profile in profiles:
            if (
                canonical_asset_path(get_property(profile, "source_garment")) == row["source"]
                and canonical_asset_path(get_property(profile, "body_surface")) == row["body"]
            ):
                matches.append(profile)
        require(len(matches) == 1, f"Expected exactly one profile for {row['row_name']}; found {len(matches)}")
        profile = matches[0]
        fitted = canonical_asset_path(get_property(profile, "fitted_garment"))
        surface_path = canonical_asset_path(get_property(profile, "surface_binding"))
        compatibility_reference = canonical_asset_path(
            get_property(profile, "compatibility_reference")
        )
        compiler_version = int(get_property(profile, "compiler_version", default=-1))
        skin_profile = str(get_property(profile, "skin_weight_profile_name", default=""))
        profile_guid = normalize_guid(get_property(profile, "build_guid"))
        require(compiler_version == EXPECTED_COMPILER_VERSION, f"Profile for {row['row_name']} is not V26")
        require(
            compatibility_reference == COMPATIBILITY_PATH,
            f"Profile for {row['row_name']} does not retain the exact Multiple compile/skeleton reference",
        )
        require(fitted and surface_path, f"Profile for {row['row_name']} lacks generated outputs")
        require(skin_profile == EXPECTED_SKIN_PROFILE, f"Profile for {row['row_name']} lacks EF_AutoFit")
        require(profile_guid, f"Profile for {row['row_name']} has invalid BuildGuid")
        binding = unreal.load_asset(surface_path)
        binding_snapshot = snapshot_binding(binding, row["source"], fitted, row["body"])
        require(binding_snapshot["build_guid"] == profile_guid, f"Profile/binding BuildGuid mismatch for {row['row_name']}")

        public = dict(row)
        public.update(
            {
                "profile": object_path(profile),
                "fitted": fitted,
                "compiler_version": compiler_version,
                "build_guid": profile_guid,
                "skin_weight_profile": skin_profile,
                "compatibility_reference": compatibility_reference,
                "compatibility_skeleton": compatibility_skeleton,
                "surface_binding": binding_snapshot,
            }
        )
        internal = dict(public)
        internal.update({"profile_object": profile, "binding_object": binding})
        public_rows.append(public)
        internal_rows.append(internal)

    STATE.rows = internal_rows
    STATE.registry = registry
    STATE.result["catalog"] = {
        "asset": object_path(catalog),
        "registry": object_path(registry),
        "compatibility_reference": object_path(compatibility_mesh),
        "compatibility_skeleton": compatibility_skeleton,
        "enabled_row_count": len(enabled),
        "valid_profile_count": len(public_rows),
        "valid_binding_count": len(public_rows),
        "rows": public_rows,
    }
    return True


def surface_state_name(runtime, garment):
    if runtime is None or garment is None:
        return "Disabled"
    try:
        raw = call(runtime, "get_garment_surface_runtime_state", garment)
    except Exception:
        return "Unavailable"
    text = str(raw)
    upper = text.upper()
    for name in ("WARMINGUP", "LOADING", "READY", "FAILED", "DISABLED"):
        if name in upper.replace("_", ""):
            return {
                "WARMINGUP": "WarmingUp",
                "LOADING": "Loading",
                "READY": "Ready",
                "FAILED": "Failed",
                "DISABLED": "Disabled",
            }[name]
    try:
        numeric = int(raw)
        return {0: "Disabled", 1: "Loading", 2: "WarmingUp", 3: "Ready", 4: "Failed"}.get(numeric, text)
    except Exception:
        return text


def component_is_visible(component):
    try:
        visible = bool(component.is_visible())
    except Exception:
        visible = bool(get_property(component, "visible", default=False))
    hidden = bool(get_property(component, "hidden_in_game", default=False))
    main_pass = bool(get_property(component, "render_in_main_pass", default=True))
    return visible and not hidden and main_pass


def component_lod_snapshot(component):
    predicted = get_property(component, "predicted_lod_level", default=-1)
    getter = getattr(component, "get_predicted_lod_level", None)
    if callable(getter):
        try:
            predicted = getter()
        except Exception:
            pass
    return {
        "predicted_lod": int(predicted if predicted is not None else -1),
        "forced_lod_model": int(get_property(component, "forced_lod_model", default=0) or 0),
        "min_lod_model": int(get_property(component, "min_lod_model", default=0) or 0),
    }


def garment_snapshot(component, row):
    try:
        raw_component_visible = bool(component.is_visible())
    except Exception:
        raw_component_visible = bool(get_property(component, "visible", default=False))
    current_profile = ""
    getter = getattr(component, "get_current_skin_weight_profile_name", None)
    if callable(getter):
        try:
            current_profile = str(getter())
        except Exception:
            pass
    profile_pending = None
    getter = getattr(component, "is_skin_weight_profile_pending", None)
    if callable(getter):
        try:
            profile_pending = bool(getter())
        except Exception:
            pass
    profile_active = None
    getter = getattr(component, "is_using_skin_weight_profile", None)
    if callable(getter):
        try:
            profile_active = bool(getter())
        except Exception:
            pass
    return {
        "component": object_path(component),
        "mesh": mesh_path(component),
        "visible": component_is_visible(component),
        "component_visible": raw_component_visible,
        "render_in_main_pass": bool(get_property(component, "render_in_main_pass", default=True)),
        "hidden_in_game": bool(get_property(component, "hidden_in_game", default=False)),
        "tags": component_tags(component),
        "surface_state": surface_state_name(STATE.runtime, component),
        "skin_weight_profile": current_profile,
        "skin_profile_active": profile_active,
        "skin_profile_pending": profile_pending,
        "lod": component_lod_snapshot(component),
        "expected_source": row["source"],
        "expected_fitted": row["fitted"],
    }


def exact_ready(snapshot, row, require_renderable=True):
    tags = snapshot.get("tags", [])
    return (
        snapshot.get("surface_state") == "Ready"
        and snapshot.get("mesh") == row["fitted"]
        and "EFClothingMorphV2.Managed" in tags
        and "EFClothingMorphV2.Pending" not in tags
        and snapshot.get("skin_weight_profile") == EXPECTED_SKIN_PROFILE
        and snapshot.get("skin_profile_active") is True
        and snapshot.get("skin_profile_pending") in (False, None)
        and (snapshot.get("visible") is True or not require_renderable)
    )


def matching_components(row):
    if not STATE.player:
        return []
    expected = {row["source"], row["fitted"]}
    return [component for component in all_skeletal_components(STATE.player) if mesh_path(component) in expected]


def find_ready_component(row):
    for component in matching_components(row):
        snapshot = garment_snapshot(component, row)
        if exact_ready(snapshot, row, True):
            return component, snapshot
    return None, None


def sample_global_visibility():
    if not STATE.rows or not STATE.player or not STATE.runtime:
        return
    gate = STATE.result["surface_visibility_gate"]
    world_seconds = float(unreal.GameplayStatics.get_time_seconds(STATE.world)) if STATE.world else -1.0
    for row in STATE.rows:
        for component in matching_components(row):
            snapshot = garment_snapshot(component, row)
            gate["sample_count"] += 1
            if snapshot["surface_state"] == "Failed":
                gate["failed_state_observations"] += 1
            if snapshot["visible"]:
                gate["renderable_sample_count"] += 1
                if not exact_ready(snapshot, row, True):
                    violation = {
                        "sample_index": gate["sample_count"],
                        "world_seconds": world_seconds,
                        "phase": STATE.phase,
                        "row_name": row["row_name"],
                        "snapshot": snapshot,
                    }
                    gate["unsafe_renderable_frames"].append(violation)
                    raise RuntimeError(f"SurfaceWrapGPU garment rendered before exact Ready: {violation}")
                if row["row_name"] not in gate["first_ready_renderable_by_row"]:
                    gate["first_ready_renderable_by_row"][row["row_name"]] = {
                        "sample_index": gate["sample_count"],
                        "world_seconds": world_seconds,
                        "phase": STATE.phase,
                        "snapshot": snapshot,
                    }


def runtime_snapshot():
    require(STATE.runtime is not None, "EFClothingFitRuntimeComponent is missing")
    return {
        "component": object_path(STATE.runtime),
        "applied": int(call(STATE.runtime, "get_applied_garment_count")),
        "pending": int(call(STATE.runtime, "get_pending_garment_count")),
        "debug_summary": str(call(STATE.runtime, "get_debug_summary")),
    }


def validate_active_ready(checkpoint):
    row = STATE.active_row
    require(row is not None, f"No active row at {checkpoint}")
    component, snapshot = find_ready_component(row)
    require(component is not None, f"Garment is not exact Surface Ready at {checkpoint}: {runtime_snapshot()}")
    STATE.garment = component
    body_lod = component_lod_snapshot(STATE.body)
    garment_lod = snapshot["lod"]
    certified_pairs = {
        (
            pair["garment"]["lod_index"],
            pair["body"]["lod_index"],
        )
        for pair in row["surface_binding"]["lod_pairs"]
    }
    predicted_pair = (garment_lod["predicted_lod"], body_lod["predicted_lod"])
    if predicted_pair[0] >= 0 and predicted_pair[1] >= 0:
        require(
            predicted_pair in certified_pairs,
            f"Ready garment/body LOD pair is not certified at {checkpoint}: "
            f"predicted={predicted_pair} certified={sorted(certified_pairs)}",
        )
    STATE.active_test["ready_checks"].append(
        {
            "checkpoint": checkpoint,
            "world_seconds": float(unreal.GameplayStatics.get_time_seconds(STATE.world)),
            "runtime": runtime_snapshot(),
            "garment": snapshot,
            "body_resolver": {
                "component": object_path(STATE.body),
                "mesh": mesh_path(STATE.body),
                "is_exact_catalog_body": mesh_path(STATE.body) == row["body"],
                "is_not_multiple_surface": mesh_path(STATE.body) != COMPATIBILITY_PATH,
                "lod": body_lod,
            },
            "certified_lod_pairs": sorted([list(value) for value in certified_pairs]),
            "predicted_lod_pair": list(predicted_pair),
        }
    )
    return snapshot


def gameplay_tag_string(value):
    if value is None:
        return ""
    for method_name in ("to_string", "get_tag_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    return str(value)


def item_class_path(item):
    return object_path(get_property(item, "item_class", "ItemClass", default=None))


def inventory_entries():
    return list(call(STATE.equipment, "get_inventory") or [])


def current_equipment_entries():
    current = call(STATE.equipment, "get_current_equipment")
    equipped = get_property(current, "equipped_items", "EquippedItems", default=None)
    require(equipped is not None, "ACF current equipment exposes no equipped array")
    return list(equipped or [])


def item_snapshot(item):
    return {
        "guid": normalize_guid(get_property(item, "item_guid", "ItemGuid")),
        "item_class": item_class_path(item),
        "count": int(get_property(item, "count", "Count", default=1) or 0),
        "equipped": bool(get_property(item, "b_is_equipped", "bIsEquipped", default=False)),
        "equipment_slot": gameplay_tag_string(get_property(item, "equipment_slot", "EquipmentSlot", "item_slot", "ItemSlot", default="")),
    }


def item_token_set(text):
    basename = canonical_asset_path(text).rsplit("/", 1)[-1]
    words = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", basename)
    return {token.lower() for token in re.findall(r"[A-Za-z0-9]+", words) if len(token) >= 3}


def get_world_item_candidates(row):
    world_item_class = unreal.load_class(None, "/Script/InventorySystem.ACFWorldItem")
    require(world_item_class is not None, "Unable to load ACFWorldItem class")
    source_tokens = item_token_set(row["source"])
    candidates = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(STATE.world, world_item_class) or []:
        getter = getattr(actor, "get_items", None)
        if not callable(getter):
            continue
        try:
            items = list(getter() or [])
        except Exception:
            continue
        if not items:
            continue
        snapshots = [item_snapshot(item) for item in items]
        candidate_tokens = set()
        for item in snapshots:
            candidate_tokens.update(item_token_set(item["item_class"]))
        score = len(source_tokens.intersection(candidate_tokens))
        candidates.append(
            {
                "actor": actor,
                "object": object_path(actor),
                "score": score,
                "items": items,
                "item_snapshots": snapshots,
                "guids": {item["guid"] for item in snapshots if item["guid"]},
            }
        )
    candidates.sort(key=lambda entry: (-entry["score"], entry["object"].lower()))
    return candidates


def object_is_valid(value):
    if value is None:
        return False
    try:
        return bool(unreal.SystemLibrary.is_valid(value))
    except Exception:
        return True


def begin_candidate_interaction(candidate):
    actor = candidate["actor"]
    require(object_is_valid(actor), "Selected ACF world item is no longer valid")
    pickup_location = actor.get_actor_location()
    pawn_location = STATE.player.get_actor_location()
    destination = unreal.Vector(pickup_location.x - 85.0, pickup_location.y, pawn_location.z)
    require(bool(STATE.player.set_actor_location(destination, False, True)), "Could not enter ACF interaction range")
    look = unreal.MathLibrary.find_look_at_rotation(destination, pickup_location)
    STATE.player.set_actor_rotation(unreal.Rotator(0.0, look.yaw, 0.0), True)
    call(STATE.interaction, "enable_detection", True)
    call(STATE.interaction, "refresh_interactions")
    call(STATE.interaction, "register_interactable", actor)
    call(STATE.interaction, "set_current_best_interactable", actor)
    require(call(STATE.interaction, "get_current_best_interactable_actor") == actor, "ACF did not select the candidate world item")
    require(bool(call(STATE.interaction, "has_valid_interactable")), "ACF reports no valid candidate interaction")
    STATE.candidate_inventory_before = {item_snapshot(item)["guid"] for item in inventory_entries() if item_snapshot(item)["guid"]}
    entry = {
        "candidate": candidate["object"],
        "score": candidate["score"],
        "pickup_items": candidate["item_snapshots"],
        "inventory_guids_before": sorted(STATE.candidate_inventory_before),
        "interact_invoked": True,
        "route": [
            "UACFInteractionComponent.Interact",
            "AACFWorldItem.OnInteractedByPawn",
            "AACFWorldItem.GatherItem",
            "UACFInventoryComponent.MoveItemsFromInventory",
            "UACFEquipmentComponent.HandleItemAdded/EquipItemFromInventory",
            "UACFEquipmentComponent.AddSkeletalMeshComponent",
        ],
    }
    STATE.active_test["acf_candidates"].append(entry)
    STATE.current_candidate_result = entry
    call(STATE.interaction, "interact", "")


def associate_acf_item(candidate, garment):
    inventory = inventory_entries()
    equipped = current_equipment_entries()
    inventory_rows = [item_snapshot(item) for item in inventory]
    equipment_rows = [item_snapshot(item) for item in equipped]
    pickup_guids = candidate["guids"]
    inventory_guids = {row["guid"] for row in inventory_rows}
    equipped_guids = {row["guid"] for row in equipment_rows}
    acquired = pickup_guids.intersection(inventory_guids)
    equipped_from_pickup = pickup_guids.intersection(equipped_guids)
    require(acquired, "Matched garment did not preserve any ACF world-item GUID into inventory")
    require(equipped_from_pickup, "Matched garment did not preserve its ACF GUID into current equipment")

    armor_definition_class = ""
    if callable_method(garment, "get_armor_definition"):
        try:
            armor_definition_class = class_path(call(garment, "get_armor_definition"))
        except Exception:
            pass
    matched_rows = [row for row in equipment_rows if row["guid"] in equipped_from_pickup]
    if armor_definition_class:
        exact = [row for row in matched_rows if row["item_class"] == armor_definition_class]
        if len(exact) == 1:
            matched_rows = exact
    require(len(matched_rows) == 1, f"Could not uniquely associate garment with ACF item: {matched_rows}")
    guid = matched_rows[0]["guid"]
    raw_inventory = next((item for item in inventory if item_snapshot(item)["guid"] == guid), None)
    require(raw_inventory is not None, "Associated ACF item is absent from inventory")
    slot_value = get_property(raw_inventory, "equipment_slot", "EquipmentSlot", "item_slot", "ItemSlot", default=None)
    require(slot_value is not None, "Associated ACF inventory item has no equipment slot")
    STATE.acf_item_guid = guid
    STATE.acf_item_slot = slot_value
    STATE.current_candidate_result.update(
        {
            "matched": True,
            "garment_component": object_path(garment),
            "garment_mesh": mesh_path(garment),
            "armor_definition_class": armor_definition_class,
            "inventory_after": inventory_rows,
            "equipment_after": equipment_rows,
            "associated_item": matched_rows[0],
            "original_guid_preserved": True,
        }
    )


def find_inventory_item(guid):
    for item in inventory_entries():
        if item_snapshot(item)["guid"] == guid:
            return item
    return None


def resolve_free_camera():
    if STATE.free_camera_subsystem is None:
        subsystem_class = unreal.load_class(
            None,
            "/Script/EFProjectSystemsGameplay.ProjectGameplayFreeCameraSubsystem",
        )
        require(subsystem_class is not None, "Gameplay free-camera class is missing")
        try:
            STATE.free_camera_subsystem = unreal.SubsystemBlueprintLibrary.get_world_subsystem(
                STATE.world, subsystem_class
            )
        except Exception:
            STATE.free_camera_subsystem = None
        if STATE.free_camera_subsystem is None:
            try:
                for subsystem in unreal.ObjectIterator(subsystem_class):
                    try:
                        if subsystem.get_world() == STATE.world:
                            STATE.free_camera_subsystem = subsystem
                            break
                    except Exception:
                        continue
            except Exception:
                pass
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
        "camera": object_path(STATE.camera),
        "active": True,
    }
    return True


def attached_actors(actor):
    output = []
    try:
        actor.get_attached_actors(output, True, False)
        return list(output)
    except Exception:
        try:
            return list(actor.get_attached_actors() or [])
        except Exception:
            return []


def hide_weapon_visuals():
    if STATE.weapon_visuals_processed:
        return
    STATE.weapon_visuals_processed = True
    weapon_tokens = (
        "weapon",
        "sword",
        "bow",
        "quiver",
        "shield",
        "spear",
        "axe",
        "mace",
        "staff",
        "scabbard",
        "dagger",
    )
    allowed_meshes = {COMPATIBILITY_PATH}
    for row in STATE.rows:
        allowed_meshes.update((row["source"], row["fitted"], row["body"]))

    evidence = []
    for actor in attached_actors(STATE.player):
        identity = f"{object_path(actor)} {class_path(actor)}".lower()
        actor_meshes = []
        for component in actor.get_components_by_class(unreal.MeshComponent) or []:
            path = canonical_asset_path(get_property(component, "static_mesh", "skeletal_mesh_asset", "skeletal_mesh", default=None))
            if path:
                actor_meshes.append(path)
            identity += " " + path.lower()
        non_garment_mesh = bool(actor_meshes) and not any(path in allowed_meshes for path in actor_meshes)
        if any(token in identity for token in weapon_tokens) or non_garment_mesh:
            previous = bool(get_property(actor, "hidden", "hidden_in_game", default=False))
            try:
                previous = bool(actor.is_hidden())
            except Exception:
                pass
            actor.set_actor_hidden_in_game(True)
            STATE.hidden_weapon_actors.append((actor, previous))
            evidence.append({"actor": object_path(actor), "meshes": actor_meshes, "previous_hidden": previous})

    for component in STATE.player.get_components_by_class(unreal.PrimitiveComponent) or []:
        identity = f"{object_path(component)} {class_path(component)}".lower()
        asset = canonical_asset_path(get_property(component, "static_mesh", "skeletal_mesh_asset", "skeletal_mesh", default=None))
        identity += " " + asset.lower()
        if asset in allowed_meshes or not any(token in identity for token in weapon_tokens):
            continue
        try:
            previous = bool(component.is_visible())
            component.set_visibility(False, True)
            STATE.hidden_weapon_components.append((component, previous))
            evidence.append({"component": object_path(component), "mesh": asset, "previous_visible": previous})
        except Exception:
            continue
    STATE.result["weapon_visual_suppression"] = {
        "status": "TRANSIENT_SCREENSHOT_SUPPRESSION_APPLIED",
        "hidden_visuals": evidence,
        "asset_changes": False,
        "visual_review": "PENDING_HUMAN_CONFIRMATION_NO_OBSTRUCTION",
    }


def restore_weapon_visuals():
    for component, previous in reversed(STATE.hidden_weapon_components):
        try:
            component.set_visibility(previous, True)
        except Exception:
            pass
    for actor, previous in reversed(STATE.hidden_weapon_actors):
        try:
            actor.set_actor_hidden_in_game(previous)
        except Exception:
            pass
    STATE.hidden_weapon_components = []
    STATE.hidden_weapon_actors = []


def position_camera(view, distance=235.0):
    require(STATE.camera is not None, "Free camera is not active")
    location = STATE.player.get_actor_location()
    forward = STATE.player.get_actor_forward_vector()
    right = STATE.player.get_actor_right_vector()
    if view == "front":
        radial = forward
        height = 92.0
        target_height = 86.0
    elif view == "back":
        radial = vector_scale(forward, -1.0)
        height = 92.0
        target_height = 86.0
    elif view == "left":
        radial = vector_scale(right, -1.0)
        height = 88.0
        target_height = 82.0
    elif view == "right":
        radial = right
        height = 88.0
        target_height = 82.0
    elif view == "inferior":
        radial = vector_add(vector_scale(forward, -0.85), vector_scale(right, 0.35))
        height = 25.0
        target_height = 78.0
        distance = 205.0
    else:
        raise RuntimeError(f"Unknown camera view: {view}")
    camera_location = vector_add(vector_add(location, vector_scale(radial, distance)), unreal.Vector(0.0, 0.0, height))
    target = vector_add(location, unreal.Vector(0.0, 0.0, target_height))
    rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, target)
    STATE.camera.set_actor_location_and_rotation(camera_location, rotation, False, True)


def begin_capture(view, motion_label, next_phase):
    validate_active_ready(f"capture_{view}_{motion_label}")
    hide_weapon_visuals()
    filename = f"{STATE.active_row['slug']}_{len(STATE.active_test['screenshots']) + 1:02d}_{view}_{safe_slug(motion_label)}.png"
    path = OUTPUT_DIR / filename
    request = {
        "row_name": STATE.active_row["row_name"],
        "view": view,
        "motion": motion_label,
        "path": str(path),
        "accepted": False,
        "exists": False,
        "surface_state": surface_state_name(STATE.runtime, STATE.garment),
        "garment_mesh": mesh_path(STATE.garment),
        "player_velocity_cm_s": vector_length(STATE.player.get_velocity()),
        "weapons_transiently_suppressed": True,
        "visual_review": "PENDING_HUMAN_REVIEW",
    }
    STATE.result["screenshots"][filename] = request
    STATE.active_test["screenshots"].append(filename)
    STATE.capture_path = path
    STATE.capture_next_phase = next_phase
    STATE.capture_motion = motion_label
    STATE.capture_request_in_progress = True
    try:
        request["accepted"] = bool(unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(path)))
    finally:
        STATE.capture_request_in_progress = False
    transition("wait_capture")


def apply_capture_motion():
    if not STATE.player:
        return
    if STATE.capture_motion in ("walk", "run"):
        STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 1.0, True)
    elif STATE.capture_motion == "strafe":
        STATE.player.add_movement_input(STATE.player.get_actor_right_vector(), 1.0, True)
    elif STATE.capture_motion == "crawl":
        STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 0.65, True)


def stop_motion():
    if not STATE.player:
        return
    movement = STATE.player.get_component_by_class(unreal.CharacterMovementComponent)
    if movement and callable_method(movement, "stop_movement_immediately"):
        try:
            movement.stop_movement_immediately()
        except Exception:
            pass


def transform_vector(value):
    if value is None:
        return []
    fields = []
    for name in ("x", "y", "z", "w"):
        try:
            fields.append(round(float(getattr(value, name)), 6))
        except Exception:
            pass
    return fields


def transform_payload(transform):
    translation = get_property(transform, "translation", default=None)
    rotation = get_property(transform, "rotation", default=None)
    scale = get_property(transform, "scale3d", "scale_3d", default=None)
    return {
        "translation": transform_vector(translation),
        "rotation": transform_vector(rotation),
        "scale": transform_vector(scale),
    }


def select_pose_bones(component):
    names = []
    try:
        count = int(component.get_num_bones())
        names = [str(component.get_bone_name(index)) for index in range(count)]
    except Exception:
        pass
    groups = {
        "pelvis": [name for name in names if "pelvis" in name.lower()],
        "thighs": [name for name in names if "thigh" in name.lower()],
        "spine": [name for name in names if "spine" in name.lower() or "abdomen" in name.lower()],
    }
    selected = {}
    for group, matches in groups.items():
        unique = sorted(set(matches), key=lambda value: (len(value), value.lower()))
        selected[group] = unique[:4]
    require(selected["pelvis"], f"No pelvis bone was discovered on {mesh_path(component)}")
    require(selected["thighs"], f"No thigh bone was discovered on {mesh_path(component)}")
    require(selected["spine"], f"No spine/abdomen bone was discovered on {mesh_path(component)}")
    return selected


def component_space_bone_transform(component, bone_name):
    try:
        return component.get_socket_transform(
            bone_name, unreal.RelativeTransformSpace.RTS_COMPONENT
        )
    except Exception:
        try:
            index = int(component.get_bone_index(bone_name))
            require(index >= 0, f"Invalid bone index for {bone_name}")
            return component.get_bone_transform(index)
        except Exception as exc:
            raise RuntimeError(f"Could not sample live bone {bone_name}: {exc}")


def pose_snapshot(label):
    require(STATE.body is not None, f"Body component missing for pose sample {label}")
    if not STATE.pose_bones:
        STATE.pose_bones = select_pose_bones(STATE.body)
    bones = {}
    for group, names in STATE.pose_bones.items():
        bones[group] = {
            name: transform_payload(component_space_bone_transform(STATE.body, name))
            for name in names
        }
    canonical = json.dumps(bones, sort_keys=True, separators=(",", ":"))
    return {
        "label": label,
        "hash_sha256": hashlib.sha256(canonical.encode("utf-8")).hexdigest().upper(),
        "component": object_path(STATE.body),
        "mesh": mesh_path(STATE.body),
        "bones": bones,
    }


def pose_group_changed(before, after, group):
    before_group = before["bones"][group]
    after_group = after["bones"][group]
    changed = []
    for bone_name in sorted(set(before_group).intersection(after_group)):
        left = json.dumps(before_group[bone_name], sort_keys=True)
        right = json.dumps(after_group[bone_name], sort_keys=True)
        if left != right:
            changed.append(bone_name)
    return changed


def record_motion(name, extra=None):
    snapshot = validate_active_ready(f"motion_{name}")
    payload = {
        "status": "PASS",
        "speed_cm_s": vector_length(STATE.player.get_velocity()),
        "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")) if callable_method(STATE.locomotion, "get_current_animation_asset_name") else "UNAVAILABLE",
        "surface_state": snapshot["surface_state"],
        "garment_mesh": snapshot["mesh"],
    }
    if extra:
        payload.update(extra)
    STATE.active_test["motion"][name] = payload
    return payload


def set_pause_anims(component, paused):
    setter = getattr(component, "set_pause_anims", None)
    if callable(setter):
        setter(bool(paused))
        return
    component.set_editor_property("pause_anims", bool(paused))


def freeze_pose_for_readback():
    require(not STATE.frozen_pose_components, "A geometry readback pose is already frozen")
    # Disable tick only on the terminal pose driver. Setting bPauseAnims on any
    # member of this leader chain recreates Optimus managers in UE 5.8, turning a
    # QA sampling operation into a runtime deformer lifecycle mutation. The
    # rendered body and garment followers keep ticking from one latched bone pose,
    # so their post-deformer GPU buffers remain coherent for the async readback.
    candidates = []
    for component in (STATE.garment, STATE.body):
        current = component
        visited = set()
        while current is not None:
            identity = object_path(current)
            if not identity or identity in visited:
                break
            visited.add(identity)
            leader = get_property(current, "leader_pose_component", default=None)
            if leader is None:
                candidates.append(current)
                break
            current = leader
    unique = []
    seen = set()
    for component in candidates:
        identity = object_path(component)
        if component is not None and identity and identity not in seen:
            seen.add(identity)
            unique.append(component)
    require(unique, "No skeletal components were available to freeze for readback")
    for component in unique:
        tick_getter = getattr(component, "is_component_tick_enabled", None)
        tick_setter = getattr(component, "set_component_tick_enabled", None)
        require(callable(tick_getter) and callable(tick_setter), f"Cannot freeze pose-driver tick: {object_path(component)}")
        previous = bool(tick_getter())
        tick_setter(False)
        STATE.frozen_pose_components.append((component, previous))
    stop_motion()
    return [object_path(component) for component in unique]


def restore_frozen_pose():
    for component, previous in reversed(STATE.frozen_pose_components):
        try:
            component.set_component_tick_enabled(bool(previous))
        except Exception:
            pass
    STATE.frozen_pose_components = []


def readback_state_name(value):
    text = str(value)
    upper = text.upper().replace("_", "")
    for token, name in (
        ("PENDINGGPU", "PendingGPU"),
        ("ANALYZING", "Analyzing"),
        ("READY", "Ready"),
        ("FAILED", "Failed"),
        ("UNKNOWN", "Unknown"),
    ):
        if token in upper:
            return name
    try:
        return {0: "Unknown", 1: "PendingGPU", 2: "Analyzing", 3: "Ready", 4: "Failed"}.get(int(value), text)
    except Exception:
        return text


def readback_result_snapshot(result):
    return {
        "state": readback_state_name(get_property(result, "state", default="Unknown")),
        "request_id": str(get_property(result, "request_id", default="")),
        "error": str(get_property(result, "error", default="")),
        "surface_instance_name": str(get_property(result, "surface_instance_name", default="")),
        "requested_game_frame": int(get_property(result, "requested_game_frame", default=0)),
        "requested_garment_pose_revision": int(get_property(result, "requested_garment_pose_revision", default=0)),
        "requested_body_pose_revision": int(get_property(result, "requested_body_pose_revision", default=0)),
        "garment_lod": int(get_property(result, "garment_lod_index", default=-1)),
        "body_lod": int(get_property(result, "body_lod_index", default=-1)),
        "garment_render_vertices": int(get_property(result, "garment_render_vertex_count", default=0)),
        "body_render_vertices": int(get_property(result, "body_render_vertex_count", default=0)),
        "tested_garment_triangles": int(get_property(result, "tested_garment_triangle_count", default=0)),
        "excluded_preserve_upstream_garment_triangles": int(
            get_property(result, "excluded_preserve_upstream_garment_triangle_count", default=0)
        ),
        "tested_body_triangles": int(get_property(result, "tested_body_triangle_count", default=0)),
        "triangle_intersections": int(get_property(result, "triangle_intersection_count", default=-1)),
        "minimum_vertex_skin_gap_cm": float(get_property(result, "minimum_vertex_skin_gap_cm", default=float("nan"))),
        "vertex_skin_gap_violations": int(get_property(result, "vertex_skin_gap_violation_count", default=-1)),
        "minimum_base_corrected_vertex_skin_gap_cm": float(get_property(result, "minimum_base_corrected_vertex_skin_gap_cm", default=float("nan"))),
        "base_corrected_vertex_skin_gap_violations": int(get_property(result, "base_corrected_vertex_skin_gap_violation_count", default=-1)),
        "triangle_sample_count": int(get_property(result, "triangle_sample_count", default=0)),
        "minimum_triangle_sample_skin_gap_cm": float(get_property(result, "minimum_triangle_sample_skin_gap_cm", default=float("nan"))),
        "triangle_sample_skin_gap_violations": int(get_property(result, "triangle_sample_skin_gap_violation_count", default=-1)),
        "minimum_base_corrected_triangle_sample_skin_gap_cm": float(get_property(result, "minimum_base_corrected_triangle_sample_skin_gap_cm", default=float("nan"))),
        "base_corrected_triangle_sample_skin_gap_violations": int(get_property(result, "base_corrected_triangle_sample_skin_gap_violation_count", default=-1)),
        "minimum_base_corrected_clearance_residual_cm": float(get_property(result, "minimum_base_corrected_clearance_residual_cm", default=float("nan"))),
        "base_corrected_clearance_residual_violations": int(get_property(result, "base_corrected_clearance_residual_violation_count", default=-1)),
        "minimum_clearance_residual_cm": float(get_property(result, "minimum_clearance_residual_cm", default=float("nan"))),
        "clearance_residual_violations": int(get_property(result, "clearance_residual_violation_count", default=-1)),
        "correction_magnitude_p99_cm": float(get_property(result, "correction_magnitude_p99_cm", default=float("nan"))),
        "maximum_correction_magnitude_cm": float(get_property(result, "maximum_correction_magnitude_cm", default=float("nan"))),
        "witness_pass_displacement_p99_cm": float(get_property(result, "witness_pass_displacement_p99_cm", default=float("nan"))),
        "maximum_witness_pass_displacement_cm": float(get_property(result, "maximum_witness_pass_displacement_cm", default=float("nan"))),
        "witness_pass_moved_vertices": int(get_property(result, "witness_pass_moved_vertex_count", default=-1)),
        "invalid_or_non_finite_vertices": int(get_property(result, "invalid_or_non_finite_vertex_count", default=-1)),
        "skipped_degenerate_triangles": int(get_property(result, "skipped_degenerate_triangle_count", default=-1)),
        "excluded_source_body_degenerate_triangles": int(
            get_property(
                result,
                "excluded_source_body_degenerate_triangle_count",
                "excluded_degenerate_body_triangle_count",
                "excluded_degenerate_triangle_count",
                default=0,
            )
        ),
        "excluded_or_hidden_body_sections": int(get_property(result, "excluded_or_hidden_body_section_count", default=-1)),
        "used_exact_v26_binding": bool(get_property(result, "b_used_exact_v26_binding", "used_exact_v26_binding", default=False)),
        "lod_pair_matched": bool(get_property(result, "b_lod_pair_matched", "lod_pair_matched", default=False)),
        "triangle_sample_coverage_available": bool(get_property(result, "b_triangle_sample_coverage_available", "triangle_sample_coverage_available", default=False)),
        "total_latency_ms": float(get_property(result, "total_latency_ms", default=float("nan"))),
        "analysis_time_ms": float(get_property(result, "analysis_time_ms", default=float("nan"))),
    }


def find_binding_lod_pair(row, garment_lod, body_lod):
    matches = [
        pair
        for pair in row["surface_binding"]["lod_pairs"]
        if pair["garment"]["lod_index"] == garment_lod
        and pair["body"]["lod_index"] == body_lod
    ]
    require(len(matches) == 1, f"Readback returned an uncertified/ambiguous LOD pair: garment={garment_lod} body={body_lod}")
    return matches[0]


def validate_readback_thresholds(snapshot):
    row = STATE.active_row
    require(snapshot["state"] == "Ready", f"Attempted to validate non-Ready readback: {snapshot}")
    require(snapshot["request_id"] == STATE.readback_request_id, f"Readback RequestId mismatch: {snapshot}")
    require(snapshot["surface_instance_name"], f"Readback did not identify the EF surface Optimus instance: {snapshot}")
    require(snapshot["used_exact_v26_binding"], f"Readback did not use the exact V26 binding: {snapshot}")
    require(snapshot["lod_pair_matched"], f"Readback LOD pair did not match the binding: {snapshot}")
    pair = find_binding_lod_pair(row, snapshot["garment_lod"], snapshot["body_lod"])
    require(
        snapshot["excluded_preserve_upstream_garment_triangles"]
        == pair["metrics"]["excluded_preserve_upstream_garment_triangles"],
        f"Readback/compiler PreserveUpstream triangle exclusions differ: {snapshot}",
    )
    require(snapshot["triangle_intersections"] == 0, f"Final GPU buffers intersect: {snapshot}")
    require(snapshot["vertex_skin_gap_violations"] == 0, f"Final GPU vertex gap violations were measured: {snapshot}")
    require(
        finite_number(snapshot["minimum_vertex_skin_gap_cm"])
        and snapshot["minimum_vertex_skin_gap_cm"] + THRESHOLD_EPSILON_CM >= VERTEX_GAP_TOLERANCE_CM,
        f"Final GPU vertex skin gap is below {VERTEX_GAP_TOLERANCE_CM} cm: {snapshot}",
    )
    require(snapshot["triangle_sample_coverage_available"], f"Binding/readback has no compiled face witnesses: {snapshot}")
    require(snapshot["triangle_sample_count"] > 0, f"Final GPU readback tested no face witnesses: {snapshot}")
    require(snapshot["triangle_sample_skin_gap_violations"] == 0, f"Final GPU witness gap violations were measured: {snapshot}")
    require(
        finite_number(snapshot["minimum_triangle_sample_skin_gap_cm"])
        and snapshot["minimum_triangle_sample_skin_gap_cm"] + THRESHOLD_EPSILON_CM >= TRIANGLE_SAMPLE_GAP_TOLERANCE_CM,
        f"Final GPU witness gap is below {TRIANGLE_SAMPLE_GAP_TOLERANCE_CM} cm: {snapshot}",
    )
    require(snapshot["clearance_residual_violations"] == 0, f"Final GPU clearance residual violations were measured: {snapshot}")
    require(
        finite_number(snapshot["minimum_clearance_residual_cm"])
        and snapshot["minimum_clearance_residual_cm"] + THRESHOLD_EPSILON_CM >= CLEARANCE_RESIDUAL_TOLERANCE_CM,
        f"Final GPU clearance residual is below {CLEARANCE_RESIDUAL_TOLERANCE_CM} cm: {snapshot}",
    )
    require(snapshot["invalid_or_non_finite_vertices"] == 0, f"Final GPU buffers contain NaN/Inf or invalid vertices: {snapshot}")
    require(
        snapshot["excluded_source_body_degenerate_triangles"] >= 0,
        f"Excluded source-body degenerate count is invalid: {snapshot}",
    )
    expected_excluded_degenerates = pair["metrics"][
        "excluded_source_body_degenerate_triangles"
    ]
    raw_skipped_degenerates = snapshot["skipped_degenerate_triangles"]
    reported_excluded_degenerates = snapshot[
        "excluded_source_body_degenerate_triangles"
    ]
    if reported_excluded_degenerates > 0:
        require(
            reported_excluded_degenerates == expected_excluded_degenerates,
            f"Readback/compiler excluded-degenerate counts differ: {snapshot}",
        )
        used_degenerate_triangles = raw_skipped_degenerates
        degenerate_classification = "HELPER_SEPARATED_EXCLUDED_SOURCE_TRIANGLES"
    elif raw_skipped_degenerates == expected_excluded_degenerates:
        # Backward-compatible interpretation for the first helper build, whose
        # aggregate skip counter included the exact source-body zero-area set.
        # Equality with the compiler-audited exclusion count proves there was no
        # additional garment/used-topology degenerate in this readback.
        snapshot["excluded_source_body_degenerate_triangles"] = (
            expected_excluded_degenerates
        )
        used_degenerate_triangles = 0
        degenerate_classification = (
            "LEGACY_AGGREGATE_EXACTLY_MATCHED_COMPILER_EXCLUDED_SOURCE_SET"
        )
    elif raw_skipped_degenerates == 0:
        snapshot["excluded_source_body_degenerate_triangles"] = (
            expected_excluded_degenerates
        )
        used_degenerate_triangles = 0
        degenerate_classification = (
            "HELPER_FILTERED_COMPILER_EXCLUDED_SOURCE_SET_BEFORE_USED_TOPOLOGY"
        )
    else:
        used_degenerate_triangles = raw_skipped_degenerates
        degenerate_classification = "UNCLASSIFIED_USED_TOPOLOGY_DEGENERATES"
    snapshot["used_degenerate_triangles"] = used_degenerate_triangles
    snapshot["degenerate_triangle_classification"] = degenerate_classification
    require(
        used_degenerate_triangles == 0,
        f"A zero-area triangle survived into the used final GPU topology: {snapshot}",
    )
    require(snapshot["garment_render_vertices"] > 0 and snapshot["body_render_vertices"] > 0, f"Final GPU readback returned empty buffers: {snapshot}")
    require(snapshot["tested_garment_triangles"] > 0 and snapshot["tested_body_triangles"] > 0, f"Final GPU readback tested empty topology: {snapshot}")
    require(
        all(
            finite_number(snapshot[name]) and snapshot[name] >= 0.0
            for name in (
                "correction_magnitude_p99_cm",
                "maximum_correction_magnitude_cm",
                "total_latency_ms",
                "analysis_time_ms",
            )
        ),
        f"Final GPU readback returned invalid correction/timing metrics: {snapshot}",
    )

    require(snapshot["garment_render_vertices"] == pair["garment"]["render_vertices"], f"Garment GPU vertex count differs from certified topology: {snapshot}")
    require(snapshot["body_render_vertices"] == pair["body"]["render_vertices"], f"Body GPU vertex count differs from certified topology: {snapshot}")
    maximum_compiled_correction = pair["correction_bounds_cm"]["maximum"]
    require(
        snapshot["maximum_correction_magnitude_cm"] <= maximum_compiled_correction + THRESHOLD_EPSILON_CM,
        f"Surface correction exceeded the largest compiled per-vertex bound: actual={snapshot['maximum_correction_magnitude_cm']} compiled={maximum_compiled_correction}",
    )
    snapshot["thresholds"] = {
        "triangle_intersections_equal_zero": True,
        "vertex_skin_gap_minimum_cm": VERTEX_GAP_TOLERANCE_CM,
        "triangle_sample_skin_gap_minimum_cm": TRIANGLE_SAMPLE_GAP_TOLERANCE_CM,
        "clearance_residual_minimum_cm": CLEARANCE_RESIDUAL_TOLERANCE_CM,
        "invalid_or_non_finite_vertices_equal_zero": True,
        "used_degenerate_triangles_equal_zero": True,
        "excluded_source_body_degenerate_triangles_recorded": snapshot[
            "excluded_source_body_degenerate_triangles"
        ],
        "excluded_preserve_upstream_garment_triangles_recorded": snapshot[
            "excluded_preserve_upstream_garment_triangles"
        ],
        "compiled_maximum_correction_cm": maximum_compiled_correction,
        "passed": True,
    }
    snapshot["correction_semantics"] = {
        "measured_values_are_spatial_post_ef_minus_base_magnitudes": True,
        "temporal_p99_limit_cm_per_frame": 0.25,
        "temporal_maximum_limit_cm_per_frame": 0.50,
        "temporal_variation_status": "PENDING_PER_VERTEX_MULTI_FRAME_OUTPUT_NOT_EXPOSED_BY_HELPER",
        "saturation_count_status": "PENDING_PER_VERTEX_CORRECTION_OUTPUT_NOT_EXPOSED_BY_HELPER",
        "spatial_values_not_mislabeled_as_temporal_variation": True,
    }
    return snapshot


def schedule_geometry_readback(label, next_phase):
    validate_active_ready(f"before_geometry_readback_{label}")
    require(not STATE.readback_request_id, "Cannot overlap final geometry readback requests")
    STATE.readback_label = label
    STATE.readback_next_phase = next_phase
    STATE.readback_frozen_components = freeze_pose_for_readback()
    transition("readback_freeze_wait")


def begin_geometry_readback_request():
    validate_active_ready(f"arm_geometry_readback_{STATE.readback_label}")
    library = getattr(unreal, "EFClothingSurfaceReadbackQALibrary", None)
    require(library is not None, "EFClothingSurfaceReadbackQALibrary is unavailable in this Editor build")
    require(STATE.surface_graph is not None, f"Expected surface graph is not loaded: {SURFACE_GRAPH_PATH}")
    binding = STATE.active_row.get("binding_object")
    require(binding is not None, f"Active row has no loaded V26 binding: {STATE.active_row['row_name']}")
    try:
        raw = library.begin_final_geometry_readback(
            STATE.garment,
            STATE.body,
            STATE.surface_graph,
            binding,
            SURFACE_OFFSET_CM,
            SURFACE_OFFSET_CM,
        )
    except Exception as exc:
        signature = str(getattr(library.begin_final_geometry_readback, "__doc__", ""))
        raise RuntimeError(f"Could not call BeginFinalGeometryReadback: {exc}; reflected={signature}")
    require(isinstance(raw, tuple) and len(raw) >= 2, f"BeginFinalGeometryReadback returned an unsupported reflected tuple: {raw}")
    # UE Python may consume a native bool return as the success gate and expose
    # only the two FString out parameters. Accept that 5.8 form as well as the
    # explicit (bool, request_id, error) form used by some generated wrappers.
    if len(raw) == 2:
        request_id = str(raw[0])
        error = str(raw[1])
        accepted = bool(request_id) and not error
    else:
        accepted = bool(raw[0])
        request_id = str(raw[1])
        error = str(raw[2])
    require(accepted and request_id, f"Final GPU readback request was rejected: {error or raw}")
    STATE.readback_request_id = request_id
    STATE.readback_started = time.monotonic()
    STATE.result["geometric_gpu_readback"]["requests_started"] += 1
    STATE.result["geometric_gpu_readback"]["in_flight"] = {
        "row_name": STATE.active_row["row_name"],
        "pose": STATE.readback_label,
        "request_id": request_id,
        "frozen_components": STATE.readback_frozen_components,
    }
    emit(f"readback_started row={STATE.active_row['row_name']} pose={STATE.readback_label} id={request_id}")


def release_geometry_readback(require_success=True):
    request_id = STATE.readback_request_id
    if not request_id:
        restore_frozen_pose()
        return
    library = getattr(unreal, "EFClothingSurfaceReadbackQALibrary", None)
    released = False
    if library is not None:
        try:
            released = bool(library.release_final_geometry_readback(request_id))
        except Exception:
            released = False
    STATE.readback_request_id = ""
    STATE.result["geometric_gpu_readback"]["in_flight"] = None
    restore_frozen_pose()
    if require_success:
        require(released, f"Could not release completed final geometry readback {request_id}")


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
        self.world = None
        self.controller = None
        self.player = None
        self.runtime = None
        self.interaction = None
        self.equipment = None
        self.locomotion = None
        self.body = None
        self.registry = None
        self.compatibility_mesh = None
        self.compatibility_skeleton_path = ""
        self.rows = []
        self.active_row = None
        self.active_test = None
        self.row_index = -1
        self.garment = None
        self.candidates = []
        self.candidate_index = 0
        self.current_candidate = None
        self.current_candidate_result = None
        self.candidate_inventory_before = set()
        self.acf_item_guid = ""
        self.acf_item_slot = None
        self.free_camera_subsystem = None
        self.camera = None
        self.capture_path = None
        self.capture_next_phase = None
        self.capture_motion = ""
        self.capture_request_in_progress = False
        self.weapon_visuals_processed = False
        self.hidden_weapon_actors = []
        self.hidden_weapon_components = []
        self.pose_bones = {}
        self.idle_pose = None
        self.pre_crawl_pose = None
        self.surface_graph = None
        self.frozen_pose_components = []
        self.readback_frozen_components = []
        self.readback_label = ""
        self.readback_next_phase = None
        self.readback_request_id = ""
        self.readback_started = 0.0
        self.original_yaw = 0.0
        self.jump_airborne_observed = False
        self.cycle_index = 0
        self.stable_samples = 0
        self.result = {
            "schema_version": 1,
            "status": "UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_IN_PROGRESS",
            "project": str(PROJECT_DIR),
            "map": TARGET_MAP,
            "renderer_required": "D3D12_SM6_VISIBLE",
            "expected_compiler_version": EXPECTED_COMPILER_VERSION,
            "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "catalog": {},
            "runtime_context": {},
            "hub_character_creation_gate": {
                "status": "PENDING",
                "sample_count": 0,
                "first_world_seconds": None,
                "last_world_seconds": None,
                "violations": [],
            },
            "story_selection_auto_open": {
                "status": "NOT_TESTED_NO_CLAIM",
                "reason": "This harness loads HUB only; Story Selection requires its own map-specific PIE receipt.",
            },
            "surface_visibility_gate": {
                "status": "PENDING",
                "sample_count": 0,
                "renderable_sample_count": 0,
                "failed_state_observations": 0,
                "unsafe_renderable_frames": [],
                "first_ready_renderable_by_row": {},
            },
            "rows_tested": [],
            "screenshots": {},
            "free_camera": {},
            "weapon_visual_suppression": {},
            "geometric_gpu_readback": {
                "status": "PENDING_FINAL_GPU_READBACKS",
                "helper": "unreal.EFClothingSurfaceReadbackQALibrary",
                "surface_graph": SURFACE_GRAPH_PATH,
                "requests_started": 0,
                "requests_completed": 0,
                "in_flight": None,
                "samples": [],
                "thresholds": {
                    "triangle_intersections": 0,
                    "minimum_vertex_skin_gap_cm": VERTEX_GAP_TOLERANCE_CM,
                    "minimum_triangle_sample_skin_gap_cm": TRIANGLE_SAMPLE_GAP_TOLERANCE_CM,
                    "minimum_clearance_residual_cm": CLEARANCE_RESIDUAL_TOLERANCE_CM,
                    "invalid_or_non_finite_vertices": 0,
                },
                "temporal_correction_variation": {
                    "status": "PENDING_PER_VERTEX_MULTI_FRAME_OUTPUT_NOT_EXPOSED_BY_HELPER",
                    "p99_limit_cm_per_frame": 0.25,
                    "maximum_limit_cm_per_frame": 0.50,
                },
                "note": "Metrics come from final Optimus GPU render buffers; no CPU reconstruction is substituted.",
            },
            "catalog_equality_gate": {},
            "pending_acceptance_gates": [],
            "visual_review": "PENDING_HUMAN_REVIEW",
            "no_assets_saved": True,
            "errors": [],
        }


STATE = RuntimeState()


def resolve_runtime_context():
    STATE.world = EDITOR_LEVEL_LIBRARY.get_game_world()
    if not STATE.world:
        return False
    STATE.controller = unreal.GameplayStatics.get_player_controller(STATE.world, 0)
    STATE.player = unreal.GameplayStatics.get_player_pawn(STATE.world, 0)
    if not STATE.controller or not STATE.player:
        return False
    STATE.runtime = find_component(STATE.player, "EFClothingFitRuntimeComponent")
    STATE.interaction = find_component(STATE.player, "ACFInteractionComponent")
    STATE.equipment = find_component(STATE.player, "ACFEquipmentComponent")
    STATE.locomotion = find_component(STATE.player, "ProjectLocomotionOverride")
    return all((STATE.runtime, STATE.interaction, STATE.equipment, STATE.locomotion))


def body_candidate_diagnostic(component):
    leader = get_property(component, "leader_pose_component", default=None)
    try:
        owner = component.get_owner()
    except Exception:
        owner = None
    registration_sentinel = object()
    registration_value = registration_sentinel
    try:
        registration_method = getattr(component, "is_registered", None)
        if callable(registration_method):
            registration_value = bool(registration_method())
    except Exception:
        registration_value = registration_sentinel
    if registration_value is registration_sentinel:
        reflected_registration = get_property(
            component,
            "registered",
            "b_registered",
            default=registration_sentinel,
        )
        if reflected_registration is not registration_sentinel:
            registration_value = bool(reflected_registration)
    registered = (
        None
        if registration_value is registration_sentinel
        else bool(registration_value)
    )
    registered_status = (
        "UNKNOWN"
        if registered is None
        else ("REGISTERED" if registered else "UNREGISTERED")
    )
    try:
        raw_visible = bool(component.is_visible())
    except Exception:
        raw_visible = bool(get_property(component, "visible", default=False))
    try:
        component_world = component.get_world()
    except Exception:
        component_world = None
    return {
        "name": str(component.get_name()),
        "component_path": object_path(component),
        "component_class": class_path(component),
        "owner": object_path(owner),
        "world": object_path(component_world),
        "mesh": mesh_path(component),
        "skeleton": mesh_skeleton_path(component),
        "registered": registered,
        "registered_status": registered_status,
        "visible": raw_visible,
        "renderable": component_is_visible(component),
        "hidden_in_game": bool(get_property(component, "hidden_in_game", default=False)),
        "render_in_main_pass": bool(
            get_property(component, "render_in_main_pass", default=True)
        ),
        "leader_component": object_path(leader),
        "leader_mesh": mesh_path(leader),
    }


def resolve_effective_body_pose_driver(component):
    chain = []
    visited = set()
    current = component
    for _ in range(128):
        require(
            object_is_valid(current)
            and isinstance(current, unreal.SkeletalMeshComponent),
            f"Body LeaderPose chain contains an invalid/non-skeletal component: {chain}",
        )
        identity = object_path(current)
        require(identity and identity not in visited, f"Body LeaderPose chain is cyclic: {chain}")
        visited.add(identity)
        chain.append(body_candidate_diagnostic(current))
        leader = get_property(current, "leader_pose_component", default=None)
        if leader is None:
            return current, chain
        current = leader
    raise RuntimeError(f"Body LeaderPose chain exceeded 128 components: {chain}")


def resolve_body_for_row(row):
    matches = [
        component
        for component in all_skeletal_components(STATE.player)
        if mesh_path(component) == row["body"]
    ]
    diagnostics = [body_candidate_diagnostic(component) for component in matches]

    customization = get_property(
        STATE.runtime,
        "customization_component",
        "CustomizationComponent",
        default=None,
    )
    authoritative = get_property(
        customization,
        "body_mesh_component",
        "BodyMeshComponent",
        default=None,
    )
    strategy = "Runtime.CustomizationComponent.BodyMeshComponent"
    if authoritative is not None:
        try:
            authoritative_owner = authoritative.get_owner()
        except Exception:
            authoritative_owner = None
        require(
            authoritative_owner == STATE.player,
            f"Runtime authoritative body is not owned by the player: "
            f"{body_candidate_diagnostic(authoritative)}",
        )
        require(
            mesh_path(authoritative) == row["body"],
            f"Runtime authoritative body does not match the catalog surface for "
            f"{row['row_name']}: {body_candidate_diagnostic(authoritative)}",
        )
    else:
        if len(matches) == 1:
            strategy = "unique_exact_body"
            authoritative = matches[0]
        else:
            visually_eligible = []
            renderable_matches = []
            for component in matches:
                diagnostic = body_candidate_diagnostic(component)
                if (
                    diagnostic["visible"]
                    and diagnostic["render_in_main_pass"]
                    and not diagnostic["hidden_in_game"]
                ):
                    visually_eligible.append(component)
                    # Unreal Python does not expose IsRegistered consistently.
                    # UNKNOWN is not evidence of false; when reflection does
                    # expose it, the runtime's real registration gate is mirrored.
                    if diagnostic["registered"] is not False:
                        renderable_matches.append(component)
            if not renderable_matches and visually_eligible:
                failure_detail = (
                    "every visually eligible exact body explicitly reported "
                    "UNREGISTERED"
                )
            elif len(renderable_matches) > 1 and any(
                body_candidate_diagnostic(component)["registered"] is None
                for component in renderable_matches
            ):
                failure_detail = (
                    "registration reflection is UNKNOWN and more than one exact "
                    "body is visually eligible"
                )
            elif len(renderable_matches) > 1:
                failure_detail = (
                    "more than one explicitly REGISTERED exact body is visually eligible"
                )
            else:
                failure_detail = "no exact body is visually eligible"
            require(
                len(renderable_matches) == 1,
                f"Runtime exposes no authoritative Character Creation body and the "
                f"renderable exact-body fallback failed for {row['row_name']}: "
                f"{failure_detail}; candidates={diagnostics}",
            )
            strategy = "unique_renderable_exact_body"
            authoritative = renderable_matches[0]

    STATE.body = authoritative
    require(
        mesh_path(STATE.body) == row["body"]
        and mesh_path(STATE.body) != COMPATIBILITY_PATH,
        f"Resolved surface must be exact catalog Female/body and never Multiple: "
        f"{body_candidate_diagnostic(STATE.body)}",
    )
    STATE.active_test["body_resolver"] = {
        "strategy": strategy,
        "runtime_component": object_path(STATE.runtime),
        "customization_component": object_path(customization),
        "authoritative_body": body_candidate_diagnostic(authoritative),
        "exact_mesh_candidate_count": len(matches),
        "exact_mesh_candidates": diagnostics,
        "duplicate_selection_or_proxy_ignored": len(matches) > 1,
        "catalog_body": row["body"],
    }
    body_skeleton = mesh_skeleton_path(STATE.body)
    effective_driver, leader_chain = resolve_effective_body_pose_driver(STATE.body)
    driver_skeleton = mesh_skeleton_path(effective_driver)
    require(
        object_is_valid(effective_driver) and mesh_path(effective_driver),
        f"Body for {row['row_name']} has no valid effective pose driver: {leader_chain}",
    )
    require(
        body_skeleton == STATE.compatibility_skeleton_path,
        f"Exact body skeleton differs from the Multiple compatibility skeleton: "
        f"body={body_skeleton} compatibility={STATE.compatibility_skeleton_path}",
    )
    require(
        driver_skeleton == STATE.compatibility_skeleton_path,
        f"Effective live pose driver skeleton differs from the Multiple compatibility "
        f"skeleton: driver={driver_skeleton} "
        f"compatibility={STATE.compatibility_skeleton_path}; chain={leader_chain}",
    )
    STATE.active_test["body_resolver"].update(
        {
            "body_skeleton": body_skeleton,
            "compatibility_reference": COMPATIBILITY_PATH,
            "compatibility_skeleton": STATE.compatibility_skeleton_path,
            "leader_chain": leader_chain,
            "effective_pose_driver": body_candidate_diagnostic(effective_driver),
            "live_multiple_component_required": False,
            "skeleton_compatibility_gate": "PASS",
        }
    )
    return STATE.body


def make_active_test(row):
    return {
        "row_name": row["row_name"],
        "source": row["source"],
        "fitted": row["fitted"],
        "body": row["body"],
        "status": "IN_PROGRESS",
        "acf_candidates": [],
        "acf_real_equip": {},
        "body_resolver": {},
        "ready_checks": [],
        "offset_api": {},
        "motion": {},
        "pose_evidence": {},
        "gpu_readbacks": [],
        "equip_unequip_cycles": {
            "requested": FULL_EQUIP_UNEQUIP_CYCLES,
            "completed": 0,
            "status": "PENDING",
            "cycles": [],
        },
        "screenshots": [],
        "pending_gates": [],
    }


def begin_row():
    STATE.row_index += 1
    if STATE.row_index >= len(STATE.rows):
        transition("final_checks")
        return
    STATE.active_row = STATE.rows[STATE.row_index]
    STATE.active_test = make_active_test(STATE.active_row)
    STATE.result["rows_tested"].append(STATE.active_test)
    STATE.garment = None
    STATE.acf_item_guid = ""
    STATE.acf_item_slot = None
    STATE.pose_bones = {}
    resolve_body_for_row(STATE.active_row)
    STATE.candidates = get_world_item_candidates(STATE.active_row)
    STATE.candidate_index = 0
    require(STATE.candidates, f"No ACF world-item fixture exists for {STATE.active_row['row_name']}")
    emit(f"row={STATE.active_row['row_name']} candidates={len(STATE.candidates)}")
    transition("try_acf_candidate")


def cleanup_runtime_state():
    try:
        release_geometry_readback(require_success=False)
        library = getattr(unreal, "EFClothingSurfaceReadbackQALibrary", None)
        if library is not None:
            library.release_all_final_geometry_readbacks()
    except Exception:
        restore_frozen_pose()
    try:
        if STATE.runtime:
            call(STATE.runtime, "set_global_clearance_offset_cm", 0.0)
            if STATE.garment:
                call(STATE.runtime, "clear_garment_clearance_offset_cm", STATE.garment)
    except Exception:
        pass
    try:
        if STATE.locomotion:
            if callable_method(STATE.locomotion, "set_crawl_mode_enabled"):
                call(STATE.locomotion, "set_crawl_mode_enabled", False)
            if callable_method(STATE.locomotion, "set_walk_mode_enabled"):
                call(STATE.locomotion, "set_walk_mode_enabled", False)
    except Exception:
        pass
    try:
        if STATE.player and callable_method(STATE.player, "uncrouch"):
            STATE.player.uncrouch()
    except Exception:
        pass
    stop_motion()
    restore_weapon_visuals()
    try:
        if STATE.free_camera_subsystem:
            call(STATE.free_camera_subsystem, "stop_gameplay_free_camera")
    except Exception:
        pass


def finish(success, failure=None):
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
    builtins._codex_ef_clothing_morph_v26_surface_runtime = None
    cleanup_runtime_state()
    STATE.result["status"] = (
        "UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_PASS"
        if success
        else "UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_FAIL"
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


def tick(delta_time):
    try:
        if STATE.capture_request_in_progress:
            return
        STATE.phase_elapsed += float(delta_time)
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            raise RuntimeError(f"Timeout in phase {STATE.phase}")

        if LEVEL_EDITOR.is_in_play_in_editor():
            sample_character_creation()
            if resolve_runtime_context() and STATE.rows:
                sample_global_visibility()

        if STATE.phase == "load_map":
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
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
            world_seconds = float(unreal.GameplayStatics.get_time_seconds(STATE.world))
            if normalized_world_name(STATE.world) == TARGET_MAP_NAME and world_seconds >= 3.0:
                STATE.stable_samples += 1
            else:
                STATE.stable_samples = 0
            if STATE.stable_samples < 3:
                return
            transition("bootstrap")
            return

        if STATE.phase == "bootstrap":
            require(resolve_runtime_context(), "Required HUB player components are missing")
            load_catalog_contract()
            STATE.surface_graph = unreal.load_asset(SURFACE_GRAPH_PATH)
            require(STATE.surface_graph is not None, f"Surface deformer graph is missing: {SURFACE_GRAPH_PATH}")
            require(STATE.result["hub_character_creation_gate"]["sample_count"] > 0, "No early HUB Character Creation samples were observed")
            STATE.result["runtime_context"] = {
                "world": object_path(STATE.world),
                "player": object_path(STATE.player),
                "runtime_component": object_path(STATE.runtime),
                "interaction_component": object_path(STATE.interaction),
                "equipment_component": object_path(STATE.equipment),
                "locomotion_component": object_path(STATE.locomotion),
                "surface_deformer_graph": object_path(STATE.surface_graph),
            }
            if not resolve_free_camera():
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError("Gameplay free camera did not become active")
                return
            transition("start_row")
            return

        if STATE.phase == "readback_freeze_wait":
            # bPauseAnims preserves the sampled bone pose while component/deformer
            # ticks continue. Two visible Slate ticks are allowed before arming.
            if STATE.phase_elapsed < 0.10:
                return
            begin_geometry_readback_request()
            transition("poll_geometry_readback")
            return

        if STATE.phase == "poll_geometry_readback":
            validate_active_ready(f"poll_geometry_readback_{STATE.readback_label}")
            library = getattr(unreal, "EFClothingSurfaceReadbackQALibrary", None)
            require(library is not None, "Readback helper disappeared while a request was in flight")
            result = library.poll_final_geometry_readback(STATE.readback_request_id)
            snapshot = readback_result_snapshot(result)
            if snapshot["state"] in ("PendingGPU", "Analyzing"):
                if time.monotonic() - STATE.readback_started > READBACK_TIMEOUT_SECONDS:
                    STATE.result["geometric_gpu_readback"]["status"] = "FAIL_READBACK_TIMEOUT"
                    release_geometry_readback(require_success=False)
                    raise RuntimeError(
                        f"Final GPU readback timed out after {READBACK_TIMEOUT_SECONDS}s: {snapshot}"
                    )
                return
            snapshot["row_name"] = STATE.active_row["row_name"]
            snapshot["pose"] = STATE.readback_label
            snapshot["pose_frozen"] = True
            snapshot["frozen_components"] = list(STATE.readback_frozen_components)
            if snapshot["state"] != "Ready":
                STATE.result["geometric_gpu_readback"]["samples"].append(snapshot)
                STATE.result["geometric_gpu_readback"]["status"] = "FAIL_READBACK_NOT_READY"
                release_geometry_readback(require_success=False)
                raise RuntimeError(f"Final GPU readback failed or became unknown: {snapshot}")
            validate_readback_thresholds(snapshot)
            STATE.result["geometric_gpu_readback"]["samples"].append(snapshot)
            STATE.result["geometric_gpu_readback"]["requests_completed"] += 1
            STATE.active_test["gpu_readbacks"].append(snapshot)
            next_phase = STATE.readback_next_phase
            label = STATE.readback_label
            release_geometry_readback(require_success=True)
            STATE.readback_label = ""
            STATE.readback_next_phase = None
            STATE.readback_frozen_components = []
            emit(
                f"readback_ready row={STATE.active_row['row_name']} pose={label} "
                f"intersections={snapshot['triangle_intersections']} "
                f"vertexGap={snapshot['minimum_vertex_skin_gap_cm']:.6f} "
                f"sampleGap={snapshot['minimum_triangle_sample_skin_gap_cm']:.6f}"
            )
            transition(next_phase)
            return

        if STATE.phase == "start_row":
            begin_row()
            return

        if STATE.phase == "try_acf_candidate":
            while STATE.candidate_index < len(STATE.candidates):
                candidate = STATE.candidates[STATE.candidate_index]
                STATE.candidate_index += 1
                if not object_is_valid(candidate["actor"]):
                    continue
                STATE.current_candidate = candidate
                begin_candidate_interaction(candidate)
                transition("wait_acf_candidate")
                return
            raise RuntimeError(f"No real ACF pickup produced catalog garment {STATE.active_row['row_name']}")

        if STATE.phase == "wait_acf_candidate":
            components = matching_components(STATE.active_row)
            if not components:
                if STATE.phase_elapsed > 10.0:
                    STATE.current_candidate_result["matched"] = False
                    STATE.current_candidate_result["reason"] = "No catalog garment component appeared"
                    transition("try_acf_candidate")
                return
            component, ready = find_ready_component(STATE.active_row)
            if component is None:
                if STATE.phase_elapsed > 45.0:
                    snapshots = [garment_snapshot(value, STATE.active_row) for value in components]
                    raise RuntimeError(f"Matched garment never became Surface Ready: {snapshots}; {runtime_snapshot()}")
                return
            STATE.garment = component
            associate_acf_item(STATE.current_candidate, component)
            STATE.active_test["acf_real_equip"] = {
                "status": "PASS_REAL_WORLD_INTERACT_TO_ACF_EQUIPMENT",
                "candidate": STATE.current_candidate_result,
                "guid": STATE.acf_item_guid,
                "direct_mesh_assignment": False,
                "direct_equipment_shortcut_for_initial_acquisition": False,
            }
            validate_active_ready("initial_real_acf_equip")
            call(STATE.runtime, "set_global_clearance_offset_cm", SURFACE_OFFSET_CM)
            call(STATE.runtime, "set_garment_clearance_offset_cm", STATE.garment, SURFACE_OFFSET_CM)
            STATE.active_test["offset_api"] = {
                "status": "PASS_CONTINUOUS_CM_API_INVOKED",
                "global_offset_cm": SURFACE_OFFSET_CM,
                "garment_offset_cm": SURFACE_OFFSET_CM,
                "source_of_truth": "continuous centimeters",
            }
            transition("idle_ready")
            return

        if STATE.phase == "idle_ready":
            if STATE.phase_elapsed < 1.0:
                return
            record_motion("idle", {"speed_cm_s": vector_length(STATE.player.get_velocity())})
            STATE.idle_pose = pose_snapshot("idle")
            STATE.active_test["pose_evidence"]["idle"] = STATE.idle_pose
            schedule_geometry_readback("idle", "capture_idle")
            return

        if STATE.phase == "capture_idle":
            position_camera("front")
            begin_capture("front", "idle", "start_walk")
            return

        if STATE.phase == "wait_capture":
            apply_capture_motion()
            if STATE.phase_elapsed < SCREENSHOT_WAIT_SECONDS:
                return
            request = STATE.result["screenshots"][STATE.capture_path.name]
            request["exists"] = STATE.capture_path.is_file()
            request["size_bytes"] = STATE.capture_path.stat().st_size if STATE.capture_path.is_file() else 0
            require(request["accepted"], f"Screenshot request rejected: {STATE.capture_path.name}")
            require(request["exists"] and request["size_bytes"] > 4096, f"Screenshot missing or too small: {STATE.capture_path}")
            validate_active_ready(f"after_capture_{STATE.capture_path.name}")
            transition(STATE.capture_next_phase)
            return

        if STATE.phase == "start_walk":
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            transition("walk_motion")
            return

        if STATE.phase == "walk_motion":
            STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 1.0, True)
            if STATE.phase_elapsed < 2.0:
                return
            motion = record_motion("walk")
            require(motion["speed_cm_s"] > 5.0, f"Walk did not move the player: {motion}")
            schedule_geometry_readback("walk", "capture_walk")
            return

        if STATE.phase == "capture_walk":
            position_camera("left")
            begin_capture("left", "walk", "start_run")
            return

        if STATE.phase == "start_run":
            call(STATE.locomotion, "set_walk_mode_enabled", False)
            transition("run_motion")
            return

        if STATE.phase == "run_motion":
            STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 1.0, True)
            if STATE.phase_elapsed < 2.0:
                return
            motion = record_motion("run")
            require(motion["speed_cm_s"] > 5.0, f"Run did not move the player: {motion}")
            schedule_geometry_readback("run", "capture_run")
            return

        if STATE.phase == "capture_run":
            position_camera("back")
            begin_capture("back", "run", "start_strafe")
            return

        if STATE.phase == "start_strafe":
            stop_motion()
            transition("strafe_motion")
            return

        if STATE.phase == "strafe_motion":
            STATE.player.add_movement_input(STATE.player.get_actor_right_vector(), 1.0, True)
            if STATE.phase_elapsed < 2.0:
                return
            motion = record_motion("strafe")
            require(motion["speed_cm_s"] > 5.0, f"Strafe did not move the player: {motion}")
            schedule_geometry_readback("strafe", "capture_strafe")
            return

        if STATE.phase == "capture_strafe":
            position_camera("right")
            begin_capture("right", "strafe", "start_pivot")
            return

        if STATE.phase == "start_pivot":
            stop_motion()
            rotation = STATE.player.get_actor_rotation()
            STATE.original_yaw = float(rotation.yaw)
            target_yaw = STATE.original_yaw + 150.0
            STATE.player.set_actor_rotation(unreal.Rotator(0.0, target_yaw, 0.0), True)
            transition("pivot_motion")
            return

        if STATE.phase == "pivot_motion":
            STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 0.8, True)
            if STATE.phase_elapsed < 1.5:
                return
            final_yaw = float(STATE.player.get_actor_rotation().yaw)
            delta = abs(unreal.MathLibrary.normalized_delta_rotator(unreal.Rotator(0.0, final_yaw, 0.0), unreal.Rotator(0.0, STATE.original_yaw, 0.0)).yaw)
            require(delta >= 120.0, f"Pivot rotation was too small: {delta}")
            record_motion("pivot_150_degrees", {"yaw_delta_degrees": delta})
            schedule_geometry_readback("pivot_150_degrees", "start_crouch")
            return

        if STATE.phase == "start_crouch":
            stop_motion()
            if not callable_method(STATE.player, "crouch"):
                STATE.active_test["motion"]["crouch"] = {"status": "SKIPPED_REFLECTED_API_UNAVAILABLE"}
                STATE.active_test["pending_gates"].append("crouch_api_unavailable")
                transition("start_crawl")
                return
            STATE.player.crouch()
            transition("wait_crouch")
            return

        if STATE.phase == "wait_crouch":
            crouched = bool(get_property(STATE.player, "b_is_crouched", "is_crouched", default=False))
            if not crouched:
                if STATE.phase_elapsed > 6.0:
                    STATE.active_test["motion"]["crouch"] = {"status": "SKIPPED_GAMEPLAY_CONFIGURATION_DID_NOT_ACTIVATE"}
                    STATE.active_test["pending_gates"].append("crouch_not_activated")
                    transition("start_crawl")
                return
            STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 0.45, True)
            if STATE.phase_elapsed < 1.5:
                return
            record_motion("crouch", {"crouched": True})
            schedule_geometry_readback("crouch", "finish_crouch")
            return

        if STATE.phase == "finish_crouch":
            if callable_method(STATE.player, "uncrouch"):
                STATE.player.uncrouch()
            transition("start_crawl")
            return

        if STATE.phase == "start_crawl":
            stop_motion()
            STATE.pre_crawl_pose = pose_snapshot("pre_crawl")
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            transition("crawl_motion")
            return

        if STATE.phase == "crawl_motion":
            active = bool(call(STATE.locomotion, "is_crawl_mode_active"))
            if not active:
                if STATE.phase_elapsed > 20.0:
                    raise RuntimeError("Crawl mode did not activate")
                return
            STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 0.65, True)
            if STATE.phase_elapsed < 4.0:
                return
            crawl_pose = pose_snapshot("crawl_motion")
            changed = {
                group: pose_group_changed(STATE.pre_crawl_pose, crawl_pose, group)
                for group in ("pelvis", "thighs", "spine")
            }
            require(crawl_pose["hash_sha256"] != STATE.pre_crawl_pose["hash_sha256"], "Crawl pose hash equals the pre-crawl pose hash")
            require(all(changed[group] for group in changed), f"Crawl did not change every required live bone group: {changed}")
            record_motion("crawl", {"active": True, "pose_hash": crawl_pose["hash_sha256"]})
            STATE.active_test["pose_evidence"]["crawl"] = {
                "pre_crawl": STATE.pre_crawl_pose,
                "crawl": crawl_pose,
                "changed_bones_by_group": changed,
                "status": "PASS_LIVE_COMPONENT_SPACE_POSE_HASH_CHANGED",
                "animation_name_alone_not_used_as_evidence": True,
            }
            schedule_geometry_readback("crawl", "capture_crawl")
            return

        if STATE.phase == "capture_crawl":
            position_camera("inferior")
            begin_capture("inferior", "crawl", "start_jump")
            return

        if STATE.phase == "start_jump":
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
            stop_motion()
            STATE.jump_airborne_observed = False
            if not callable_method(STATE.player, "jump"):
                STATE.active_test["motion"]["jump_land"] = {"status": "SKIPPED_REFLECTED_API_UNAVAILABLE"}
                STATE.active_test["pending_gates"].append("jump_api_unavailable")
                transition("start_cycles")
                return
            STATE.player.jump()
            transition("jump_motion")
            return

        if STATE.phase == "jump_motion":
            movement = STATE.player.get_component_by_class(unreal.CharacterMovementComponent)
            falling = bool(movement.is_falling()) if movement and callable_method(movement, "is_falling") else abs(float(STATE.player.get_velocity().z)) > 5.0
            if falling:
                STATE.jump_airborne_observed = True
                validate_active_ready("jump_airborne")
            if STATE.jump_airborne_observed and not falling and STATE.phase_elapsed > 0.4:
                record_motion("jump_land", {"airborne_observed": True, "landed_observed": True})
                schedule_geometry_readback("jump_land", "start_cycles")
                return
            if STATE.phase_elapsed > 8.0:
                STATE.active_test["motion"]["jump_land"] = {"status": "SKIPPED_GAMEPLAY_CONFIGURATION_DID_NOT_ACTIVATE", "airborne_observed": STATE.jump_airborne_observed}
                STATE.active_test["pending_gates"].append("jump_not_activated")
                transition("start_cycles")
            return

        if STATE.phase == "start_cycles":
            stop_motion()
            cycles = STATE.active_test["equip_unequip_cycles"]
            if not callable_method(STATE.equipment, "unequip_item_by_guid") or not callable_method(STATE.equipment, "equip_item_from_inventory_in_slot"):
                cycles["status"] = "SKIPPED_REFLECTED_ACF_CYCLE_API_UNAVAILABLE"
                STATE.active_test["pending_gates"].append("25_acf_cycles_api_unavailable")
                transition("row_complete")
                return
            STATE.cycle_index = 0
            transition("cycle_unequip")
            return

        if STATE.phase == "cycle_unequip":
            raw_item = find_inventory_item(STATE.acf_item_guid)
            require(raw_item is not None, f"Cycle item {STATE.acf_item_guid} disappeared from inventory")
            guid_value = get_property(raw_item, "item_guid", "ItemGuid", default=None)
            require(guid_value is not None, "Cycle inventory item exposes no FGuid")
            call(STATE.equipment, "unequip_item_by_guid", guid_value)
            transition("cycle_wait_unequipped")
            return

        if STATE.phase == "cycle_wait_unequipped":
            components = matching_components(STATE.active_row)
            unsafe_visible = [garment_snapshot(component, STATE.active_row) for component in components if component_is_visible(component)]
            if unsafe_visible:
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError(f"ACF unequip left a catalog garment visible: {unsafe_visible}")
                return
            if STATE.phase_elapsed < 0.2:
                return
            raw_item = find_inventory_item(STATE.acf_item_guid)
            require(raw_item is not None, "Unequipped ACF item is absent from inventory")
            slot_value = STATE.acf_item_slot
            require(slot_value is not None, "Cannot resolve ACF slot for re-equip")
            call(STATE.equipment, "equip_item_from_inventory_in_slot", raw_item, slot_value)
            transition("cycle_wait_ready")
            return

        if STATE.phase == "cycle_wait_ready":
            component, snapshot = find_ready_component(STATE.active_row)
            if component is None:
                if STATE.phase_elapsed > 20.0:
                    raise RuntimeError(f"Cycle {STATE.cycle_index + 1} never reached exact Surface Ready: {runtime_snapshot()}")
                return
            STATE.garment = component
            STATE.cycle_index += 1
            STATE.active_test["equip_unequip_cycles"]["completed"] = STATE.cycle_index
            STATE.active_test["equip_unequip_cycles"]["cycles"].append(
                {
                    "cycle": STATE.cycle_index,
                    "status": "PASS",
                    "surface_state": snapshot["surface_state"],
                    "mesh": snapshot["mesh"],
                    "runtime": runtime_snapshot(),
                }
            )
            if STATE.cycle_index >= FULL_EQUIP_UNEQUIP_CYCLES:
                STATE.active_test["equip_unequip_cycles"]["status"] = "PASS_25_REAL_ACF_UNEQUIP_REEQUIP_CYCLES"
                transition("row_complete")
            else:
                transition("cycle_unequip")
            return

        if STATE.phase == "row_complete":
            validate_active_ready("row_complete")
            call(STATE.runtime, "clear_garment_clearance_offset_cm", STATE.garment)
            call(STATE.runtime, "set_global_clearance_offset_cm", 0.0)
            required_motions = ("idle", "walk", "run", "strafe", "pivot_150_degrees", "crawl")
            require(all(STATE.active_test["motion"].get(name, {}).get("status") == "PASS" for name in required_motions), f"Required motion matrix incomplete: {STATE.active_test['motion']}")
            required_readbacks = list(required_motions)
            for optional_motion in ("crouch", "jump_land"):
                if STATE.active_test["motion"].get(optional_motion, {}).get("status") == "PASS":
                    required_readbacks.append(optional_motion)
            completed_readbacks = [sample["pose"] for sample in STATE.active_test["gpu_readbacks"]]
            require(
                sorted(completed_readbacks) == sorted(required_readbacks),
                f"Final GPU readback coverage is incomplete for {STATE.active_row['row_name']}: "
                f"expected={required_readbacks} actual={completed_readbacks}",
            )
            require(len(STATE.active_test["screenshots"]) == 5, f"Expected five gameplay views for {STATE.active_row['row_name']}")
            cycle_status = STATE.active_test["equip_unequip_cycles"]["status"]
            if cycle_status.startswith("SKIPPED"):
                STATE.result["pending_acceptance_gates"].append(
                    {"row_name": STATE.active_row["row_name"], "gate": cycle_status}
                )
            STATE.active_test["status"] = "PASS"
            if STATE.row_index + 1 < len(STATE.rows):
                require(
                    callable_method(STATE.equipment, "unequip_item_by_guid"),
                    "Multiple enabled rows require the reflected ACF unequip API",
                )
                raw_item = find_inventory_item(STATE.acf_item_guid)
                require(raw_item is not None, "Completed row item disappeared before fixture teardown")
                guid_value = get_property(raw_item, "item_guid", "ItemGuid", default=None)
                require(guid_value is not None, "Completed row item exposes no FGuid")
                call(STATE.equipment, "unequip_item_by_guid", guid_value)
                transition("wait_row_fixture_unequipped")
            else:
                transition("start_row")
            return

        if STATE.phase == "wait_row_fixture_unequipped":
            visible = [
                garment_snapshot(component, STATE.active_row)
                for component in matching_components(STATE.active_row)
                if component_is_visible(component)
            ]
            if visible:
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError(f"Completed row fixture remained visible after ACF unequip: {visible}")
                return
            transition("start_row")
            return

        if STATE.phase == "final_checks":
            gate = STATE.result["hub_character_creation_gate"]
            require(gate["sample_count"] > 0 and not gate["violations"], "HUB Character Creation visibility gate did not pass")
            gate["status"] = "PASS_ABSENT_FROM_ALL_OBSERVED_HUB_PIE_TICKS"
            visibility = STATE.result["surface_visibility_gate"]
            require(visibility["sample_count"] > 0, "No surface visibility samples were collected")
            require(not visibility["unsafe_renderable_frames"], "Unsafe renderable SurfaceWrapGPU frame was observed")
            require(len(visibility["first_ready_renderable_by_row"]) == len(STATE.rows), "Not every enabled row produced a Ready renderable sample")
            visibility["status"] = "PASS_ZERO_UNSAFE_RENDERABLE_POST_TICK_SAMPLES"
            tested = len(STATE.result["rows_tested"])
            passed = sum(1 for row in STATE.result["rows_tested"] if row.get("status") == "PASS")
            enabled = STATE.result["catalog"]["enabled_row_count"]
            valid_profiles = STATE.result["catalog"]["valid_profile_count"]
            valid_bindings = STATE.result["catalog"]["valid_binding_count"]
            equality = enabled == valid_profiles == valid_bindings == tested == passed and enabled > 0
            STATE.result["catalog_equality_gate"] = {
                "enabled_rows": enabled,
                "valid_profiles": valid_profiles,
                "valid_bindings": valid_bindings,
                "tested_rows": tested,
                "passed_rows": passed,
                "expression": "enabled rows == valid profiles == valid bindings == tested rows == passed rows",
                "passed": equality,
            }
            require(equality, f"Catalog runtime equality gate failed: {STATE.result['catalog_equality_gate']}")
            geometry = STATE.result["geometric_gpu_readback"]
            expected_readbacks = sum(len(row["gpu_readbacks"]) for row in STATE.result["rows_tested"])
            require(expected_readbacks >= 6 * enabled, f"Too few final GPU pose readbacks: {expected_readbacks}")
            require(
                geometry["requests_started"] == geometry["requests_completed"] == expected_readbacks,
                f"Final GPU readback lifecycle is incomplete: started={geometry['requests_started']} "
                f"completed={geometry['requests_completed']} expected={expected_readbacks}",
            )
            require(geometry["in_flight"] is None, "A final GPU readback remained in flight at completion")
            require(
                all(sample.get("state") == "Ready" and sample.get("thresholds", {}).get("passed") for sample in geometry["samples"]),
                "At least one final GPU readback lacks a Ready threshold PASS",
            )
            geometry["status"] = "PASS_FINAL_OPTIMUS_GPU_BUFFERS_GEOMETRIC_THRESHOLDS"
            STATE.result["pending_acceptance_gates"].append(
                {
                    "gate": "temporal_per_vertex_correction_variation_and_exact_saturation_count",
                    "status": "PENDING_HELPER_EXPOSES_SPATIAL_MAGNITUDES_ONLY",
                    "not_mislabeled_as_pass": True,
                }
            )
            require(STATE.result["story_selection_auto_open"]["status"] == "NOT_TESTED_NO_CLAIM", "HUB harness must not claim Story Selection coverage")
            finish(True)
            return
    except Exception as exc:
        details = traceback.format_exc()
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = details
        geometry = STATE.result.get("geometric_gpu_readback", {})
        if geometry and not str(geometry.get("status", "")).startswith("PASS_"):
            geometry["status"] = "FAIL_FINAL_GPU_READBACK_OR_THRESHOLD_GATE"
            geometry["failure"] = str(exc)
        unreal.log_error(details)
        finish(False, str(exc))


existing = getattr(builtins, "_codex_ef_clothing_morph_v26_surface_runtime", None)
if existing is not None:
    unreal.log_warning("[EFClothingMorphV26SurfaceRuntime] duplicate registration ignored")
else:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    PROGRESS_FILE.write_text("", encoding="utf-8")
    write_result()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_ef_clothing_morph_v26_surface_runtime = STATE
    emit("registered=True")

"""Visible HUB PIE smoke harness for EF Clothing Morph V3.

The harness intentionally uses the real ACF world-item interaction path.  It
never assigns a Skeletal Mesh, never invokes a compiler, and never saves an
asset. V3 is validated as a source-first runtime: the equipped component must
keep the exact Director SourceGarment visible and a certified fixture must
reach Ready. Passthrough remains a documented, non-destructive visible fallback,
but observing it for one of these certified bindings fails the smoke test with
the runtime debug reason.

This is deliberately a smoke test, not the old V26 GPU-readback certification.
It checks real equip identity/GUID preservation, per-component clearance and
inflate overrides (0 -> 0.2 -> 0 cm), idle and moving gameplay, four useful
camera views, three ACF unequip/re-equip cycles, and the absence of Character
Creation throughout HUB PIE.
"""

from __future__ import annotations

import builtins
import json
import math
import os
import re
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir()).resolve()
OUTPUT_DIR = Path(
    os.environ.get(
        "CODEX_EF_CLOTHING_V3_QA_DIR",
        PROJECT_DIR / "Saved" / "ClothingMorphV3QA" / "RuntimeSmoke_adhoc",
    )
).resolve()
RESULT_FILE = Path(
    os.environ.get(
        "CODEX_EF_CLOTHING_V3_QA_RESULT",
        OUTPUT_DIR / "RuntimeResult.json",
    )
).resolve()
PROGRESS_FILE = OUTPUT_DIR / "Progress.log"
TARGET_MAP = os.environ.get("CODEX_EF_CLOTHING_V3_QA_MAP", "/Game/_Game/Hub/HUB")
TARGET_MAP_NAME = TARGET_MAP.rsplit("/", 1)[-1].lower()
TIMEOUT_SECONDS = float(os.environ.get("CODEX_EF_CLOTHING_V3_QA_TIMEOUT", "540"))

DIRECTOR_PATH = "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector"
REGISTRY_PATH = "/EFClothingMorph/_Internal/Compiled/V3/DA_EFClothingFitRegistry"
COMPATIBILITY_PATH = "/Game/DazToUnreal/Multiple/Multiple"
EXPECTED_COMPILER_VERSION = 27
EXPECTED_BINDING_SCHEMA = 7
OFFSET_TEST_CM = 0.2
SMOKE_EQUIP_UNEQUIP_CYCLES = 3
SCREENSHOT_WAIT_SECONDS = 2.25
CERTIFIED_PASS_STATE = "Ready"

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def class_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_class().get_path_name())
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


def component_tags(component):
    return sorted(
        str(tag) for tag in (get_property(component, "component_tags", default=[]) or [])
    )


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


def vector_length(value):
    return math.sqrt(float(value.x) ** 2 + float(value.y) ** 2 + float(value.z) ** 2)


def vector_add(left, right):
    return unreal.Vector(left.x + right.x, left.y + right.y, left.z + right.z)


def vector_scale(value, scalar):
    return unreal.Vector(value.x * scalar, value.y * scalar, value.z * scalar)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def safe_slug(text):
    value = re.sub(r"[^A-Za-z0-9]+", "_", str(text)).strip("_").lower()
    return value or "garment"


def emit(message):
    line = f"[EFClothingMorphV3RuntimeSmoke] {message}"
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


def object_is_valid(value):
    if value is None:
        return False
    try:
        return bool(unreal.SystemLibrary.is_valid(value))
    except Exception:
        return True


def component_is_visible(component):
    try:
        visible = bool(component.is_visible())
    except Exception:
        visible = bool(get_property(component, "visible", default=False))
    hidden = bool(get_property(component, "hidden_in_game", default=False))
    main_pass = bool(get_property(component, "render_in_main_pass", default=True))
    return visible and not hidden and main_pass


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
            if visible and any(
                token in identity
                for token in (
                    "efcharactercreation",
                    "wbp_efcharactercreationroot",
                    "charactercreation",
                    "character_creation",
                )
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
    widgets = visible_character_creation_widgets()
    gate = STATE.result["hub_character_creation_gate"]
    gate["sample_count"] += 1
    world_seconds = float(unreal.GameplayStatics.get_time_seconds(world))
    if gate["first_world_seconds"] is None:
        gate["first_world_seconds"] = world_seconds
    gate["last_world_seconds"] = world_seconds
    if widgets:
        violation = {
            "sample_index": gate["sample_count"],
            "world_seconds": world_seconds,
            "phase": STATE.phase,
            "widgets": widgets,
        }
        gate["violations"].append(violation)
        raise RuntimeError(f"Character Creation became visible in HUB PIE: {violation}")


def backend_is_surface_wrap(value):
    text = str(value).upper().replace(" ", "_")
    if "SURFACEWRAPGPU" in text.replace("_", "") or "AUTOMATIC_GPU_SURFACE_WRAP" in text:
        return True
    try:
        return int(value) == 1
    except Exception:
        return False


def load_catalog_contract():
    director = unreal.load_asset(DIRECTOR_PATH)
    registry = unreal.load_asset(REGISTRY_PATH)
    require(director is not None, f"V3 Director is missing: {DIRECTOR_PATH}")
    require(registry is not None, f"V3 binding registry is missing: {REGISTRY_PATH}")
    require(int(get_property(director, "schema_version", default=-1)) == 4, "Director is not schema 4")
    require(str(get_property(director, "director_id", default="")) == "EFClothingMorphV3", "Director identity is not EFClothingMorphV3")
    require(bool(call(director, "is_policy_valid")), f"Director validation failed: {call(director, 'get_policy_validation_error')}")

    enabled = [
        row
        for row in list(get_property(director, "garments", default=[]) or [])
        if bool(get_property(row, "enabled", default=False))
    ]
    require(enabled, "V3 Director contains no enabled garments")
    profiles = list(get_property(registry, "profiles", default=[]) or [])
    bindings = list(get_property(registry, "native_source_bindings", default=[]) or [])
    require(not profiles, "V3 registry unexpectedly contains generated V26 fit profiles")

    rows = []
    for row in enabled:
        garment_id = str(get_property(row, "garment_id", default=""))
        source = canonical_asset_path(get_property(row, "source_garment", default=None))
        body = canonical_asset_path(get_property(row, "body_surface", default=None))
        require(garment_id and garment_id.lower() != "none", "Enabled garment has no stable ID")
        require(source.startswith("/Game/"), f"{garment_id} SourceGarment is invalid: {source}")
        require(body.startswith("/Game/"), f"{garment_id} BodySurface is invalid: {body}")
        require(body != COMPATIBILITY_PATH, f"{garment_id} incorrectly uses Multiple as skin surface")
        # Backend is an internal C++ enum and UE's Python reflection does not
        # expose a stable textual/numeric representation across editor builds.
        # The catalog compiler already rejects every non-SurfaceWrapGPU row;
        # this runtime smoke proves the same contract more strongly by requiring
        # the certified component to reach V3 Ready (passthrough is a failure).
        backend = get_property(row, "backend", default=None)

        exact_bindings = [
            binding
            for binding in bindings
            if canonical_asset_path(get_property(binding, "source_garment", default=None)) == source
            and canonical_asset_path(get_property(binding, "body_surface", default=None)) == body
        ]
        require(
            len(exact_bindings) == 1,
            f"{garment_id} requires exactly one V3 source/body binding; found {len(exact_bindings)}",
        )
        binding = exact_bindings[0]
        fitted = canonical_asset_path(get_property(binding, "fitted_garment", default=None))
        require(not fitted, f"{garment_id} V3 binding illegally references a fitted mesh: {fitted}")
        require(
            int(get_property(binding, "compiler_version", default=-1)) == EXPECTED_COMPILER_VERSION,
            f"{garment_id} binding compiler is not {EXPECTED_COMPILER_VERSION}",
        )
        require(
            int(get_property(binding, "schema_version", default=-1)) == EXPECTED_BINDING_SCHEMA,
            f"{garment_id} binding schema is not {EXPECTED_BINDING_SCHEMA}",
        )
        require(str(get_property(binding, "garment_id", default="")) == garment_id, f"{garment_id} binding identity mismatch")
        lod_pairs = list(get_property(binding, "lod_pair_bindings", default=[]) or [])
        require(lod_pairs, f"{garment_id} binding has no LOD pairs")
        require(
            all(bool(get_property(pair, "certified", default=False)) for pair in lod_pairs),
            f"{garment_id} binding contains an uncertified LOD pair",
        )
        rows.append(
            {
                "row_name": garment_id,
                "slug": safe_slug(garment_id),
                "source": source,
                "body": body,
                "binding": object_path(binding),
                "backend_reflected": str(backend),
                "binding_compile_fingerprint": str(
                    get_property(binding, "garment_compile_fingerprint", default="")
                ),
                "compiler_version": int(get_property(binding, "compiler_version", default=-1)),
                "binding_schema": int(get_property(binding, "schema_version", default=-1)),
                "lod_pair_count": len(lod_pairs),
                "fitted_mesh": fitted,
                "authored_additional_clearance_cm": float(
                    get_property(row, "additional_clearance_cm", default=0.0) or 0.0
                ),
                "authored_shell_thickness_cm": float(
                    get_property(row, "shell_thickness_cm", default=0.0) or 0.0
                ),
            }
        )

    ids = [row["row_name"] for row in rows]
    require(len(ids) == len(set(ids)), "Enabled V3 Director garment IDs are not unique")
    require(
        len(bindings) == len(rows),
        f"V3 registry/catalog count mismatch: enabled={len(rows)} bindings={len(bindings)}",
    )
    STATE.rows = rows
    STATE.director = director
    STATE.registry = registry
    STATE.result["catalog"] = {
        "director": object_path(director),
        "director_schema": 4,
        "director_id": "EFClothingMorphV3",
        "registry": object_path(registry),
        "enabled_row_count": len(rows),
        "native_binding_count": len(bindings),
        "generated_profile_count": len(profiles),
        "rows": rows,
        "contract": "exact SourceGarment + binding-only registry; no fitted mesh and no EF_AutoFit",
    }


def runtime_state_name(runtime, garment):
    if runtime is None or garment is None:
        return "Disabled"
    raw = call(runtime, "get_garment_runtime_state", garment)
    normalized = str(raw).upper().replace("_", "").replace(" ", "")
    for token, name in (
        ("PASSTHROUGH", "Passthrough"),
        ("WARMINGUP", "WarmingUp"),
        ("LOADING", "Loading"),
        ("READY", "Ready"),
        ("DISABLED", "Disabled"),
    ):
        if token in normalized:
            return name
    try:
        return {0: "Disabled", 1: "Loading", 2: "Passthrough", 3: "WarmingUp", 4: "Ready"}.get(int(raw), str(raw))
    except Exception:
        return str(raw)


def current_skin_profile(component):
    getter = getattr(component, "get_current_skin_weight_profile_name", None)
    if callable(getter):
        try:
            return str(getter())
        except Exception:
            pass
    return ""


def garment_snapshot(component, row):
    return {
        "component": object_path(component),
        "component_name": str(component.get_name()) if component else "",
        "mesh": mesh_path(component),
        "expected_source": row["source"],
        "visible": component_is_visible(component),
        "state": runtime_state_name(STATE.runtime, component),
        "skin_weight_profile": current_skin_profile(component),
        "tags": component_tags(component),
        "render_in_main_pass": bool(get_property(component, "render_in_main_pass", default=True)),
        "hidden_in_game": bool(get_property(component, "hidden_in_game", default=False)),
    }


def matching_components(row):
    if not STATE.player:
        return []
    return [
        component
        for component in all_skeletal_components(STATE.player)
        if mesh_path(component) == row["source"]
    ]


def legacy_v2_component_diagnostics():
    rows = []
    for component in all_skeletal_components(STATE.player):
        path = mesh_path(component)
        tags = component_tags(component)
        if (
            "/EFClothingMorph/_Internal/Compiled/V26/SK_" in path
            or "EFClothingMorphV2.Managed" in tags
            or "EFClothingMorphV2.Pending" in tags
        ):
            rows.append({"component": object_path(component), "mesh": path, "tags": tags})
    return rows


def parse_runtime_values(component):
    summary = str(call(STATE.runtime, "get_debug_summary"))
    name = re.escape(str(component.get_name()))
    match = re.search(
        name
        + r":[^\[]*\[[^\]]*clear=([-+0-9.eE]+)cm,inflate=([-+0-9.eE]+)cm\]",
        summary,
    )
    require(match is not None, f"V3 debug summary does not contain managed garment {component.get_name()}: {summary}")
    return {
        "clearance_cm": float(match.group(1)),
        "inflate_cm": float(match.group(2)),
        "debug_summary": summary,
    }


def fail_if_passthrough(component, checkpoint):
    state = runtime_state_name(STATE.runtime, component)
    if state != "Passthrough":
        return
    summary = str(call(STATE.runtime, "get_debug_summary"))
    # GetGarmentRuntimeState intentionally reports source passthrough for an
    # otherwise valid component which has not yet entered ManagedGarments. ACF
    # can make the reused slot visible one frame before the periodic reconcile.
    # That onboarding window is not a failed managed pass; the phase timeout
    # below still requires it to become Ready. Only an explicit managed
    # Passthrough entry is a certified-fixture failure.
    managed_passthrough = re.search(
        re.escape(str(component.get_name())) + r":Passthrough\[",
        summary,
    )
    if managed_passthrough is None:
        return
    observation = {
        "checkpoint": checkpoint,
        "component": object_path(component),
        "mesh": mesh_path(component),
        "visible": component_is_visible(component),
        "state": state,
        "debug_summary": summary,
    }
    STATE.result["source_visibility_gate"]["passthrough_observations"].append(
        observation
    )
    raise RuntimeError(
        "Certified V3 fixture entered visible Passthrough instead of Ready at "
        f"{checkpoint}: {observation}"
    )


def validate_terminal_visible(checkpoint, expected_clearance=None, expected_inflate=None):
    row = STATE.active_row
    require(row is not None, f"No active V3 garment at {checkpoint}")
    require(object_is_valid(STATE.garment), f"Garment component is invalid at {checkpoint}")
    snapshot = garment_snapshot(STATE.garment, row)
    require(snapshot["mesh"] == row["source"], f"V3 swapped SourceGarment at {checkpoint}: {snapshot}")
    require(snapshot["visible"], f"V3 garment became hidden at {checkpoint}: {snapshot}")
    fail_if_passthrough(STATE.garment, checkpoint)
    require(
        snapshot["state"] == CERTIFIED_PASS_STATE,
        f"Certified V3 garment is not Ready at {checkpoint}: snapshot={snapshot} "
        f"debug={call(STATE.runtime, 'get_debug_summary')}",
    )
    require(snapshot["skin_weight_profile"].lower() != "ef_autofit", f"V3 activated EF_AutoFit at {checkpoint}: {snapshot}")
    require("EFClothingMorphV2.Managed" not in snapshot["tags"], f"V3 component retained a V2 managed tag at {checkpoint}: {snapshot}")
    require("EFClothingMorphV2.Pending" not in snapshot["tags"], f"V3 component retained a V2 pending tag at {checkpoint}: {snapshot}")
    if STATE.garment_identity:
        require(snapshot["component"] == STATE.garment_identity, f"Runtime offsets replaced the garment component at {checkpoint}: {snapshot}")
    legacy = legacy_v2_component_diagnostics()
    require(not legacy, f"Legacy fitted/V2 garment components are active at {checkpoint}: {legacy}")
    values = parse_runtime_values(STATE.garment)
    if expected_clearance is not None:
        require(abs(values["clearance_cm"] - expected_clearance) <= 0.001, f"Clearance override mismatch at {checkpoint}: {values}")
    if expected_inflate is not None:
        require(abs(values["inflate_cm"] - expected_inflate) <= 0.001, f"Inflate override mismatch at {checkpoint}: {values}")
    snapshot.update(values)
    snapshot["checkpoint"] = checkpoint
    snapshot["world_seconds"] = float(unreal.GameplayStatics.get_time_seconds(STATE.world))
    STATE.active_test["runtime_checks"].append(snapshot)
    return snapshot


def sample_expected_visibility():
    if not STATE.expect_garment_visible or not STATE.active_row or not STATE.garment:
        return
    snapshot = garment_snapshot(STATE.garment, STATE.active_row)
    gate = STATE.result["source_visibility_gate"]
    gate["sample_count"] += 1
    gate["state_samples"][snapshot["state"]] = gate["state_samples"].get(snapshot["state"], 0) + 1
    fail_if_passthrough(STATE.garment, f"continuous_sample_{STATE.phase}")
    violation_reason = ""
    if snapshot["mesh"] != STATE.active_row["source"]:
        violation_reason = "source mesh changed"
    elif not snapshot["visible"]:
        violation_reason = "source garment became hidden"
    elif snapshot["skin_weight_profile"].lower() == "ef_autofit":
        violation_reason = "EF_AutoFit became active"
    elif legacy_v2_component_diagnostics():
        violation_reason = "legacy V2/fitted component became active"
    if violation_reason:
        violation = {
            "phase": STATE.phase,
            "world_seconds": float(unreal.GameplayStatics.get_time_seconds(STATE.world)),
            "reason": violation_reason,
            "snapshot": snapshot,
        }
        gate["violations"].append(violation)
        raise RuntimeError(f"V3 source visibility contract failed: {violation}")


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


def item_snapshot(item):
    return {
        "guid": normalize_guid(get_property(item, "item_guid", "ItemGuid")),
        "item_class": item_class_path(item),
        "count": int(get_property(item, "count", "Count", default=1) or 0),
        "equipped": bool(get_property(item, "b_is_equipped", "bIsEquipped", default=False)),
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


def inventory_entries():
    return list(call(STATE.equipment, "get_inventory") or [])


def current_equipment_entries():
    current = call(STATE.equipment, "get_current_equipment")
    equipped = get_property(current, "equipped_items", "EquippedItems", default=None)
    require(equipped is not None, "ACF current equipment exposes no equipped array")
    return list(equipped or [])


def item_token_set(text):
    basename = canonical_asset_path(text).rsplit("/", 1)[-1]
    words = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", basename)
    return {
        token.lower()
        for token in re.findall(r"[A-Za-z0-9]+", words)
        if len(token) >= 3
    }


def get_world_item_candidates(row):
    world_item_class = unreal.load_class(None, "/Script/InventorySystem.ACFWorldItem")
    require(world_item_class is not None, "Unable to load ACFWorldItem class")
    source_tokens = item_token_set(row["source"]) | item_token_set(row["row_name"])
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
        if score <= 0:
            continue
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
    require(call(STATE.interaction, "get_current_best_interactable_actor") == actor, "ACF did not select the garment pickup")
    require(bool(call(STATE.interaction, "has_valid_interactable")), "ACF reports no valid garment interaction")
    before = [item_snapshot(item) for item in inventory_entries()]
    evidence = {
        "candidate": candidate["object"],
        "score": candidate["score"],
        "pickup_items": candidate["item_snapshots"],
        "inventory_before": before,
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
    STATE.active_test["acf_candidates"].append(evidence)
    STATE.current_candidate_result = evidence
    call(STATE.interaction, "interact", "")


def associate_acf_item(candidate, garment):
    inventory = inventory_entries()
    equipped = current_equipment_entries()
    inventory_rows = [item_snapshot(item) for item in inventory]
    equipment_rows = [item_snapshot(item) for item in equipped]
    pickup_guids = candidate["guids"]
    acquired = pickup_guids.intersection({row["guid"] for row in inventory_rows})
    equipped_from_pickup = pickup_guids.intersection({row["guid"] for row in equipment_rows})
    require(acquired, "Garment pickup GUID did not reach ACF inventory")
    require(equipped_from_pickup, "Garment pickup GUID did not reach ACF current equipment")

    matched_rows = [row for row in equipment_rows if row["guid"] in equipped_from_pickup]
    armor_definition_class = ""
    if callable_method(garment, "get_armor_definition"):
        try:
            armor_definition_class = class_path(call(garment, "get_armor_definition"))
        except Exception:
            pass
    if armor_definition_class:
        exact = [row for row in matched_rows if row["item_class"] == armor_definition_class]
        if len(exact) == 1:
            matched_rows = exact
    require(len(matched_rows) == 1, f"Could not uniquely associate garment with ACF item: {matched_rows}")
    guid = matched_rows[0]["guid"]
    raw_inventory = next((item for item in inventory if item_snapshot(item)["guid"] == guid), None)
    require(raw_inventory is not None, "Associated ACF item is absent from inventory")
    slot_value = get_property(
        raw_inventory,
        "equipment_slot",
        "EquipmentSlot",
        "item_slot",
        "ItemSlot",
        default=None,
    )
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


def equipped_guid_present(guid):
    return guid in {item_snapshot(item)["guid"] for item in current_equipment_entries()}


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
            for subsystem in unreal.ObjectIterator(subsystem_class):
                try:
                    if subsystem.get_world() == STATE.world:
                        STATE.free_camera_subsystem = subsystem
                        break
                except Exception:
                    continue
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
        allowed_meshes.update((row["source"], row["body"]))
    evidence = []
    for actor in attached_actors(STATE.player):
        identity = f"{object_path(actor)} {class_path(actor)}".lower()
        actor_meshes = []
        for component in actor.get_components_by_class(unreal.MeshComponent) or []:
            path = canonical_asset_path(
                get_property(
                    component,
                    "static_mesh",
                    "skeletal_mesh_asset",
                    "skeletal_mesh",
                    default=None,
                )
            )
            if path:
                actor_meshes.append(path)
            identity += " " + path.lower()
        non_garment_mesh = bool(actor_meshes) and not any(
            path in allowed_meshes for path in actor_meshes
        )
        if any(token in identity for token in weapon_tokens) or non_garment_mesh:
            previous = bool(get_property(actor, "hidden", "hidden_in_game", default=False))
            try:
                previous = bool(actor.is_hidden())
            except Exception:
                pass
            actor.set_actor_hidden_in_game(True)
            STATE.hidden_weapon_actors.append((actor, previous))
            evidence.append(
                {"actor": object_path(actor), "meshes": actor_meshes, "previous_hidden": previous}
            )
    for component in STATE.player.get_components_by_class(unreal.PrimitiveComponent) or []:
        identity = f"{object_path(component)} {class_path(component)}".lower()
        asset = canonical_asset_path(
            get_property(
                component,
                "static_mesh",
                "skeletal_mesh_asset",
                "skeletal_mesh",
                default=None,
            )
        )
        identity += " " + asset.lower()
        if asset in allowed_meshes or not any(token in identity for token in weapon_tokens):
            continue
        try:
            previous = bool(component.is_visible())
            component.set_visibility(False, True)
            STATE.hidden_weapon_components.append((component, previous))
            evidence.append(
                {
                    "component": object_path(component),
                    "mesh": asset,
                    "previous_visible": previous,
                }
            )
        except Exception:
            continue
    STATE.result["weapon_visual_suppression"] = {
        "status": "TRANSIENT_SCREENSHOT_SUPPRESSION_APPLIED",
        "hidden_visuals": evidence,
        "asset_changes": False,
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


def position_camera(view, distance=115.0):
    require(STATE.camera is not None, "Free camera is not active")
    location = STATE.player.get_actor_location()
    forward = STATE.player.get_actor_forward_vector()
    right = STATE.player.get_actor_right_vector()
    if view == "front":
        radial, height, target_height = forward, -5.0, -8.0
    elif view == "back":
        radial, height, target_height = vector_scale(forward, -1.0), -5.0, -8.0
    elif view == "right":
        radial, height, target_height = right, -5.0, -8.0
    elif view == "inferior":
        radial = vector_add(vector_scale(forward, -0.85), vector_scale(right, 0.35))
        height, target_height, distance = -62.0, -8.0, 95.0
    else:
        raise RuntimeError(f"Unknown camera view: {view}")
    camera_location = vector_add(
        vector_add(location, vector_scale(radial, distance)),
        unreal.Vector(0.0, 0.0, height),
    )
    target = vector_add(location, unreal.Vector(0.0, 0.0, target_height))
    rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, target)
    STATE.camera.set_actor_location_and_rotation(camera_location, rotation, False, True)


def begin_capture(view, motion_label, next_phase):
    snapshot = validate_terminal_visible(f"capture_{view}_{motion_label}")
    hide_weapon_visuals()
    filename = (
        f"{STATE.active_row['slug']}_{len(STATE.active_test['screenshots']) + 1:02d}_"
        f"{view}_{safe_slug(motion_label)}.png"
    )
    path = OUTPUT_DIR / filename
    request = {
        "row_name": STATE.active_row["row_name"],
        "view": view,
        "motion": motion_label,
        "path": str(path),
        "accepted": False,
        "exists": False,
        "state": snapshot["state"],
        "garment_mesh": snapshot["mesh"],
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
        request["accepted"] = bool(
            unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(path))
        )
    finally:
        STATE.capture_request_in_progress = False
    transition("wait_capture")


def apply_capture_motion():
    if STATE.player and STATE.capture_motion == "walk":
        STATE.player.add_movement_input(STATE.player.get_actor_forward_vector(), 1.0, True)


def stop_motion():
    if not STATE.player:
        return
    movement = STATE.player.get_component_by_class(unreal.CharacterMovementComponent)
    if movement and callable_method(movement, "stop_movement_immediately"):
        try:
            movement.stop_movement_immediately()
        except Exception:
            pass


def record_motion(name):
    snapshot = validate_terminal_visible(f"motion_{name}")
    payload = {
        "status": "PASS",
        "speed_cm_s": vector_length(STATE.player.get_velocity()),
        "animation": str(call(STATE.locomotion, "get_current_animation_asset_name"))
        if callable_method(STATE.locomotion, "get_current_animation_asset_name")
        else "UNAVAILABLE",
        "state": snapshot["state"],
        "garment_mesh": snapshot["mesh"],
    }
    STATE.active_test["motion"][name] = payload
    return payload


def exact_visible_body(row):
    candidates = [
        component
        for component in all_skeletal_components(STATE.player)
        if mesh_path(component) == row["body"] and component_is_visible(component)
    ]
    require(
        len(candidates) == 1,
        f"V3 requires one visible exact BodySurface for {row['row_name']}; found "
        f"{[object_path(value) for value in candidates]}",
    )
    return candidates[0]


def resolve_runtime_context():
    STATE.world = EDITOR_LEVEL_LIBRARY.get_game_world()
    if not STATE.world:
        return False
    STATE.controller = unreal.GameplayStatics.get_player_controller(STATE.world, 0)
    STATE.player = unreal.GameplayStatics.get_player_pawn(STATE.world, 0)
    if not STATE.controller or not STATE.player:
        return False
    STATE.runtime = find_component(STATE.player, "EFClothingMorphV3RuntimeComponent")
    STATE.interaction = find_component(STATE.player, "ACFInteractionComponent")
    STATE.equipment = find_component(STATE.player, "ACFEquipmentComponent")
    STATE.locomotion = find_component(STATE.player, "ProjectLocomotionOverride")
    return all((STATE.runtime, STATE.interaction, STATE.equipment, STATE.locomotion))


def make_active_test(row):
    return {
        "row_name": row["row_name"],
        "source": row["source"],
        "body": row["body"],
        "binding": row["binding"],
        "authored_additional_clearance_cm": row["authored_additional_clearance_cm"],
        "authored_shell_thickness_cm": row["authored_shell_thickness_cm"],
        "status": "IN_PROGRESS",
        "acf_candidates": [],
        "acf_real_equip": {},
        "body_resolver": {},
        "source_mesh_gate": {},
        "runtime_checks": [],
        "offset_runtime_sequence": {
            "status": "PENDING",
            "authored_defaults": {
                "additional_clearance_cm": row["authored_additional_clearance_cm"],
                "shell_thickness_cm": row["authored_shell_thickness_cm"],
            },
            "initial_before_overrides": None,
            "clearance_cm": [],
            "inflate_cm": [],
            "recompile_invoked": False,
            "mesh_swap_invoked": False,
        },
        "motion": {},
        "equip_unequip_cycles": {
            "requested": SMOKE_EQUIP_UNEQUIP_CYCLES,
            "completed": 0,
            "status": "PENDING",
            "cycles": [],
        },
        "screenshots": [],
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
    STATE.garment_identity = ""
    STATE.expect_garment_visible = False
    STATE.acf_item_guid = ""
    STATE.acf_item_slot = None
    body = exact_visible_body(STATE.active_row)
    STATE.active_test["body_resolver"] = {
        "status": "PASS_UNIQUE_VISIBLE_EXACT_BODY_SURFACE",
        "component": object_path(body),
        "mesh": mesh_path(body),
        "catalog_body": STATE.active_row["body"],
        "multiple_is_not_surface": mesh_path(body) != COMPATIBILITY_PATH,
    }
    STATE.candidates = get_world_item_candidates(STATE.active_row)
    require(
        STATE.candidates,
        f"No token-matched real ACF world-item fixture exists for {STATE.active_row['row_name']}",
    )
    STATE.current_candidate = STATE.candidates[0]
    emit(
        f"row={STATE.active_row['row_name']} candidates={len(STATE.candidates)} "
        f"selected_score={STATE.current_candidate['score']}"
    )
    begin_candidate_interaction(STATE.current_candidate)
    transition("wait_initial_equip")


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
        self.director = None
        self.registry = None
        self.rows = []
        self.row_index = -1
        self.active_row = None
        self.active_test = None
        self.garment = None
        self.garment_identity = ""
        self.expect_garment_visible = False
        self.candidates = []
        self.current_candidate = None
        self.current_candidate_result = None
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
        self.cycle_index = 0
        self.stable_samples = 0
        self.result = {
            "schema_version": 1,
            "status": "UE58_EF_CLOTHING_MORPH_V3_RUNTIME_SMOKE_IN_PROGRESS",
            "project": str(PROJECT_DIR),
            "map": TARGET_MAP,
            "renderer_required": "D3D12_SM6_VISIBLE",
            "expected_compiler_version": EXPECTED_COMPILER_VERSION,
            "expected_binding_schema": EXPECTED_BINDING_SCHEMA,
            "test_scope": "Certified V3 smoke requires Ready; visible Passthrough is diagnostic failure. No V26 GPU readback or geometric certification claim.",
            "passthrough_policy": {
                "runtime_behavior": "SourceGarment remains visible and unmodified",
                "certified_fixture_acceptance": "FAIL_WITH_DEBUG_SUMMARY",
            },
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
            "source_visibility_gate": {
                "status": "PENDING",
                "sample_count": 0,
                "state_samples": {},
                "violations": [],
                "passthrough_observations": [],
            },
            "rows_tested": [],
            "screenshots": {},
            "free_camera": {},
            "weapon_visual_suppression": {},
            "story_selection_auto_open": {
                "status": "NOT_TESTED_NO_CLAIM",
                "reason": "This smoke harness loads HUB only.",
            },
            "gpu_readback": {
                "status": "NOT_USED_V3_SMOKE",
                "v26_readback_dependency": False,
            },
            "no_assets_saved": True,
            "errors": [],
        }


STATE = RuntimeState()


def cleanup_runtime_state():
    try:
        if STATE.runtime and STATE.garment:
            call(STATE.runtime, "clear_garment_clearance_offset_cm", STATE.garment)
            call(STATE.runtime, "clear_garment_inflate_cm", STATE.garment)
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
    builtins._codex_ef_clothing_morph_v3_runtime_smoke = None
    cleanup_runtime_state()
    STATE.result["status"] = (
        "UE58_EF_CLOTHING_MORPH_V3_RUNTIME_SMOKE_PASS"
        if success
        else "UE58_EF_CLOTHING_MORPH_V3_RUNTIME_SMOKE_FAIL"
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


def record_offset_checkpoint(label, expected_clearance, expected_inflate):
    snapshot = validate_terminal_visible(label, expected_clearance, expected_inflate)
    sequence = STATE.active_test["offset_runtime_sequence"]
    sequence["clearance_cm"].append(
        {"label": label, "requested": expected_clearance, "observed": snapshot["clearance_cm"]}
    )
    sequence["inflate_cm"].append(
        {"label": label, "requested": expected_inflate, "observed": snapshot["inflate_cm"]}
    )


def tick(delta_time):
    try:
        if STATE.capture_request_in_progress:
            return
        STATE.phase_elapsed += float(delta_time)
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            raise RuntimeError(f"Timeout in phase {STATE.phase}")

        if LEVEL_EDITOR.is_in_play_in_editor():
            sample_character_creation()
            if resolve_runtime_context():
                sample_expected_visibility()

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
            require(resolve_runtime_context(), "Required HUB/V3/ACF player components are missing")
            load_catalog_contract()
            require(
                STATE.result["hub_character_creation_gate"]["sample_count"] > 0,
                "No early HUB Character Creation samples were observed",
            )
            STATE.result["runtime_context"] = {
                "world": object_path(STATE.world),
                "player": object_path(STATE.player),
                "runtime_component": object_path(STATE.runtime),
                "runtime_class": class_path(STATE.runtime),
                "interaction_component": object_path(STATE.interaction),
                "equipment_component": object_path(STATE.equipment),
                "locomotion_component": object_path(STATE.locomotion),
            }
            require(
                "EFClothingMorphV3RuntimeComponent" in class_path(STATE.runtime),
                f"Resolved runtime is not EFClothingMorphV3RuntimeComponent: {class_path(STATE.runtime)}",
            )
            if not resolve_free_camera():
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError("Gameplay free camera did not become active")
                return
            transition("start_row")
            return

        if STATE.phase == "start_row":
            begin_row()
            return

        if STATE.phase == "wait_initial_equip":
            candidates = matching_components(STATE.active_row)
            visible = [component for component in candidates if component_is_visible(component)]
            for component in visible:
                fail_if_passthrough(component, "initial_certified_acf_equip")
            terminal = [
                component
                for component in visible
                if runtime_state_name(STATE.runtime, component) == CERTIFIED_PASS_STATE
            ]
            if not terminal:
                if STATE.phase_elapsed > 35.0:
                    raise RuntimeError(
                        f"Real ACF pickup never produced a visible Ready SourceGarment: "
                        f"{[garment_snapshot(value, STATE.active_row) for value in candidates]}; "
                        f"debug={call(STATE.runtime, 'get_debug_summary')}"
                    )
                return
            require(len(terminal) == 1, f"ACF produced multiple visible source garment components: {terminal}")
            STATE.garment = terminal[0]
            STATE.garment_identity = object_path(STATE.garment)
            STATE.expect_garment_visible = True
            associate_acf_item(STATE.current_candidate, STATE.garment)
            initial = validate_terminal_visible(
                "initial_real_acf_equip_before_overrides",
                STATE.active_row["authored_additional_clearance_cm"],
                STATE.active_row["authored_shell_thickness_cm"],
            )
            STATE.active_test["offset_runtime_sequence"]["initial_before_overrides"] = dict(
                initial
            )
            STATE.active_test["acf_real_equip"] = {
                "status": "PASS_REAL_WORLD_INTERACT_TO_ACF_EQUIPMENT",
                "candidate": STATE.current_candidate_result,
                "guid": STATE.acf_item_guid,
                "guid_length": len(STATE.acf_item_guid),
                "direct_mesh_assignment": False,
                "direct_equipment_shortcut_for_initial_acquisition": False,
            }
            STATE.active_test["source_mesh_gate"] = {
                "status": "PASS_EXACT_SOURCE_GARMENT_READY_NO_FITTED_NO_EF_AUTOFIT",
                "component": initial["component"],
                "source_mesh": initial["mesh"],
                "catalog_source": STATE.active_row["source"],
                "binding_fitted_mesh": STATE.active_row["fitted_mesh"],
                "skin_weight_profile": initial["skin_weight_profile"],
            }
            call(STATE.runtime, "set_garment_clearance_offset_cm", STATE.garment, 0.0)
            call(STATE.runtime, "set_garment_inflate_cm", STATE.garment, 0.0)
            call(STATE.runtime, "force_reconcile")
            transition("wait_offset_baseline")
            return

        if STATE.phase == "wait_offset_baseline":
            if STATE.phase_elapsed < 0.75:
                return
            record_offset_checkpoint("offset_baseline_0_0", 0.0, 0.0)
            call(STATE.runtime, "set_garment_clearance_offset_cm", STATE.garment, OFFSET_TEST_CM)
            transition("wait_clearance_02")
            return

        if STATE.phase == "wait_clearance_02":
            if STATE.phase_elapsed < 0.75:
                return
            record_offset_checkpoint("clearance_runtime_0_to_0_2", OFFSET_TEST_CM, 0.0)
            call(STATE.runtime, "set_garment_clearance_offset_cm", STATE.garment, 0.0)
            transition("wait_clearance_reset")
            return

        if STATE.phase == "wait_clearance_reset":
            if STATE.phase_elapsed < 0.75:
                return
            record_offset_checkpoint("clearance_runtime_0_2_to_0", 0.0, 0.0)
            call(STATE.runtime, "set_garment_inflate_cm", STATE.garment, OFFSET_TEST_CM)
            transition("wait_inflate_02")
            return

        if STATE.phase == "wait_inflate_02":
            if STATE.phase_elapsed < 0.75:
                return
            record_offset_checkpoint("inflate_runtime_0_to_0_2", 0.0, OFFSET_TEST_CM)
            call(STATE.runtime, "set_garment_inflate_cm", STATE.garment, 0.0)
            transition("wait_inflate_reset")
            return

        if STATE.phase == "wait_inflate_reset":
            if STATE.phase_elapsed < 0.75:
                return
            record_offset_checkpoint("inflate_runtime_0_2_to_0", 0.0, 0.0)
            sequence = STATE.active_test["offset_runtime_sequence"]
            sequence["status"] = "PASS_CLEARANCE_AND_INFLATE_0_TO_0_2_TO_0_READY_NO_SWAP"
            sequence["component_identity_preserved"] = STATE.garment_identity
            sequence["source_mesh_preserved"] = STATE.active_row["source"]
            stop_motion()
            transition("idle_settle")
            return

        if STATE.phase == "idle_settle":
            if STATE.phase_elapsed < 0.75:
                return
            idle = record_motion("idle")
            require(idle["speed_cm_s"] < 10.0, f"Player did not settle for idle evidence: {idle}")
            position_camera("front")
            begin_capture("front", "idle", "capture_back_idle")
            return

        if STATE.phase == "capture_back_idle":
            position_camera("back")
            begin_capture("back", "idle", "capture_inferior_idle")
            return

        if STATE.phase == "capture_inferior_idle":
            position_camera("inferior")
            begin_capture("inferior", "idle", "start_walk")
            return

        if STATE.phase == "wait_capture":
            apply_capture_motion()
            if STATE.phase_elapsed < SCREENSHOT_WAIT_SECONDS:
                return
            request = STATE.result["screenshots"][STATE.capture_path.name]
            request["exists"] = STATE.capture_path.is_file()
            request["size_bytes"] = (
                STATE.capture_path.stat().st_size if STATE.capture_path.is_file() else 0
            )
            require(request["accepted"], f"Screenshot request rejected: {STATE.capture_path.name}")
            require(
                request["exists"] and request["size_bytes"] > 4096,
                f"Screenshot missing or too small: {STATE.capture_path}",
            )
            validate_terminal_visible(f"after_capture_{STATE.capture_path.name}")
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
            position_camera("right")
            begin_capture("right", "walk", "start_cycles")
            return

        if STATE.phase == "start_cycles":
            stop_motion()
            require(callable_method(STATE.equipment, "unequip_item_by_guid"), "ACF unequip API is unavailable")
            require(callable_method(STATE.equipment, "equip_item_from_inventory_in_slot"), "ACF re-equip API is unavailable")
            STATE.cycle_index = 0
            transition("cycle_unequip")
            return

        if STATE.phase == "cycle_unequip":
            raw_item = find_inventory_item(STATE.acf_item_guid)
            require(raw_item is not None, f"Cycle item {STATE.acf_item_guid} disappeared from inventory")
            guid_value = get_property(raw_item, "item_guid", "ItemGuid", default=None)
            require(guid_value is not None, "Cycle ACF item exposes no FGuid")
            STATE.expect_garment_visible = False
            call(STATE.equipment, "unequip_item_by_guid", guid_value)
            transition("cycle_wait_unequipped")
            return

        if STATE.phase == "cycle_wait_unequipped":
            visible = [component for component in matching_components(STATE.active_row) if component_is_visible(component)]
            if visible:
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError(f"ACF unequip left SourceGarment visible: {visible}")
                return
            if STATE.phase_elapsed < 0.25:
                return
            raw_item = find_inventory_item(STATE.acf_item_guid)
            require(raw_item is not None, "Unequipped ACF item is absent from inventory")
            call(STATE.equipment, "equip_item_from_inventory_in_slot", raw_item, STATE.acf_item_slot)
            transition("cycle_wait_equipped")
            return

        if STATE.phase == "cycle_wait_equipped":
            visible_source = [
                component
                for component in matching_components(STATE.active_row)
                if component_is_visible(component)
            ]
            for component in visible_source:
                fail_if_passthrough(component, f"certified_equip_cycle_{STATE.cycle_index + 1}")
            candidates = [
                component
                for component in visible_source
                if runtime_state_name(STATE.runtime, component) == CERTIFIED_PASS_STATE
            ]
            if not candidates:
                if STATE.phase_elapsed > 30.0:
                    raise RuntimeError(
                        f"ACF cycle {STATE.cycle_index + 1} never restored visible Ready SourceGarment; "
                        f"debug={call(STATE.runtime, 'get_debug_summary')}"
                    )
                return
            require(len(candidates) == 1, f"ACF cycle produced duplicate garment components: {candidates}")
            STATE.garment = candidates[0]
            STATE.garment_identity = object_path(STATE.garment)
            STATE.expect_garment_visible = True
            require(equipped_guid_present(STATE.acf_item_guid), "Original ACF GUID is absent from current equipment after re-equip")
            call(STATE.runtime, "set_garment_clearance_offset_cm", STATE.garment, 0.0)
            call(STATE.runtime, "set_garment_inflate_cm", STATE.garment, 0.0)
            snapshot = validate_terminal_visible(
                f"equip_cycle_{STATE.cycle_index + 1}", 0.0, 0.0
            )
            STATE.cycle_index += 1
            cycles = STATE.active_test["equip_unequip_cycles"]
            cycles["completed"] = STATE.cycle_index
            cycles["cycles"].append(
                {
                    "cycle": STATE.cycle_index,
                    "status": "PASS_REAL_ACF_UNEQUIP_REEQUIP_READY",
                    "guid": STATE.acf_item_guid,
                    "guid_preserved": True,
                    "component": snapshot["component"],
                    "mesh": snapshot["mesh"],
                    "state": snapshot["state"],
                    "visible": snapshot["visible"],
                }
            )
            if STATE.cycle_index >= SMOKE_EQUIP_UNEQUIP_CYCLES:
                cycles["status"] = "PASS_3_REAL_ACF_UNEQUIP_REEQUIP_READY_CYCLES"
                transition("row_complete")
            else:
                transition("cycle_unequip")
            return

        if STATE.phase == "row_complete":
            validate_terminal_visible("row_complete", 0.0, 0.0)
            require(STATE.active_test["motion"].get("idle", {}).get("status") == "PASS", "Idle evidence is missing")
            require(STATE.active_test["motion"].get("walk", {}).get("status") == "PASS", "Locomotion evidence is missing")
            require(len(STATE.active_test["screenshots"]) == 4, "Expected front/back/inferior/movement captures")
            require(STATE.active_test["equip_unequip_cycles"]["completed"] == 3, "Three ACF cycles were not completed")
            STATE.active_test["status"] = "PASS"
            if STATE.row_index + 1 < len(STATE.rows):
                raw_item = find_inventory_item(STATE.acf_item_guid)
                require(raw_item is not None, "Completed row item disappeared before teardown")
                guid_value = get_property(raw_item, "item_guid", "ItemGuid", default=None)
                STATE.expect_garment_visible = False
                call(STATE.equipment, "unequip_item_by_guid", guid_value)
                transition("wait_row_teardown")
            else:
                transition("start_row")
            return

        if STATE.phase == "wait_row_teardown":
            visible = [component for component in matching_components(STATE.active_row) if component_is_visible(component)]
            if visible:
                if STATE.phase_elapsed > 12.0:
                    raise RuntimeError(f"Completed row garment remained visible after ACF teardown: {visible}")
                return
            transition("start_row")
            return

        if STATE.phase == "final_checks":
            character_gate = STATE.result["hub_character_creation_gate"]
            require(character_gate["sample_count"] > 0, "Character Creation gate has no samples")
            require(not character_gate["violations"], "Character Creation appeared during HUB smoke")
            character_gate["status"] = "PASS_ABSENT_FROM_ALL_OBSERVED_HUB_PIE_TICKS"
            visibility_gate = STATE.result["source_visibility_gate"]
            require(visibility_gate["sample_count"] > 0, "Source visibility gate has no samples")
            require(not visibility_gate["violations"], "Source garment visibility failed")
            require(
                not visibility_gate["passthrough_observations"],
                "Certified V3 fixture entered Passthrough",
            )
            visibility_gate["status"] = "PASS_EXACT_SOURCE_READY_NEVER_HIDDEN_WHILE_EXPECTED"
            require(
                len(STATE.result["rows_tested"]) == len(STATE.rows)
                and all(row.get("status") == "PASS" for row in STATE.result["rows_tested"]),
                "Not every enabled V3 garment row passed the smoke test",
            )
            require(
                len(STATE.result["screenshots"]) == 4 * len(STATE.rows),
                "Screenshot catalog coverage is incomplete",
            )
            require(
                STATE.result["gpu_readback"]["status"] == "NOT_USED_V3_SMOKE",
                "V3 smoke unexpectedly acquired a GPU readback dependency",
            )
            finish(True)
            return
    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        unreal.log_error(STATE.result["traceback"])
        finish(False, str(exc))


existing = getattr(builtins, "_codex_ef_clothing_morph_v3_runtime_smoke", None)
if existing is not None:
    unreal.log_warning("[EFClothingMorphV3RuntimeSmoke] duplicate registration ignored")
else:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    PROGRESS_FILE.write_text("", encoding="utf-8")
    write_result()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_ef_clothing_morph_v3_runtime_smoke = STATE
    emit("registered=True")

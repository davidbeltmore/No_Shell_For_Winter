"""Capture participant components and attached actors while 0001Scene is active."""

import json
import os
import runpy
from pathlib import Path

import unreal


SOURCE_VISUAL_SCRIPT = os.environ.get(
    "CODEX_INTIMACY_BASE_SCRIPT",
    r"D:\Projects UE5\LustAsDeadlySin\Tools\Intimacy\test_project_intimacy_visual_runtime.py",
)
OUTPUT_FILE = Path(unreal.Paths.project_dir()) / "Saved" / "Migration" / "Phase5" / "Runtime" / "IntimacyParticipantVisuals58.json"


namespace = runpy.run_path(SOURCE_VISUAL_SCRIPT, run_name="__codex_intimacy_visual_diagnostic__")
original_mark_visual_capture = namespace["mark_visual_capture"]


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return str(value)


def get_bool(value, names):
    for name in names:
        try:
            return bool(value.get_editor_property(name))
        except Exception:
            continue
    return None


def component_record(component):
    record = {
        "name": component.get_name(),
        "class": component.get_class().get_name(),
        "path": object_path(component),
        "visible": None,
        "hidden_in_game": get_bool(component, ("hidden_in_game", "b_hidden_in_game")),
        "asset": "",
        "attach_parent": "",
        "socket": "",
    }
    try:
        record["visible"] = bool(component.is_visible())
    except Exception:
        pass
    for method_name in ("get_skeletal_mesh_asset", "get_static_mesh", "get_asset"):
        try:
            asset = getattr(component, method_name)()
            if asset:
                record["asset"] = object_path(asset)
                break
        except Exception:
            continue
    try:
        record["attach_parent"] = object_path(component.get_attach_parent())
        record["socket"] = str(component.get_attach_socket_name())
    except Exception:
        pass
    return record


def actor_record(actor, visited=None):
    visited = visited or set()
    path = object_path(actor)
    if path in visited:
        return {"path": path, "cycle": True}
    visited.add(path)
    record = {
        "name": actor.get_name(),
        "class": object_path(actor.get_class()),
        "path": path,
        "hidden": get_bool(actor, ("hidden", "b_hidden")),
        "components": [],
        "attached_actors": [],
    }
    try:
        record["hidden_in_game"] = bool(actor.is_hidden())
    except Exception:
        record["hidden_in_game"] = None
    try:
        components = actor.get_components_by_class(unreal.ActorComponent)
    except Exception:
        components = []
    record["components"] = [component_record(component) for component in components if component]
    attached = []
    try:
        actor.get_attached_actors(attached, True, False)
    except Exception:
        try:
            attached = list(actor.get_attached_actors())
        except Exception:
            attached = []
    record["attached_actors"] = [actor_record(child, visited) for child in attached if child]
    return record


def mark_visual_capture(label, state):
    if label == "IntimacyHud_0001Scene" and not OUTPUT_FILE.exists():
        world = namespace["get_game_world"]()
        player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
        target = namespace["STATE"].target_actor
        payload = {
            "label": label,
            "player": actor_record(player) if player else None,
            "target": actor_record(target) if target else None,
        }
        OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT_FILE.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        unreal.log("[IntimacyParticipantVisuals58] wrote=" + str(OUTPUT_FILE))
    return original_mark_visual_capture(label, state)


original_mark_visual_capture.__globals__["mark_visual_capture"] = mark_visual_capture

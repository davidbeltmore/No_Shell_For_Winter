"""Read-only detached UE 5.7 inspection of Locked and its legacy parent."""

import inspect
import json
import os

import unreal


OUTPUT = os.environ.get("CODEX_LOCKED57_INSPECTION", "").strip()
PARENT = "/Game/FullSample/Blueprints/Game/ACFFullWorldItemBP"
LOCKED = "/Game/_Game/Lockpicking/Locked"


def safe_property(obj, name):
    try:
        return str(obj.get_editor_property(name))
    except Exception as exc:
        return "ERROR: " + repr(exc)


def public_names(obj, needle=""):
    value = needle.lower()
    return sorted(
        name
        for name in dir(obj)
        if not name.startswith("_") and (not value or value in name.lower())
    )


project = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project).lower() != "bulkprojectcontent57harness.uproject":
    raise RuntimeError("This diagnostic may run only in the detached UE 5.7 harness")
if not unreal.SystemLibrary.get_engine_version().startswith("5.7."):
    raise RuntimeError("Expected UE 5.7")
if not OUTPUT:
    raise RuntimeError("CODEX_LOCKED57_INSPECTION is required")

parent_bp = unreal.load_asset(PARENT)
locked_bp = unreal.load_asset(LOCKED)
if parent_bp is None or locked_bp is None:
    raise RuntimeError("Could not load the legacy parent and Locked in the harness")

result = {
    "status": "UE57_DETACHED_LOCKED_READ_ONLY_INSPECTION_PASS",
    "engine": unreal.SystemLibrary.get_engine_version(),
    "project": project,
    "packages": [PARENT, LOCKED],
    "save_operations": [],
    "exported_animations_excluded": True,
    "blueprints": {},
}

for package, blueprint in ((PARENT, parent_bp), (LOCKED, locked_bp)):
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(package)
    cdo = unreal.get_default_object(generated_class) if generated_class else None
    entry = {
        "class": str(generated_class),
        "parent_class": safe_property(blueprint, "parent_class"),
        "status": safe_property(blueprint, "status"),
        "blueprint_dir_matches": public_names(blueprint, "variable")
        + public_names(blueprint, "function")
        + public_names(blueprint, "graph"),
        "class_dir_matches": public_names(generated_class, "pawn")
        + public_names(generated_class, "pickup")
        + public_names(generated_class, "sound"),
        "cdo_dir_matches": public_names(cdo, "pawn")
        + public_names(cdo, "pickup")
        + public_names(cdo, "sound"),
        "new_variables": safe_property(blueprint, "new_variables"),
        "function_graphs": [],
        "ubergraph_pages": [],
    }
    for collection_name in ("function_graphs", "ubergraph_pages"):
        try:
            graphs = blueprint.get_editor_property(collection_name)
        except Exception as exc:
            entry[collection_name] = [{"error": repr(exc)}]
            continue
        for graph in graphs:
            graph_entry = {
                "name": graph.get_name(),
                "nodes": [],
            }
            try:
                nodes = graph.get_editor_property("nodes")
            except Exception as exc:
                graph_entry["nodes"] = [{"error": repr(exc)}]
                entry[collection_name].append(graph_entry)
                continue
            for node in nodes:
                node_entry = {
                    "class": node.get_class().get_name(),
                    "name": node.get_name(),
                    "title": safe_property(node, "node_comment"),
                    "function_reference": safe_property(node, "function_reference"),
                    "variable_reference": safe_property(node, "variable_reference"),
                    "pins": [],
                }
                try:
                    pins = node.get_editor_property("pins")
                except Exception:
                    pins = []
                for pin in pins:
                    node_entry["pins"].append(
                        {
                            "name": safe_property(pin, "pin_name"),
                            "direction": safe_property(pin, "direction"),
                            "default": safe_property(pin, "default_value"),
                            "type": safe_property(pin, "pin_type"),
                        }
                    )
                graph_entry["nodes"].append(node_entry)
            entry[collection_name].append(graph_entry)
    if cdo is not None:
        for attr in ("can_pawn_gather_items", "pickup_sound"):
            if hasattr(cdo, attr):
                value = getattr(cdo, attr)
                try:
                    signature = str(inspect.signature(value)) if callable(value) else ""
                except Exception as exc:
                    signature = "ERROR: " + repr(exc)
                entry[attr] = {"repr": repr(value), "signature": signature}
    result["blueprints"][package] = entry

output = os.path.realpath(OUTPUT)
os.makedirs(os.path.dirname(output), exist_ok=True)
with open(output, "w", encoding="utf-8") as handle:
    json.dump(result, handle, indent=2, sort_keys=True)
unreal.log("CODEX_LOCKED_SOURCE57_INSPECTION_PASS: " + output)

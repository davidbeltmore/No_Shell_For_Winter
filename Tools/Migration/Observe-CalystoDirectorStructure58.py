"""Arm a bounded read-only structural capture before a real-door PIE test.

Set builtins.CALYSTO_DIRECTOR_STRUCTURE_OUTPUT to a new project Saved evidence
directory, then run this script in the target editor. It issues no generation,
travel, navigation, asset-save or player-state commands. It unregisters after a
terminal first-floor observation or 60 seconds. Its measured overhead is reported.
"""
import builtins
import datetime
import json
import math
import time
import traceback
from pathlib import Path

import unreal

ROOT = Path(r"D:/Projects UE5/NoShellForWinter").resolve()
OUTPUT = Path(getattr(builtins, "CALYSTO_DIRECTOR_STRUCTURE_OUTPUT", "")).resolve()
if Path(unreal.Paths.project_dir()).resolve() != ROOT or not OUTPUT.is_relative_to(ROOT / "Saved"):
    raise RuntimeError("Structural observation requires the target project and an explicit Saved evidence path.")
if OUTPUT.exists():
    raise RuntimeError("Refusing to overwrite structural evidence.")
OUTPUT.mkdir(parents=True)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


class StructuralObserver:
    def __init__(self):
        self.started = time.monotonic()
        self.last_signature = None
        self.handle = None
        self.overhead = 0.0
        self.samples = []
        self.saw_pie = False
        self.director = None
        self.result = dict(schema=1, project=str(ROOT), status="ARMED", samples=self.samples,
                           generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())

    def finish(self, status, error=""):
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        self.result.update(status=status, error=error, elapsed_seconds=time.monotonic()-self.started,
                           observer_work_seconds=self.overhead)
        (OUTPUT / "structure.json").write_text(json.dumps(self.result, indent=2), encoding="utf-8")
        self.director = None

    def tick(self, _delta):
        began = time.monotonic()
        try:
            if began-self.started >= 60:
                self.finish("DEADLINE")
                return
            world = EDITOR.get_game_world()
            if not world:
                if self.saw_pie:
                    self.finish("PIE_ENDED")
                return
            self.saw_pie = True
            if self.director is None:
                instance_path = str(unreal.GameplayStatics.get_game_instance(world).get_path_name()) + "."
                for index, candidate in enumerate(unreal.ObjectIterator(unreal.EFCalystoDirectorSubsystem)):
                    if index >= 2000:
                        raise RuntimeError("Director discovery exceeded 2000 matching objects.")
                    if str(candidate.get_path_name()).startswith(instance_path):
                        self.director = candidate
                        break
            diagnostics = json.loads(self.director.get_diagnostics_json()) if self.director else {}
            actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
            if len(actors) > 4096:
                raise RuntimeError("Structural actor observation exceeded 4096 actors.")
            components = []
            native_actors = []
            for actor in actors:
                if str(actor.get_class().get_path_name()).endswith("BP_MassiveDungeon_C"):
                    native_actors.append(actor)
                    components.extend(actor.get_components_by_class(unreal.InstancedStaticMeshComponent))
            if len(components) > 1024:
                raise RuntimeError("Structural component observation exceeded 1024 components.")
            signature = (str(world.get_path_name()), str(diagnostics.get("attempt_id")),
                         str(diagnostics.get("state")), tuple((str(c.get_path_name()), c.get_instance_count()) for c in components))
            if signature != self.last_signature:
                if len(self.samples) >= 32:
                    raise RuntimeError("Structural snapshot observation exceeded 32 changes.")
                self.last_signature = signature
                sample = dict(elapsed_seconds=began-self.started, diagnostics=diagnostics, components=[], native_actors=[])
                for actor in native_actors:
                    authored = dict(path=str(actor.get_path_name()))
                    for title, names in {"wall_height": ("Wall Height", "Wall_Height", "wall_height"),
                                         "initial_wall_height": ("Initial Wall Height", "Initial_Wall_Height", "initial_wall_height")}.items():
                        for name in names:
                            try:
                                authored[title] = float(actor.get_editor_property(name))
                                break
                            except Exception:
                                continue
                    authored["pcg_outputs"] = []
                    for pcg in actor.get_components_by_class(unreal.PCGComponent):
                        collection = pcg.get_generated_graph_output()
                        data_rows, tagged_rows = unreal.PCGDataFunctionLibrary.get_typed_inputs(collection)
                        if len(tagged_rows) > 512:
                            raise RuntimeError("Native PCG output observation exceeded 512 tagged rows.")
                        if len(data_rows) != len(tagged_rows):
                            raise RuntimeError("Native PCG output data/tag cardinality differs.")
                        for data, tagged in zip(data_rows, tagged_rows):
                            tags, pin = tagged.tags, tagged.pin
                            row = dict(component=str(pcg.get_path_name()), pin=str(pin), tags=sorted(str(tag) for tag in tags),
                                       data_class=str(data.get_class().get_path_name()) if data else None)
                            if isinstance(data, unreal.PCGBasePointData):
                                row["point_count"] = data.get_num_points()
                                if str(pin) == "EF Native Upper Wall Extensions":
                                    row["points"] = []
                                    row["points_truncated"] = row["point_count"] > 256
                                    for index in range(min(row["point_count"], 256)):
                                        t = data.get_transform(index)
                                        row["points"].append(dict(index=index,
                                            location=[t.translation.x, t.translation.y, t.translation.z],
                                            scale=[t.scale3d.x, t.scale3d.y, t.scale3d.z],
                                            rotation=[t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w]))
                            authored["pcg_outputs"].append(row)
                    sample["native_actors"].append(authored)
                total = 0
                for component in components:
                    count = component.get_instance_count()
                    total += count
                    if total > 12000:
                        raise RuntimeError("Structural instance observation exceeded 12000 instances.")
                    mesh = component.get_editor_property("static_mesh")
                    row = dict(path=str(component.get_path_name()), mesh=str(mesh.get_path_name()) if mesh else None,
                               count=count, collision=str(component.get_collision_enabled()), instances=[],
                               component_tags=[str(tag) for tag in component.get_editor_property("component_tags")])
                    for index in range(count):
                        transform = component.get_instance_transform(index, world_space=True)
                        if transform is None:
                            row["instances"].append(dict(index=index, read_failed=True))
                            continue
                        location, scale, rotation = transform.translation, transform.scale3d, transform.rotation
                        values = [location.x, location.y, location.z, scale.x, scale.y, scale.z,
                                  rotation.x, rotation.y, rotation.z, rotation.w]
                        row["instances"].append(dict(index=index, location=values[:3], scale=values[3:6], rotation=values[6:],
                            finite=all(math.isfinite(v) for v in values), degenerate=min(abs(v) for v in values[3:6]) <= 1e-8))
                    sample["components"].append(row)
                self.samples.append(sample)
                self.result["status"] = "OBSERVING"
                (OUTPUT / "structure.json").write_text(json.dumps(self.result, indent=2), encoding="utf-8")
            if str(diagnostics.get("state", "")).lower() in {"failed", "ready", "cancelled"}:
                self.finish("TERMINAL_OBSERVED")
        except Exception:
            self.finish("FAIL", traceback.format_exc())
        finally:
            self.overhead += time.monotonic()-began


previous = getattr(builtins, "CALYSTO_DIRECTOR_STRUCTURE_OBSERVER", None)
if previous is not None and previous.handle is not None:
    raise RuntimeError("A structural observer already owns a live callback.")
observer = StructuralObserver()
observer.handle = unreal.register_slate_post_tick_callback(observer.tick)
builtins.CALYSTO_DIRECTOR_STRUCTURE_OBSERVER = observer
unreal.log("CALYSTO_DIRECTOR_STRUCTURE_OBSERVER_ARMED " + str(OUTPUT))

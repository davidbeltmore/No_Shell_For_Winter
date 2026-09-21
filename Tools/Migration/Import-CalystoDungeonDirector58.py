"""Import an exact V6 authoring snapshot into the direct unversioned Director.

Run inside the target UE 5.8 editor after a cold build. Saves only the exact new
candidate package; never changes Config authority or saves V6. Unsupported fields
remain in the complete source export and explicit PENDING mapping report. An
unchanged second run makes zero save calls. Authoring is not gameplay validation.
"""
from __future__ import annotations

import datetime
import hashlib
import json
import math
import time
import traceback
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import unreal

ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
SOURCE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
SOURCE_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
DIRECTORY = "/Game/_Game/Data/CalystoDungeon"
NAME = "DA_CalystoDungeonDirector"
TARGET = DIRECTORY + "/" + NAME
TARGET_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorAsset"
SOURCE_FILE = ROOT / "Content/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.uasset"
TARGET_FILE = ROOT / "Content/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector.uasset"
OUTPUT = ROOT / "Saved/Migration/CalystoDungeonDirectorV7"
RECEIPT = OUTPUT / "ImportDirector.json"
START = time.monotonic()


@dataclass(frozen=True)
class Raw:
    text: str


def checkpoint():
    if time.monotonic() - START > 60.0:
        raise RuntimeError("Importer exceeded its 60-second budget; migration remains PENDING.")


def split_top(value, separator=","):
    parts, depth, quote, escaped, start = [], 0, "", False, 0
    for index, char in enumerate(value):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
        elif char in ('"', "'"):
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == separator and depth == 0:
            parts.append(value[start:index])
            start = index + 1
    if quote or depth != 0:
        raise ValueError("Unbalanced Unreal property export.")
    parts.append(value[start:])
    return parts


def parse(value):
    value = value.strip()
    if not value.startswith("("):
        return Raw(value)
    if not value.endswith(")"):
        raise ValueError("Malformed Unreal tuple.")
    if not value[1:-1]:
        return []
    parts = split_top(value[1:-1])
    named = [split_top(part, "=") for part in parts]
    if all(len(part) == 2 for part in named):
        return {key.strip(): parse(item) for key, item in named}
    if any(len(part) == 2 for part in named):
        raise ValueError("Mixed named and positional tuple.")
    return [parse(part) for part in parts]


def text(value):
    if isinstance(value, Raw):
        return value.text
    if isinstance(value, dict):
        return "(" + ",".join(key + "=" + text(item) for key, item in value.items()) + ")"
    if isinstance(value, (list, tuple)):
        return "(" + ",".join(text(item) for item in value) + ")"
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, (float, int)) and math.isfinite(value):
        return repr(value)
    raise TypeError("Unsupported or nonfinite property: " + repr(value))


def plain(value):
    if isinstance(value, Raw):
        return {"unreal_text": value.text}
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value


def number(value):
    return float(value.text if isinstance(value, Raw) else value)


def token(value):
    result = value.text if isinstance(value, Raw) else str(value)
    if not result.startswith('"'):
        return result.split("::")[-1]
    if not result.endswith('"'):
        raise ValueError("Malformed quoted Unreal property.")
    # Unreal accepts escaped apostrophes; JSON intentionally does not.
    escapes = {"n": "\n", "r": "\r", "t": "\t", "b": "\b", "f": "\f", "v": "\v",
               "\\": "\\", '"': '"', "'": "'"}
    output, index = [], 1
    while index < len(result) - 1:
        if result[index] == "\\" and index + 1 < len(result) - 1:
            following = result[index + 1]
            output.append(escapes.get(following, "\\" + following))
            index += 2
        else:
            output.append(result[index])
            index += 1
    return "".join(output)


def identity(value):
    # FGuid::ImportTextItem accepts exactly 32 hex digits; labels are excluded.
    return Raw(hashlib.sha256(("EFCalystoEditorMigration|" + value.casefold()).encode()).hexdigest()[:32].upper())


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode()).hexdigest().upper()


def sha(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            value.update(chunk)
    return value.hexdigest().upper()


def source_hashes():
    return {str(path): sha(path) for path in sorted(SOURCE_FILE.parent.glob("*"))
            if path.is_file() and path.suffix.lower() in {".uasset", ".uexp", ".ubulk", ".uptnl"}}


def dirty_packages():
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return {str(value.get_path_name()) for value in values}


class Mapper:
    def __init__(self):
        self.records, self.covered = [], set()
        self.theme_level_offsets = None

    def record(self, source, target, value, status="MAPPED", note=""):
        self.covered.add(source)
        self.records.append(dict(source_field=source, target_field=target,
                                 source_value=plain(value), status=status, note=note,
                                 runtime_verification="PENDING"))

    def take(self, obj, key, source, target, transform=None, note=""):
        if key not in obj:
            raise KeyError("Complete native export omitted required field " + source + "." + key)
        value = obj[key]
        self.record(source + "." + key, target, value, "CORRECTED" if note else "MAPPED", note)
        return transform(value) if transform else value

    def copy(self, obj, source, target, mapping):
        return {new: self.take(obj, old, source, target + "." + new)
                for old, new in mapping.items() if old in obj}

    def selection(self, obj, source, target, kind, key="StableId"):
        result = {"Id": identity(kind + "|" + token(obj[key])), "DisplayName": token(obj[key])}
        self.record(source + "." + key, target + ".Id", obj[key], "CORRECTED",
                    "Persisted GUID from source identity; independent of display labels.")
        result.update(self.copy(obj, source, target, dict(DisplayName="DisplayName",
                      SelectionWeight="Weight", bEnabled="bEnabled",
                      FirstEligibleFloor="FirstEligibleFloor", CooldownFloors="CooldownFloors")))
        return result

    def distribution(self, obj, source, target, scale=1.0):
        result = {"Distribution": Raw("Triangular")}
        for key in ("Minimum", "Mode", "Maximum"):
            result[key] = self.take(obj, key, source, target + "." + key,
                                    lambda value: number(value) * scale,
                                    "Exact continuous triangular law; convert percent units where required.")
        if "Shape" in obj:
            self.record(source + ".Shape", None, obj["Shape"], "CORRECTED",
                        "Remove the mislabeled PERT contraction toward Mode; archive its exact source value.")
        return result

    def chance(self, obj, source, target):
        result = {"FirstFloor": 1, "LastFloor": 100}
        for old, new in (("ChanceAtFloor1", "FirstPercent"), ("ChanceAtFloor100", "LastPercent")):
            result[new] = self.take(obj, old, source, target + "." + new,
                                    lambda value: 100.0 * number(value),
                                    "Fraction to percent, exact endpoints and linear interpolation.")
        self.record(source + ".Tau", None, obj["Tau"], "CORRECTED",
                    "Exponential progression replaced by exact linear endpoints.")
        return result

    def lighting(self, obj, source, target):
        # V6 ResolveFloorIntent: mode controls native height/spacing, not color.
        mode = self.take(obj, "Mode", source, target, token,
                         "Expand V6 mode into explicit height and 1:2:1 tile-spacing distributions; no runtime mode/name parsing or color change.")
        ranges = {"Warm": (170.0, 210.0, 9), "Balanced": (180.0, 220.0, 10),
                  "Cold": (200.0, 240.0, 11), "Dark": (220.0, 260.0, 13)}
        if mode not in ranges:
            raise ValueError(source + ".Mode has no supported V6 lighting mapping: " + mode)
        value = self.take(obj, "IntensityMultiplier", source, target + ".IntensityMultiplier",
                          note="Restore V6 uniform [0.92*m,1.08*m] variation with explicit IntensityCeiling=4; retain saturation mass, not a clipped input range.")
        try:
            multiplier = number(value)
        except (TypeError, ValueError) as error:
            raise ValueError(source + ".IntensityMultiplier must be finite within 0..4.") from error
        if isinstance(value, bool) or not math.isfinite(multiplier) or not 0.0 <= multiplier <= 4.0:
            raise ValueError(source + ".IntensityMultiplier must be finite within 0..4.")
        low, high, middle = ranges[mode]
        return dict(IntensityMultiplier=dict(Distribution=Raw("Uniform"), Minimum=0.92 * multiplier,
                                             Maximum=1.08 * multiplier), IntensityCeiling=4.0,
                    WallLightHeight=dict(Distribution=Raw("Uniform"), Minimum=low, Maximum=high),
                    WallLightTileDistance=dict(Distribution=Raw("Weighted"),
                        Choices=[dict(Tiles=middle - 1, Weight=1.0), dict(Tiles=middle, Weight=2.0),
                                 dict(Tiles=middle + 1, Weight=1.0)]))

    def validated_sizes(self, values):
        source = "PerformanceAndSafety.ValidatedDungeonSizes"
        supported = isinstance(values, (list, tuple)) and len(values) == 13
        numbers = []
        for value in values if isinstance(values, (list, tuple)) else []:
            try:
                size = number(value)
                if isinstance(value, bool) or not math.isfinite(size) or not size.is_integer():
                    supported = False
                numbers.append(size)
            except (TypeError, ValueError):
                supported = False
        supported = supported and set(numbers) == set(range(18, 31))
        note = ("Exact unique source set 18..30 matches UEFCalystoDungeonDirectorAsset::Compile's existing layout-support guard. Original order/values stay archived; this is not empirical size certification."
                if supported else "Unsupported source size set: only the exact unique integers 18..30 have a direct compiled-range equivalent. Sparse, duplicate or malformed inputs remain PENDING; no min/max coercion or resampling.")
        if isinstance(values, (list, tuple)) and values:
            for index, value in enumerate(values):
                self.record(f"{source}[{index}]", "Styles[*].Layout.DungeonSize" if supported else None,
                            value, "CORRECTED" if supported else "PENDING", note)
        else:
            self.record(source, None, values, "PENDING", note)
        return supported

    def rarity(self, obj, source, target):
        result = {"FirstFloor": 1, "LastFloor": 100}
        for old, new in (("AtFloor1", "FirstWeights"), ("AtFloor100", "LastWeights")):
            result[new] = self.copy(obj[old], source + "." + old, target + "." + new,
                                    {tier: tier for tier in ("Common", "Uncommon", "Rare", "Epic")})
            result[new]["Winter"] = 0.01
            self.record(None, target + "." + new + ".Winter", 0.01, "CORRECTED",
                        "V6 had no Winter weight. Add explicit relative weight 0.01; preserve the four existing numeric weights and normalize populated eligible tiers. This authored correction is not runtime behavior verification.")
            self.record(source + "." + old + ".Nothing", None, obj[old]["Nothing"], "CORRECTED",
                        "Remove implicit rarity absence; Chance is the sole category absence roll.")
        return result

    def architecture_list(self, values, source, target, scope, kind="Mesh"):
        result, occurrences = [], {}
        if not values:
            self.record(source, target, values)
        for index, obj in enumerate(values):
            checkpoint()
            stable = {key: value for key, value in obj.items() if key != "SelectionWeight"}
            key = digest(plain(stable))[:16]
            ordinal = occurrences.get(key, 0)
            occurrences[key] = ordinal + 1
            sp, tp = f"{source}[{index}]", f"{target}[{index}]"
            item = {"Selection": {"Id": identity(scope + "|" + key + "|" + str(ordinal)),
                                  "DisplayName": kind + " " + str(index + 1)}}
            item["Selection"]["Weight"] = self.take(obj, "SelectionWeight", sp, tp + ".Selection.Weight")
            payload = kind
            if "Type" in obj:
                payload = {"StaticMesh": "Mesh", "ActorBlueprint": "Actor", "LevelInstance": "BakedPCG"}[token(obj["Type"])]
                self.record(sp + ".Type", tp + ".Payload", obj["Type"])
            old = {"Mesh": "Mesh", "Actor": "ActorClass", "BakedPCG": "LevelInstance"}[payload]
            active = obj.get(old, Raw("None"))
            item["Payload"] = Raw(payload if token(active) not in {"None", ""} else "Empty")
            if token(active) not in {"None", ""}:
                target_key = "BakedPCG" if old == "LevelInstance" else old
                item[target_key] = self.take(obj, old, sp, tp + "." + target_key)
            else:
                self.record(sp + "." + old, tp + ".Payload", active, "CORRECTED",
                            "Intentional native null decoration becomes explicit Empty with its authored weight.")
            item.update(self.copy(obj, sp, tp, dict(Transform="Transform", Variation="Variation", Rotation="Rotation")))
            for inactive in ("Mesh", "ActorClass", "LevelInstance"):
                if inactive in obj and inactive != old:
                    target_key = "BakedPCG" if inactive == "LevelInstance" else inactive
                    item[target_key] = obj[inactive]
                    self.record(sp + "." + inactive, tp + "." + target_key, obj[inactive], "INACTIVE_RESOURCE",
                                "Preserved in the matching native union field; only the selected payload participates in reachable dependencies.")
            result.append(item)
        return result

    def decoration(self, obj, source, target, scope, style=False, chance_percent=100.0):
        fields = ((name + "Objects", name) for name in ("WallBottom", "WallMiddle", "WallTop", "Roof")) if style else (
            (name, name) for name in ("WallBottom", "WallMiddle", "WallTop", "Floor", "CornerBottom", "CornerMiddle", "CornerTop", "Roof"))
        result = []
        for old, zone in fields:
            if old not in obj:
                continue
            values = self.architecture_list(obj[old], source + "." + old,
                                           f"{target}[{len(result)}].Alternatives", scope + "|" + zone)
            if values:
                result.append({"Zone": Raw(zone), "Chance": {"FirstPercent": chance_percent, "LastPercent": chance_percent}, "Alternatives": values})
        return result

    def content(self, values, source, target, offsets):
        roles = {"enemy": ("Enemy", "Enemy"), "npc": ("SupportNPC", "SpecialEvent"),
                 "chest": ("Container", "Chest"), "chestcontents": ("ContainerContent", "None"),
                 "food": ("Food", "LooseFood"), "drink": ("Drink", "LooseFood"),
                 "looseloot": ("LooseLoot", "LootActor"), "clothing": ("Armor", "LootActor"),
                 "specialevent": ("SpecialEvent", "SpecialEvent"), "prop": ("Prop", "None")}
        result = []
        if not values:
            self.record(source, target, values)
        for index, obj in enumerate(values):
            sp, tp = f"{source}[{index}]", f"{target}[{len(result)}]"
            name, mode = token(obj["CategoryId"]), token(obj["Mode"])
            if name.casefold() not in roles:
                self.record(sp, None, obj, "PENDING", "Unknown category requires an explicit typed mapping.")
                continue
            role, budget = roles[name.casefold()]
            item = dict(Id=identity("category|" + name), DisplayName=name, Role=Raw(role), Budget=Raw(budget), Mode=Raw(mode))
            self.record(sp + ".CategoryId", tp + ".Role", obj["CategoryId"], "CORRECTED",
                        "One-time explicit category-to-role/budget migration; never runtime name parsing.")
            self.record(sp + ".Mode", tp + ".Mode", obj["Mode"])
            item["Chance"] = self.chance(obj["Presence"], sp + ".Presence", tp + ".Chance")
            item["Rarity"] = self.rarity(obj["Tiers"], sp + ".Tiers", tp + ".Rarity")
            item["MaximumPerFloor"] = self.take(obj["Limits"], "MaximumPerFloor", sp + ".Limits", tp + ".MaximumPerFloor")
            item["Amount"] = dict(Distribution=Raw("Fixed"), Amount=max(1, int(number(obj["Limits"]["MinimumWhenPresent"]))))
            self.record(sp + ".Limits.MinimumWhenPresent", tp + ".Amount", obj["Limits"]["MinimumWhenPresent"], "CORRECTED",
                        "Preserve the old effective fixed positive count. A capacity is not silently reinterpreted as a quantity maximum; new uniform/triangular choices are explicit.")
            if mode == "Extend":
                item.update(bOverrideChance=True, bOverrideAmount=True, bOverrideRarity=True)
                self.record(sp + ".Mode", tp, obj["Mode"], "CORRECTED",
                            "Preserve old Extend's authored probability/count/rarity values using visible explicit override switches. Style capacity remains separate.")
            entries = []
            catalog = "ChestContentsCatalog" if role == "ContainerContent" else "Catalog"
            for ei, entry in enumerate(obj[catalog]):
                ep, et = f"{sp}.{catalog}[{ei}]", f"{tp}.Entries[{len(entries)}]"
                output = {"Selection": self.selection(entry, ep, et + ".Selection", "entry")}
                if "Rule" in entry:
                    output["Selection"]["bEnabled"] = token(entry["Rule"]) == "Allow"
                    self.record(ep + ".Rule", et + ".Selection.bEnabled", entry["Rule"], "CORRECTED",
                                "A blocked entry is retained with all authored payload/properties and disabled. In Extend it replaces the same inherited identity with a disabled entry.")
                output.update(self.copy(entry, ep, et, dict(ActorClass="ActorClass", ContentClass="InventoryClass", Tier="Rarity",
                              Archetype="Archetype", Gender="Gender", Lifecycle="Lifecycle", BaseThreatCost="ThreatCost",
                              MaximumPerVariant="MaximumPerFloor", MaximumPerFloor="MaximumPerFloor",
                              bRequiresGraveyardEligibility="bRequiresGraveyardEligibility")))
                output.update(MinimumLevelOffset=offsets[0], MaximumLevelOffset=offsets[1])
                output["Placement"] = self.copy(entry, ep, et + ".Placement", dict(PlacementZone="Zone", PositionJitterCm="PositionVariationCm"))
                entries.append(output)
            item.update(Entries=entries, RemovedEntryIds=[])
            if not obj[catalog]:
                self.record(sp + "." + catalog, tp + ".Entries", obj[catalog], "EMPTY_SOURCE")
            other = "Catalog" if catalog == "ChestContentsCatalog" else "ChestContentsCatalog"
            self.record(sp + "." + other, None, obj[other], "PENDING" if obj[other] else "EMPTY_SOURCE",
                        "Mixed actor/inventory payloads require a separate typed group." if obj[other] else "")
            result.append(item)
        return result

    def decals(self, obj, source, target):
        output = self.copy(obj, source, target, dict(MaximumPerRoom="MaximumPerRoom",
                           MaximumActivePerFloor="ActiveFloorBudget", FloorLimit="FloorLimit", WallLimit="WallLimit",
                           RoofLimit="RoofLimit"))
        output["CullDistanceCm"] = self.take(obj, "CullDistanceCm", source, target + ".CullDistanceCm",
            note="Mathematical correction: V7 pooled components use this actual camera distance for hard visibility culling. V6 used it only inside a screen-size approximation; no old hard-distance equivalence is claimed.")
        output["Mode"] = self.take(obj, "Mode", source, target + ".Mode",
            lambda value: Raw("Inherit" if token(value) == "InheritStyle" else token(value)))
        output["ChancePercent"] = self.take(obj, "ChancePerEligibleRoom", source, target + ".ChancePercent",
                                           lambda value: number(value) * 100.0, "Fraction to displayed percent.")
        output["SizeCm"] = dict(Distribution=Raw("Uniform"),
                               Minimum=self.take(obj, "MinimumSizeCm", source, target + ".SizeCm.Minimum"),
                               Maximum=self.take(obj, "MaximumSizeCm", source, target + ".SizeCm.Maximum"))
        if "FadeStartDistanceCm" not in obj:
            raise KeyError("Complete native export omitted required field " + source + ".FadeStartDistanceCm")
        output["SourceFadeStartDistanceCm"] = obj["FadeStartDistanceCm"]
        self.record(source + ".FadeStartDistanceCm", target + ".SourceFadeStartDistanceCm", obj["FadeStartDistanceCm"],
                    "MATHEMATICAL_CORRECTION", "RETIRED_DISTANCE_APPROXIMATION: retain exact input as hidden editor-only metadata. V6 threshold=max(poolFadeScreenSize,clamp(max(Size.Y,Size.Z)*0.5*(1/CullDistanceCm+1/FadeStartDistanceCm),0.0001,0.25)); renderer FOV/viewport/scale also affect fading, so this was not exact distance fading. V7 uses explicit screen-size fade plus real hard distance culling.")
        output["FadeScreenSize"] = 0.01
        self.record(None, target + ".FadeScreenSize", 0.01, "INITIAL_DEFAULT",
                    "Explicit renderer screen-size threshold preserves the prior native pool default 0.01. The size-dependent distance approximation is retired, not silently reinterpreted as world-distance fade.")
        mask = int(number(obj["AllowedSurfaces"]))
        self.record(source + ".AllowedSurfaces", target + ".Variants", obj["AllowedSurfaces"],
                    note="Intersect profile mask with each declared variant mask.")
        variants = []
        for index, item in enumerate(obj["Catalog"]):
            sp, tp = f"{source}.Catalog[{index}]", f"{target}.Variants[{index}]"
            variant = {"Selection": self.selection(item, sp, tp + ".Selection", "decal")}
            variant.update(self.copy(item, sp, tp, dict(Material="Material", SourceColorTexture="ColorTexture", SourceNormalTexture="NormalTexture")))
            surfaces = mask & int(number(item["AllowedSurfaces"]))
            variant.update(bFloor=bool(surfaces & 1), bWall=bool(surfaces & 2), bRoof=bool(surfaces & 4))
            self.record(sp + ".AllowedSurfaces", tp, item["AllowedSurfaces"], note="Explicit Floor/Wall/Roof flags.")
            variants.append(variant)
        output["Variants"] = variants
        if not obj["Catalog"]:
            self.record(source + ".Catalog", target + ".Variants", obj["Catalog"], "EMPTY_SOURCE")
        return output

    def style(self, obj, index):
        sp, tp = f"Styles[{index}]", f"Styles[{index}]"
        scope = "style|" + token(obj["StyleId"])
        output = {"Selection": self.selection(obj, sp, tp + ".Selection", "style", "StyleId")}
        output["Traits"] = self.traits(obj["Traits"], sp + ".Traits", tp + ".Traits")
        output["Layout"] = {}
        for old, new, scale in (("DungeonSize", "DungeonSize", 1.0), ("CandidateDensity", "CandidateDensity", 1.0), ("SidePathChance", "SidePathPercent", 100.0)):
            output["Layout"][new] = self.distribution(obj["Layout"][old], sp + ".Layout." + old, tp + ".Layout." + new, scale)
        output["Layout"].update(self.copy(obj["Layout"], sp + ".Layout", tp + ".Layout", dict(MinimumRoomSize="MinimumRoomSize", MaximumRoomSize="MaximumRoomSize")))
        output["Materials"] = self.copy(obj["DungeonMaterials"], sp + ".DungeonMaterials", tp + ".Materials", dict(FloorMaterial="Floor", WallMaterial="Wall", RoofMaterial="Roof"))
        architecture, mapped = obj["Architecture"], {}
        for old, new, kind in (("Floor", "Floor", "Mesh"), ("Wall", "Wall", "Mesh"), ("Roof", "Roof", "Mesh"),
                               ("DoorFrame", "DoorFrames", "Mesh"), ("Door", "Doors", "Actor"),
                               ("RampTop", "RampTop", "Mesh"), ("RampBottom", "RampBottom", "Mesh")):
            mapped[new] = self.architecture_list(architecture[old], sp + ".Architecture." + old, tp + ".Architecture." + new, scope + "|" + new, kind)
        mapped["Doorways"] = []
        doorway_occurrences = {}
        for index, door in enumerate(architecture["WallDoor"]):
            dp, dt = f"{sp}.Architecture.WallDoor[{index}]", f"{tp}.Architecture.Doorways[{index}]"
            doorway_key = digest(plain({key: value for key, value in door.items() if key != "SelectionWeight"}))[:16]
            ordinal = doorway_occurrences.get(doorway_key, 0)
            doorway_occurrences[doorway_key] = ordinal + 1
            entry = {"Selection": {"Id": identity(scope + "|doorway|" + doorway_key + "|" + str(ordinal)), "DisplayName": "Doorway",
                                   "Weight": self.take(door, "SelectionWeight", dp, dt + ".Selection.Weight")}}
            entry.update(self.copy(door, dp, dt, {key: key for key in ("WallMesh", "FrameMesh", "DoorClass", "WallTransform", "FrameTransform", "DoorTransform")}))
            mapped["Doorways"].append(entry)
        decoration_policy = obj["Decoration"]
        chance_percent = number(decoration_policy["Density"]) * 100.0
        if isinstance(decoration_policy["Density"], bool) or not math.isfinite(chance_percent) or not 0 <= chance_percent <= 100:
            raise ValueError(sp + ".Decoration.Density must be a finite fraction in [0,1].")
        decoration_cap = number(decoration_policy["MaximumDecorationsPerRoom"])
        if isinstance(decoration_policy["MaximumDecorationsPerRoom"], bool) or not math.isfinite(decoration_cap) or not decoration_cap.is_integer() or not 0 <= decoration_cap <= 256:
            raise ValueError(sp + ".Decoration.MaximumDecorationsPerRoom must be an integer capacity within [0,256].")
        mapped["DecorationChance"] = dict(FirstPercent=chance_percent, LastPercent=chance_percent)
        self.record(sp + ".Decoration.Density", tp + ".Architecture.DecorationChance", decoration_policy["Density"],
                    "MATHEMATICAL_CORRECTION", "Implement previously ineffective density as explicit Chance per compatible native optional-decoration opportunity. Both linear endpoints preserve the authored fraction as a displayed percentage; required structure and lights are excluded.")
        mapped["MaximumDecorationsPerRoom"] = self.take(decoration_policy, "MaximumDecorationsPerRoom",
                    sp + ".Decoration", tp + ".Architecture.MaximumDecorationsPerRoom",
                    note="Implement the previously ineffective capacity across all optional zones and Theme decoration in one room, before Chance. Explicit Empty consumes no physical reservation; required structure and native lights are excluded.")
        mapped["Decoration"] = self.decoration(architecture, sp + ".Architecture", tp + ".Architecture.Decoration", scope, True, chance_percent)
        progression_door_mesh = "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors.SM_SquaredArchedWoodenDoors"
        mapped["ProgressionDoorMesh"] = Raw(json.dumps(progression_door_mesh))
        self.record(None, tp + ".Architecture.ProgressionDoorMesh", progression_door_mesh, "INITIAL_DEFAULT",
                    "V7 assigns the supported Calysto door appearance. Progression actor identity and the door approach contract are travel-owned, so this field never replaces the source StartBlueprint or EndBlueprint.")
        for field in ("StartBlueprint", "EndBlueprint"):
            self.record(sp + ".Architecture." + field, None, architecture[field], "CORRECTED",
                        "Progression actor identity and door approach remain travel-owned. Source reference is archived; V7 assigns only the supported ProgressionDoorMesh appearance and never overwrites travel.")
        output["Architecture"] = mapped
        output["Lighting"] = self.lighting(obj["Lighting"], sp + ".Lighting", tp + ".Lighting")
        output["Lighting"]["WallLights"] = self.architecture_list(architecture["WallLights"], sp + ".Architecture.WallLights", tp + ".Lighting.WallLights", scope + "|lights", "Actor")
        placements = {(token(item["PlacementZone"]), number(item["PositionJitterCm"])) for item in architecture["WallLights"]}
        if len(placements) == 1:
            zone, jitter = next(iter(placements))
            output["Lighting"]["Placement"] = dict(Zone=Raw(zone), PositionVariationCm=jitter)
            for index, item in enumerate(architecture["WallLights"]):
                for field in ("PlacementZone", "PositionJitterCm"):
                    self.record(f"{sp}.Architecture.WallLights[{index}].{field}", tp + ".Lighting.Placement", item[field])
        budgets = self.copy(obj["GlobalBudgets"], sp + ".GlobalBudgets", tp + ".FloorBudgets",
                            dict(MaximumEnemies="Enemies", MaximumLooseFood="LooseFood", MaximumChests="Chests",
                                 MaximumLootActors="LootActors", MaximumSpecialEvents="SpecialEvents", MaximumDirectorActors="TotalActors"))
        threat = obj["Threat"]
        budgets["Threat"] = dict(FirstFloor=1, LastFloor=100,
                                FirstValue=self.take(threat, "BudgetAtFloor1", sp + ".Threat", tp + ".FloorBudgets.Threat.FirstValue"),
                                LastValue=self.take(threat, "BudgetAtFloor100", sp + ".Threat", tp + ".FloorBudgets.Threat.LastValue"))
        self.record(sp + ".Threat.Tau", None, threat["Tau"], "CORRECTED", "Exact linear threat endpoints.")
        offsets = threat["MinimumLevelOffset"], threat["MaximumLevelOffset"]
        for field in ("MinimumLevelOffset", "MaximumLevelOffset"):
            self.record(sp + ".Threat." + field, tp + ".Content.Entries." + field, threat[field],
                        note="Frozen into typed actor entries.")
        output.update(FloorBudgets=budgets,
                      Content=self.content(obj["Catalogs"], sp + ".Catalogs", tp + ".Content", offsets),
                      Decals=self.decals(obj["Decals"], sp + ".Decals", tp + ".Decals"))
        return output

    def traits(self, obj, source, target):
        result = {}
        for field in ("Mystery", "Danger", "Safe", "Abundance", "ClothingInfluence"):
            value = self.take(obj, field, source, target + "." + field,
                note="Preserve exact normalized input. Actual content Chance/entry Weight effects require explicit typed Adaptation bindings; no trait name implies a category or effect. Initial migration keeps adaptation disabled and bindings empty, preserving the source's inactive behavior.")
            try:
                valid = not isinstance(value, bool) and math.isfinite(number(value)) and 0 <= number(value) <= 1
            except (TypeError, ValueError):
                valid = False
            if not valid:
                raise ValueError(source + "." + field + " must be a finite normalized value in [0,1].")
            result[field] = value
        return result

    def safety_ceilings(self, obj):
        source, target = "PerformanceAndSafety.HardCeilings", "Advanced.HardCeilings"
        result = {}
        for old, new in (("MaximumEnemies", "Enemies"), ("MaximumLooseFood", "LooseFood"),
                         ("MaximumChests", "Chests"), ("MaximumLootActors", "LootActors"),
                         ("MaximumSpecialEvents", "SpecialEvents"), ("MaximumDirectorActors", "TotalActors")):
            value = self.take(obj, old, source, target + "." + new,
                              note="Restore the independent global authoring ceiling. Active Style capacities above it reject compilation; shared reservation feasibility consumes the validated Style capacity without clamping or a probability change.")
            try:
                numeric = number(value)
                valid = not isinstance(value, bool) and math.isfinite(numeric) and numeric.is_integer() and 0 <= numeric <= 2147483647
            except (TypeError, ValueError):
                valid = False
            if not valid:
                raise ValueError(source + "." + old + " must be a nonnegative int32 capacity.")
            result[new] = value
        return result

    def theme_metadata(self, obj, source, target):
        note = "Editor-only authoring metadata retained exactly; inspector export only, no gameplay random identity or material effect."
        result = dict(Description=self.take(obj, "Description", source, target + ".Description", note=note))
        if "PreviewColor" not in obj or not isinstance(obj["PreviewColor"], dict):
            raise KeyError("Complete native export omitted required color " + source + ".PreviewColor")
        result["PreviewColor"] = {}
        for channel in ("R", "G", "B", "A"):
            value = self.take(obj["PreviewColor"], channel, source + ".PreviewColor",
                              target + ".PreviewColor." + channel, note=note)
            try:
                finite = not isinstance(value, bool) and math.isfinite(number(value))
            except (TypeError, ValueError):
                finite = False
            if not finite:
                raise ValueError(source + ".PreviewColor." + channel + " must be finite.")
            result["PreviewColor"][channel] = value
        return result

    def theme(self, obj, index):
        sp, tp = f"RoomThemes[{index}]", f"RoomThemes[{index}]"
        output = {"Selection": self.selection(obj, sp, tp + ".Selection", "theme", "ThemeId"), "Materials": {}}
        output["Traits"] = self.traits(obj["Traits"], sp + ".Traits", tp + ".Traits")
        output.update(self.theme_metadata(obj, sp, tp))
        materials = obj["RoomMaterials"]
        for surface in ("Floor", "Wall", "Roof"):
            mode = token(materials[surface + "Mode"])
            output["Materials"][surface] = dict(Mode=Raw("Inherit" if mode == "InheritStyle" else mode),
                Material=self.take(materials["Overrides"], surface + "Material", sp + ".RoomMaterials.Overrides", tp + ".Materials." + surface + ".Material"))
            self.record(sp + ".RoomMaterials." + surface + "Mode", tp + ".Materials." + surface + ".Mode", materials[surface + "Mode"])
        offsets = self.theme_level_offsets if self.theme_level_offsets is not None else (-2, 2)
        output.update(Architecture=self.decoration(obj["Architecture"], sp + ".Architecture", tp + ".Architecture", "theme|" + token(obj["ThemeId"])),
                      Content=self.content(obj["Catalogs"], sp + ".Catalogs", tp + ".Content", offsets),
                      Decals=self.decals(obj["Decals"], sp + ".Decals", tp + ".Decals"))
        self.record("Styles[*].Threat.LevelOffsets", tp + ".Content[*].Entries[*].LevelOffsets",
                    offsets, "MAPPED" if self.theme_level_offsets is not None else "PENDING",
                    "All source Styles share these exact offsets, including Theme-selected actors." if self.theme_level_offsets is not None else
                    "Source Styles have different offsets. Theme entries need explicit Style-level inheritance before gameplay migration is complete; current native defaults are not equivalence proof.")
        return output

    def finish(self, value, path=""):
        if path in self.covered:
            return
        if isinstance(value, dict):
            for key, item in value.items():
                self.finish(item, path + "." + key if path else key)
        elif isinstance(value, list) and value:
            for index, item in enumerate(value):
                self.finish(item, f"{path}[{index}]")
        else:
            derived = path.rsplit(".", 1)[-1] in {"EditorSummary", "NormalizedSelectionProbability", "ConditionalSelectionProbability", "OverallEligibleRoomProbability"}
            self.record(path, None, value, "DERIVED_REMOVED" if derived else "PENDING",
                        "On-demand diagnostics replace permanent summary rows." if derived else
                        "No implemented direct-model equivalent yet. Exact source value is retained in the immutable source JSON.")


def native_struct(name, value):
    result = getattr(unreal, name)()
    if not result.import_text(text(value)):
        raise RuntimeError("Native import rejected " + name)
    return result


def assert_authored_roundtrip(expected, actual, path=""):
    """Reject ignored fields, enum fallback, dropped entries and changed mapped values."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            raise RuntimeError("Native authoring roundtrip changed structure at " + path)
        for key, value in expected.items():
            if key not in actual:
                raise RuntimeError("Native authoring roundtrip omitted " + path + "." + key)
            assert_authored_roundtrip(value, actual[key], path + "." + key if path else key)
        return
    if isinstance(expected, (list, tuple)):
        if not isinstance(actual, (list, tuple)) or len(expected) != len(actual):
            raise RuntimeError("Native authoring roundtrip changed array capacity at " + path)
        for index, (left, right) in enumerate(zip(expected, actual)):
            assert_authored_roundtrip(left, right, f"{path}[{index}]")
        return
    left, right = token(expected), token(actual)
    if left == right:
        return
    try:
        if math.isclose(float(left), float(right), rel_tol=1e-7, abs_tol=1e-7):
            return
    except (ValueError, TypeError):
        pass
    if len(left.replace("-", "")) == 32 and left.replace("-", "").casefold() == right.replace("-", "").casefold():
        return
    raise RuntimeError(f"Native authoring roundtrip changed {path}: {left!r} -> {right!r}")


def validate_mapper_primitives():
    """Small synthetic tests; no assets, worlds, floor generation or behavior claim."""
    sample = {"Name": 'A, label="quoted"', "Nested": [Raw("WallMiddle"), {"Value": 2.25}], "Empty": []}
    assert_authored_roundtrip(sample, parse(text(sample)))
    assert token(Raw('"Winter\\\'s Recall"')) == "Winter's Recall"
    mapper = Mapper()
    actor = dict(Type=Raw("ActorBlueprint"), ActorClass=Raw('"/Game/Test/Actor.Actor_C"'),
                 Mesh=Raw('"/Game/Test/InactiveMesh.InactiveMesh"'), LevelInstance=Raw("None"),
                 SelectionWeight=Raw("3.5"), Rotation=Raw("Degrees90"), Variation={})
    mapped = mapper.architecture_list([actor], "Source", "Target", "Fixture")[0]
    assert token(mapped["Payload"]) == "Actor" and mapped["ActorClass"] == actor["ActorClass"]
    assert mapped["Mesh"] == actor["Mesh"] and number(mapped["Selection"]["Weight"]) == 3.5
    decal = dict(Mode=Raw("InheritStyle"), MaximumPerRoom=1, MaximumActivePerFloor=8,
                 FloorLimit=3, WallLimit=4, RoofLimit=1, CullDistanceCm=4000, ChancePerEligibleRoom=0.1,
                 MinimumSizeCm=35, MaximumSizeCm=110, FadeStartDistanceCm=2500, AllowedSurfaces=7, Catalog=[])
    assert token(mapper.decals(decal, "Decals", "Target.Decals")["Mode"]) == "Inherit"
    chance = dict(ChanceAtFloor1=0.1, ChanceAtFloor100=0.9, Tau=12)
    tiers = {key: dict(Common=0.6, Uncommon=0.2, Rare=0.08, Epic=0.02, Nothing=0.1)
             for key in ("AtFloor1", "AtFloor100")}
    entry = dict(StableId=Raw('"BlockedEntry"'), DisplayName="Keep this label", Rule=Raw("Block"),
                 ActorClass=Raw('"/Game/Test/Actor.Actor_C"'), SelectionWeight=2.0, PlacementZone=Raw("Roof"),
                 PositionJitterCm=2.0, Gender=Raw("Female"), Tier=Raw("Winter"), Archetype=Raw('"Melee"'),
                 Lifecycle=Raw("Recruitable"), BaseThreatCost=3.0, FirstEligibleFloor=7,
                 MaximumPerVariant=4, CooldownFloors=2)
    group = dict(CategoryId=Raw('"Enemy"'), Mode=Raw("Extend"), Presence=chance, Tiers=tiers,
                 Limits=dict(MinimumWhenPresent=2, MaximumPerFloor=9), Catalog=[entry], ChestContentsCatalog=[])
    mapped_group = mapper.content([group], "Catalogs", "Content", (-4, 6))[0]
    kept = mapped_group["Entries"][0]
    assert kept["Selection"]["bEnabled"] is False and kept["ActorClass"] == entry["ActorClass"]
    assert kept["Selection"]["DisplayName"] == entry["DisplayName"] and kept["MinimumLevelOffset"] == -4
    assert kept["MaximumLevelOffset"] == 6 and token(kept["Rarity"]) == "Winter"
    assert mapped_group["RemovedEntryIds"] == [] and mapped_group["bOverrideChance"]
    assert mapped_group["Chance"]["FirstPercent"] == 10.0 and mapped_group["Chance"]["LastPercent"] == 90.0
    assert mapped_group["Rarity"]["FirstWeights"]["Common"] == 0.6
    assert mapped_group["Rarity"]["FirstWeights"]["Winter"] == 0.01
    try:
        assert_authored_roundtrip({"Mode": Raw("Inherit")}, {"Mode": Raw("Replace")}, "Fixture")
    except RuntimeError:
        pass
    else:
        raise AssertionError("Native enum substitution must be detected.")
    try:
        mapper.take({}, "SelectionWeight", "Architecture.Floor[0]", "Selection.Weight")
    except KeyError as error:
        assert "Architecture.Floor[0].SelectionWeight" in str(error)
    else:
        raise AssertionError("Incomplete source exports must be rejected with the exact field.")


def snapshot(asset):
    exporter = getattr(unreal, "EFCalystoDirectorEditorLibrary", None)
    if exporter is None:
        raise RuntimeError("Complete native exporter is required for an idempotent authoring comparison.")
    result = {field: exporter.export_authored_field(asset, field)
              for field in ("Dungeon", "Styles", "RoomThemes", "Advanced")}
    if not all(result.values()):
        raise RuntimeError("Complete native exporter rejected the candidate authoring snapshot.")
    return result


def validate(asset):
    if hasattr(asset, "get_authoring_errors"):
        errors = [str(item) for item in asset.get_authoring_errors()]
        return not errors, errors
    result = asset.validate_authoring()
    if isinstance(result, tuple):
        return bool(result[0]), [str(item) for item in result[1]]
    # UE's Python wrapper consumes a bool success return when there are output
    # parameters: success yields the output array, failure yields None.
    if isinstance(result, (unreal.Array, list)):
        errors = [str(item) for item in result]
        return not errors, errors
    if result is None:
        raise RuntimeError("Native authoring validation failed; inspect the staged asset's exact native validation errors.")
    raise RuntimeError("Unexpected validate_authoring result: " + repr(result))


def main(*, stage_only=False):
    global START
    START = time.monotonic()
    receipt = OUTPUT / "ImportDirectorStaged.json" if stage_only else RECEIPT
    result = dict(schema=2, status="PENDING", stage_only=stage_only, saved_packages=[], gameplay="PENDING", source=SOURCE, target=TARGET,
                  authority_cutover="PENDING", runtime_mapping_verification="PENDING",
                  generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    before, dirty_before = {}, set()
    try:
        if Path(unreal.Paths.project_dir()).resolve() != ROOT or Path(unreal.Paths.get_project_file_path()).name.casefold() != "noshellforwinter.uproject":
            raise RuntimeError("Exact writable target editor is required.")
        if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
            raise RuntimeError("UE 5.8 is required.")
        if not SOURCE_FILE.is_file():
            raise RuntimeError("Immutable V6 source is missing.")
        before, dirty_before = source_hashes(), dirty_packages()
        if SOURCE in dirty_before or TARGET in dirty_before:
            raise RuntimeError("Source or target is already dirty; preserve those edits before migration.")
        source = unreal.load_asset(SOURCE)
        if source is None or str(source.get_class().get_path_name()) != SOURCE_CLASS:
            raise RuntimeError("Exact V6 native source class is unavailable.")
        exporter = getattr(unreal, "EFCalystoDirectorEditorLibrary", None)
        if exporter is None:
            raise RuntimeError("Complete native field exporter is unavailable; cold build/reopen is required.")
        raw = {field: exporter.export_authored_field(source, field)
               for field in ("Styles", "RoomThemes", "PerformanceAndSafety")}
        if not all(raw.values()):
            raise RuntimeError("Native export rejected an authored field.")
        parsed = {field: parse(value) for field, value in raw.items()}
        OUTPUT.mkdir(parents=True, exist_ok=True)
        export = OUTPUT / ("SourceV6_Complete_" + before[str(SOURCE_FILE)] + ".json")
        contents = json.dumps(dict(asset=SOURCE, native_class=SOURCE_CLASS, package_hashes=before,
                                   raw_authored_export=raw, parsed_authored_values=plain(parsed)),
                              indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        if export.exists() and export.read_text(encoding="utf-8") != contents:
            raise RuntimeError("Same source package hash produced differing immutable export; never rebaseline.")
        if not export.exists():
            export.write_text(contents, encoding="utf-8")
        result.update(source_export=str(export), source_export_sha256=sha(export))
        checkpoint()
        validate_mapper_primitives()
        result["mapper_primitive_tests"] = "PASS"
        mapper = Mapper()
        size_field = "ValidatedDungeonSizes"
        if size_field not in parsed["PerformanceAndSafety"]:
            raise KeyError("Complete native export omitted required field PerformanceAndSafety.ValidatedDungeonSizes")
        sizes_supported = mapper.validated_sizes(parsed["PerformanceAndSafety"][size_field])
        offset_pairs = {(int(number(style["Threat"]["MinimumLevelOffset"])),
                         int(number(style["Threat"]["MaximumLevelOffset"]))) for style in parsed["Styles"]}
        if len(offset_pairs) == 1:
            mapper.theme_level_offsets = next(iter(offset_pairs))
        styles = [mapper.style(item, index) for index, item in enumerate(parsed["Styles"])]
        themes = [mapper.theme(item, index) for index, item in enumerate(parsed["RoomThemes"])]
        chances = {number(style["RoomThemeChance"]) for style in parsed["Styles"]}
        for index, style in enumerate(parsed["Styles"]):
            mapper.record(f"Styles[{index}].RoomThemeChance", "Dungeon.AdditionalRoomThemeChancePercent",
                          style["RoomThemeChance"], "CORRECTED" if len(chances) == 1 else "PENDING",
                          "Additional-room chance follows one guaranteed room." if len(chances) == 1 else
                          "Differing Style probabilities need a direct per-Style override before complete migration.")
        advanced = mapper.copy(parsed["PerformanceAndSafety"], "PerformanceAndSafety", "Advanced",
                               dict(RoomIdentityQuantizationCm="RoomIdentityQuantizationCm", DecalComponentPoolCapacity="DecalPoolCapacity", MaximumRoomRecords="MaximumRoomRecords"))
        advanced.update(Adaptation=dict(bEnabled=False, Bindings=[]), MaximumAttempts=4, RequestDeadlineSeconds=30.0)
        advanced["HardCeilings"] = mapper.safety_ceilings(parsed["PerformanceAndSafety"]["HardCeilings"])
        mapper.record(None, "Advanced.Adaptation.bEnabled", False, "INITIAL_DEFAULT",
                      "Adaptation starts disabled. No adaptation runtime verification is inferred.")
        mapper.record(None, "Advanced.Adaptation.Bindings", [], "INITIAL_DEFAULT",
                      "No implicit trait/category association or invented effect constants. Explicit native bindings can target Chance or exact-entry Weight; runtime snapshot capture and enabled full-floor behavior remain separate gates.")
        mapper.record(None, "Advanced.MaximumAttempts", 4, "INITIAL_DEFAULT",
                      "Four total attempts share one request deadline.")
        mapper.record(None, "Advanced.RequestDeadlineSeconds", 30.0, "INITIAL_DEFAULT",
                      "One 30-second request budget includes rejected attempts and cleanup.")
        mapper.record(None, "Dungeon.GuaranteedThemedRooms", 1, "CORRECTED",
                      "One guaranteed eligible room plus independent Additional Room Theme Chance on remaining eligible rooms.")
        mapper.finish(parsed)
        result.update(field_mapping=mapper.records, unmapped_fields=[row for row in mapper.records if row["status"] == "PENDING"],
                      mapping_status_counts={status: sum(row["status"] == status for row in mapper.records)
                                             for status in sorted({row["status"] for row in mapper.records})})
        result["validated_layout_sizes_equivalent"] = sizes_supported
        if not sizes_supported:
            raise RuntimeError("PerformanceAndSafety.ValidatedDungeonSizes has no exact compiled-range equivalent; migration remains PENDING before candidate staging.")
        native_type = getattr(unreal, "EFCalystoDungeonDirectorAsset", None)
        if native_type is None:
            raise RuntimeError("New native Director class is unavailable; cold build/reopen is required.")
        staged = unreal.new_object(native_type)
        dungeon = dict(GuaranteedThemedRooms=1,
                       AdditionalRoomThemeChancePercent=100.0 * next(iter(chances)) if len(chances) == 1 else 25.0)
        staged.set_editor_property("dungeon", native_struct("EFCalystoDungeonRules", dungeon))
        staged.set_editor_property("styles", [native_struct("EFCalystoStyle", item) for item in styles])
        staged.set_editor_property("room_themes", [native_struct("EFCalystoTheme", item) for item in themes])
        staged.set_editor_property("advanced", native_struct("EFCalystoDirectorAdvanced", advanced))
        valid, errors = validate(staged)
        result.update(authoring_valid=valid, authoring_errors=errors)
        if not valid:
            raise RuntimeError("Staged candidate failed authoring validation; target asset was not changed.")
        staged_snapshot = snapshot(staged)
        expected = dict(Dungeon=dungeon, Styles=styles, RoomThemes=themes, Advanced=advanced)
        assert_authored_roundtrip(expected, {field: parse(value) for field, value in staged_snapshot.items()})
        result["mapped_values_roundtrip_verified"] = True
        wanted = digest(staged_snapshot)
        candidate_export = OUTPUT / ("CandidateV7_" + wanted + "_" + digest(mapper.records)[:12] + ".json")
        candidate_contents = json.dumps(dict(source_export=str(export), source_export_sha256=sha(export),
            authored_native_export=staged_snapshot, mapped_authored_values=plain(expected),
            mapping=mapper.records, runtime_verification="PENDING"), indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        if candidate_export.exists() and candidate_export.read_text(encoding="utf-8") != candidate_contents:
            raise RuntimeError("An immutable candidate export fingerprint has conflicting contents.")
        if not candidate_export.exists():
            candidate_export.write_text(candidate_contents, encoding="utf-8")
        result.update(candidate_export=str(candidate_export), candidate_export_sha256=sha(candidate_export))
        if stage_only:
            checkpoint()
            result.update(status="STAGED_AUTHORING_VERIFIED", complete_migration=not result["unmapped_fields"],
                          candidate_created_or_verified=False, idempotent_zero_saves=True)
            return result
        existing = unreal.load_asset(TARGET) if unreal.EditorAssetLibrary.does_asset_exist(TARGET) else None
        previous = json.loads(RECEIPT.read_text(encoding="utf-8")) if RECEIPT.exists() else {}
        if existing is not None:
            if str(existing.get_class().get_path_name()) != TARGET_CLASS:
                raise RuntimeError("Target path belongs to an incompatible class.")
            current = digest(snapshot(existing))
            if current != wanted and current != previous.get("target_authored_fingerprint"):
                raise RuntimeError("Target contains authored changes beyond the recorded importer output; preserving them.")
        checkpoint()
        if existing is None:
            factory = unreal.DataAssetFactory()
            native_class = unreal.load_class(None, TARGET_CLASS)
            factory.set_editor_property("data_asset_class", native_class)
            existing = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DIRECTORY, native_class, factory)
            if existing is None or str(existing.get_path_name()) != TARGET + "." + NAME:
                raise RuntimeError("Exact candidate creation failed.")
        if digest(snapshot(existing)) != wanted:
            existing.modify()
            for field in ("dungeon", "styles", "room_themes", "advanced"):
                existing.set_editor_property(field, staged.get_editor_property(field))
            valid, errors = validate(existing)
            if not valid or digest(snapshot(existing)) != wanted:
                raise RuntimeError("Candidate assignment differs from its validated staged data: " + repr(errors))
            if not unreal.EditorAssetLibrary.save_loaded_asset(existing, only_if_is_dirty=True):
                raise RuntimeError("Exact candidate save failed.")
            result["saved_packages"].append(TARGET)
        result.update(target_authored_fingerprint=digest(snapshot(existing)), target_package_sha256=sha(TARGET_FILE),
                      candidate_created_or_verified=True, complete_migration=not result["unmapped_fields"],
                      idempotent_zero_saves=not result["saved_packages"])
        result["status"] = "PENDING" if result["unmapped_fields"] else "AUTHORING_MIGRATED"
    except Exception as exc:
        result.update(status="FAIL", error=str(exc), traceback=traceback.format_exc())
    finally:
        result["elapsed_seconds"] = round(time.monotonic() - START, 3)
        if before:
            after, dirty_after = source_hashes(), dirty_packages()
            result.update(source_hashes_before=before, source_hashes_after=after, source_unchanged=before == after,
                          dirty_packages_before=sorted(dirty_before), dirty_packages_after=sorted(dirty_after))
            if before != after or SOURCE in dirty_after - dirty_before:
                result.update(status="FAIL", error="Immutable V6 source changed or became dirty; importer must not save it.")
            if stage_only and dirty_before != dirty_after:
                result.update(status="FAIL", error="Staging changed the editor dirty-package set.")
        OUTPUT.mkdir(parents=True, exist_ok=True)
        receipt.write_text(json.dumps(result, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
        unreal.log("CALYSTO_DIRECTOR_IMPORT=" + json.dumps({key: value for key, value in result.items()
                   if key not in {"field_mapping", "unmapped_fields", "traceback"}}, sort_keys=True))
        if stage_only and result["status"] == "FAIL":
            raise RuntimeError(result.get("error", "Director staging failed."))
    if result["status"] == "FAIL":
        raise RuntimeError(result.get("error", "Director import failed."))
    return result


if __name__ == "__main__":
    main()

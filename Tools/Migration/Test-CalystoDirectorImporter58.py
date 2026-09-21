"""Pure migration-value checks; never import Unreal, create assets or execute importer main."""
from __future__ import annotations

import ast
import copy
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import time
import unittest

SOURCE = Path(__file__).with_name("Import-CalystoDungeonDirector58.py")
TREE = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
NAMES = {"Raw", "checkpoint", "split_top", "parse", "text", "plain", "number", "token", "identity", "digest", "Mapper"}
NODES = [node for node in TREE.body if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name in NAMES]
assert {node.name for node in NODES} == NAMES
SCOPE = dict(dataclass=dataclass, hashlib=hashlib, json=json, math=math, time=time,
             START=time.monotonic(), __name__=__name__)
exec(compile(ast.Module(body=NODES, type_ignores=[]), str(SOURCE), "exec"), SCOPE)
Mapper, Raw = SCOPE["Mapper"], SCOPE["Raw"]


class MigrationValueTests(unittest.TestCase):
    def lighting(self, mode, multiplier=1.0):
        return Mapper().lighting(dict(Mode=Raw(mode), IntensityMultiplier=multiplier),
                                 "Styles[0].Lighting", "Styles[0].Lighting")

    def test_exact_four_mode_values(self):
        expected = {"Warm": ((170, 210), [8, 9, 10]), "Balanced": ((180, 220), [9, 10, 11]),
                    "Cold": ((200, 240), [10, 11, 12]), "Dark": ((220, 260), [12, 13, 14])}
        for mode, (height, tiles) in expected.items():
            with self.subTest(mode=mode):
                result = self.lighting(mode)
                self.assertEqual(result["WallLightHeight"], dict(Distribution=Raw("Uniform"), Minimum=height[0], Maximum=height[1]))
                self.assertEqual(result["WallLightTileDistance"]["Distribution"], Raw("Weighted"))
                self.assertEqual(result["WallLightTileDistance"]["Choices"],
                                 [dict(Tiles=tile, Weight=weight) for tile, weight in zip(tiles, [1.0, 2.0, 1.0])])
                self.assertEqual(result["IntensityMultiplier"], dict(Distribution=Raw("Uniform"), Minimum=.92, Maximum=1.08))
                self.assertEqual(result["IntensityCeiling"], 4.0)
        self.assertEqual(self.lighting("EEFCalystoLightingModeV6::Warm"), self.lighting("Warm"))

    def test_intensity_saturation_and_zero(self):
        result = self.lighting("Warm", 4.0)
        low, high = result["IntensityMultiplier"]["Minimum"], result["IntensityMultiplier"]["Maximum"]
        self.assertAlmostEqual(low, 3.68)
        self.assertAlmostEqual(high, 4.32)
        # Independent uniform-tail probability: retaining the upper tail preserves the atom at four.
        self.assertAlmostEqual((high - result["IntensityCeiling"]) / (high - low), .5)
        self.assertEqual(self.lighting("Warm", 0)["IntensityMultiplier"],
                         dict(Distribution=Raw("Uniform"), Minimum=0.0, Maximum=0.0))

    def test_bad_mode_multiplier_and_missing_fields_reject_exact_field(self):
        for mode in ("warm", "Unknown", ""):
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, r"Styles\[0\]\.Lighting\.Mode"):
                self.lighting(mode)
        for value in (-1, 4.01, float("nan"), float("inf"), True, Raw("garbage")):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, r"Styles\[0\]\.Lighting\.IntensityMultiplier"):
                self.lighting("Warm", value)
        for missing in ("Mode", "IntensityMultiplier"):
            data = dict(Mode=Raw("Warm"), IntensityMultiplier=1)
            del data[missing]
            with self.assertRaisesRegex(KeyError, r"Styles\[0\]\.Lighting\." + missing):
                Mapper().lighting(data, "Styles[0].Lighting", "Styles[0].Lighting")

    def test_exact_size_set_and_reordered_source_preserve_leaf_values(self):
        for values in (list(range(18, 31)), [Raw(str(v)) for v in reversed(range(18, 31))]):
            original = copy.deepcopy(values)
            mapper = Mapper()
            self.assertTrue(mapper.validated_sizes(values))
            self.assertEqual(values, original)
            self.assertEqual(len(mapper.records), 13)
            self.assertEqual([row["source_value"] for row in mapper.records], SCOPE["plain"](original))
            self.assertTrue(all(row["status"] == "CORRECTED" and row["runtime_verification"] == "PENDING" for row in mapper.records))

    def test_sparse_duplicate_noninteger_and_invalid_sizes_stay_pending(self):
        invalid = [[], [18, 30], list(range(18, 30)) + [29], list(range(19, 32)),
                   list(range(18, 30)) + [29.5], list(range(18, 30)) + [float("nan")],
                   list(range(18, 30)) + [True], list(range(18, 30)) + [Raw("bad")], {}]
        for values in invalid:
            with self.subTest(values=values):
                mapper = Mapper()
                self.assertFalse(mapper.validated_sizes(values))
                self.assertTrue(mapper.records)
                self.assertTrue(all(row["status"] == "PENDING" and row["target_field"] is None for row in mapper.records))

    def test_archived_styles_keep_every_nonlighting_value_and_native_torch(self):
        base = SOURCE.parents[2] / "Saved/Migration/CalystoDungeonDirectorV7"
        archive = json.loads((base / "SourceV6_Complete_F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323.json").read_text(encoding="utf-8"))
        candidate = json.loads((base / "CandidateV7_ED7C013BED8F094DF080342E7D722C505583154D8A9F36C4BB27413940C65953_7E39035BFFF7.json").read_text(encoding="utf-8"))
        parsed = {key: SCOPE["parse"](value) for key, value in archive["raw_authored_export"].items()}
        mapper = Mapper()
        for index, original in enumerate(parsed["Styles"]):
            actual = SCOPE["plain"](mapper.style(original, index))
            prior = copy.deepcopy(candidate["mapped_authored_values"]["Styles"][index])
            lighting, prior_lighting = actual.pop("Lighting"), prior.pop("Lighting")
            self.assertEqual(actual["Decals"].pop("SourceFadeStartDistanceCm"), SCOPE["plain"](original["Decals"]["FadeStartDistanceCm"]))
            self.assertEqual(actual["Decals"].pop("FadeScreenSize"), .01)
            self.assertEqual(actual.pop("Traits"), SCOPE["plain"](original["Traits"]))
            self.assertEqual(actual["Architecture"].pop("MaximumDecorationsPerRoom"), SCOPE["plain"](original["Decoration"]["MaximumDecorationsPerRoom"]))
            self.assertEqual(SCOPE["number"](original["Decoration"]["MaximumDecorationsPerRoom"]), 4)
            self.assertEqual(actual["Architecture"].pop("DecorationChance"), dict(FirstPercent=35.0, LastPercent=35.0))
            for decoration in actual["Architecture"]["Decoration"]:
                self.assertEqual(decoration["Chance"], dict(FirstPercent=35.0, LastPercent=35.0))
                # The old candidate had an inactive all-100 default; compare the preserved payload separately.
                decoration["Chance"] = dict(FirstPercent=100.0, LastPercent=100.0)
            self.assertEqual(actual, prior)
            self.assertEqual(lighting["WallLights"], prior_lighting["WallLights"])
            self.assertEqual(lighting["Placement"], prior_lighting["Placement"])
            self.assertEqual(lighting["WallLightHeight"]["Minimum"], [170, 180, 180][index])
        self.assertTrue(mapper.validated_sizes(parsed["PerformanceAndSafety"]["ValidatedDungeonSizes"]))
        pending_paths = {row["source_field"] for row in candidate["mapping"] if row["status"] == "PENDING"}
        covered = {row["source_field"] for row in mapper.records if row["status"] == "CORRECTED" and row["source_field"]
                   and (row["source_field"].endswith(".Lighting.Mode") or row["source_field"].startswith("PerformanceAndSafety.ValidatedDungeonSizes["))}
        self.assertEqual(len(pending_paths & covered), 16)
        self.assertEqual(len(pending_paths - covered), 52)

    def test_archived_theme_metadata_covers_ten_leaves_without_other_changes(self):
        base = SOURCE.parents[2] / "Saved/Migration/CalystoDungeonDirectorV7"
        archive = json.loads((base / "SourceV6_Complete_F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323.json").read_text(encoding="utf-8"))
        candidate = json.loads((base / "CandidateV7_ED7C013BED8F094DF080342E7D722C505583154D8A9F36C4BB27413940C65953_7E39035BFFF7.json").read_text(encoding="utf-8"))
        themes = SCOPE["parse"](archive["raw_authored_export"]["RoomThemes"])
        descriptions = ["Combat, armor, locked chests, cooked meat, and strong supplies.",
                        "Mage threats, support, companions, potions, water, and Winter's Recall."]
        colors = [(1.0, .32, .02, 1.0), (.03, .32, 1.0, 1.0)]
        mapper = Mapper()
        original = copy.deepcopy(themes)
        for index, theme in enumerate(themes):
            actual = mapper.theme(theme, index)
            self.assertEqual(SCOPE["token"](actual.pop("Description")), descriptions[index])
            color = actual.pop("PreviewColor")
            self.assertEqual(tuple(SCOPE["number"](color[key]) for key in "RGBA"), colors[index])
            self.assertEqual(actual["Decals"].pop("SourceFadeStartDistanceCm"), theme["Decals"]["FadeStartDistanceCm"])
            self.assertEqual(actual["Decals"].pop("FadeScreenSize"), .01)
            self.assertEqual(actual.pop("Traits"), theme["Traits"])
            self.assertEqual(SCOPE["plain"](actual), candidate["mapped_authored_values"]["RoomThemes"][index])
        self.assertEqual(themes, original)
        pending_paths = {row["source_field"] for row in candidate["mapping"] if row["status"] == "PENDING"}
        covered = [row for row in mapper.records if row["source_field"] in pending_paths and row["status"] == "CORRECTED"
                   and (row["source_field"].endswith(".Description") or ".PreviewColor." in row["source_field"])]
        self.assertEqual(len(covered), 10)
        self.assertEqual(len({row["source_field"] for row in covered}), 10)
        self.assertTrue(all(row["status"] == "CORRECTED" and row["runtime_verification"] == "PENDING" for row in covered))

    def test_theme_metadata_preserves_escaped_notes_hdr_color_and_alpha(self):
        notes = 'Notes, "quoted", Winter\'s Recall.\nSecond line with café.'
        source = SCOPE["parse"](SCOPE["text"](dict(Description=notes, PreviewColor=dict(R=2.0, G=-.25, B=.03, A=.125))))
        result = Mapper().theme_metadata(source, "RoomThemes[1]", "RoomThemes[1]")
        self.assertEqual(result, source)
        self.assertEqual(SCOPE["token"](result["Description"]), notes)
        self.assertEqual(SCOPE["parse"](SCOPE["text"](result)), source)

    def test_missing_theme_metadata_or_nonfinite_channel_rejects_exact_field(self):
        source = dict(Description=Raw('"Notes"'), PreviewColor={key: Raw("1.0") for key in "RGBA"})
        for field in ("Description", "PreviewColor", "R", "G", "B", "A"):
            broken = copy.deepcopy(source)
            if field in "RGBA":
                del broken["PreviewColor"][field]
                path = "PreviewColor." + field
            else:
                del broken[field]
                path = field
            with self.subTest(field=field), self.assertRaisesRegex(KeyError, r"RoomThemes\[1\]\." + path):
                Mapper().theme_metadata(broken, "RoomThemes[1]", "RoomThemes[1]")
        for value in (Raw("nan"), Raw("inf"), Raw("garbage"), True, None):
            source["PreviewColor"]["A"] = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, r"RoomThemes\[1\]\.PreviewColor\.A"):
                Mapper().theme_metadata(source, "RoomThemes[1]", "RoomThemes[1]")

    def test_archived_global_ceilings_map_all_six_exact_leaf_values(self):
        base = SOURCE.parents[2] / "Saved/Migration/CalystoDungeonDirectorV7"
        archive = json.loads((base / "SourceV6_Complete_F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323.json").read_text(encoding="utf-8"))
        source = SCOPE["parse"](archive["raw_authored_export"]["PerformanceAndSafety"])["HardCeilings"]
        original = copy.deepcopy(source)
        mapper = Mapper()
        result = mapper.safety_ceilings(source)
        expected = dict(Enemies=25, LooseFood=8, Chests=3, LootActors=4, SpecialEvents=4, TotalActors=36)
        self.assertEqual({key: SCOPE["number"](value) for key, value in result.items()}, expected)
        self.assertEqual(source, original)
        self.assertEqual(len(mapper.records), 6)
        self.assertEqual({row["target_field"] for row in mapper.records}, {"Advanced.HardCeilings." + key for key in expected})
        self.assertTrue(all(row["status"] == "CORRECTED" and row["runtime_verification"] == "PENDING" for row in mapper.records))
        self.assertEqual(SCOPE["parse"](SCOPE["text"](result)), result)

    def test_global_ceilings_are_exact_capacities_without_clamping_or_defaults(self):
        source = dict(MaximumEnemies=0, MaximumLooseFood=2, MaximumChests=17,
                      MaximumLootActors=31, MaximumSpecialEvents=2147483647, MaximumDirectorActors=9)
        self.assertEqual(Mapper().safety_ceilings(source), dict(Enemies=0, LooseFood=2, Chests=17,
                         LootActors=31, SpecialEvents=2147483647, TotalActors=9))
        for field in source:
            missing = dict(source)
            del missing[field]
            with self.subTest(field=field), self.assertRaisesRegex(KeyError, "PerformanceAndSafety.HardCeilings." + field):
                Mapper().safety_ceilings(missing)

    def test_malformed_global_ceilings_reject_exact_source_field(self):
        for value in (-1, 1.5, float("inf"), float("nan"), True, Raw("bad"), 2147483648):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "PerformanceAndSafety.HardCeilings.MaximumEnemies"):
                Mapper().safety_ceilings(dict(MaximumEnemies=value))

    def test_five_archived_decal_distances_are_explicit_retired_approximation(self):
        base = SOURCE.parents[2] / "Saved/Migration/CalystoDungeonDirectorV7"
        archive = json.loads((base / "SourceV6_Complete_F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323.json").read_text(encoding="utf-8"))
        mapper = Mapper()
        for family in ("Styles", "RoomThemes"):
            for index, original in enumerate(SCOPE["parse"](archive["raw_authored_export"][family])):
                source = copy.deepcopy(original["Decals"])
                path = f"{family}[{index}].Decals"
                result = mapper.decals(source, path, path)
                self.assertEqual(SCOPE["number"](result["SourceFadeStartDistanceCm"]), 2500.0)
                self.assertEqual(SCOPE["number"](result["CullDistanceCm"]), 4000.0)
                self.assertEqual(result["FadeScreenSize"], .01)
                self.assertEqual(source, original["Decals"])
                self.assertEqual(result["SourceFadeStartDistanceCm"], source["FadeStartDistanceCm"])
        retired = [row for row in mapper.records if row["status"] == "MATHEMATICAL_CORRECTION"]
        self.assertEqual(len(retired), 5)
        self.assertEqual(len({row["source_field"] for row in retired}), 5)
        self.assertTrue(all("RETIRED_DISTANCE_APPROXIMATION" in row["note"] and "0.0001,0.25" in row["note"]
                            and row["runtime_verification"] == "PENDING" for row in retired))


    def test_optional_decoration_maps_shared_policy_and_rejects_malformed_values(self):
        base = SOURCE.parents[2] / "Saved/Migration/CalystoDungeonDirectorV7"
        archive = json.loads((base / "SourceV6_Complete_F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323.json").read_text(encoding="utf-8"))
        original = SCOPE["parse"](archive["raw_authored_export"]["Styles"])[0]
        mapped = Mapper().style(original, 0)["Architecture"]
        self.assertEqual(mapped["DecorationChance"], dict(FirstPercent=35.0, LastPercent=35.0))
        self.assertEqual(SCOPE["number"](mapped["MaximumDecorationsPerRoom"]), 4)
        for field, invalid_values in (("Density", (-.1, 1.1, True, float("nan"))),
                                      ("MaximumDecorationsPerRoom", (-1, 257, 4.5, True, float("inf")))):
            for value in invalid_values:
                bad = copy.deepcopy(original)
                bad["Decoration"][field] = value
                with self.subTest(field=field, value=value), self.assertRaisesRegex(ValueError, r"Styles\[0\]\.Decoration\." + field):
                    Mapper().style(bad, 0)
        for density, capacity in ((0, 0), (1, 256)):
            source = copy.deepcopy(original)
            source["Decoration"] = dict(Density=density, MaximumDecorationsPerRoom=capacity)
            result = Mapper().style(source, 0)["Architecture"]
            self.assertEqual(result["DecorationChance"], dict(FirstPercent=density*100, LastPercent=density*100))
            self.assertEqual(result["MaximumDecorationsPerRoom"], capacity)

    def test_archived_traits_retain_all_25_values_without_implicit_bindings(self):
        base = SOURCE.parents[2] / "Saved/Migration/CalystoDungeonDirectorV7"
        archive = json.loads((base / "SourceV6_Complete_F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323.json").read_text(encoding="utf-8"))
        fields = ("Mystery", "Danger", "Safe", "Abundance", "ClothingInfluence")
        expected = {"Styles": [(0, 0, 0, 0, 0), (0, 0, 0, 0, 0), (.30, 0, 0, 0, 0)],
                    "RoomThemes": [(0, .85, 0, .45, 0), (.65, 0, .70, 0, 0)]}
        mapper = Mapper()
        for family in expected:
            originals = SCOPE["parse"](archive["raw_authored_export"][family])
            preserved = copy.deepcopy(originals)
            for index, original in enumerate(originals):
                path = f"{family}[{index}].Traits"
                result = mapper.traits(original["Traits"], path, path)
                self.assertEqual(tuple(SCOPE["number"](result[field]) for field in fields), expected[family][index])
                self.assertEqual(result, original["Traits"])
                self.assertEqual(SCOPE["parse"](SCOPE["text"](result)), result)
            self.assertEqual(originals, preserved)
        self.assertEqual(len(mapper.records), 25)
        self.assertEqual(len({row["source_field"] for row in mapper.records}), 25)
        self.assertTrue(all(row["status"] == "CORRECTED" and "bindings empty" in row["note"]
                            and row["runtime_verification"] == "PENDING" for row in mapper.records))

    def test_traits_reject_each_invalid_or_missing_exact_field(self):
        source = dict(Mystery=0, Danger=0, Safe=0, Abundance=0, ClothingInfluence=0)
        for field in source:
            for invalid in (-.01, 1.01, float("nan"), float("inf"), True, Raw("bad")):
                bad = dict(source, **{field: invalid})
                with self.subTest(field=field, invalid=invalid), self.assertRaisesRegex(ValueError, "Traits." + field):
                    Mapper().traits(bad, "Traits", "Traits")
            bad = dict(source)
            del bad[field]
            with self.subTest(field=field), self.assertRaisesRegex(KeyError, "Traits." + field):
                Mapper().traits(bad, "Traits", "Traits")
        self.assertEqual(Mapper().traits(dict.fromkeys(source, 1.0), "Traits", "Traits"), dict.fromkeys(source, 1.0))


if __name__ == "__main__":
    unittest.main()

"""Build the deterministic 81-entry food pickup -> item -> mesh manifest.

This is a filesystem-only audit.  It never opens or writes the UE 5.7 source.
"""

from __future__ import annotations

import hashlib
import json
import re
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


SOURCE = Path(r"D:\Projects UE5\LustAsDeadlySin")
TARGET = Path(r"D:\Projects UE5\NoShellForWinter")
MESH_ROOT = SOURCE / "Content" / "Food_Props_Kit" / "Meshes"
FOOD_ROOT = SOURCE / "Content" / "_Game" / "FoodSystem" / "Food" / "Items"
TARGET_FOOD_ROOT = TARGET / "Content" / "_Game" / "FoodSystem" / "Food" / "Items"
OUTPUT = TARGET / "Saved" / "Migration" / "FoodKitAlcohol" / "FoodKit81Manifest.json"

PATTERNS = (
    (r"^SM_Apple(\d+)$", "Food", "Apple"),
    (r"^SM_Avocado(\d+)$", "Food", "Avocado"),
    (r"^SM_Banana(\d+)$", "Food", "Banana"),
    (r"^SM_Bananas(\d+)$", "Food", "Bananas"),
    (r"^SM_Beet(\d+)$", "Food", "Beet"),
    (r"^SM_Bellpepper(\d+)$", "Food", "BellPepper"),
    (r"^SM_BottleGourd(\d+)$", "Food", "BottleGourd"),
    (r"^SM_Bread(\d+)$", "Food", "Bread"),
    (r"^SM_Cabbage(\d+)$", "Food", "Cabbage"),
    (r"^SM_Carrot(\d+)$", "Food", "Carrot"),
    (r"^SM_Cheese(\d+)$", "Food", "Cheese"),
    (r"^SM_Chicken(\d+)$", "Food", "RawChicken"),
    (r"^SM_Coconut(\d+)$", "Food", "Coconut"),
    (r"^SM_Corn(\d+)$", "Food", "Corn"),
    (r"^SM_Eggplant(\d+)$", "Food", "Eggplant"),
    (r"^SM_Fish(\d+)$", "Food", "RawFish"),
    (r"^SM_Garlic(\d+)$", "Food", "Garlic"),
    (r"^SM_Ginger(\d+)$", "Food", "Ginger"),
    (r"^SM_Gourd(\d+)$", "Food", "Gourd"),
    (r"^SM_Ham(\d+)$", "Food", "Ham"),
    (r"^SM_Lemon(\d+)$", "Food", "Lemon"),
    (r"^SM_Meat(\d+)$", "Food", "RawMeat"),
    (r"^SM_Melon(\d+)$", "Food", "Melon"),
    (r"^SM_Mushroom(\d+)$", "Food", "Mushroom"),
    (r"^SM_Onion(\d+)$", "Food", "Onion"),
    (r"^SM_Orange(\d+)$", "Food", "Orange"),
    (r"^SM_Pepper(\d+)$", "Food", "Pepper"),
    (r"^SM_Pikcles(\d+)$", "Food", "Pickles"),
    (r"^SM_Pineapple(\d+)$", "Food", "Pineapple"),
    (r"^SM_Potato(\d+)$", "Food", "Potato"),
    (r"^SM_Pumpkin(\d+)$", "Food", "Pumpkin"),
    (r"^SM_Radish(\d+)$", "Food", "Radish"),
    (r"^SM_Ribs(\d+)$", "Food", "RawRibs"),
    (r"^SM_Sausage(\d+)$", "Food", "Sausage"),
    (r"^SM_SweetPotato(\d+)$", "Food", "SweetPotato"),
    (r"^SM_Tomato(\d+)$", "Food", "Tomato"),
    (r"^SM_Watermelon(\d+)$", "Food", "Watermelon"),
    (r"^MeatRoasted(\d+)$", "Food", "CookedMeat"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def classify(name: str):
    bottle = re.fullmatch(r"SM_Bottle(\d+)Fill", name)
    if bottle:
        number = int(bottle.group(1))
        if 1 <= number <= 6:
            return "Drink", "WaterBottle", number
        if 7 <= number <= 10:
            return "Drink", "AlcoholBottle", number
        return None
    if name.endswith("Piece"):
        return None
    for pattern, group, core in PATTERNS:
        match = re.fullmatch(pattern, name)
        if match:
            return group, core, int(match.group(1))
    return None


def contains_reference(path: Path, object_path: str) -> bool:
    data = path.read_bytes()
    ascii_ref = object_path.encode("ascii")
    utf16_ref = object_path.encode("utf-16-le")
    return ascii_ref in data or utf16_ref in data


def main() -> None:
    if SOURCE.resolve() == TARGET.resolve():
        raise RuntimeError("Source and target roots must differ")
    if not (SOURCE / "ACFSample.uproject").is_file():
        raise RuntimeError("Read-only source project is absent")
    if not (TARGET / "NoShellForWinter.uproject").is_file():
        raise RuntimeError("Target project is absent")

    rows = []
    for mesh_file in sorted(MESH_ROOT.glob("*.uasset"), key=lambda value: value.name.lower()):
        result = classify(mesh_file.stem)
        if result is None:
            continue
        group, core, number = result
        item_name = f"BP_{group}_{core}{number:02d}"
        pickup_name = f"BP_Pickup_{group}_{core}{number:02d}"
        item_relative = Path(group) / f"{item_name}.uasset"
        pickup_relative = Path("PickuableItems") / group / f"{pickup_name}.uasset"
        source_item = FOOD_ROOT / item_relative
        source_pickup = FOOD_ROOT / pickup_relative
        target_item = TARGET_FOOD_ROOT / item_relative
        target_pickup = TARGET_FOOD_ROOT / pickup_relative
        mesh_package = f"/Game/Food_Props_Kit/Meshes/{mesh_file.stem}"
        item_package = f"/Game/_Game/FoodSystem/Food/Items/{item_relative.as_posix()[:-7]}"
        pickup_package = f"/Game/_Game/FoodSystem/Food/Items/{pickup_relative.as_posix()[:-7]}"
        for label, path in (
            ("source item", source_item),
            ("source pickup", source_pickup),
            ("target item", target_item),
            ("target pickup", target_pickup),
        ):
            if not path.is_file():
                raise RuntimeError(f"Missing {label}: {path}")
        if not contains_reference(source_item, mesh_package):
            raise RuntimeError(f"Source item does not serialize {mesh_package}: {source_item}")
        if not contains_reference(source_pickup, mesh_package):
            raise RuntimeError(f"Source pickup does not serialize {mesh_package}: {source_pickup}")
        if not contains_reference(target_item, mesh_package):
            raise RuntimeError(f"Target item lost WorldMesh reference {mesh_package}: {target_item}")
        rows.append(
            {
                "item_package": item_package,
                "pickup_package": pickup_package,
                "mesh_package": mesh_package,
                "mesh_source_file": str(mesh_file.resolve()),
                "mesh_source_length": mesh_file.stat().st_size,
                "mesh_source_sha256": sha256(mesh_file),
                "source_item_file": str(source_item.resolve()),
                "source_pickup_file": str(source_pickup.resolve()),
                "target_item_file": str(target_item.resolve()),
                "target_pickup_file": str(target_pickup.resolve()),
                "target_item_world_mesh_serialized": True,
                "source_pickup_mesh_serialized": True,
            }
        )

    if len(rows) != 81:
        raise RuntimeError(f"Expected 81 classified meshes, found {len(rows)}")
    for key in ("item_package", "pickup_package", "mesh_package"):
        if len({row[key] for row in rows}) != 81:
            raise RuntimeError(f"Manifest contains duplicate {key}")

    family_counts = Counter(
        "Alcohol" if "AlcoholBottle" in row["item_package"] else
        "Water" if "WaterBottle" in row["item_package"] else "Food"
        for row in rows
    )
    fingerprint_lines = [
        f"{row['pickup_package']}|{row['item_package']}|{row['mesh_package']}|{row['mesh_source_length']}|{row['mesh_source_sha256']}"
        for row in rows
    ]
    fingerprint = hashlib.sha256("\n".join(fingerprint_lines).encode("utf-8")).hexdigest().upper()
    payload = {
        "schema_version": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "status": "FOOD_KIT_81_MANIFEST_PASS",
        "source_root": str(SOURCE),
        "target_root": str(TARGET),
        "entry_count": len(rows),
        "family_counts": dict(sorted(family_counts.items())),
        "fingerprint": fingerprint,
        "fingerprint_algorithm": "SHA256 of LF-joined pickup|item|mesh|mesh_length|mesh_sha256 rows",
        "entries": rows,
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"FOOD_KIT_81_MANIFEST_PASS: {OUTPUT}")
    print(f"entries=81 fingerprint={fingerprint} counts={dict(family_counts)}")


if __name__ == "__main__":
    main()

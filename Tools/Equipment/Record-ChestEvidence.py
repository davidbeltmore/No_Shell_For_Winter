"""Copy selected current chest QA artifacts and bind them to their exact bytes."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
saved = root / "Saved/EquipmentQA"
out = root / "Docs/Migration/Evidence/ProceduralChests"
out.mkdir(parents=True, exist_ok=True)
paths = [
    "ProceduralChests/build07.log", "ProceduralChests/game02.log",
    "ProceduralChests/author.json", "ProceduralChests/identities.json",
    "ProceduralChests/protected_before.json", "ProceduralChests/protected_final.json",
    "ProceduralChests/protected_historical.json", "ProceduralChests/calysto_before.json",
    "ProceduralChests/calysto_after.json", "ProceduralChests/Native04/Report/index.json",
    "ProceduralChests/native04-launch.log",
    "ProceduralChests/runtime-assets-final.json", "ProceduralChests/cook-closure-final.json",
    "ProceduralChestsPIE02/result.json", "ProceduralChestsPIE02/user_save_after.json",
    "ProceduralChestsPIE02/details-deposit.png", "ProceduralChestsPIE02/details-locked-focused.png",
    "ProceduralChestTravel01/result.json", "ProceduralChestTravel01/user_save_after.json",
    "StorageProceduralChests02/result.json", "StorageProceduralChests02/user_save_after.json",
    "StorageProceduralChests02/bag.png",
]
# Optional release receipts are included only if they exist. Absence is not PASS.
for name in ("ChestsNative01", "ChestsEquipment01", "ChestsVisual01"):
    paths += [f"Packaged_{name}/launch.json"]
    paths += [f"Packaged_{name}/Report/index.json"] if "Visual" not in name else [
        f"Packaged_{name}/User/Saved/EquipmentQA/Packaged/result.json",
        f"Packaged_{name}/User/Saved/EquipmentQA/Packaged/inventory.png",
    ]

artifacts, missing = {}, []
for relative in paths:
    source = saved / relative
    if not source.is_file():
        missing.append(relative)
        continue
    destination = out / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    artifacts[relative] = hashlib.sha256(destination.read_bytes()).hexdigest()

tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
source_hashes = {}
for relative in tracked:
    if (relative.startswith(("Plugins/EFEquipment/Source/", "Plugins/EFProjectSystems/Source/"))
            or relative.startswith("Content/_Game/Data/Equipment/Chests/")
            or relative in ("Content/_Game/Hub/HUB.umap", "Content/_Game/Tests/ChestProcedural.umap",
                            "Content/_Game/Items/Chests/BP_EquipmentDeposit.uasset",
                            "Content/_Game/Items/Chests/BP_EquipmentLockedChest.uasset",
                            "Config/DefaultEngine.ini", "Config/Tags/ChestContent.ini")):
        source_hashes[relative] = hashlib.sha256((root / relative).read_bytes()).hexdigest()
manifest = {
    "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root).decode().strip(),
    "scope": "Artifact inventory, not an automatic PASS declaration; see per-gate receipts.",
    "artifacts": artifacts, "missing": missing, "source_sha256": source_hashes,
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"copied": len(artifacts), "missing": missing}))

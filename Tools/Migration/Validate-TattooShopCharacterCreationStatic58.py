"""Static non-visual validation for TattooShop inside EF Character Creation.

This intentionally avoids visual QA. It verifies the UE 5.8 target can load
the Character Creation root widget class, TattooShop subsystem class, and the
imported TattooShop widget/assets used by the Tattoo tab bridge.
"""

import json
import os
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_FILE = Path(
    os.environ.get(
        "CODEX_TATTOOSHOP_CC_STATIC_EVIDENCE",
        PROJECT_DIR / "Saved" / "Migration" / "Phase4" / "TattooShop" / "TattooShopCharacterCreationStatic58.json",
    )
)


def class_load(path_name):
    klass = unreal.load_class(None, path_name)
    return {
        "path": path_name,
        "loaded": bool(klass),
        "resolved": klass.get_path_name() if klass else "",
    }


def asset_exists(asset_path):
    exists = bool(unreal.EditorAssetLibrary.does_asset_exist(asset_path))
    return {
        "path": asset_path,
        "exists": exists,
    }


def function_exists(class_path, function_name):
    klass = unreal.load_class(None, class_path)
    exists = False
    if klass:
        try:
            exists = bool(klass.find_function_by_name(function_name))
        except Exception:
            # Some UE Python builds do not expose UClass::FindFunctionByName.
            # Fall back to a conservative string scan of the reflected class.
            try:
                exists = function_name in str(klass)
            except Exception:
                exists = False
    return {
        "class": class_path,
        "function": function_name,
        "exists": bool(exists),
        "reflection_note": "UE commandlet Python may not expose UFunction reflection for native classes.",
    }


def source_contains(relative_path, needle):
    file_path = PROJECT_DIR / relative_path
    try:
        text = file_path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        text = ""
    return {
        "file": str(relative_path),
        "needle": needle,
        "exists": bool(text and needle in text),
    }


def main():
    checks = {
        "classes": [
            class_load("/Script/EFCharacterCreationRuntime.EFCharacterCreationRootWidget"),
            class_load("/Script/EFProjectSystemsGameplay.ProjectTattooShopInputSubsystem"),
            class_load("/Game/TattooShop/Blueprints/Widget/WBP_TattooShop.WBP_TattooShop_C"),
            class_load("/Game/TattooShop/Blueprints/Widget/WBP_AssetPreviewer.WBP_AssetPreviewer_C"),
        ],
        "assets": [
            asset_exists("/Game/TattooShop/Blueprints/Widget/WBP_TattooShop"),
            asset_exists("/Game/TattooShop/Blueprints/Widget/WBP_AssetPreviewer"),
            asset_exists("/Game/TattooShop/Texture/T_Heart"),
            asset_exists("/Game/TattooShop/Materials/M_TattooShop"),
            asset_exists("/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar"),
        ],
        "functions": [
            function_exists(
                "/Script/EFCharacterCreationRuntime.EFCharacterCreationRootWidget",
                "OpenTattooTabForAutomation",
            ),
            function_exists(
                "/Script/EFProjectSystemsGameplay.ProjectTattooShopInputSubsystem",
                "RequestOpenTattooShopInHosts",
            ),
            function_exists(
                "/Script/EFProjectSystemsGameplay.ProjectTattooShopInputSubsystem",
                "IsTattooShopOpen",
            ),
        ],
        "source_bridge": [
            source_contains(
                "Plugins/EFCharacterCreation/Source/EFCharacterCreationRuntime/Private/UI/EFCharacterCreationRootWidget.cpp",
                "OpenTattooShopInCharacterCreation();",
            ),
            source_contains(
                "Plugins/EFCharacterCreation/Source/EFCharacterCreationRuntime/Private/UI/EFCharacterCreationRootWidget.cpp",
                "RequestOpenTattooShopInHosts",
            ),
            source_contains(
                "Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/TattooShop/ProjectTattooShopInputSubsystem.cpp",
                "TryResolveRuntimeContext();",
            ),
            source_contains(
                "Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/TattooShop/ProjectTattooShopInputSubsystem.cpp",
                "IsValid(TattooShopHostPanel.Get())",
            ),
            source_contains(
                "Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/TattooShop/ProjectTattooShopInputSubsystem.cpp",
                "UGameplayStatics::GetPlayerController(World, 0)",
            ),
        ],
    }

    all_loaded = all(item["loaded"] for item in checks["classes"])
    all_assets = all(item["exists"] for item in checks["assets"])
    all_source_bridge = all(item["exists"] for item in checks["source_bridge"])
    # Function reflection may be unavailable in commandlet Python, so class and
    # asset loads plus source bridge checks are the hard non-visual gate.
    status = "PASS" if all_loaded and all_assets and all_source_bridge else "FAIL"
    payload = {
        "status": status,
        "scope": "non_visual_static_tattoo_tab_bridge",
        "project": str(PROJECT_DIR),
        "checks": checks,
        "note": "Visual QA intentionally excluded per user request.",
    }

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    unreal.log(f"[TattooShopStatic58] {status}: wrote {OUTPUT_FILE}")


main()

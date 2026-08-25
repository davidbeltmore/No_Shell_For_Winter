import json
import os
import traceback

import unreal


PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RECEIPT_DIR = os.path.join(PROJECT_DIR, "Saved", "ClothingMorphV2QA")
RECEIPT_PATH = os.path.join(RECEIPT_DIR, "compiler_receipt.json")


def _write_receipt(payload):
    os.makedirs(RECEIPT_DIR, exist_ok=True)
    with open(RECEIPT_PATH, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)


def _get_property(value, *names):
    for name in names:
        try:
            return value.get_editor_property(name)
        except Exception:
            pass
        try:
            return getattr(value, name)
        except Exception:
            pass
    raise AttributeError("Unable to resolve property: " + ", ".join(names))


def main():
    source = unreal.load_asset("/Game/DazToUnreal/UnderWearPanty/UnderWearPanty")
    body = unreal.load_asset("/Game/DazToUnreal/Female/Female")
    compatibility = unreal.load_asset("/Game/DazToUnreal/Multiple/Multiple")
    if not source or not body or not compatibility:
        raise RuntimeError("Could not load source garment, Female body surface, or Multiple compatibility reference.")

    options = unreal.EFClothingFitCompileOptions()
    options.set_editor_property("output_root", "/Game/_Generated/EFClothingMorphV2")
    options.set_editor_property("minimum_clearance_cm", 0.45)
    options.set_editor_property("maximum_push_cm", 2.5)
    options.set_editor_property("smoothing_iterations", 4)
    options.set_editor_property("maximum_influences", 8)
    options.set_editor_property("transfer_missing_body_morphs", True)
    options.set_editor_property("maximum_transferred_morphs", 96)
    options.set_editor_property("minimum_transferred_morph_delta_cm", 0.02)
    options.set_editor_property("copy_body_deformer_to_derived", True)

    result = unreal.EFClothingFitCompilerLibrary.compile_fit_profile(
        source,
        body,
        compatibility,
        options,
    )
    success = bool(_get_property(result, "success", "b_success", "bSuccess"))
    report = str(_get_property(result, "report"))
    profile = _get_property(result, "profile")
    derived = _get_property(result, "derived_garment")

    validation_success = False
    validation_report = "Profile was not generated."
    if profile:
        validation_result = unreal.EFClothingFitCompilerLibrary.validate_compiled_profile(profile)
        if isinstance(validation_result, tuple):
            validation_success = bool(validation_result[0])
            validation_report = str(validation_result[1])
        else:
            validation_success = bool(validation_result)

    receipt = {
        "success": success and validation_success,
        "compile_success": success,
        "validation_success": validation_success,
        "compile_report": report,
        "validation_report": validation_report,
        "source": source.get_path_name(),
        "body": body.get_path_name(),
        "compatibility": compatibility.get_path_name(),
        "derived": derived.get_path_name() if derived else None,
        "profile": profile.get_path_name() if profile else None,
        "settings": {
            "minimum_clearance_cm": 0.45,
            "maximum_push_cm": 2.5,
            "smoothing_iterations": 4,
            "maximum_influences": 8,
            "maximum_transferred_morphs": 96,
        },
    }
    _write_receipt(receipt)
    unreal.log("EF_CLOTHING_MORPH_V2_COMPILER_RECEIPT=" + RECEIPT_PATH)
    unreal.log("EF_CLOTHING_MORPH_V2_COMPILER_RESULT=" + json.dumps(receipt, sort_keys=True))
    if not receipt["success"]:
        raise RuntimeError("EF Clothing Morph V2 compile/validation gate failed: " + report + " | " + validation_report)


try:
    main()
except Exception as exc:
    failure = {
        "success": False,
        "exception": str(exc),
        "traceback": traceback.format_exc(),
    }
    _write_receipt(failure)
    unreal.log_error("EF Clothing Morph V2 compiler failed: " + json.dumps(failure, sort_keys=True))
finally:
    unreal.SystemLibrary.quit_editor()

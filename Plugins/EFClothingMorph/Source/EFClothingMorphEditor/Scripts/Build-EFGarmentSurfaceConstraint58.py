"""Build and validate the canonical EF Clothing Morph V3 Optimus graph.

Run through Tools/Migration/Launch-NoShellForWinterEditor58.ps1 so the DAZ
plugin receipt guard remains active. The C++ builder owns all asset mutation;
this file is only a deterministic commandlet entry point and receipt writer.
"""

from __future__ import annotations

import datetime
import json
import os

import unreal


def _read(result: object, property_name: str):
    return result.get_editor_property(property_name)


def main() -> None:
    unreal.load_module("EFClothingMorphEditor")
    library = unreal.EFClothingSurfaceDeformerBuilderLibrary

    build = library.build_or_update_surface_constraint_deformer(False)
    build_success = bool(_read(build, "success"))
    build_report = str(_read(build, "report"))
    unreal.log(build_report)
    if not build_success:
        raise RuntimeError(f"EF garment surface graph build failed:\n{build_report}")

    validation = library.validate_surface_constraint_deformer()
    validation_success = bool(_read(validation, "success"))
    validation_report = str(_read(validation, "report"))
    unreal.log(validation_report)
    if not validation_success:
        raise RuntimeError(
            f"EF garment surface graph post-save validation failed:\n{validation_report}"
        )

    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    receipt_directory = os.path.join(
        unreal.Paths.project_saved_dir(), "ClothingMorphV3QA", "GraphBuilder"
    )
    os.makedirs(receipt_directory, exist_ok=True)
    receipt_path = os.path.join(
        receipt_directory, f"DG_EFGarmentSurfaceConstraint_{timestamp}.json"
    )
    with open(receipt_path, "w", encoding="utf-8") as receipt_file:
        json.dump(
            {
                "schema": "EFClothingMorph.SurfaceGraph.27.0",
                "asset": (
                    "/EFClothingMorph/Deformers/"
                    "DG_EFGarmentSurfaceConstraint."
                    "DG_EFGarmentSurfaceConstraint"
                ),
                "success": True,
                "rebuilt": bool(_read(build, "rebuilt")),
                "build_report": build_report,
                "validation_report": validation_report,
                "timestamp_utc": timestamp,
            },
            receipt_file,
            indent=2,
            sort_keys=True,
        )
    unreal.log(f"EF Clothing Morph V3 graph receipt: {receipt_path}")


if __name__ == "__main__":
    main()

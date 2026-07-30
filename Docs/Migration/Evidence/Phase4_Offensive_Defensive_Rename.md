# Phase 4 Offensive / Defensive Rename

Date: 2026-07-13 (America/Bogota)

## Scope

- Standardized the two Inner Doctrine attributes as `Offensive` and `Defensive` across project-owned C++, config, UI settings, tests, debug commands, character backgrounds, combat/defeat flow, emote presentation, and migration automation filters.
- Standardized compact labels as `OFF` and `DEF`.
- Renamed 20 project assets through Unreal Editor asset APIs and regenerated the affected Attributes, Inner Doctrine Altar, and Gameplay Debug Widget Blueprint manifests.
- Migrated the two Character Background DataTables and the Project Emote menu data asset.
- Added Core Redirects for serialized enum values, native classes, reflected properties, and the renamed Blueprint-callable function.

## Evidence

| Gate | Result | Evidence |
| --- | --- | --- |
| UE 5.8 Editor build | PASS | `Build.bat NoShellForWinterEditor Win64 Development`; UBT result `Succeeded` |
| Focused native automation | PASS (7/7) | `Saved/Migration/Automation/OffensiveDefensiveRename/index.json`; `Saved/Migration/Logs/OffensiveDefensiveRename.log` |
| Text reference scan | PASS except compatibility redirects | Case-insensitive scan of project text files; remaining legacy tokens are restricted to `Config/DefaultEngine.ini` Core Redirect `OldName`/`ValueChanges` entries |
| Asset filename scan | PASS | No `.uasset` or `.umap` filename contains the retired attribute names |
| Protected target re-hash | PASS | `Saved/Migration/Evidence/OffensiveDefensiveRename_ProtectedInvariants.json`: ACFU 5,043/5,043, DazToUnreal 213/213, target Daz assets 189/189 |
| Blueprint compile | PASS for regenerated affected manifests | Commandlet regeneration completed for Attributes, Inner Doctrine Altar, and Gameplay Debug without Python or compiler errors |
| PIE / visual QA | PENDING | Not run in this focused rename pass |
| Cook / package / packaged runtime | PENDING | Not run in this focused rename pass |

## Notes

Historical Interchange node-container metadata was cleared only on the 12 renamed texture assets after their source filenames were migrated. UE still serializes the original external PNG provenance string in those texture packages; it is editor-only import history, not an asset path, gameplay reference, identifier, or UI label. Runtime texture settings and pixel data remain unchanged.

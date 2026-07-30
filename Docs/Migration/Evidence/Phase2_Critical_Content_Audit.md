# Critical content audit

Status: `SANITIZED_PUBLIC_SUMMARY`

The file-level historical inventory is private because its original paths encode obsolete internal identities. Public migration decisions remain:

| Content area | Classification | Required gate |
|---|---|---|
| Player, Female, Male, Multiple and Frederick | `KEEP_TARGET_PROTECTED` | Byte hash |
| ACFU and DazToUnreal content | `KEEP_TARGET_PROTECTED` | Plugin manifest hash |
| Inner Doctrine UI and data | `PROJECT_OWNED_CANONICAL` | Load, Blueprint compile, PIE, cook |
| Curse, Cursed and recovery status data | `PROJECT_OWNED_CANONICAL` | Data validation and runtime boundary tests |
| Chronicle UI and barks | `PROTECTED_PROJECT_CONTENT` | Bark hash plus SFW resolver tests |
| Defeat UI and presentation | `PROJECT_OWNED_CANONICAL` | Policy matrix and authoritative outcome tests |
| Optional mature presentation | `OPTIONAL_ISOLATED_CONTENT` | Adult/consent gates and cook-closure audit |
| Procedural level flow | `PROJECT_OWNED_MIGRATED` | Blueprint compile, dungeon PIE, cook and package |
| Demo duplicates and unreferenced legacy assets | `EXCLUDE_PENDING_REFERENCER_SCAN` | Asset Registry and Editor referencer scan |

No historical file-level result is promoted by this summary. Unexecuted gates remain `PENDING`.

See [Public_Evidence_Redaction.md](Public_Evidence_Redaction.md).

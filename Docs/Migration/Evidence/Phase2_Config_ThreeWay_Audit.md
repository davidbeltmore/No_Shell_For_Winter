# UE 5.8 configuration audit

Status: `SANITIZED_PUBLIC_SUMMARY`

The detailed comparison against the legacy private build is excluded from the public repository. This summary preserves the decisions and their validation state without publishing historical paths or identifiers.

## Authority

1. Unreal Engine 5.8 configuration schema.
2. ACF Ultimate 4.3.5.
3. Current DazToUnreal and protected character assets.
4. NoShellForWinter project-owned settings.

## Decisions

| Area | Decision | Gate |
|---|---|---|
| Engine and module identity | Keep NoShellForWinter UE 5.8 target values | Project generation and cold build |
| ACF configuration | Keep current ACFU 4.3.5 values | Plugin load, gameplay-tag resolution, PIE and cook |
| Daz configuration | Keep complete current target section | Protected plugin and mesh hashes |
| Project settings | Merge only project-owned owner sections | CDO probe, native tests and runtime smoke |
| Redirects | Temporary migration use only; none in public final state | Asset Registry scan and clean cook without redirects |
| Packaging | Cook only verified dependency closures | Cook manifest, package and packaged runtime |
| Content policy | SFW default; Streamer Safe enforced before presentation load | Startup and mode matrix |
| Inner Doctrine | Canonical Doctrine, DXP, Curse and Guard Recovery settings | Native tests, Blueprint compile and PIE |

## Gate status

- Structural target configuration: `PASS` for previously validated owner sections.
- New canonical gameplay sections: `PENDING` until the current refactor builds and its tests execute.
- PIE, full cook, package, and packaged runtime: `PENDING`.

See [Public_Evidence_Redaction.md](Public_Evidence_Redaction.md) for the public evidence boundary.

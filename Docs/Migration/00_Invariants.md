# Migration invariants

## Project identity

- Public project: NoShellForWinter.
- Migration context: a legacy private build was used as strictly read-only behavioral reference.
- Target engine association observed in `NoShellForWinter.uproject`: `5.8`.
- Historical source paths, repository identity, and private evidence are intentionally excluded from the public tree.

## Authority order

1. Unreal Engine 5.8 APIs.
2. Target ACFU 4.3.5 and its current configuration.
3. Target DazToUnreal and current Daz assets.
4. Target Player, Frederick, Multiple, Female, and Male assets.
5. Approved project-owned behavior and content.
6. Current Inner Doctrine, DXP, and Curse specifications.
7. Project-owned reconstruction when a clean port is impossible.

## Prohibited operations

- No writes or UE 5.8 resaves against the legacy private build.
- No bulk replacement of `Content`, `Config`, `Plugins`, `/Game/FullSample`, or `Player`.
- No replacement or direct edits of ACFU, DazToUnreal, Marketplace plugins, or Engine plugins.
- No success claim based on compilation alone.

## Verified recovery point

- Snapshot: private restorable NoShellForWinter baseline captured before migration.
- Included: full non-generated target state, including `Content`, `Config`, `Source`, and project descriptors.
- Excluded as regenerable: `Binaries`, `Intermediate`, `Saved`, `DerivedDataCache`, and `.vs`.
- Files verified by SHA-256: `5916`.
- Verified bytes: `5.781 GB`.
- Hash mismatches: `0`.
- Manifest: private SHA-256 baseline retained outside the public repository.

## Legacy reference handling

- Read-only handling gate: `PASS`.
- Historical repository coordinates, paths, hashes, dirty-tree details, and private file manifests are retained outside the public repository.
- The public record intentionally states only the gate outcome; it does not identify or reconstruct the legacy private build.

## Protected target manifests

- ACFU 4.3.5: 5,043 files / 6,170,659,421 bytes; manifest SHA-256 `69F46CACC120E44AC3B1729342E059CCD24D77D01534CB4E0F36C4A8A26D87F9`.
- DazToUnreal 5.8.0.491: 213 files / 172,602,860 bytes; manifest SHA-256 `523200EBEED3B1284445C0570029CE746C10D4E5039B41AAD769544166E2B491`.
- Target Daz assets: 189 files / 980,685,486 bytes; manifest SHA-256 `A0F3176C7AFF078DC3D8669D93D1A65ACA6D520AF6938F593500967835414FAA`.
- Player/Female/Multiple/Male individual SHA-256 hashes are recorded in `Docs/Migration/Evidence/Phase0_Target_Invariant_Hashes.json`.

## Phase-gate rule

Advance automatically only after the current phase has evidence for its gate. A failed gate is corrected and repeated. An external blocker is documented while independent work continues.

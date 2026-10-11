# DAZ bow carry movement — 2026-10-10

The previous bow repair stopped directional head collapse, but its static pose
matrix did not establish natural neck motion while walking or turning. User
feedback exposed that gap. Baseline commit: `19fd703749072fdb636066c8eab713632c94f976`.

## Diagnosis and change

Live UE 5.8 MCP confirmed `/Game/_Game/Hub/HUB`. The actual TattooShop-derived
player uses the owned `ABP_DAZBowOverlay`; runtime `NewLayering` selects the owned
carry/aim profiles. Both previously forced Spine to absolute overlay weight 1,
while Head restored the locomotion base pose at weight 1. This disconnected the
torso from the locomotion that drives the head.

A bounded four-stage trace (unarmed walk, bow carry, moving aim, release; 7 seconds
each, 40-second watchdog) used real movement input at 325cm/s and sinusoidal yaw
of +/-45 degrees. Moving aim also swept pitch +/-30 degrees. No asset was saved
during baseline capture. The reference is `Saved/BowMotion/baseline.json`.

| Maximum component-space head/chest angular separation | Before | Candidate |
| --- | ---: | ---: |
| Unarmed moving reference | 20.86° | 20.94° |
| Bow carry | 41.59° | 24.71° |
| Moving aim | 41.61° | 36.21° |
| After aim release | 41.59° | 24.57° |

Candidate evidence: `Saved/BowMotion/candidate.json`; 83–87 samples per settled
stage. The controlled change is absolute Spine **0.2 carry / 0.8 aim**, retaining
Head BASE_POSE 1 and arm overrides. These match native bow torso weights without
restoring the native additive branches that previously distorted DAZ bodies.
The owned directional samples and arm compensation remain intact. No main
AnimBP, native animation, body mesh or Marketplace plugin is changed.

`AuthorDAZWeaponOverlays.py` persists this bow-specific policy. Generic pistol and
rifle policies are unchanged. `Project.GunPose.QA bow` now also runs 8 seconds of
actual player movement for each gender, including aim entry/release and rendered
temporal frames. It requires >=50 samples, >400cm travel, carry head/chest <=32°,
aim <=45°, neck length <1.35x and elevation >0.70x unarmed. These scoped regression
limits distinguish the reported failure; they are not universal anatomy limits.

The first native movement run failed when its +X route left the finite HUB floor
after about 7.7 seconds. `Saved/BowMotion/native-observer.json` records 17 falling
frames, with speed increasing to 585cm/s; neck length stayed near 18.8cm while
world-vertical elevation fell with the falling animation. The failure receipt is
`Saved/BowMotion/first-native-failure.json`. The fixture now travels toward the
captured player origin and explicitly requires grounded movement after settling.
No pose threshold was weakened to accept that unrelated falling pose.

## Gates

Implementation snapshot: `7eeb34d6`.

| Gate | Result | Evidence under `Saved/` |
| --- | --- | --- |
| Editor build | PASS, 24.271s / 600s limit | NPCDAZ/bow-motion-editor-final-build-process.json |
| Game build | PASS, 34.038s / 600s limit | NPCDAZ/bow-motion-game-final-build-process.json |
| Blueprint compile | PASS, Bow/Pistol/Rifle up to date | BowMotion/compile.json |
| PIE | PASS, 4/4, 59.629s native / 110s limit | BowMotion/PIE/runtime.txt |
| PIE host cleanup | PASS, 64.812s / 140s limit | BowMotion/pie-host.json |
| Rendered moving poses | PASS for reviewed carry, aim and release frames | BowMotion/PIE/Case_02_motion_02.png; Case_03_motion_04.png; Case_03_motion_07.png |
| Protected files | PASS, 7,643 unchanged | BowMotion/Baseline/after.json |
| Saves | PASS, 4 checked after editor shutdown | BowMotion/SaveGuard/user_save_after.json |
| Editor state before cook | PASS, profiles 0.2/0.8, no PIE or dirty packages | BowMotion/final-editor.json |
| Cook/package creation | PASS, 209.583s host / 900s limit | NPCDAZ/bow-motion-cook-process.json; BodyPresetsQA/Package_20261010_231159/package-result.json |
| Packaged bow matrix | PASS, 4/4; 59.465s native, 76.647s process / 180s limit; zero feature errors | BowPose/Packaged_20261010_231542/process.json |
| Packaged visual review | PASS, moving female aim frame | BowPose/Packaged_20261010_231542/UserData/Saved/BowPose/Runtime/Case_03_motion_04.png |

Native moving measurements: Male.02, 105 samples / 2487.64cm travel, maximum
head/chest 22.31° carry and 31.00° aim; Female.01, 108 samples / 2489.32cm,
22.35° carry and 30.23° aim. Grounding, proportions, directional hand rotation
and native arrow consumption pass. Static NPC male/female checks also pass.

This is representative carry/aim locomotion validation, not exhaustive coverage
of all bow charge montages, crouch/sprint states or extreme morph combinations.

Packaged motion: Male.02 117 samples / 2491.76cm, 21.66° carry / 30.98° aim;
Female.01 118 samples / 2490.85cm, 21.59° carry / 31.02° aim.
Compact receipts and screenshot hashes are tracked in
`Evidence/DAZBowMotion20261010.json`.

Retention dry-run and apply confirmed exactly the previous package's Cooked and
Staged directories, beneath `Saved/BodyPresetsQA/Package_20261010_204511`.
4,989,027,567 bytes were removed; logs, receipts, screenshots, saves and backups
remain. The retained tested package is `Package_20261010_231159`. Evidence:
`Saved/NPCDAZ/bow-motion-retention-{dry,apply}.log` and process receipts.

Publication verification records exact branch/remote HEAD equality and a clean
working tree in `Saved/BowMotion/publication.json`; editor delivery readiness is
recorded in `Saved/NPCDAZ/bow-motion-delivery-ready-process.json`.

Fresh protected baseline: `Saved/BowMotion/Baseline/before.json` (7,643 files).
Fresh user-save backup: `Saved/BowMotion/SaveGuard/user_save_before.json`.

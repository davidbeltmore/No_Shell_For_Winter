# Hygiene: DirtyPawn and existing HUB water

Target: `D:/Projects UE5/NoShellForWinter`; source `D:/Projects UE5/LustAsDeadlySin` untouched.
Base snapshot: `c8d3996996c997d5899f36ebeef36070715ac0ec`. All implementation gates below were measured for this revision.

## Contract and units

All rates are points per calendar hour. Default day 1800 simulation seconds: one hour is 75 seconds. Gameplay uses the existing slow scheduler (one simulation second); no extra tick/timer, mesh traversal, GPU readback, or per-effect timer. Air/Awareness keep their existing acute scheduler. This is a survival-game approximation, not a physiological claim.

DirtyPawn owns a portable, current-opacity surface snapshot. Interval endpoints come from environmental/stain/wash bands and live water contact bands. Heights use DirtyPawn's body reference frame, including its crouch conversion. Approximate cumulative area knots are (height fraction, area fraction): (0,0), (.1,.04), (.5,.40), (.85,.88), (1,1). They weight feet, legs, trunk/arms and head/shoulders. This is height-band coverage, not pixel-perfect painted skin coverage: textures and anatomical masking are not read back.

For each disjoint region j with area weight w:
- c_j = max(mud,sand,snow current opacity).
- k_j = max(11.5*mud,5.25*sand,.5*snow).
- C = sum(w*c_j), K = sum(w*k_j).
- Environmental loss = C*K. A single substance at effective coverage C gives k*C?. Full disjoint half mud/half sand gives 8.375 extra points/hour. Repeated overlapping bands use max, never sum.
- Slow stain loss = sum(w*max(dirt*(1-max(mud,sand)),2*blood,.25*burn)). Smear uses dirt's channel. Dirt beneath mud/sand is suppressed to avoid the automatic companion stain being charged twice. Burn here represents surface residue; injury remains Sensation.
- Continuous loss = clamp(1 + C*K + slow stains + effort,0,12.5).
- Effort alone retains the existing 12-point/day replenishing budget. Environmental exposure cannot exhaust that budget and disappear. Bathroom accidents remain separate discrete losses.

| Sustained full surface | Loss/hour | 100 to 0, game hours | Real minutes at default day |
|---|---:|---:|---:|
| Mud | 12.5 | 8 | 10 |
| Sand | 6.25 | 16 | 20 |
| Snow | 1.5 | 66.667 | 83.333 |
| Dirt | 2 | 50 | 62.5 |
| Blood | 3 | 33.333 | 41.667 |
| Burn residue | 1.25 | 80 | 100 |

Ten-percent effective mud coverage gives 1.115/hour; half coverage gives 3.875/hour. These times require sustained exposure. DirtyPawn's existing 60-second hold and 4-second fade are unchanged. Fading removes future exposure, never restores lost Hygiene. Remaining dirt continues its own slower effect.

## Water and care

The actual HUB `BP_Water_C_2` and `BP_Water_C_4` already inherit project-owned DirtyPawn Water. Each source now stores current contact min/max heights; ending overlap removes that source. Invalid actors are ignored. Overlapping sources are unioned, not added. Mud/bleach contact blocks the same region's clean-water benefit. Wetness after leaving is not water contact.

Clean-water recovery = 30 * contacted area/hour, with natural-water ceiling 85. Net change sums recovery and loss before applying the bound. Fully clean contact recovers 29/hour after baseline: 20 to 85 takes 2.2414 game hours, approximately 168.10 real seconds. Small contact gives small benefit and may not overcome ongoing contamination. Feet-only 4% contact gives net +.2/hour. Partial immersion is an abstract slower rinse of overall Hygiene, not separately persisted limb cleanliness.

Above 85, water never snaps the value down: normal degradation continues until 85. Repeated entry/exit and repeated Wash actions cannot create instant bonuses. Explicit external contamination disables the benefit; the character merely being Dirty does not. The existing full-bath API still reaches 100, while washcloths preserve their bounded consumable effect. The Wash action now recognizes real DirtyPawn Water and acknowledges ongoing automatic washing, rather than granting instant100 through a PhysicsVolume-only check. Full-bath visual washing uses actual reference body height instead of hardcoded 55.

Hygiene remains the same saved scalar; no actor contacts or visual snapshot are serialized into Inner State. DirtyPawn retains its existing own persistence and reconstructs exposure; live water contact is transient. Existing poor-Hygiene stress, wellbeing, madness and contamination/sickness synergies remain; infection exposure now includes area-weighted mud/dirt/blood. UI remains observer-only; the compact glyph is vector soap with bubbles.

## Tuning and validation

Central tuning: `ProjectInnerStateSettings.SurfaceHygiene` / `ProjectHygieneMath.h`; baseline and daily effort budget remain existing settings. `Tools/InnerState/Calibrate58.py --day-seconds 1800` derives times from defaults; run at 3600 to verify calendar scaling. Configuration overrides must be inspected separately.

Focused automation adds mixed coverage, capped load, fade alpha, duplicate bands, wash bounds and time partition checks. The existing global real-gameplay suite adds entry/exit into the existing HUB Water, gradual recovery, no action bonus, and soap-HUD screenshot, within its unchanged 110s watchdog / 120s external bound. Existing simultaneous 200-influence CPU/frame samples and save roundtrip remain.

Evidence directory: `Saved/Migration/Hygiene20261005`. Read-only water inspection: `water_inspection.json`. Protected baseline: `Saved/Migration/InnerState20261005/protected_hygiene_before.json` PASS. Validation ledger:
- Editor build PASS: `Saved/Migration/Hygiene20261005/build_editor.log`, 70.81s, approved Daz receipt repair PASS.
- Game build PASS: `build_game.log`, 72.02s, descriptor restored byte-exact and receipt repair PASS.
- Focused math PASS: five tests, zero warnings/failures, `Saved/Migration/InnerState20261005/Automation_20261005_174853/index.json`.
- Blueprint compile PASS: 26 assets including Player, HUD/care content and actual DirtyPawn Water/Mud/Sand/Snow BPs; `Saved/Migration/Hygiene20261005/blueprint_compile.json`. No protected assets saved.
- Real HUB PIE PASS: 46/46 checks, 87.805s, `Saved/InnerStateQA/20261005_125224/report.json`. Includes real water overlap/exit, wash action anti-bonus, inventory, poison, snapshot roundtrip, rest, knockout and death. `Saved/Migration/Hygiene20261005/pie.log` preserves the session.
- PIE performance: 51 baseline / 50 loaded component samples; p95 .0898?.1743ms with 200 influences; medians .0406?.0453ms. Frame medians 17.936?17.908ms; p95 21.822?23.108ms. This is one bounded sample, not a zero-lag guarantee.
- Visual PASS: reviewed actual `hygiene_water_soap.png` in that PIE directory: compact soap with two bubbles, seven cells, six main rows preserved; Hygiene 22 after water exposure. Editor viewport resolution/layout affects the overall capture, so packaged capture will also be reviewed.
- Calendar scaling PASS: `Saved/Migration/Hygiene20261005/calibration_1800.json` and `calibration_3600.json`; doubling day length doubles real slow-need durations.
- Full HUB cook/package PASS: `Saved/Migration/InnerState20261005/Package_20261005_125417`, BuildCookRun 215.60s, exit 0; cook zero errors and 26 pre-existing warnings. `Saved/Migration/Hygiene20261005/package.log` records the gate.
- Packaged gameplay PASS: 46/46 in 87.556s, `Saved/Migration/InnerState20261005/PackagedQA_20261005_125800/report.json`; strict validator also found no handled ensure/fatal/native inventory errors and exit 0. 51/50 component samples; p95 .1167?.3654ms, median .0372?.0531ms. Frame median 9.578?10.055ms, p95 12.441?14.813ms. 200 influence sources, zero per-influence timers. Rendered 1600x1000 soap-HUD screenshot inspected PASS.
- Final protected hashes PASS, no changed invariants: `Saved/Migration/InnerState20261005/protected_hygiene_delivery.json`. ACFU, Daz, Player, Female, Frederick, Multiple, Male preserved.
- Persistence contract unchanged: no new serialized fields. The existing real component disk roundtrip and progression snapshot checks passed in both runtime suites. No redundant full ALS travel test was added.
- Final code/content snapshot and staged executable hash parity: `Saved/Migration/Hygiene20261005/delivery_snapshot.json`. The unrelated user HUB/chest work is neither committed nor reverted.

Unrelated user HUB/chest/cleanup work is excluded from implementation commits. The map is inspected and used in disposable gameplay, never saved by this task.

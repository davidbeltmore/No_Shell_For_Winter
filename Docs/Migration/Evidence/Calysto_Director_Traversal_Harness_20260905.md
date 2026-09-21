# Bounded Director traversal harness

Status: **IMPLEMENTED; LIVE RUN PENDING**. The Python checks below validate the
harness's receipt rules and forbidden actions. They do not establish native
generation, navigation, gameplay, visual quality, or release acceptance.

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8. The source project remains
read-only. Implementation:

- `Tools/Migration/Validate-CalystoDirectorTraversal58.py`
- `Tools/Migration/Test-CalystoDirectorTraversal58.py`

The harness arms while PIE is stopped and the editor already has HUB open.
Discover and describe the native MCP Python execution and PIE tools after the
current editor launch. Execute this Python in the editor, replacing the output
directory with a fresh unique child of the shown evidence root:

```python
import builtins
builtins.CALYSTO_DIRECTOR_TRAVERSAL_OPTIONS = {
    "output_dir": r"D:/Projects UE5/NoShellForWinter/Saved/Migration/CalystoDungeonDirectorV7/Traversal_unique",
    "mode": "native_parity",
}
exec(open(r"D:/Projects UE5/NoShellForWinter/Tools/Migration/Validate-CalystoDirectorTraversal58.py", encoding="utf-8").read())
```

Then start PIE once through the discovered MCP tool within 15 seconds. Do not
start an additional harness or run. Native parity mode requires the editor to
have been launched with `-CalystoDirectorNativeParity`. Future full mode uses
`"mode": "full"` and rejects that flag. Optional `expected_run_seed` only checks
the actual entrance's seed; it never overrides a run or treats topology seeds
as run seeds.

The sequence is HUB's real `DoorToLevel`, accepted floor 1, that floor's real
owned progression door, accepted floor 2, its progression door, and accepted
floor 3. The harness moves the player into interaction range, requires the
exact door to remain ACF's selected overlapping actor for three samples, and
calls ACF `Interact` once. It never calls start/advance/retry/ready/release APIs
to make a floor pass. All floors must share the run seed and epoch, have
increasing committed identities, distinct requests, and reroll index zero.

`GetDiagnosticsJson()` schema 1 must provide actual retained evidence for the
unique owned Start/End, blocking floor and capsule clearance, complete relevant
navigation route, room/Theme contract, material realization, published entry,
and ordered attempt receipts. Every attempt must have exactly one root
generation request. Every rejected attempt must have verified cleanup before
the sole accepted attempt. Missing fields fail closed. The player must be
within 35 cm of the published entry. Native parity requires the separate
native verification flag and rejects a full gameplay verification claim.

The harness requests one viewport PNG for each accepted floor, checks the
complete PNG container, and records its SHA-256. Visual review remains pending
until the agent inspects those images. After floor 3, it explicitly requests
Return to HUB and observes HUB, then stops only its PIE session. That explicit
test return does not prove the intended final floor-cap return.

Timing uses one monotonic 150-second total deadline, including a five-second
PIE-stop reserve. Per-phase limits are 15 seconds for external PIE start,
eight seconds for interaction, 35 seconds to observe each request, five
seconds for each PNG, and ten seconds for explicit Return to HUB. Each
accepted Director request must itself report at most 30 seconds across all
attempts. Phase caps are bounded observation limits, not promises or additive
extensions to the total deadline. The harness stops at the first terminal
failure; only the Director's finite recovery mechanism may reseed.

The harness preserves existing dirty packages, performs no editor map load or
asset save, and flags newly dirty packages. It leaves the editor open. Receipt
status is `NATIVE_TRAVERSAL_ONLY`, `TRAVERSAL_OBSERVED`, or `FAIL`; no harness
result implies full V7 completion. Process exit and full log audit, protected
hash comparisons, probability tests, image review, final floor-cap return,
cook, packaging, and release acceptance remain separate gates. An unresponsive
editor cannot execute Slate callbacks: the external operator must enforce its
own 150-second observation bound and mark unfinished evidence failed/pending.

Local verification performed on 2026-09-05:

```powershell
python Tools/Migration/Test-CalystoDirectorTraversal58.py
```

Result: six tests passed, process exit 0, 0.004 seconds. Coverage includes
finite verified recovery, missing evidence, duplicate generation, unverified
rollback, invalid identities, repeated floor 1, false gameplay claims, the
shared request deadline, invalid/mismatched entry, and AST checks forbidding
readiness injection, direct run/advance/retry, editor map loads/saves, or
harness-owned PIE starts. No Unreal operation was executed by these tests.

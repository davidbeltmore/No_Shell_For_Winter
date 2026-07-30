# TODO

Last updated: 2026-07-24.

## Migration Closeout

- Replace the retired SFW/NSFW selector and independent opt-ins with one neutral-primary presentation router.
- Gate Intimacy and Fap at Charisma level 10; revalidate Intimacy adult, consent, life, consciousness, non-hostility, combat-state, location, and cancellation rules.
- Revalidate mature defeat as one authoritative 10-percent roll after a real minigame loss, independent of Charisma and Intimacy consent, with a 90-percent direct-respawn path.
- Revalidate Chronicle bark selection at 90 percent neutral and 10 percent protected original, with Streamer Safe forcing neutral output.
- Confirm `-StreamerSafe` suppresses all adult interaction, defeat, bark, preview, debug, and direct-action presentation paths.
- Remove obsolete selector/opt-in config, UI, save, and Blueprint references without deleting retained functional content.
- Complete nonvisual PIE validation for map, door, StartPoint, dungeon generation, cleanup, quest/map-marker behavior, and related runtime flows.
- Complete final build and Blueprint compilation after the single-game routing change.
- Complete final cook, cooked-manifest validation, packaged build, and packaged-runtime validation after the single-game routing change.
- Re-run protected invariant hashes after each migration phase touching ACFU, DazToUnreal, Player, Female, Frederick, Multiple, or Male.

## Current Scope Decisions

- SaveGame migration and legacy-slot compatibility are `OUT_OF_SCOPE_BY_USER`.
- The 660 source packages under `/Game/ExportedAnimations` are `OUT_OF_SCOPE_BY_USER`.
- Visual QA is `USER_OWNED_OUT_OF_SCOPE`.

These items should remain documented as scope decisions rather than promoted to `PASS`.

# Public evidence boundary

NoShellForWinter was migrated to Unreal Engine 5.8 using a legacy private build as read-only behavioral reference.

The public repository intentionally excludes detailed historical payloads that would disclose private repository coordinates, local paths, obsolete internal identifiers, file-level import receipts, or historical asset registries. Those records remain in private engineering storage and are not required to build or run the public project.

Public evidence retains:

- gate result and scope;
- target-owned asset or subsystem identity;
- validation method;
- relevant target logs or screenshots;
- `PASS`, `PENDING`, or external-blocker state;
- protected-target invariant results.

Removing a private payload does not promote its gate. Any validation that has not been rerun against the sanitized Unreal Engine 5.8 tree remains `PENDING`.

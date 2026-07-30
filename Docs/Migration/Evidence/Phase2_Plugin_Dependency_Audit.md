# Project plugin dependency audit

Status: `SANITIZED_PUBLIC_SUMMARY`

The descriptor and source-file receipts from the legacy private build are excluded. The target-owned dependency order is:

```text
EFCharacterCreation
|- EFCharacterCreationDazBridge
|- EFClothingMorph
|- EFCharacterCreationACFUBridge
`- EFLevelFlow

EFProcedural
`- EFLevelFlow

ACFTrainingSystem ---------.
CodeWidgetDesignerBridge --+
DirtyPawnRuntime ----------+
EFCharacterCreation -------+
EFProcedural --------------+
EFLevelFlow ---------------`- EFProjectSystems
```

## Public gate record

| Plugin | UE 5.8 Editor/Game build | Runtime | Cook/package |
|---|---|---|---|
| EFCharacterCreation | `PASS` | `PENDING` | `PENDING` |
| EFCharacterCreationDazBridge | `PASS` | Editor-only probe `PASS` | Not applicable |
| EFClothingMorph | `PASS` | `PENDING` | `PENDING` |
| EFProcedural | `PASS` | `PENDING` | `PENDING` |
| EFLevelFlow | `PASS` | Static load `PASS`; interaction `PENDING` | `PENDING` |
| EFCharacterCreationACFUBridge | `PASS` | `PENDING` | `PENDING` |
| EFProjectSystems | Prior baseline `PASS`; canonical refactor `PENDING` | `PENDING` | `PENDING` |
| ACFTrainingSystem | Prior baseline `PASS`; canonical Curse integration `PENDING` | `PENDING` | `PENDING` |

Marketplace and Engine plugins remain immutable. Project-owned compatibility code must use adapters, interfaces, subclasses, or composition.

See [Public_Evidence_Redaction.md](Public_Evidence_Redaction.md).

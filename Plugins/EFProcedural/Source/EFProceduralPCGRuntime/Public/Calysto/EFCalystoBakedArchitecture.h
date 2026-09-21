#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDirectorProbability.h"

class UPCGDataAsset;

enum class EEFCalystoBakedRotation : uint8 { Preserve, RelativeYaw, WorldYaw };

/** A native baked child. Authored Chance tags are explicit child opportunities, resolved before spawning. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoBakedMesh
{
	FGuid Id;
	FSoftObjectPath Mesh;
	/** Native level-to-asset Material attribute controls slot zero; remaining mesh slots stay authored. */
	FSoftObjectPath Material;
	FTransform LocalTransform = FTransform::Identity;
	FBox ExportedLocalBounds = FBox(ForceInit);
	double ChancePercent = 100;
	EEFCalystoBakedRotation Rotation = EEFCalystoBakedRotation::Preserve;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoBakedDecision
{
	FGuid Id;
	FEFCalystoBakedMesh Child;
	FTransform WorldTransform = FTransform::Identity;
	bool bSelected = false;
};

/** Read-only UE 5.8 native Root/Points decoding. Never executes a graph or loads/spawns a world.
 * Exported bounds are metadata, not collision/placement proof. The placement owner must use the
 * loaded mesh geometry and validate every child before reserving/materializing the selected payload. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoBakedArchitecture final
{
public:
	static constexpr int32 MaximumChildren = 256;
	static bool Decode(const UPCGDataAsset& Payload, TArray<FEFCalystoBakedMesh>& Children,
		TArray<FSoftObjectPath>& Dependencies, FString& Error);
	/** Conservative geometric envelope of every supported child rotation, before parent/child trials.
	 * This is not a surface or collision proof. The placement owner must validate this entire volume. */
	static bool GetEnvelope(const FEFCalystoBakedMesh& Child, const FTransform& Parent,
		FBox& Envelope, FString& Error);
	/** Caller already reserved and proved the whole payload's envelopes before selecting its parent.
	 * Rejects any missing enclosure before the first draw; no selected child is silently dropped. */
	static bool Freeze(const FEFCalystoRandomKey& Random, const FGuid& ParentId, const FTransform& Parent,
		TConstArrayView<FEFCalystoBakedMesh> Children, const FBox& ReservedEnvelope,
		TArray<FEFCalystoBakedDecision>& Decisions, FString& Error);
};

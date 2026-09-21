#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoFloorTransaction.h"
#include "UObject/GCObject.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UWorld;

/** Final child selection from the reserved architecture manifest. Materials bind every real mesh slot.
 * Feasibility and native baked Chance/rotation must already be frozen; this owner never chooses content. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoFrozenArchitectureMesh
{
	FGuid Id;
	FGuid ParentReservationId;
	FSoftObjectPath Mesh;
	TArray<FSoftObjectPath> Materials;
	FTransform WorldTransform=FTransform::Identity;
};

enum class EEFCalystoArchitectureMeshState : uint8 { Idle, Preparing, Prepared, Active, Releasing, Released, Failed };

/** Native ISM realization under the attempt's existing actor. No actor/MID per instance, graph execution,
 * random draw, loading, position search or fallback. The request owner retains this object until release. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoArchitectureMeshBatch final : public FGCObject
{
public:
	static constexpr int32 MaximumInstances=512*256;
	bool Begin(const FEFCalystoAttemptToken& Token,UWorld* World,AActor* AttemptOwner,
		TConstArrayView<FEFCalystoFrozenArchitectureMesh> Frozen,double DeadlineSeconds,FString& Error);
	/** Bounded game-thread preparation; static collision/nav register while the player remains protected. */
	bool Prepare(double NowSeconds,int32 Operations,FString& Error);
	/** Read-only exact native component/instance/material evidence. */
	bool Verify(TSet<FGuid>& VerifiedElements,FString& Error) const;
	/** Called only after coordinator acceptance. A failure keeps remaining components hidden and must
	 * be handled as accepted cleanup; it never authorizes a rejected-attempt retry or player release. */
	bool Activate(const FEFCalystoAttemptToken& Token,FString& Error);
	void Release(const FEFCalystoAttemptToken& Token);
	FEFCalystoRollbackEvidence GetReleaseEvidence() const;
	EEFCalystoArchitectureMeshState GetState() const { return State; }
	int32 GetOwnedComponentCount() const { int32 Count=0; for (const auto& G:Groups) Count+=G.Component!=nullptr; return Count; }
	virtual ~FEFCalystoArchitectureMeshBatch() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FEFCalystoArchitectureMeshBatch"); }
private:
	struct FGroup
	{
		TObjectPtr<UStaticMesh> Mesh=nullptr;
		TArray<TObjectPtr<UMaterialInterface>> Materials;
		TObjectPtr<UInstancedStaticMeshComponent> Component=nullptr;
		int32 InstanceCount=0;
	};
	struct FBinding
	{
		FEFCalystoFrozenArchitectureMesh Frozen;
		int32 Group=INDEX_NONE;
		int32 Instance=INDEX_NONE;
	};
	FEFCalystoAttemptToken Token;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> Owner;
	TArray<FGroup> Groups;
	TArray<FBinding> Bindings;
	EEFCalystoArchitectureMeshState State=EEFCalystoArchitectureMeshState::Idle;
	double Deadline=0,LastNow=-1;
	int32 Next=0;
	bool OwnerValid() const;
	bool Fail(const FString& Message,FString& Error);
};

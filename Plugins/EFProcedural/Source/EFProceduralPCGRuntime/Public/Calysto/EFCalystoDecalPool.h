#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Calysto/EFCalystoFloorTransaction.h"
#include "Calysto/EFCalystoNativeAdapter.h"
#include "EFCalystoDecalPool.generated.h"

class UDecalComponent;
class UPrimitiveComponent;

enum class EEFCalystoDecalSurface : uint8 { Floor, Wall, Roof };

/** One interior witness for a collision face that jointly proves an entire selected decal footprint.
 * This is retained with the reservation so realization rechecks every contributing native surface,
 * not only the centre support. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalCoverageSupport
{
	TWeakObjectPtr<UPrimitiveComponent> Support;
	int32 InstanceIndex = INDEX_NONE;
	FTransform SupportTransform = FTransform::Identity;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
};

/** Selected elsewhere. The pool rechecks the exact native opportunity and physical support;
 * there is no caller-supplied success flag, random selection, replacement or position search. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoReservedDecal
{
	FGuid Id;
	int64 RoomId = 0;
	int64 OpportunityId = 0;
	FGuid ThemeId;
	FGuid VariantId;
	EEFCalystoDecalSurface Surface = EEFCalystoDecalSurface::Floor;
	FTransform WorldTransform = FTransform::Identity;
	FVector Size = FVector(8, 64, 64);
	FVector SurfaceNormal = FVector::UpVector;
	TWeakObjectPtr<UPrimitiveComponent> Support;
	int32 InstanceIndex = INDEX_NONE;
	FTransform SupportTransform = FTransform::Identity;
	TArray<FEFCalystoDecalCoverageSupport> FootprintSupports;
};

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalReleaseEvidence
{
	int32 AllocatedSlots = 0;
	int32 LeasedSlots = 0;
	int32 RetainedResources = 0;
	bool bAllSlotsHiddenAndCleared = false;
	bool bReleased = false;
};

/** One transient owner with 24 reusable components. No per-stain actor, MID, load or timer.
 * Begin stages hidden; explicit transaction commit permits activation. Native capability
 * preflight remains responsible for accepting the material closure and reservation producer. */
UCLASS(Transient, NotBlueprintable)
class EFPROCEDURALPCGRUNTIME_API AEFCalystoDecalPoolOwner final : public AActor
{
	GENERATED_BODY()
public:
	static constexpr int32 PoolCapacity = 24;
	AEFCalystoDecalPoolOwner();
	bool Begin(const FEFCalystoAttemptToken& Token, const FEFCalystoCompiledDirector& Configuration,
		TSharedRef<const FEFCalystoNativeAdapter> Native,
		TConstArrayView<FEFCalystoReservedDecal> Proposals, TConstArrayView<UObject*> LoadedResources, FString& Error);
	bool Verify(const FEFCalystoAttemptToken& Token, FString& Error) const;
	bool ActivateCommitted(const FEFCalystoAttemptToken& Token, FString& Error);
	/** Tick supplies actual local-player cameras. Empty views hide every leased component. */
	bool UpdateViewLocations(const FEFCalystoAttemptToken& Token, TConstArrayView<FVector> Views, FString& Error);
	bool Release(const FEFCalystoAttemptToken& Token);
	FEFCalystoDecalReleaseEvidence GetReleaseEvidence(const FEFCalystoAttemptToken& Token) const;
	const FString& GetProposalHash() const { return ProposalHash; }
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FEFCalystoDecalPoolLifecycleTest;
	friend class FEFCalystoDecalPoolRejectionTest;
	struct FBinding
	{
		FEFCalystoReservedDecal Proposal;
		TWeakObjectPtr<UMaterialInterface> Material;
		double CullDistance = 0;
		float ScreenFade = 0;
		bool bVisible = false;
	};
	bool BeginObserved(const FEFCalystoAttemptToken& Token, const FEFCalystoCompiledDirector& Configuration,
		const FEFCalystoNativeRoomConfig& RoomConfig, AActor* InNativeOwner, UPCGComponent* InControlledPCG,
		const FEFCalystoNativeResult& Native, TConstArrayView<FEFCalystoReservedDecal> Proposals,
		TConstArrayView<UObject*> LoadedResources, FString& Error);
	bool VerifySupport(const FEFCalystoReservedDecal& Proposal, FString& Error) const;
	bool OwnsSupport(UPrimitiveComponent* Component) const;
	void ClearSlots();
	UPROPERTY(Transient) TArray<TObjectPtr<UDecalComponent>> Slots;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> ResourceLeases;
	TArray<FBinding> Bindings;
	TSharedPtr<const FEFCalystoNativeAdapter> NativeLease;
	TWeakObjectPtr<AActor> NativeOwner;
	TWeakObjectPtr<UPCGComponent> ControlledPCG;
	FEFCalystoAttemptToken LeaseToken;
	FEFCalystoAttemptToken ReleasedToken;
	FString ProposalHash;
	FString Failure;
	bool bLeased = false;
	bool bCommitted = false;
};

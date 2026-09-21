#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

#include "EFCalystoDecalPoolV6.generated.h"

class UDecalComponent;
class UMaterialInterface;
struct FEFCalystoResolvedFloorPlanV6;
struct FEFCalystoRoomManifestV6;

UENUM(BlueprintType)
enum class EEFCalystoPCGDecalSurfaceV6 : uint8
{
	Floor,
	Wall,
	Roof
};

/** A pre-resolved, already-loaded decal request. This API never loads a material. */
USTRUCT(BlueprintType)
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPooledDecalRequestV6
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
	FName StableDecalId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
	EEFCalystoPCGDecalSurfaceV6 Surface = EEFCalystoPCGDecalSurfaceV6::Floor;

	/** Must already be resident. Null requests are rejected instead of loaded synchronously. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (ClampMin = "0.1", Units = "cm"))
	FVector DecalSize = FVector(32.0, 64.0, 64.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (ClampMin = "0.0"))
	float FadeStartDelay = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (ClampMin = "0.0"))
	float FadeDuration = 0.0f;

	/** Authored distance contract used to derive renderer-side screen fading without tick or MIDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (ClampMin = "1.0", Units = "cm"))
	float FadeStartDistanceCm = 2500.0f;

	/** Hard visibility target. Must exceed FadeStartDistanceCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Decal", meta = (ClampMin = "1.0", Units = "cm"))
	float CullDistanceCm = 4000.0f;
};

/** Immutable, trace-ready decal intent compiled only from the frozen V6 floor plan and room manifest. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalPlacementCandidateV6
{
	int64 StableRoomId = 0;
	FName StableDecalId = NAME_None;
	FName VariantId = NAME_None;
	EEFCalystoPCGDecalSurfaceV6 Surface = EEFCalystoPCGDecalSurfaceV6::Floor;
	TSoftObjectPtr<UMaterialInterface> Material;
	FVector LocalTraceStart = FVector::ZeroVector;
	FVector LocalTraceEnd = FVector::ZeroVector;
	float SizeCm = 0.0f;
	float RollDegrees = 0.0f;
	float FadeStartDistanceCm = 0.0f;
	float CullDistanceCm = 0.0f;
};

/** Pure deterministic policy compiler. World traces and component acquisition remain in the subsystem. */
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalPlacementMathV6
{
	static bool IsPlacementProtectedRoom(int32 RoomFlags);
	static double UniformLane(int64 FloorSeed, int64 StableRoomId, FName StyleId, FName ThemeId, const TCHAR* Lane);
	static bool BuildCandidates(
		const FEFCalystoResolvedFloorPlanV6& FloorPlan,
		const FEFCalystoRoomManifestV6& Manifest,
		TArray<FEFCalystoDecalPlacementCandidateV6>& OutCandidates,
		FString& OutError);
};

/** Generation-checked handle that cannot release a slot after it has been reused. */
USTRUCT(BlueprintType)
struct EFPROCEDURALPCGRUNTIME_API FEFCalystoPooledDecalHandleV6
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decal")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decal")
	int32 SlotGeneration = 0;

	bool IsValid() const { return SlotIndex != INDEX_NONE && SlotGeneration > 0; }
	void Reset() { SlotIndex = INDEX_NONE; SlotGeneration = 0; }
};

/** Fixed-ceiling, explicit-release decal pool. Neither the pool nor its owner ticks. */
UCLASS(ClassGroup = (Calysto), meta = (BlueprintSpawnableComponent, DisplayName = "Calysto Decal Pool V6"))
class EFPROCEDURALPCGRUNTIME_API UEFCalystoDecalPoolComponentV6 : public USceneComponent
{
	GENERATED_BODY()

public:
	UEFCalystoDecalPoolComponentV6();

	/** Contractual hard ceiling for the entire floor. Active-budget limits remain independent. */
	static constexpr int32 HardMaximumDecals = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calysto V6|Performance", meta = (ClampMin = "0", ClampMax = "24"))
	int32 MaximumDecals = HardMaximumDecals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calysto V6|Rendering", meta = (ClampMin = "0.0"))
	float FadeScreenSize = 0.01f;

	/** One shared sort order preserves renderer batching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calysto V6|Rendering")
	int32 SharedSortOrder = 0;

	/** Rebuilds the inactive pool to the requested hard ceiling; active slots are released first. */
	UFUNCTION(BlueprintCallable, Category = "Calysto V6|Decals")
	bool InitializePool(int32 InMaximumDecals);

	/** Fails when the hard ceiling is reached. It never evicts an active decal. */
	UFUNCTION(BlueprintCallable, Category = "Calysto V6|Decals")
	bool AcquireDecal(const FEFCalystoPooledDecalRequestV6& Request, FEFCalystoPooledDecalHandleV6& OutHandle);

	UFUNCTION(BlueprintCallable, Category = "Calysto V6|Decals")
	bool ReleaseDecal(const FEFCalystoPooledDecalHandleV6& Handle);

	UFUNCTION(BlueprintCallable, Category = "Calysto V6|Decals")
	void ReleaseAllDecals();

	UFUNCTION(BlueprintPure, Category = "Calysto V6|Decals")
	int32 GetActiveDecalCount() const;

	UFUNCTION(BlueprintPure, Category = "Calysto V6|Decals")
	UDecalComponent* GetDecalComponent(const FEFCalystoPooledDecalHandleV6& Handle) const;

protected:
	virtual void OnRegister() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	bool EnsurePoolSize(int32 TargetSize);
	void DeactivateSlot(int32 SlotIndex);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> DecalComponents;

	TArray<uint8> ActiveSlots;
	TArray<int32> SlotGenerations;
	TArray<FName> StableDecalIds;
};

/** Lightweight transient owner for one floor's pooled decals. */
UCLASS(NotPlaceable, NotBlueprintable, Transient)
class EFPROCEDURALPCGRUNTIME_API AEFCalystoDecalPoolOwnerV6 : public AActor
{
	GENERATED_BODY()

public:
	AEFCalystoDecalPoolOwnerV6();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Calysto V6")
	TObjectPtr<UEFCalystoDecalPoolComponentV6> DecalPool;
};

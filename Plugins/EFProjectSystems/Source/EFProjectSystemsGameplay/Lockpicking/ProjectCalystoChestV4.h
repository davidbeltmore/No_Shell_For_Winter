#pragma once

#include "CoreMinimal.h"
#include "Lockpicking/ProjectLockedWorldItem.h"
#include "ProjectCalystoChestV4.generated.h"

class UACFItem;
class USkeletalMesh;
class USkeletalMeshComponent;

/** Immutable content entry selected by the V4 Director before actor materialization. */
USTRUCT()
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoResolvedChestEntryV4
{
	GENERATED_BODY()

	UPROPERTY()
	FName StableAttemptId = NAME_None;

	UPROPERTY()
	FName ContentCatalogId = NAME_None;

	UPROPERTY()
	TSubclassOf<UACFItem> ItemClass;

	UPROPERTY()
	int32 Quantity = 1;
};

/**
 * Project-owned ACF storage bridge for Calysto V4. Geometry and placement stay
 * under Calysto/PCG; only the already-resolved content is injected here.
 */
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API AProjectCalystoChestV4 : public AProjectLockedWorldItem
{
	GENERATED_BODY()

public:
	AProjectCalystoChestV4();
	static FSoftObjectPath GetDefaultVisualMeshPath();

	/** Called exclusively while the actor is deferred, before BeginPlay. */
	bool ConfigureResolvedLoot(
		TConstArrayView<FProjectCalystoResolvedChestEntryV4> Entries,
		FString& OutError);

	/** Seeds ACF storage and returns the exact frozen catalog IDs after verification. */
	bool FinalizeAndVerifyResolvedLoot(TArray<FName>& OutVerifiedContentIds, FString& OutError);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project|Calysto|V4")
	TObjectPtr<USkeletalMeshComponent> ChestVisual;

	/** Loaded only if this concrete chest class was selected for the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project|Calysto|V4",
		meta = (DisplayName = "Chest Appearance", ToolTip = "Project-owned soft reference; does not modify the existing vendor chest."))
	TSoftObjectPtr<USkeletalMesh> ChestVisualMesh;

private:
	bool VerifyFrozenLootStorage(FString& OutError) const;

	UPROPERTY(Transient)
	TArray<FProjectCalystoResolvedChestEntryV4> FrozenResolvedLoot;

	bool bConfiguredByDirector = false;
	bool bFrozenLootSeeded = false;
	bool bVerifiedByDirector = false;
};

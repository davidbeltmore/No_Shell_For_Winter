#include "Lockpicking/ProjectCalystoChest.h"

#include "ACFItemSystemFunctionLibrary.h"
#include "Components/ACFStorageComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Items/ACFItem.h"

AProjectCalystoChest::AProjectCalystoChest()
{
	bUseWorldMeshFromFirstItem = false;
	bDestroyOnGather = false;

	ChestVisual = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ProjectCalystoChestVisual"));
	if (ChestVisual)
	{
		ChestVisual->SetupAttachment(RootComp);
		ChestVisual->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	ChestVisualMesh = TSoftObjectPtr<USkeletalMesh>(GetDefaultVisualMeshPath());

	if (ObjectMesh)
	{
		ObjectMesh->SetVisibility(false, true);
		ObjectMesh->SetHiddenInGame(true, true);
		ObjectMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

FSoftObjectPath AProjectCalystoChest::GetDefaultVisualMeshPath()
{
	return FSoftObjectPath(TEXT("/Game/FullSample/Assets/Infinity_Blade_Assets/Chest.Chest"));
}

bool AProjectCalystoChest::ConfigureResolvedLoot(
	const TConstArrayView<FProjectCalystoResolvedChestEntry> Entries,
	FString& OutError)
{
	OutError.Reset();
	if (HasActorBegunPlay() || bFrozenLootSeeded || bConfiguredByDirector)
	{
		OutError = TEXT("Calysto chest content must be configured exactly once while SpawnActorDeferred is active.");
		return false;
	}
	if (Entries.Num() > 3)
	{
		OutError = TEXT("A Calysto chest cannot contain more than three frozen content attempts.");
		return false;
	}
	// Blueprints migrated from the retired V4 class can retain the inherited
	// native component while their serialized TObjectPtr is null. Recover the
	// exact native default subobject without resaving or depending on V4.
	if (!ChestVisual)
	{
		ChestVisual = Cast<USkeletalMeshComponent>(
			GetDefaultSubobjectByName(TEXT("ProjectCalystoChestVisual")));
	}
	if (!ChestVisual)
	{
		OutError = TEXT("The project-owned Calysto chest visual component is missing.");
		return false;
	}

	USkeletalMesh* ResolvedVisual = ChestVisualMesh.Get();
	if (!ResolvedVisual)
	{
		OutError = FString::Printf(TEXT("The selected Calysto chest visual was not preloaded: %s"),
			*ChestVisualMesh.ToSoftObjectPath().ToString());
		return false;
	}
	ChestVisual->SetSkeletalMeshAsset(ResolvedVisual);

	TSet<FName> AttemptIds;
	for (const FProjectCalystoResolvedChestEntry& Entry : Entries)
	{
		if (Entry.StableAttemptId.IsNone() || Entry.ContentCatalogId.IsNone()
			|| !Entry.ItemClass || Entry.Quantity != 1 || AttemptIds.Contains(Entry.StableAttemptId))
		{
			OutError = TEXT("A Calysto chest content entry has an invalid or duplicate ID, class, or quantity.");
			return false;
		}

		FItemDescriptor Descriptor;
		if (!UACFItemSystemFunctionLibrary::GetItemData(Entry.ItemClass, Descriptor))
		{
			OutError = FString::Printf(
				TEXT("ACF rejected Calysto chest item class %s."), *GetPathNameSafe(*Entry.ItemClass));
			return false;
		}
		AttemptIds.Add(Entry.StableAttemptId);
	}

	if (!HasAuthority() || !StorageComponent || !GetItems().IsEmpty())
	{
		OutError = TEXT("Calysto chest storage must be authoritative and empty during deferred configuration.");
		return false;
	}

	FrozenResolvedLoot.Reset(Entries.Num());
	FrozenResolvedLoot.Append(Entries.GetData(), Entries.Num());
	for (const FProjectCalystoResolvedChestEntry& Entry : FrozenResolvedLoot)
	{
		StorageComponent->AddItem(FBaseItem(Entry.ItemClass, Entry.Quantity));
	}
	if (!VerifyFrozenLootStorage(OutError))
	{
		return false;
	}
	bConfiguredByDirector = true;
	bFrozenLootSeeded = true;
	return true;
}

bool AProjectCalystoChest::VerifyFrozenLootStorage(FString& OutError) const
{
	TMap<UClass*, int32> ExpectedCounts;
	for (const FProjectCalystoResolvedChestEntry& Entry : FrozenResolvedLoot)
	{
		ExpectedCounts.FindOrAdd(*Entry.ItemClass) += Entry.Quantity;
	}

	TMap<UClass*, int32> RealizedCounts;
	for (const FInventoryItem& Item : GetItems())
	{
		if (!Item.ItemClass || Item.Count <= 0)
		{
			OutError = TEXT("ACF storage realized an invalid Calysto chest item.");
			return false;
		}
		RealizedCounts.FindOrAdd(*Item.ItemClass) += Item.Count;
	}
	if (!RealizedCounts.OrderIndependentCompareEqual(ExpectedCounts))
	{
		OutError = TEXT("Realized ACF chest storage does not exactly match the frozen directives.");
		return false;
	}
	return true;
}

bool AProjectCalystoChest::FinalizeAndVerifyResolvedLoot(
	TArray<FName>& OutVerifiedContentIds,
	FString& OutError)
{
	OutVerifiedContentIds.Reset();
	OutError.Reset();
	if (!bConfiguredByDirector || !bFrozenLootSeeded || bVerifiedByDirector
		|| !HasActorBegunPlay())
	{
		OutError = TEXT("Calysto chest finalization requires one deferred seed followed by BeginPlay.");
		return false;
	}
	if (!HasAuthority())
	{
		OutError = TEXT("Calysto chest post-BeginPlay verification requires authority.");
		return false;
	}
	if (!VerifyFrozenLootStorage(OutError))
	{
		return false;
	}
	for (const FProjectCalystoResolvedChestEntry& Entry : FrozenResolvedLoot)
	{
		OutVerifiedContentIds.Add(Entry.ContentCatalogId);
	}
	bVerifiedByDirector = true;
	return true;
}

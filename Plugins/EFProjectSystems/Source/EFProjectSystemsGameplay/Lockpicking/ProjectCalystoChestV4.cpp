#include "Lockpicking/ProjectCalystoChestV4.h"

#include "ACFItemSystemFunctionLibrary.h"
#include "Components/ACFStorageComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Items/ACFItem.h"

AProjectCalystoChestV4::AProjectCalystoChestV4()
{
	bUseWorldMeshFromFirstItem = false;
	bDestroyOnGather = false;

	ChestVisual = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ProjectCalystoChestVisualV4"));
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

FSoftObjectPath AProjectCalystoChestV4::GetDefaultVisualMeshPath()
{
	return FSoftObjectPath(TEXT("/Game/FullSample/Assets/Infinity_Blade_Assets/Chest.Chest"));
}

bool AProjectCalystoChestV4::ConfigureResolvedLoot(
	const TConstArrayView<FProjectCalystoResolvedChestEntryV4> Entries,
	FString& OutError)
{
	OutError.Reset();
	if (HasActorBegunPlay() || bFrozenLootSeeded || bConfiguredByDirector)
	{
		OutError = TEXT("V4 chest content must be configured exactly once while SpawnActorDeferred is active.");
		return false;
	}
	if (Entries.Num() > 3)
	{
		OutError = TEXT("A V4 chest cannot contain more than three frozen content attempts.");
		return false;
	}
	USkeletalMesh* ResolvedVisual = ChestVisualMesh.Get();
	if (!ChestVisual || !ResolvedVisual)
	{
		OutError = FString::Printf(TEXT("The selected V4 chest visual was not preloaded: %s"),
			*ChestVisualMesh.ToSoftObjectPath().ToString());
		return false;
	}
	ChestVisual->SetSkeletalMeshAsset(ResolvedVisual);

	TSet<FName> AttemptIds;
	for (const FProjectCalystoResolvedChestEntryV4& Entry : Entries)
	{
		if (Entry.StableAttemptId.IsNone() || Entry.ContentCatalogId.IsNone()
			|| !Entry.ItemClass || Entry.Quantity != 1 || AttemptIds.Contains(Entry.StableAttemptId))
		{
			OutError = TEXT("A V4 chest content entry has an invalid/duplicate ID, class or quantity.");
			return false;
		}

		FItemDescriptor Descriptor;
		if (!UACFItemSystemFunctionLibrary::GetItemData(Entry.ItemClass, Descriptor))
		{
			OutError = FString::Printf(
				TEXT("ACF rejected V4 chest item class %s."), *GetPathNameSafe(*Entry.ItemClass));
			return false;
		}

		AttemptIds.Add(Entry.StableAttemptId);
	}

	if (!HasAuthority() || !StorageComponent || !GetItems().IsEmpty())
	{
		OutError = TEXT("V4 chest storage must be authoritative and empty during deferred configuration.");
		return false;
	}

	FrozenResolvedLoot.Reset(Entries.Num());
	FrozenResolvedLoot.Append(Entries.GetData(), Entries.Num());
	for (const FProjectCalystoResolvedChestEntryV4& Entry : FrozenResolvedLoot)
	{
		// Use the storage component directly. AACFWorldItem::AddItem would also
		// synchronously replace the hidden ObjectMesh for every content entry.
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

bool AProjectCalystoChestV4::VerifyFrozenLootStorage(FString& OutError) const
{
	TMap<UClass*, int32> ExpectedCounts;
	for (const FProjectCalystoResolvedChestEntryV4& Entry : FrozenResolvedLoot)
	{
		ExpectedCounts.FindOrAdd(*Entry.ItemClass) += Entry.Quantity;
	}

	TMap<UClass*, int32> RealizedCounts;
	for (const FInventoryItem& Item : GetItems())
	{
		if (!Item.ItemClass || Item.Count <= 0)
		{
			OutError = TEXT("ACF storage realized an invalid V4 chest item.");
			return false;
		}
		RealizedCounts.FindOrAdd(*Item.ItemClass) += Item.Count;
	}
	if (!RealizedCounts.OrderIndependentCompareEqual(ExpectedCounts))
	{
		OutError = TEXT("Realized ACF chest storage does not exactly match the frozen V4 directives.");
		return false;
	}
	return true;
}

bool AProjectCalystoChestV4::FinalizeAndVerifyResolvedLoot(
	TArray<FName>& OutVerifiedContentIds,
	FString& OutError)
{
	OutVerifiedContentIds.Reset();
	OutError.Reset();
	if (!bConfiguredByDirector || !bFrozenLootSeeded || bVerifiedByDirector
		|| !HasActorBegunPlay())
	{
		OutError = TEXT("V4 chest finalization requires one deferred seed followed by BeginPlay.");
		return false;
	}

	if (!HasAuthority())
	{
		OutError = TEXT("V4 chest post-BeginPlay verification requires authority.");
		return false;
	}
	if (!VerifyFrozenLootStorage(OutError))
	{
		return false;
	}

	for (const FProjectCalystoResolvedChestEntryV4& Entry : FrozenResolvedLoot)
	{
		OutVerifiedContentIds.Add(Entry.ContentCatalogId);
	}
	bVerifiedByDirector = true;
	return true;
}

#include "Companions/ProjectCompanionRevivalConsumable.h"

#include "Companions/ProjectRunCompanionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "ProjectCompanionRevivalConsumable"

UProjectCompanionRevivalConsumable::UProjectCompanionRevivalConsumable()
{
	ItemInfo.Name = LOCTEXT("WintersRecallName", "Winter's Recall");
	ItemInfo.Description = LOCTEXT(
		"WintersRecallDescription",
		"Recalls one fallen run companion at a safe NavMesh location outside combat.");
	ItemInfo.ItemType = EItemType::Consumable;
	ItemInfo.MaxInventoryStack = 1;
	ItemInfo.ItemWeight = 0.25f;
	ItemInfo.bDroppable = false;
	ItemInfo.bSellable = false;
	ItemInfo.bUpgradable = false;
	bConsumeOnUse = false;

	IconSource = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(
		TEXT("/Game/FullSample/UI/Icons/T_SM_Potion_C_Blu_Icon.T_SM_Potion_C_Blu_Icon")));
	// The class itself is loaded only when selected/owned. Resolving its tiny UI
	// icon here keeps ACF's non-virtual GetThumbnailImage contract functional.
	ItemInfo.ThumbNail = IconSource.LoadSynchronous();
}

FName UProjectCompanionRevivalConsumable::GetStableItemId()
{
	static const FName StableId(TEXT("Item.CompanionRevival.WintersRecall"));
	return StableId;
}

bool UProjectCompanionRevivalConsumable::CanBeUsed_Implementation(const APawn* Pawn) const
{
	FString Error;
	const UGameInstance* GameInstance = Pawn && Pawn->GetWorld() ? Pawn->GetWorld()->GetGameInstance() : nullptr;
	const UProjectRunCompanionSubsystem* Roster = GameInstance
		? GameInstance->GetSubsystem<UProjectRunCompanionSubsystem>()
		: nullptr;
	return Roster && Roster->CanBeginRevival(Pawn, this, Error);
}

void UProjectCompanionRevivalConsumable::OnItemUsed_Implementation(APawn* Target)
{
	UGameInstance* GameInstance = Target && Target->GetWorld() ? Target->GetWorld()->GetGameInstance() : nullptr;
	UProjectRunCompanionSubsystem* Roster = GameInstance
		? GameInstance->GetSubsystem<UProjectRunCompanionSubsystem>()
		: nullptr;
	if (!Roster)
	{
		return;
	}

	FGuid TransactionId;
	FString Error;
	Roster->BeginRevivalSelection(Target, this, TransactionId, Error);
}

#undef LOCTEXT_NAMESPACE

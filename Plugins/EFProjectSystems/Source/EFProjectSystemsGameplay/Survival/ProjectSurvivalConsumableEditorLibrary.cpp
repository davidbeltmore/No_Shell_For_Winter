#include "Survival/ProjectSurvivalConsumableEditorLibrary.h"

#include "Components/ACFStorageComponent.h"
#include "Items/ACFItem.h"
#include "Items/ACFWorldItem.h"
#include "Survival/ProjectSurvivalConsumableBlueprintLibrary.h"
#include "Survival/ProjectSurvivalLog.h"
#include "Survival/ProjectSurvivalStatusCatalog.h"
#include "Survival/ProjectSurvivalStatusTypes.h"

#if WITH_EDITOR
#include "Containers/ScriptArray.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "GameFramework/Pawn.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UnrealType.h"
#endif

namespace
{
#if WITH_EDITOR
	const FName OnItemUsedFunctionName(TEXT("OnItemUsed"));
	const FName StorageItemsPropertyName(TEXT("Items"));

	void ClearGraphNodes(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return;
		}

		Graph->Modify();

		TArray<UEdGraphNode*> NodesToRemove = Graph->Nodes;
		for (UEdGraphNode* Node : NodesToRemove)
		{
			if (Node)
			{
				Node->DestroyNode();
			}
		}
	}

	bool BuildOnItemUsedGraph(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return false;
		}

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (!EventGraph)
		{
			FKismetEditorUtilities::CreateDefaultEventGraphs(Blueprint);
			EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		}

		if (!EventGraph)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Missing EventGraph on %s"), *GetNameSafe(Blueprint));
			return false;
		}

		ClearGraphNodes(EventGraph);

		UClass* ParentClass = Blueprint->ParentClass;
		if (!ParentClass && Blueprint->GeneratedClass)
		{
			ParentClass = Blueprint->GeneratedClass->GetSuperClass();
		}

		if (!ParentClass)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Missing parent class on %s"), *GetNameSafe(Blueprint));
			return false;
		}

		UFunction* OnItemUsedFunction = ParentClass->FindFunctionByName(OnItemUsedFunctionName);
		if (!OnItemUsedFunction)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Could not find OnItemUsed on %s"), *GetNameSafe(ParentClass));
			return false;
		}

		UFunction* GetItemOwnerFunction = ParentClass->FindFunctionByName(TEXT("GetItemOwner"));
		if (!GetItemOwnerFunction)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Could not find GetItemOwner on %s"), *GetNameSafe(ParentClass));
			return false;
		}

		int32 EventNodePosY = 0;
		UK2Node_Event* EventNode = FKismetEditorUtilities::AddDefaultEventNode(Blueprint, EventGraph, OnItemUsedFunctionName, ParentClass, EventNodePosY);
		if (!EventNode)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Failed to create OnItemUsed event node on %s"), *GetNameSafe(Blueprint));
			return false;
		}
		EventNode->NodePosX = 0;
		EventNode->NodePosY = 0;

		UFunction* ApplyFunction = UProjectSurvivalConsumableBlueprintLibrary::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UProjectSurvivalConsumableBlueprintLibrary, ApplySurvivalConsumableFromSource));
		if (!ApplyFunction)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Could not resolve ApplySurvivalConsumableProfile"));
			return false;
		}

		UK2Node_CallFunction* ApplyNode = NewObject<UK2Node_CallFunction>(EventGraph);
		EventGraph->AddNode(ApplyNode, true, false);
		ApplyNode->CreateNewGuid();
		ApplyNode->PostPlacedNewNode();
		ApplyNode->SetFromFunction(ApplyFunction);
		ApplyNode->AllocateDefaultPins();
		ApplyNode->NodePosX = 320;
		ApplyNode->NodePosY = 0;

		UK2Node_CallFunction* GetOwnerNode = NewObject<UK2Node_CallFunction>(EventGraph);
		EventGraph->AddNode(GetOwnerNode, true, false);
		GetOwnerNode->CreateNewGuid();
		GetOwnerNode->PostPlacedNewNode();
		GetOwnerNode->SetFromFunction(GetItemOwnerFunction);
		GetOwnerNode->AllocateDefaultPins();
		GetOwnerNode->NodePosX = 320;
		GetOwnerNode->NodePosY = 180;

		UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(EventGraph);
		EventGraph->AddNode(SelfNode, true, false);
		SelfNode->CreateNewGuid();
		SelfNode->PostPlacedNewNode();
		SelfNode->AllocateDefaultPins();
		SelfNode->NodePosX = 40;
		SelfNode->NodePosY = 180;

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(EventGraph);
		EventGraph->AddNode(CastNode, true, false);
		CastNode->CreateNewGuid();
		CastNode->PostPlacedNewNode();
		CastNode->TargetType = APawn::StaticClass();
		CastNode->SetPurity(true);
		CastNode->AllocateDefaultPins();
		CastNode->NodePosX = 620;
		CastNode->NodePosY = 180;

		UEdGraphPin* EventExecPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* CallExecPin = ApplyNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		UEdGraphPin* CallConsumerPin = ApplyNode->FindPin(TEXT("Consumer"));
		UEdGraphPin* CallSourceAssetPin = ApplyNode->FindPin(TEXT("SourceAsset"));
		UEdGraphPin* SelfPin = SelfNode->FindPin(UEdGraphSchema_K2::PN_Self);
		UEdGraphPin* GetOwnerSelfPin = GetOwnerNode->FindPin(UEdGraphSchema_K2::PN_Self);
		UEdGraphPin* GetOwnerReturnPin = GetOwnerNode->GetReturnValuePin();
		UEdGraphPin* CastSourcePin = CastNode->GetCastSourcePin();
		UEdGraphPin* CastResultPin = CastNode->GetCastResultPin();

		const bool bConnectedExec = EventExecPin && CallExecPin && K2Schema->TryCreateConnection(EventExecPin, CallExecPin);
		const bool bConnectedGetOwnerSelf = SelfPin && GetOwnerSelfPin && K2Schema->TryCreateConnection(SelfPin, GetOwnerSelfPin);
		const bool bConnectedCastSource = GetOwnerReturnPin && CastSourcePin && K2Schema->TryCreateConnection(GetOwnerReturnPin, CastSourcePin);
		const bool bConnectedConsumer = CastResultPin && CallConsumerPin && K2Schema->TryCreateConnection(CastResultPin, CallConsumerPin);
		const bool bConnectedSource = SelfPin && CallSourceAssetPin && K2Schema->TryCreateConnection(SelfPin, CallSourceAssetPin);

		if (!bConnectedExec || !bConnectedGetOwnerSelf || !bConnectedCastSource || !bConnectedConsumer || !bConnectedSource)
		{
			if (!bConnectedConsumer || !bConnectedGetOwnerSelf || !bConnectedCastSource)
			{
				for (UEdGraphPin* Pin : GetOwnerNode->Pins)
				{
					if (!Pin)
					{
						continue;
					}

					const UClass* PinClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
					UE_LOG(
						LogProjectSurvival,
						Log,
						TEXT("[ProjectSurvivalConsumableEditor] GetItemOwner pin Name=%s Direction=%d Category=%s Class=%s"),
						*Pin->PinName.ToString(),
						static_cast<int32>(Pin->Direction),
						*Pin->PinType.PinCategory.ToString(),
						*GetNameSafe(PinClass));
				}

				if (GetOwnerReturnPin)
				{
					const UClass* ReturnClass = Cast<UClass>(GetOwnerReturnPin->PinType.PinSubCategoryObject.Get());
					UE_LOG(
						LogProjectSurvival,
						Log,
						TEXT("[ProjectSurvivalConsumableEditor] GetItemOwner return pin Name=%s Category=%s Class=%s"),
						*GetOwnerReturnPin->PinName.ToString(),
						*GetOwnerReturnPin->PinType.PinCategory.ToString(),
						*GetNameSafe(ReturnClass));
				}
			}

			UE_LOG(
				LogProjectSurvival,
				Warning,
				TEXT("[ProjectSurvivalConsumableEditor] Failed to connect OnItemUsed graph on %s Exec=%s GetOwnerSelf=%s CastSource=%s Consumer=%s Source=%s"),
				*GetNameSafe(Blueprint),
				bConnectedExec ? TEXT("true") : TEXT("false"),
				bConnectedGetOwnerSelf ? TEXT("true") : TEXT("false"),
				bConnectedCastSource ? TEXT("true") : TEXT("false"),
				bConnectedConsumer ? TEXT("true") : TEXT("false"),
				bConnectedSource ? TEXT("true") : TEXT("false"));
			return false;
		}

		return true;
	}

	bool ConfigureWorldItemPickupDefaults(
		UBlueprint* Blueprint,
		const TSubclassOf<UACFItem>& ItemClass,
		UStaticMesh* WorldMesh,
		const int32 ItemCount,
		const float MeshScale)
	{
		if (!Blueprint || !ItemClass || !WorldMesh)
		{
			return false;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UClass* GeneratedClass = Blueprint->GeneratedClass;
		AACFWorldItem* WorldItemCDO = GeneratedClass ? Cast<AACFWorldItem>(GeneratedClass->GetDefaultObject()) : nullptr;
		if (!WorldItemCDO)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Pickup blueprint %s is not an AACFWorldItem"), *GetNameSafe(Blueprint));
			return false;
		}

		UACFStorageComponent* StorageComponent = WorldItemCDO->FindComponentByClass<UACFStorageComponent>();
		if (!StorageComponent)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Pickup blueprint %s has no storage component"), *GetNameSafe(Blueprint));
			return false;
		}

		UStaticMeshComponent* ObjectMeshComponent = WorldItemCDO->FindComponentByClass<UStaticMeshComponent>();
		if (!ObjectMeshComponent)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Pickup blueprint %s has no object mesh component"), *GetNameSafe(Blueprint));
			return false;
		}

		FArrayProperty* ItemsProperty = FindFProperty<FArrayProperty>(UACFStorageComponent::StaticClass(), StorageItemsPropertyName);
		FStructProperty* InnerStructProperty = ItemsProperty ? CastField<FStructProperty>(ItemsProperty->Inner) : nullptr;
		if (!ItemsProperty || !InnerStructProperty || InnerStructProperty->Struct != FBaseItem::StaticStruct())
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Could not resolve storage items property on %s"), *GetNameSafe(StorageComponent));
			return false;
		}

		const int32 EffectiveItemCount = FMath::Max(ItemCount, 1);
		const FBaseItem PickupItem(ItemClass, EffectiveItemCount);

		Blueprint->Modify();
		WorldItemCDO->Modify();
		StorageComponent->Modify();
		ObjectMeshComponent->Modify();

		void* ItemsArrayAddress = ItemsProperty->ContainerPtrToValuePtr<void>(StorageComponent);
		FScriptArrayHelper ItemsArrayHelper(ItemsProperty, ItemsArrayAddress);
		ItemsArrayHelper.EmptyValues();
		const int32 NewEntryIndex = ItemsArrayHelper.AddValue();
		if (NewEntryIndex == INDEX_NONE)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Failed to allocate pickup storage entry on %s"), *GetNameSafe(Blueprint));
			return false;
		}

		FBaseItem* EntryValue = reinterpret_cast<FBaseItem*>(ItemsArrayHelper.GetRawPtr(NewEntryIndex));
		if (!EntryValue)
		{
			UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] Failed to resolve pickup storage entry on %s"), *GetNameSafe(Blueprint));
			return false;
		}

		*EntryValue = PickupItem;
		WorldItemCDO->SetItemMesh(PickupItem);
		ObjectMeshComponent->SetStaticMesh(WorldMesh);
		ObjectMeshComponent->SetRelativeScale3D(FVector(FMath::Max(MeshScale, KINDA_SMALL_NUMBER)));
		Blueprint->MarkPackageDirty();

		return true;
	}
#endif
}

bool UProjectSurvivalConsumableEditorLibrary::ConfigureBlueprintAsSurvivalConsumable(UBlueprint* Blueprint)
{
#if !WITH_EDITOR
	UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] ConfigureBlueprintAsSurvivalConsumable is editor-only"));
	return false;
#else
	if (!Blueprint)
	{
		return false;
	}

	if (!BuildOnItemUsedGraph(Blueprint))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();

	UE_LOG(LogProjectSurvival, Log, TEXT("[ProjectSurvivalConsumableEditor] Configured survival consumable blueprint %s"), *GetNameSafe(Blueprint));
	return true;
#endif
}

bool UProjectSurvivalConsumableEditorLibrary::ConfigureBlueprintAsWorldItemPickup(
	UBlueprint* Blueprint,
	TSubclassOf<UACFItem> ItemClass,
	UStaticMesh* WorldMesh,
	const int32 ItemCount,
	const float MeshScale)
{
#if !WITH_EDITOR
	UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] ConfigureBlueprintAsWorldItemPickup is editor-only"));
	return false;
#else
	if (!Blueprint || !ItemClass || !WorldMesh)
	{
		return false;
	}

	if (!ConfigureWorldItemPickupDefaults(Blueprint, ItemClass, WorldMesh, ItemCount, MeshScale))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();

	UE_LOG(
		LogProjectSurvival,
		Log,
		TEXT("[ProjectSurvivalConsumableEditor] Configured pickup blueprint %s ItemClass=%s WorldMesh=%s Count=%d MeshScale=%.3f"),
		*GetNameSafe(Blueprint),
		*GetNameSafe(ItemClass.Get()),
		*GetPathNameSafe(WorldMesh),
		FMath::Max(ItemCount, 1),
		MeshScale);
	return true;
#endif
}

bool UProjectSurvivalConsumableEditorLibrary::EnsureNutritionAlcoholStatusRows(UDataTable* StatusTable)
{
#if !WITH_EDITOR
	UE_LOG(LogProjectSurvival, Warning, TEXT("[ProjectSurvivalConsumableEditor] EnsureNutritionAlcoholStatusRows is editor-only"));
	return false;
#else
	if (!StatusTable || StatusTable->GetRowStruct() != FProjectSurvivalStatusTableRow::StaticStruct())
	{
		return false;
	}

	auto MakeRow = [](const FProjectSurvivalStatusDefinition& Definition)
	{
		FProjectSurvivalStatusTableRow Row;
		Row.StatusName = Definition.StatusName;
		Row.DisplayName = Definition.DisplayName;
		Row.Description = Definition.Description;
		Row.SourceNeedName = Definition.SourceNeedName;
		Row.SourceEntryName = Definition.SourceEntryName;
		Row.SourceType = Definition.SourceType;
		Row.ThresholdMode = Definition.ThresholdMode;
		Row.ActivationThresholdNormalized = Definition.ActivationThresholdNormalized;
		Row.DeactivationThresholdNormalized = Definition.DeactivationThresholdNormalized;
		Row.IconTextureAsset = Definition.IconTextureAsset;
		Row.MinimalIconName = Definition.MinimalIconName;
		Row.DamagePerSecond = Definition.DamagePerSecond;
		Row.DurationSeconds = Definition.DurationSeconds;
		Row.bBlocksHealthRecovery = Definition.bBlocksHealthRecovery;
		Row.bTriggerAtNeedEmpty = Definition.bTriggerAtNeedEmpty;
		Row.bTriggersExhaustionSequence = Definition.bTriggersExhaustionSequence;
		Row.bInvertMovementInput = Definition.bInvertMovementInput;
		Row.MovementInputScale = Definition.MovementInputScale;
		Row.ReapplyPolicy = Definition.ReapplyPolicy;
		Row.AttributeModifiers = Definition.AttributeModifiers;
		Row.NeedDecayModifiers = Definition.NeedDecayModifiers;
		Row.SensationModifiers = Definition.SensationModifiers;
		Row.Tint = Definition.Tint;
		Row.HudPriority = Definition.HudPriority;
		Row.HudSlotSize = Definition.HudSlotSize;
		Row.HudIconSize = Definition.HudIconSize;
		Row.HudIconSlotOffset = Definition.HudIconSlotOffset;
		Row.HudSlotOffset = Definition.HudSlotOffset;
		Row.HudNameFontAsset = Definition.HudNameFontAsset;
		Row.HudDescriptionFontAsset = Definition.HudDescriptionFontAsset;
		Row.HudMetaFontAsset = Definition.HudMetaFontAsset;
		Row.HudNameFontSize = Definition.HudNameFontSize;
		Row.HudDescriptionFontSize = Definition.HudDescriptionFontSize;
		Row.HudMetaFontSize = Definition.HudMetaFontSize;
		Row.HudNameTextColor = Definition.HudNameTextColor;
		Row.HudDescriptionTextColor = Definition.HudDescriptionTextColor;
		Row.HudMetaTextColor = Definition.HudMetaTextColor;
		Row.HudNameTextOffset = Definition.HudNameTextOffset;
		Row.HudDescriptionTextOffset = Definition.HudDescriptionTextOffset;
		Row.HudDurationTextOffset = Definition.HudDurationTextOffset;
		Row.HudDamageTextOffset = Definition.HudDamageTextOffset;
		return Row;
	};

	const FProjectSurvivalStatusCatalog& Catalog = GetProjectSurvivalStatusCatalog();
	for (const FName StatusName : { FName(TEXT("WellFed")), FName(TEXT("Alcoholized")) })
	{
		const FProjectSurvivalStatusDefinition* Definition = Catalog.StatusDefinitions.FindByPredicate(
			[StatusName](const FProjectSurvivalStatusDefinition& Candidate)
			{
				return Candidate.StatusName == StatusName;
			});
		if (!Definition)
		{
			return false;
		}
		StatusTable->AddRow(StatusName, MakeRow(*Definition));
	}

	StatusTable->MarkPackageDirty();
	return true;
#endif
}

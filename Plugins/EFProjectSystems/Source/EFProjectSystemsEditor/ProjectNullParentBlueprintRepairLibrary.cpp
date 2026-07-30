#include "ProjectNullParentBlueprintRepairLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lockpicking/ProjectLockedWorldItem.h"
#include "InnerDoctrine/ProjectInnerDoctrineAltar.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectNullParentBlueprintRepair, Log, All);

namespace ProjectNullParentBlueprintRepairPrivate
{
	const TArray<FName>& AltarRequiredEvents()
	{
		static const TArray<FName> Events = {
			FName(TEXT("OnInteractedByPawn")),
		};
		return Events;
	}

	const TArray<FName>& LockedRequiredEvents()
	{
		static const TArray<FName> Events = {
			FName(TEXT("OnInteractedByPawn")),
			FName(TEXT("OnLocalInteractedByPawn")),
		};
		return Events;
	}

	FString GuidToken(const FGuid& Guid)
	{
		return Guid.IsValid()
			? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
			: FString(TEXT("invalid-guid"));
	}

	void GatherOverrideEvents(UBlueprint* Blueprint, TArray<UK2Node_Event*>& OutEvents)
	{
		OutEvents.Reset();
		if (!Blueprint)
		{
			return;
		}

		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, OutEvents);
		OutEvents.RemoveAll([](const UK2Node_Event* EventNode)
		{
			return !EventNode || !EventNode->bOverrideFunction;
		});
		OutEvents.Sort([](const UK2Node_Event& Left, const UK2Node_Event& Right)
		{
			const FString LeftKey = Left.EventReference.GetMemberName().ToString() + GuidToken(Left.NodeGuid);
			const FString RightKey = Right.EventReference.GetMemberName().ToString() + GuidToken(Right.NodeGuid);
			return LeftKey < RightKey;
		});
	}

	TSet<FString> CaptureEventLinkTokens(UBlueprint* Blueprint)
	{
		TSet<FString> Tokens;
		TArray<UK2Node_Event*> Events;
		GatherOverrideEvents(Blueprint, Events);
		for (const UK2Node_Event* EventNode : Events)
		{
			const FString EventGuid = GuidToken(EventNode->NodeGuid);
			for (const UEdGraphPin* EventPin : EventNode->Pins)
			{
				if (!EventPin)
				{
					continue;
				}

				for (const UEdGraphPin* LinkedPin : EventPin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}

					Tokens.Add(FString::Printf(
						TEXT("%s|%s|%s|%s"),
						*EventGuid,
						*EventPin->PinName.ToString(),
						*GuidToken(LinkedPin->GetOwningNode()->NodeGuid),
						*LinkedPin->PinName.ToString()));
				}
			}
		}
		return Tokens;
	}

	bool SetsEqual(const TSet<FString>& Left, const TSet<FString>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (const FString& Value : Left)
		{
			if (!Right.Contains(Value))
			{
				return false;
			}
		}
		return true;
	}

	bool ParentCanProvideReference(UClass* TargetParentClass, UClass* ReferenceParentClass)
	{
		if (!TargetParentClass || !ReferenceParentClass)
		{
			return false;
		}

		return TargetParentClass->IsChildOf(ReferenceParentClass)
			|| (ReferenceParentClass->HasAnyClassFlags(CLASS_Interface)
				&& TargetParentClass->ImplementsInterface(ReferenceParentClass));
	}

	bool RepairLockedLegacyGraphContracts(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return false;
		}

		const FName PickupSoundName(TEXT("PickupSound"));
		const FName StorageComponentName(TEXT("StorageComponent"));
		const FName CanPawnGatherItemsName(TEXT("CanPawnGatherItems"));
		int32 ReboundPickupNodes = 0;
		int32 ReboundStorageNodes = 0;
		int32 ReconnectedTargets = 0;

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			TArray<UK2Node_VariableGet*> StorageGetters;
			TArray<UK2Node_CallFunction*> GatherCalls;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
				{
					const FName MemberName = VariableNode->VariableReference.GetMemberName();
					bool bLooksLikePickupSound = MemberName == PickupSoundName
						|| VariableNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(
							TEXT("Pickup Sound"),
							ESearchCase::IgnoreCase);
					if (!bLooksLikePickupSound)
					{
						bLooksLikePickupSound = VariableNode->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
						{
							return Pin && Pin->PinName.ToString().Contains(
								TEXT("PickupSound"),
								ESearchCase::IgnoreCase);
						});
					}

					if (bLooksLikePickupSound)
					{
						VariableNode->Modify();
						VariableNode->VariableReference.SetSelfMember(PickupSoundName);
						VariableNode->ReconstructNode();
						++ReboundPickupNodes;
					}
					else if (MemberName == StorageComponentName)
					{
						VariableNode->Modify();
						VariableNode->VariableReference.SetSelfMember(StorageComponentName);
						VariableNode->ReconstructNode();
						if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(VariableNode))
						{
							StorageGetters.Add(VariableGet);
						}
						++ReboundStorageNodes;
					}
				}
				else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
				{
					if (CallNode->FunctionReference.GetMemberName() == CanPawnGatherItemsName)
					{
						GatherCalls.Add(CallNode);
					}
				}
			}

			for (UK2Node_CallFunction* GatherCall : GatherCalls)
			{
				UEdGraphPin* TargetPin = GatherCall ? GatherCall->FindPin(UEdGraphSchema_K2::PN_Self) : nullptr;
				if (!TargetPin || TargetPin->LinkedTo.Num() > 0)
				{
					continue;
				}

				bool bConnected = false;
				for (UK2Node_VariableGet* StorageGetter : StorageGetters)
				{
					UEdGraphPin* StoragePin = StorageGetter ? StorageGetter->GetValuePin() : nullptr;
					if (StoragePin && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(StoragePin, TargetPin))
					{
						bConnected = true;
						++ReconnectedTargets;
						break;
					}
				}

				if (!bConnected)
				{
					UE_LOG(
						LogProjectNullParentBlueprintRepair,
						Error,
						TEXT("Could not restore StorageComponent target for CanPawnGatherItems on %s"),
						*GetNameSafe(Blueprint));
					return false;
				}
			}
		}

		if (ReboundPickupNodes == 0 || ReboundStorageNodes == 0)
		{
			UE_LOG(
				LogProjectNullParentBlueprintRepair,
				Error,
				TEXT("Expected legacy graph contracts were absent on %s (pickup=%d storage=%d)"),
				*GetNameSafe(Blueprint),
				ReboundPickupNodes,
				ReboundStorageNodes);
			return false;
		}

		UE_LOG(
			LogProjectNullParentBlueprintRepair,
			Log,
			TEXT("Restored Locked legacy graph contracts: pickup=%d storage=%d targets=%d"),
			ReboundPickupNodes,
			ReboundStorageNodes,
			ReconnectedTargets);
		return true;
	}

	bool ValidateRequiredEvents(
		UBlueprint* Blueprint,
		UClass* TargetParentClass,
		const TArray<FName>& RequiredEvents,
		const bool bLogFailures)
	{
		if (!Blueprint || !TargetParentClass || Blueprint->ParentClass != TargetParentClass)
		{
			if (bLogFailures)
			{
				UE_LOG(LogProjectNullParentBlueprintRepair, Warning, TEXT("Blueprint or exact parent is invalid"));
			}
			return false;
		}

		TArray<UK2Node_Event*> Events;
		GatherOverrideEvents(Blueprint, Events);
		for (const FName RequiredEvent : RequiredEvents)
		{
			const bool bFound = Events.ContainsByPredicate([RequiredEvent](const UK2Node_Event* EventNode)
			{
				return EventNode && EventNode->EventReference.GetMemberName() == RequiredEvent;
			});
			if (!bFound)
			{
				if (bLogFailures)
				{
					UE_LOG(
						LogProjectNullParentBlueprintRepair,
						Warning,
						TEXT("Required event %s is absent on %s"),
						*RequiredEvent.ToString(),
						*GetNameSafe(Blueprint));
				}
				return false;
			}
		}

		for (UK2Node_Event* EventNode : Events)
		{
			const FName EventName = EventNode->EventReference.GetMemberName();
			UFunction* TargetFunction = TargetParentClass->FindFunctionByName(EventName);
			UFunction* ResolvedFunction = EventNode->FindEventSignatureFunction();
			UClass* ContextClass = Blueprint->GeneratedClass
				? Blueprint->GeneratedClass.Get()
				: TargetParentClass;
			UClass* ReferenceParentClass = EventNode->EventReference.GetMemberParentClass(ContextClass);
			const bool bValid = TargetFunction
				&& ResolvedFunction
				&& TargetFunction->IsSignatureCompatibleWith(ResolvedFunction)
				&& ParentCanProvideReference(TargetParentClass, ReferenceParentClass);
			if (!bValid)
			{
				if (bLogFailures)
				{
					UE_LOG(
						LogProjectNullParentBlueprintRepair,
						Warning,
						TEXT("Override event %s remains incompatible on %s"),
						*EventName.ToString(),
						*GetNameSafe(Blueprint));
				}
				return false;
			}
		}

		return true;
	}

	bool RepairNullParentBlueprint(
		UBlueprint* Blueprint,
		UClass* TargetParentClass,
		const TArray<FName>& RequiredEvents)
	{
		if (!Blueprint || !TargetParentClass)
		{
			return false;
		}

		if (Blueprint->ParentClass && Blueprint->ParentClass != TargetParentClass)
		{
			UE_LOG(
				LogProjectNullParentBlueprintRepair,
				Warning,
				TEXT("Refusing to replace non-null foreign parent %s on %s"),
				*GetNameSafe(Blueprint->ParentClass),
				*GetNameSafe(Blueprint));
			return false;
		}

		if (Blueprint->ParentClass == TargetParentClass)
		{
			return ValidateRequiredEvents(Blueprint, TargetParentClass, RequiredEvents, true);
		}

		TArray<UK2Node_Event*> Events;
		GatherOverrideEvents(Blueprint, Events);
		for (const FName RequiredEvent : RequiredEvents)
		{
			if (!Events.ContainsByPredicate([RequiredEvent](const UK2Node_Event* EventNode)
			{
				return EventNode && EventNode->EventReference.GetMemberName() == RequiredEvent;
			}))
			{
				UE_LOG(
					LogProjectNullParentBlueprintRepair,
					Warning,
					TEXT("Refusing repair because required event %s is absent on %s"),
					*RequiredEvent.ToString(),
					*GetNameSafe(Blueprint));
				return false;
			}
		}

		TMap<UK2Node_Event*, UFunction*> TargetFunctions;
		for (UK2Node_Event* EventNode : Events)
		{
			const FName EventName = EventNode->EventReference.GetMemberName();
			UFunction* TargetFunction = TargetParentClass->FindFunctionByName(EventName);
			if (!TargetFunction || !UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(TargetFunction))
			{
				UE_LOG(
					LogProjectNullParentBlueprintRepair,
					Warning,
					TEXT("Target parent %s cannot provide override event %s required by %s"),
					*GetNameSafe(TargetParentClass),
					*EventName.ToString(),
					*GetNameSafe(Blueprint));
				return false;
			}
			TargetFunctions.Add(EventNode, TargetFunction);
		}

		const TSet<FString> LinksBefore = CaptureEventLinkTokens(Blueprint);
		Blueprint->Modify();
		Blueprint->ParentClass = TargetParentClass;
		if (Blueprint->SimpleConstructionScript)
		{
			Blueprint->SimpleConstructionScript->Modify();
			Blueprint->SimpleConstructionScript->ValidateSceneRootNodes();
		}

		int32 RefreshedEventCount = 0;
		for (UK2Node_Event* EventNode : Events)
		{
			UFunction* TargetFunction = TargetFunctions.FindRef(EventNode);
			UFunction* ExistingFunction = EventNode->FindEventSignatureFunction();
			UClass* ContextClass = Blueprint->GeneratedClass
				? Blueprint->GeneratedClass.Get()
				: TargetParentClass;
			UClass* ReferenceParentClass = EventNode->EventReference.GetMemberParentClass(ContextClass);
			const bool bCompatible = ExistingFunction
				&& TargetFunction->IsSignatureCompatibleWith(ExistingFunction)
				&& ParentCanProvideReference(TargetParentClass, ReferenceParentClass);
			if (bCompatible)
			{
				continue;
			}

			EventNode->Modify();
			if (UEdGraph* Graph = EventNode->GetGraph())
			{
				Graph->Modify();
			}
			EventNode->EventReference.SetExternalMember(
				EventNode->EventReference.GetMemberName(),
				TargetFunction->GetOwnerClass());
			EventNode->bOverrideFunction = true;
			EventNode->ReconstructNode();
			++RefreshedEventCount;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		if (TargetParentClass == AProjectLockedWorldItem::StaticClass()
			&& !RepairLockedLegacyGraphContracts(Blueprint))
		{
			return false;
		}
		const TSet<FString> LinksAfter = CaptureEventLinkTokens(Blueprint);
		if (!SetsEqual(LinksBefore, LinksAfter))
		{
			UE_LOG(
				LogProjectNullParentBlueprintRepair,
				Error,
				TEXT("Event links changed during null-parent repair of %s; refusing save"),
				*GetNameSafe(Blueprint));
			return false;
		}

		if (!ValidateRequiredEvents(Blueprint, TargetParentClass, RequiredEvents, true))
		{
			return false;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (!Blueprint->IsUpToDate())
		{
			UE_LOG(
				LogProjectNullParentBlueprintRepair,
				Error,
				TEXT("Blueprint %s did not compile after null-parent repair"),
				*GetNameSafe(Blueprint));
			return false;
		}

		Blueprint->MarkPackageDirty();
		UE_LOG(
			LogProjectNullParentBlueprintRepair,
			Log,
			TEXT("Repaired %s with parent %s; refreshed_events=%d preserved_event_links=%d"),
			*GetNameSafe(Blueprint),
			*GetNameSafe(TargetParentClass),
			RefreshedEventCount,
			LinksAfter.Num());
		return true;
	}
}

bool UProjectNullParentBlueprintRepairLibrary::RepairInnerDoctrineAltarNullParent(UBlueprint* Blueprint)
{
	return ProjectNullParentBlueprintRepairPrivate::RepairNullParentBlueprint(
		Blueprint,
		AProjectInnerDoctrineAltar::StaticClass(),
		ProjectNullParentBlueprintRepairPrivate::AltarRequiredEvents());
}

bool UProjectNullParentBlueprintRepairLibrary::RepairLockedWorldItemNullParent(UBlueprint* Blueprint)
{
	return ProjectNullParentBlueprintRepairPrivate::RepairNullParentBlueprint(
		Blueprint,
		AProjectLockedWorldItem::StaticClass(),
		ProjectNullParentBlueprintRepairPrivate::LockedRequiredEvents());
}

bool UProjectNullParentBlueprintRepairLibrary::ValidateInnerDoctrineAltarRepair(UBlueprint* Blueprint)
{
	return ProjectNullParentBlueprintRepairPrivate::ValidateRequiredEvents(
		Blueprint,
		AProjectInnerDoctrineAltar::StaticClass(),
		ProjectNullParentBlueprintRepairPrivate::AltarRequiredEvents(),
		true);
}

bool UProjectNullParentBlueprintRepairLibrary::ValidateLockedWorldItemRepair(UBlueprint* Blueprint)
{
	return ProjectNullParentBlueprintRepairPrivate::ValidateRequiredEvents(
		Blueprint,
		AProjectLockedWorldItem::StaticClass(),
		ProjectNullParentBlueprintRepairPrivate::LockedRequiredEvents(),
		true);
}

TArray<FString> UProjectNullParentBlueprintRepairLibrary::DescribeOverrideEventLinks(UBlueprint* Blueprint)
{
	TArray<FString> Descriptions;
	TArray<UK2Node_Event*> Events;
	ProjectNullParentBlueprintRepairPrivate::GatherOverrideEvents(Blueprint, Events);
	for (const UK2Node_Event* EventNode : Events)
	{
		TArray<FString> Links;
		for (const UEdGraphPin* EventPin : EventNode->Pins)
		{
			if (!EventPin)
			{
				continue;
			}

			for (const UEdGraphPin* LinkedPin : EventPin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode())
				{
					continue;
				}

				Links.Add(FString::Printf(
					TEXT("%s>%s.%s"),
					*EventPin->PinName.ToString(),
					*ProjectNullParentBlueprintRepairPrivate::GuidToken(LinkedPin->GetOwningNode()->NodeGuid),
					*LinkedPin->PinName.ToString()));
			}
		}
		Links.Sort();
		Descriptions.Add(FString::Printf(
			TEXT("event=%s|node=%s|links=%s"),
			*EventNode->EventReference.GetMemberName().ToString(),
			*ProjectNullParentBlueprintRepairPrivate::GuidToken(EventNode->NodeGuid),
			*FString::Join(Links, TEXT(","))));
	}
	Descriptions.Sort();
	return Descriptions;
}

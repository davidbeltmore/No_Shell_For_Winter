#include "Lockpicking/ProjectLockpickingEditorLibrary.h"

#include "Lockpicking/ProjectLockpickingBlueprintLibrary.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogProjectLockpickingEditor, Log, All);

namespace
{
#if WITH_EDITOR
	UEdGraphPin* FindPinCaseInsensitive(UEdGraphNode* Node, const FName PinName)
	{
		if (!Node)
		{
			return nullptr;
		}

		if (UEdGraphPin* Pin = Node->FindPin(PinName))
		{
			return Pin;
		}

		const FString PinNameString = PinName.ToString();
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinName.ToString().Equals(PinNameString, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UK2Node_Event* FindOrCreateEventNode(UBlueprint* Blueprint, UEdGraph* EventGraph, UClass* ParentClass, const FName EventFunctionName, const int32 NodePosY)
	{
		for (UEdGraphNode* GraphNode : EventGraph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(GraphNode))
			{
				if (EventNode->EventReference.GetMemberName() == EventFunctionName)
				{
					return EventNode;
				}
			}
		}

		int32 InOutNodePosY = NodePosY;
		UK2Node_Event* EventNode = FKismetEditorUtilities::AddDefaultEventNode(
			Blueprint,
			EventGraph,
			EventFunctionName,
			ParentClass,
			InOutNodePosY);
		if (EventNode)
		{
			EventNode->NodePosX = 0;
			EventNode->NodePosY = NodePosY;
		}

		return EventNode;
	}

	UK2Node_CallFunction* FindCallNode(UEdGraph* EventGraph, UFunction* Function)
	{
		for (UEdGraphNode* GraphNode : EventGraph->Nodes)
		{
			if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(GraphNode))
			{
				if (CallNode->GetTargetFunction() == Function)
				{
					return CallNode;
				}
			}
		}

		return nullptr;
	}

	UK2Node_CallFunction* CreateCallNode(UEdGraph* EventGraph, UFunction* Function, const int32 NodePosX, const int32 NodePosY)
	{
		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(EventGraph);
		EventGraph->AddNode(CallNode, true, false);
		CallNode->CreateNewGuid();
		CallNode->PostPlacedNewNode();
		CallNode->SetFromFunction(Function);
		CallNode->AllocateDefaultPins();
		CallNode->NodePosX = NodePosX;
		CallNode->NodePosY = NodePosY;
		return CallNode;
	}

	UK2Node_CallParentFunction* CreateCallParentNode(UEdGraph* EventGraph, UFunction* ParentFunction, const int32 NodePosX, const int32 NodePosY)
	{
		UK2Node_CallParentFunction* CallParentNode = NewObject<UK2Node_CallParentFunction>(EventGraph);
		EventGraph->AddNode(CallParentNode, true, false);
		CallParentNode->CreateNewGuid();
		CallParentNode->PostPlacedNewNode();
		CallParentNode->SetFromFunction(ParentFunction);
		CallParentNode->AllocateDefaultPins();
		CallParentNode->NodePosX = NodePosX;
		CallParentNode->NodePosY = NodePosY;
		return CallParentNode;
	}

	UK2Node_IfThenElse* CreateBranchNode(UEdGraph* EventGraph, const int32 NodePosX, const int32 NodePosY)
	{
		UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(EventGraph);
		EventGraph->AddNode(BranchNode, true, false);
		BranchNode->CreateNewGuid();
		BranchNode->PostPlacedNewNode();
		BranchNode->AllocateDefaultPins();
		BranchNode->NodePosX = NodePosX;
		BranchNode->NodePosY = NodePosY;
		return BranchNode;
	}

	UK2Node_Self* CreateSelfNode(UEdGraph* EventGraph, const int32 NodePosX, const int32 NodePosY)
	{
		UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(EventGraph);
		EventGraph->AddNode(SelfNode, true, false);
		SelfNode->CreateNewGuid();
		SelfNode->PostPlacedNewNode();
		SelfNode->AllocateDefaultPins();
		SelfNode->NodePosX = NodePosX;
		SelfNode->NodePosY = NodePosY;
		return SelfNode;
	}

	bool WireParentInputs(
		const UEdGraphSchema_K2* K2Schema,
		UK2Node_Event* EventNode,
		UK2Node_CallParentFunction* CallParentNode)
	{
		if (!K2Schema || !EventNode || !CallParentNode)
		{
			return false;
		}

		bool bSuccess = true;
		for (UEdGraphPin* EventPin : EventNode->Pins)
		{
			if (!EventPin || EventPin->Direction != EGPD_Output || EventPin->PinName == UEdGraphSchema_K2::PN_Then)
			{
				continue;
			}

			if (UEdGraphPin* ParentPin = FindPinCaseInsensitive(CallParentNode, EventPin->PinName))
			{
				bSuccess &= EventPin->LinkedTo.Contains(ParentPin) || K2Schema->TryCreateConnection(EventPin, ParentPin);
			}
		}

		return bSuccess;
	}

	bool AddComponentGateToACFEvent(
		UBlueprint* Blueprint,
		UClass* ParentClass,
		UEdGraph* EventGraph,
		const FName EventFunctionName,
		UFunction* ConsumeFunction,
		const int32 NodePosY)
	{
		if (!Blueprint || !ParentClass || !EventGraph || !ConsumeFunction)
		{
			return false;
		}

		UFunction* ParentFunction = ParentClass->FindFunctionByName(EventFunctionName);
		if (!ParentFunction)
		{
			UE_LOG(
				LogProjectLockpickingEditor,
				Warning,
				TEXT("[LockpickingEditor] Could not find %s on parent %s"),
				*EventFunctionName.ToString(),
				*GetNameSafe(ParentClass));
			return false;
		}

		UK2Node_Event* EventNode = FindOrCreateEventNode(Blueprint, EventGraph, ParentClass, EventFunctionName, NodePosY);
		if (!EventNode)
		{
			UE_LOG(
				LogProjectLockpickingEditor,
				Warning,
				TEXT("[LockpickingEditor] Failed to create %s event node on %s"),
				*EventFunctionName.ToString(),
				*GetNameSafe(Blueprint));
			return false;
		}

		if (FindCallNode(EventGraph, ConsumeFunction))
		{
			UE_LOG(
				LogProjectLockpickingEditor,
				Log,
				TEXT("[LockpickingEditor] %s already has lockpicking hook for %s"),
				*GetNameSafe(Blueprint),
				*EventFunctionName.ToString());
			return true;
		}

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		UEdGraphPin* EventExecPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
		if (!EventExecPin)
		{
			return false;
		}

		TArray<UEdGraphPin*> OriginalExecLinks = EventExecPin->LinkedTo;
		EventExecPin->BreakAllPinLinks();

		UK2Node_CallFunction* ConsumeNode = CreateCallNode(EventGraph, ConsumeFunction, 340, NodePosY);
		UK2Node_IfThenElse* BranchNode = CreateBranchNode(EventGraph, 640, NodePosY);
		UK2Node_Self* SelfNode = CreateSelfNode(EventGraph, 120, NodePosY + 180);

		UEdGraphPin* ConsumeExecPin = ConsumeNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		UEdGraphPin* ConsumeThenPin = ConsumeNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* ConsumeActorPin = FindPinCaseInsensitive(ConsumeNode, FName(TEXT("Actor")));
		UEdGraphPin* ConsumePawnPin = FindPinCaseInsensitive(ConsumeNode, FName(TEXT("Pawn")));
		UEdGraphPin* ConsumeInteractionTypePin = FindPinCaseInsensitive(ConsumeNode, FName(TEXT("InteractionType")));
		UEdGraphPin* ConsumeReturnPin = ConsumeNode->GetReturnValuePin();
		UEdGraphPin* SelfPin = SelfNode->FindPin(UEdGraphSchema_K2::PN_Self);
		UEdGraphPin* EventPawnPin = FindPinCaseInsensitive(EventNode, FName(TEXT("Pawn")));
		UEdGraphPin* EventInteractionTypePin = FindPinCaseInsensitive(EventNode, FName(TEXT("interactionType")));
		UEdGraphPin* BranchExecPin = BranchNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		UEdGraphPin* BranchConditionPin = BranchNode->GetConditionPin();
		UEdGraphPin* BranchFalsePin = BranchNode->GetElsePin();

		const bool bConnectedGate =
			EventExecPin && ConsumeExecPin && K2Schema->TryCreateConnection(EventExecPin, ConsumeExecPin)
			&& ConsumeThenPin && BranchExecPin && K2Schema->TryCreateConnection(ConsumeThenPin, BranchExecPin)
			&& ConsumeReturnPin && BranchConditionPin && K2Schema->TryCreateConnection(ConsumeReturnPin, BranchConditionPin)
			&& SelfPin && ConsumeActorPin && K2Schema->TryCreateConnection(SelfPin, ConsumeActorPin)
			&& EventPawnPin && ConsumePawnPin && K2Schema->TryCreateConnection(EventPawnPin, ConsumePawnPin);

		const bool bConnectedInteractionType = !ConsumeInteractionTypePin
			|| !EventInteractionTypePin
			|| K2Schema->TryCreateConnection(EventInteractionTypePin, ConsumeInteractionTypePin);

		if (!bConnectedGate || !bConnectedInteractionType)
		{
			UE_LOG(
				LogProjectLockpickingEditor,
				Warning,
				TEXT("[LockpickingEditor] Failed to connect lockpicking gate on %s for %s"),
				*GetNameSafe(Blueprint),
				*EventFunctionName.ToString());
			return false;
		}

		if (OriginalExecLinks.Num() == 0)
		{
			UK2Node_CallParentFunction* CallParentNode = CreateCallParentNode(EventGraph, ParentFunction, 940, NodePosY);
			UEdGraphPin* ParentExecPin = CallParentNode->FindPin(UEdGraphSchema_K2::PN_Execute);
			if (!BranchFalsePin || !ParentExecPin || !K2Schema->TryCreateConnection(BranchFalsePin, ParentExecPin))
			{
				return false;
			}

			return WireParentInputs(K2Schema, EventNode, CallParentNode);
		}

		for (UEdGraphPin* OriginalPin : OriginalExecLinks)
		{
			if (OriginalPin)
			{
				K2Schema->TryCreateConnection(BranchFalsePin, OriginalPin);
			}
		}

		return true;
	}
#endif
}

bool UProjectLockpickingEditorLibrary::ConfigureBlueprintForComponentLockpicking(UBlueprint* Blueprint)
{
#if !WITH_EDITOR
	UE_LOG(LogProjectLockpickingEditor, Warning, TEXT("[LockpickingEditor] ConfigureBlueprintForComponentLockpicking is editor-only"));
	return false;
#else
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

	UClass* ParentClass = Blueprint->ParentClass;
	if (!ParentClass && Blueprint->GeneratedClass)
	{
		ParentClass = Blueprint->GeneratedClass->GetSuperClass();
	}

	if (!EventGraph || !ParentClass)
	{
		return false;
	}

	UFunction* ServerConsumeFunction = UProjectLockpickingBlueprintLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UProjectLockpickingBlueprintLibrary, ConsumeActorACFInteractionIfLocked));
	UFunction* LocalConsumeFunction = UProjectLockpickingBlueprintLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UProjectLockpickingBlueprintLibrary, ConsumeActorACFLocalInteractionIfLocked));

	const bool bServerHooked = AddComponentGateToACFEvent(
		Blueprint,
		ParentClass,
		EventGraph,
		FName(TEXT("OnInteractedByPawn")),
		ServerConsumeFunction,
		0);
	const bool bLocalHooked = AddComponentGateToACFEvent(
		Blueprint,
		ParentClass,
		EventGraph,
		FName(TEXT("OnLocalInteractedByPawn")),
		LocalConsumeFunction,
		360);

	if (!bServerHooked || !bLocalHooked)
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();
	return true;
#endif
}

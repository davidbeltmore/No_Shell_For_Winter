#include "ProjectBlueprintMigrationLibrary.h"

#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/SpringArmComponent.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectBlueprintMigration, Log, All);

namespace
{
struct FProjectBlueprintVariableContract
{
	FBPVariableDescription Description;
	bool bHasCompiledDefault = false;
	FString CompiledDefault;
};

struct FProjectBlueprintGraphContract
{
	TWeakObjectPtr<UEdGraph> Graph;
	FName Name;
	int32 NodeCount = 0;
};

struct FProjectBlueprintSCSContract
{
	TWeakObjectPtr<USCS_Node> Node;
	FName VariableName;
	FGuid VariableGuid;
	TWeakObjectPtr<UClass> ComponentClass;
	TWeakObjectPtr<UActorComponent> ComponentTemplate;
	FName AttachToName;
	FName ParentComponentOrVariableName;
	FName ParentComponentOwnerClassName;
	bool bIsParentComponentNative = false;
	TArray<TWeakObjectPtr<USCS_Node>> Children;
};

static bool ExportLocalBlueprintDefault(
	const UClass* GeneratedClass,
	const UObject* DefaultObject,
	const FName VariableName,
	FString& OutValue)
{
	if (!IsValid(GeneratedClass) || !IsValid(DefaultObject))
	{
		return false;
	}

	const FProperty* Property = FindFProperty<FProperty>(GeneratedClass, VariableName);
	if (!Property || Property->GetOwnerClass() != GeneratedClass)
	{
		return false;
	}

	OutValue.Reset();
	Property->ExportText_InContainer(
		0,
		OutValue,
		DefaultObject,
		DefaultObject,
		const_cast<UObject*>(DefaultObject),
		PPF_Copy);
	return true;
}

static bool AreVariableDescriptionsIdentical(
	const FBPVariableDescription& Before,
	const FBPVariableDescription& After)
{
	if (Before.VarName != After.VarName
		|| Before.VarGuid != After.VarGuid
		|| Before.VarType != After.VarType
		|| Before.FriendlyName != After.FriendlyName
		|| !Before.Category.EqualTo(After.Category)
		|| Before.PropertyFlags != After.PropertyFlags
		|| Before.RepNotifyFunc != After.RepNotifyFunc
		|| Before.ReplicationCondition != After.ReplicationCondition
		|| Before.DefaultValue != After.DefaultValue
		|| Before.MetaDataArray.Num() != After.MetaDataArray.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Before.MetaDataArray.Num(); ++Index)
	{
		if (Before.MetaDataArray[Index].DataKey != After.MetaDataArray[Index].DataKey
			|| Before.MetaDataArray[Index].DataValue != After.MetaDataArray[Index].DataValue)
		{
			return false;
		}
	}

	return true;
}

struct FProjectBlueprintContractSnapshot
{
	TWeakObjectPtr<UClass> ParentClass;
	TArray<FProjectBlueprintVariableContract> Variables;
	TArray<FProjectBlueprintGraphContract> Graphs;
	TArray<FProjectBlueprintSCSContract> SCSNodes;
	TArray<TObjectPtr<UEdGraph>> UbergraphPages;
	TArray<TObjectPtr<UEdGraph>> FunctionGraphs;
	TArray<TObjectPtr<UEdGraph>> DelegateSignatureGraphs;
	TArray<TObjectPtr<UEdGraph>> MacroGraphs;
	TArray<TObjectPtr<UActorComponent>> ComponentTemplates;
	TArray<TObjectPtr<UTimelineTemplate>> Timelines;
	TArray<FBPInterfaceDescription> ImplementedInterfaces;

	explicit FProjectBlueprintContractSnapshot(UBlueprint* Blueprint)
	{
		ParentClass = Blueprint->ParentClass;
		UbergraphPages = Blueprint->UbergraphPages;
		FunctionGraphs = Blueprint->FunctionGraphs;
		DelegateSignatureGraphs = Blueprint->DelegateSignatureGraphs;
		MacroGraphs = Blueprint->MacroGraphs;
		ComponentTemplates = Blueprint->ComponentTemplates;
		Timelines = Blueprint->Timelines;
		ImplementedInterfaces = Blueprint->ImplementedInterfaces;

		const UClass* GeneratedClass = Blueprint->GeneratedClass;
		const UObject* DefaultObject = IsValid(GeneratedClass)
			? GeneratedClass->GetDefaultObject(false)
			: nullptr;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			FProjectBlueprintVariableContract& Contract = Variables.AddDefaulted_GetRef();
			Contract.Description = Variable;
			Contract.bHasCompiledDefault = ExportLocalBlueprintDefault(
				GeneratedClass,
				DefaultObject,
				Variable.VarName,
				Contract.CompiledDefault);
		}

		auto CaptureGraphs = [this](const TArray<TObjectPtr<UEdGraph>>& SourceGraphs)
		{
			for (UEdGraph* Graph : SourceGraphs)
			{
				if (!Graph || Graphs.ContainsByPredicate(
					[Graph](const FProjectBlueprintGraphContract& Existing)
					{
						return Existing.Graph.Get() == Graph;
					}))
				{
					continue;
				}

				FProjectBlueprintGraphContract& Contract = Graphs.AddDefaulted_GetRef();
				Contract.Graph = Graph;
				Contract.Name = Graph->GetFName();
				Contract.NodeCount = Graph->Nodes.Num();
			}
		};
		CaptureGraphs(Blueprint->UbergraphPages);
		CaptureGraphs(Blueprint->FunctionGraphs);
		CaptureGraphs(Blueprint->DelegateSignatureGraphs);
		CaptureGraphs(Blueprint->MacroGraphs);
		for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
		{
			CaptureGraphs(Interface.Graphs);
		}

		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}

				FProjectBlueprintSCSContract& Contract = SCSNodes.AddDefaulted_GetRef();
				Contract.Node = Node;
				Contract.VariableName = Node->GetVariableName();
				Contract.VariableGuid = Node->VariableGuid;
				Contract.ComponentClass = Node->ComponentClass;
				Contract.ComponentTemplate = Node->ComponentTemplate;
				Contract.AttachToName = Node->AttachToName;
				Contract.ParentComponentOrVariableName = Node->ParentComponentOrVariableName;
				Contract.ParentComponentOwnerClassName = Node->ParentComponentOwnerClassName;
				Contract.bIsParentComponentNative = Node->bIsParentComponentNative;
				for (USCS_Node* Child : Node->GetChildNodes())
				{
					Contract.Children.Add(Child);
				}
			}
		}
	}

	void RestoreSerializedContract(UBlueprint* Blueprint, USCS_Node* AddedCameraBoomNode) const
	{
		// This is a best-effort in-memory rollback. The helper never saves; callers
		// must treat a false return as a hard stop so the on-disk package remains the
		// authoritative full snapshot.
		Blueprint->ParentClass = ParentClass.Get();
		Blueprint->NewVariables.Reset(Variables.Num());
		for (const FProjectBlueprintVariableContract& Contract : Variables)
		{
			Blueprint->NewVariables.Add(Contract.Description);
		}
		Blueprint->UbergraphPages = UbergraphPages;
		Blueprint->FunctionGraphs = FunctionGraphs;
		Blueprint->DelegateSignatureGraphs = DelegateSignatureGraphs;
		Blueprint->MacroGraphs = MacroGraphs;
		Blueprint->ComponentTemplates = ComponentTemplates;
		Blueprint->Timelines = Timelines;
		Blueprint->ImplementedInterfaces = ImplementedInterfaces;

		if (AddedCameraBoomNode && Blueprint->SimpleConstructionScript)
		{
			Blueprint->SimpleConstructionScript->RemoveNode(AddedCameraBoomNode, false);
		}
	}

	bool ValidatePreservedContract(const UBlueprint* Blueprint, FString& OutFailure) const
	{
		if (Blueprint->NewVariables.Num() != Variables.Num())
		{
			OutFailure = FString::Printf(
				TEXT("NewVariables count changed from %d to %d"),
				Variables.Num(),
				Blueprint->NewVariables.Num());
			return false;
		}

		const UClass* GeneratedClass = Blueprint->GeneratedClass;
		const UObject* DefaultObject = IsValid(GeneratedClass)
			? GeneratedClass->GetDefaultObject(false)
			: nullptr;
		for (int32 Index = 0; Index < Variables.Num(); ++Index)
		{
			const FProjectBlueprintVariableContract& Before = Variables[Index];
			const FBPVariableDescription& After = Blueprint->NewVariables[Index];
			if (!AreVariableDescriptionsIdentical(Before.Description, After))
			{
				OutFailure = FString::Printf(
					TEXT("Blueprint variable contract changed at index %d (%s)"),
					Index,
					*Before.Description.VarName.ToString());
				return false;
			}

			const FProperty* GeneratedProperty = IsValid(GeneratedClass)
				? FindFProperty<FProperty>(GeneratedClass, Before.Description.VarName)
				: nullptr;
			if (!GeneratedProperty || GeneratedProperty->GetOwnerClass() != GeneratedClass)
			{
				OutFailure = FString::Printf(
					TEXT("Blueprint variable %s was not emitted on the generated class"),
					*Before.Description.VarName.ToString());
				return false;
			}

			if (Before.bHasCompiledDefault)
			{
				FString CompiledDefaultAfter;
				if (!ExportLocalBlueprintDefault(
					GeneratedClass,
					DefaultObject,
					Before.Description.VarName,
					CompiledDefaultAfter)
					|| CompiledDefaultAfter != Before.CompiledDefault)
				{
					OutFailure = FString::Printf(
						TEXT("Compiled default changed for Blueprint variable %s"),
						*Before.Description.VarName.ToString());
					return false;
				}
			}
		}

		for (const FProjectBlueprintGraphContract& Contract : Graphs)
		{
			const UEdGraph* Graph = Contract.Graph.Get();
			if (!Graph || Graph->GetFName() != Contract.Name || Graph->Nodes.Num() != Contract.NodeCount)
			{
				OutFailure = FString::Printf(
					TEXT("Persistent graph %s was removed, renamed, or changed node count"),
					*Contract.Name.ToString());
				return false;
			}
		}

		for (const FBPInterfaceDescription& Before : ImplementedInterfaces)
		{
			const FBPInterfaceDescription* After = Blueprint->ImplementedInterfaces.FindByPredicate(
				[&Before](const FBPInterfaceDescription& Candidate)
				{
					return Candidate.Interface == Before.Interface;
				});
			if (!After)
			{
				OutFailure = FString::Printf(
					TEXT("Implemented interface %s was removed"),
					*GetNameSafe(Before.Interface.Get()));
				return false;
			}

			for (UEdGraph* Graph : Before.Graphs)
			{
				if (!After->Graphs.Contains(Graph))
				{
					OutFailure = FString::Printf(
						TEXT("Interface graph %s was removed"),
						*GetNameSafe(Graph));
					return false;
				}
			}
		}

		const TArray<USCS_Node*> CurrentSCSNodes = Blueprint->SimpleConstructionScript
			? Blueprint->SimpleConstructionScript->GetAllNodes()
			: TArray<USCS_Node*>();
		for (const FProjectBlueprintSCSContract& Contract : SCSNodes)
		{
			USCS_Node* Node = Contract.Node.Get();
			if (!Node
				|| !CurrentSCSNodes.Contains(Node)
				|| Node->GetVariableName() != Contract.VariableName
				|| Node->VariableGuid != Contract.VariableGuid
				|| Node->ComponentClass != Contract.ComponentClass.Get()
				|| Node->ComponentTemplate != Contract.ComponentTemplate.Get()
				|| Node->AttachToName != Contract.AttachToName
				|| Node->ParentComponentOrVariableName != Contract.ParentComponentOrVariableName
				|| Node->ParentComponentOwnerClassName != Contract.ParentComponentOwnerClassName
				|| Node->bIsParentComponentNative != Contract.bIsParentComponentNative
				|| Node->GetChildNodes().Num() != Contract.Children.Num())
			{
				OutFailure = FString::Printf(
					TEXT("SCS component contract changed for %s"),
					*Contract.VariableName.ToString());
				return false;
			}

			for (int32 ChildIndex = 0; ChildIndex < Contract.Children.Num(); ++ChildIndex)
			{
				if (Node->GetChildNodes()[ChildIndex] != Contract.Children[ChildIndex].Get())
				{
					OutFailure = FString::Printf(
						TEXT("SCS child order changed for %s"),
						*Contract.VariableName.ToString());
					return false;
				}
			}
		}

		if (Blueprint->ComponentTemplates != ComponentTemplates
			|| Blueprint->Timelines != Timelines)
		{
			OutFailure = TEXT("Blueprint component templates or timelines changed");
			return false;
		}

		return true;
	}
};
} // namespace

bool UProjectBlueprintMigrationLibrary::RepairTattooShopCharacterBlueprint(
	UBlueprint* Blueprint,
	UClass* TargetParentClass,
	UClass* CharacterCustomizationInterface)
{
	if (!IsValid(Blueprint)
		|| !IsValid(TargetParentClass)
		|| !IsValid(CharacterCustomizationInterface)
		|| !CharacterCustomizationInterface->HasAnyClassFlags(CLASS_Interface))
	{
		return false;
	}

	if (Blueprint->ParentClass && Blueprint->ParentClass != TargetParentClass)
	{
		UE_LOG(
			LogProjectBlueprintMigration,
			Error,
			TEXT("Refusing TattooShop repair because %s has foreign parent %s instead of %s."),
			*GetNameSafe(Blueprint),
			*GetNameSafe(Blueprint->ParentClass),
			*GetNameSafe(TargetParentClass));
		return false;
	}

	const FProjectBlueprintContractSnapshot Snapshot(Blueprint);
	USCS_Node* AddedCameraBoomNode = nullptr;
	auto FailWithoutSaving =
		[Blueprint, &Snapshot, &AddedCameraBoomNode](const FString& Reason)
		{
			Snapshot.RestoreSerializedContract(Blueprint, AddedCameraBoomNode);
			UE_LOG(
				LogProjectBlueprintMigration,
				Error,
				TEXT("TattooShop BP_TSChar repair aborted without saving: %s"),
				*Reason);
			return false;
		};

	Blueprint->Modify();
	Blueprint->ParentClass = TargetParentClass;

	const FTopLevelAssetPath InterfacePath = CharacterCustomizationInterface->GetClassPathName();
	const bool bAlreadyImplementsInterface = Blueprint->ImplementedInterfaces.ContainsByPredicate(
		[CharacterCustomizationInterface](const FBPInterfaceDescription& Description)
		{
			return Description.Interface == CharacterCustomizationInterface;
		});
	const bool bParentImplementsInterface = TargetParentClass->ImplementsInterface(
		CharacterCustomizationInterface);
	if (!bAlreadyImplementsInterface
		&& !bParentImplementsInterface
		&& !FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfacePath))
	{
		return FailWithoutSaving(FString::Printf(
			TEXT("Failed to restore interface %s on %s"),
			*CharacterCustomizationInterface->GetPathName(),
			*GetNameSafe(Blueprint)));
	}

	// The UE 5.7 parent exposed CameraBoom. The authoritative UE 5.8 Player no
	// longer does, so restore that inherited contract locally on BP_TSChar.
	if (Blueprint->SimpleConstructionScript
		&& !Blueprint->SimpleConstructionScript->FindSCSNode(TEXT("CameraBoom")))
	{
		Blueprint->SimpleConstructionScript->Modify();
		AddedCameraBoomNode = Blueprint->SimpleConstructionScript->CreateNode(
			USpringArmComponent::StaticClass(),
			TEXT("CameraBoom"));
		if (!AddedCameraBoomNode)
		{
			return FailWithoutSaving(TEXT("Failed to create the local CameraBoom SCS node"));
		}
		Blueprint->SimpleConstructionScript->AddNode(AddedCameraBoomNode);
	}

	TArray<UK2Node_Variable*> VariableNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, VariableNodes);
	for (UK2Node_Variable* VariableNode : VariableNodes)
	{
		if (VariableNode
			&& VariableNode->VariableReference.GetMemberName() == FName(TEXT("CameraBoom")))
		{
			VariableNode->Modify();
			VariableNode->VariableReference.SetSelfMember(TEXT("CameraBoom"));
			VariableNode->ReconstructNode();
		}
	}

	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	const EBlueprintCompileOptions CompileOptions =
		EBlueprintCompileOptions::SkipSave
		| EBlueprintCompileOptions::UseDeltaSerializationDuringReinstancing
		| EBlueprintCompileOptions::SkipNewVariableDefaultsDetection;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOptions);

	const bool bGenerated = IsValid(Blueprint->GeneratedClass);
	const bool bInterfaceRestored = bGenerated
		&& Blueprint->GeneratedClass->ImplementsInterface(CharacterCustomizationInterface);
	const bool bCameraBoomRestored = Blueprint->SimpleConstructionScript
		&& Blueprint->SimpleConstructionScript->FindSCSNode(TEXT("CameraBoom"));
	const bool bUpToDate = Blueprint->IsUpToDate() && Blueprint->Status != BS_Error;
	FString ContractFailure;
	const bool bContractPreserved = bGenerated
		&& Snapshot.ValidatePreservedContract(Blueprint, ContractFailure);
	const bool bRepairSucceeded = bGenerated
		&& bInterfaceRestored
		&& bCameraBoomRestored
		&& bUpToDate
		&& bContractPreserved;
	if (bRepairSucceeded)
	{
		Blueprint->MarkPackageDirty();
		UE_LOG(
			LogProjectBlueprintMigration,
			Log,
			TEXT("TattooShop BP_TSChar repair passed with %d variables, %d persistent graphs, and %d original SCS nodes preserved."),
			Snapshot.Variables.Num(),
			Snapshot.Graphs.Num(),
			Snapshot.SCSNodes.Num());
	}
	else
	{
		UE_LOG(
			LogProjectBlueprintMigration,
			Error,
			TEXT("TattooShop BP_TSChar repair failed: generated=%s interface=%s camera_boom=%s up_to_date=%s contract=%s (%s)."),
			bGenerated ? TEXT("true") : TEXT("false"),
			bInterfaceRestored ? TEXT("true") : TEXT("false"),
			bCameraBoomRestored ? TEXT("true") : TEXT("false"),
			bUpToDate ? TEXT("true") : TEXT("false"),
			bContractPreserved ? TEXT("true") : TEXT("false"),
			ContractFailure.IsEmpty() ? TEXT("no additional contract detail") : *ContractFailure);
		Snapshot.RestoreSerializedContract(Blueprint, AddedCameraBoomNode);
	}
	return bRepairSucceeded;
}

bool UProjectBlueprintMigrationLibrary::RefreshAllBlueprintNodes(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint) || !IsValid(Blueprint->ParentClass))
	{
		return false;
	}

	Blueprint->Modify();
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

bool UProjectBlueprintMigrationLibrary::ReconstructAllBlueprintNodes(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint) || !IsValid(Blueprint->ParentClass))
	{
		return false;
	}

	Blueprint->Modify();
	FBlueprintEditorUtils::ReconstructAllNodes(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

bool UProjectBlueprintMigrationLibrary::RefreshAllBlueprintNodesAndCompileWithoutSaving(UBlueprint* Blueprint)
{
	if (!RefreshAllBlueprintNodes(Blueprint))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();
	return Blueprint->IsUpToDate();
}

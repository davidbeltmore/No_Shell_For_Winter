#include "ProjectCompatibilityInspectionLibrary.h"
#include "Engine/Blueprint.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Editor.h"
#include "Engine/World.h"
#include "ActorFactories/ActorFactoryBlueprint.h"
#include "AssetRegistry/AssetData.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/ACFCharacterInitializerComponent.h"
#include "UObject/UnrealType.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UProjectCompatibilityInspectionLibrary::SyncOwnedNPCLogicDefaults(UBlueprint* Source, UBlueprint* Target, bool bApply)
{
	TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Differences, Errors;
	int32 Compared=0;
	if (!Source || !Target || !Source->GeneratedClass || !Target->GeneratedClass ||
		!Target->GetOutermost()->GetName().StartsWith(TEXT("/Game/_Game/Characters/"))) return TEXT("{\"error\":\"Invalid owned NPC pair\"}");
	auto* SourceClass=CastChecked<UBlueprintGeneratedClass>(Source->GeneratedClass);
	auto* TargetClass=CastChecked<UBlueprintGeneratedClass>(Target->GeneratedClass);
	auto Copy=[&](UObject* From,UObject* To,const FString& Component)
	{
		if (!From || !To || From->GetClass()!=To->GetClass()) {Errors.Add(MakeShared<FJsonValueString>(Component+TEXT(": missing or different component class")));return;}
		for (TFieldIterator<FProperty> It(From->GetClass());It;++It)
		{
			FProperty* P=*It;
			if (!P->HasAnyPropertyFlags(CPF_Edit) || P->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated|CPF_EditConst)) continue;
			++Compared;
			const bool bInstanced=P->HasAnyPropertyFlags(CPF_InstancedReference|CPF_ContainsInstancedReference);
			if (P->Identical_InContainer(From,To,0,bInstanced?PPF_DeepComparison:PPF_None)) continue;
			FString Before,Expected;
			P->ExportText_InContainer(0,Before,To,To,To,PPF_None);
			P->ExportText_InContainer(0,Expected,From,From,From,PPF_None);
			// Instanced references require explicit duplication, never alias source ownership.
			auto* Conditions=CastField<FArrayProperty>(P);
			auto* ConditionObject=Conditions?CastField<FObjectPropertyBase>(Conditions->Inner):nullptr;
			const bool bConditionArray=bInstanced && P->GetFName()==TEXT("AnimStartingConditions") && ConditionObject;
			if (bInstanced && !bConditionArray)
			{Errors.Add(MakeShared<FJsonValueString>(Component+TEXT(".")+P->GetName()+TEXT(": instanced property requires explicit adapter")));continue;}
			TSharedRef<FJsonObject> D=MakeShared<FJsonObject>();
			D->SetStringField(TEXT("component"),Component);D->SetStringField(TEXT("property"),P->GetName());
			D->SetStringField(TEXT("before"),Before);D->SetStringField(TEXT("expected"),Expected);
			Differences.Add(MakeShared<FJsonValueObject>(D));
			if (bApply)
			{
				To->Modify();
				if (bConditionArray)
				{
					FScriptArrayHelper SourceArray(Conditions,Conditions->ContainerPtrToValuePtr<void>(From));
					FScriptArrayHelper DestArray(Conditions,Conditions->ContainerPtrToValuePtr<void>(To));
					DestArray.EmptyAndAddValues(SourceArray.Num());
					for (int32 I=0;I<SourceArray.Num();++I)
					{
						UObject* Original=ConditionObject->GetObjectPropertyValue(SourceArray.GetRawPtr(I));
						UObject* Clone=Original?StaticDuplicateObject(Original,To,Original->GetFName()):nullptr;
						ConditionObject->SetObjectPropertyValue(DestArray.GetRawPtr(I),Clone);
					}
				}
				else P->CopyCompleteValue_InContainer(To,From);
			}
		}
	};
	auto Skip=[](const UActorComponent* C)
	{
		return C->IsA<USkeletalMeshComponent>() || C->IsA<UACFCharacterInitializerComponent>() ||
			C->GetClass()->GetName().Contains(TEXT("Groom")) || C->GetClass()->GetName().Contains(TEXT("LipSync")) || C->GetClass()->GetName().Contains(TEXT("LODSync"));
	};
	TMap<FName,USCS_Node*> TargetNodes;
	for (auto* C=TargetClass;C;C=Cast<UBlueprintGeneratedClass>(C->GetSuperClass()))
		if (C->SimpleConstructionScript) for (auto* N:C->SimpleConstructionScript->GetAllNodes())
			if (!TargetNodes.Contains(N->GetVariableName())) TargetNodes.Add(N->GetVariableName(),N);
	TSet<FName> Visited;
	for (auto* C=SourceClass;C;C=Cast<UBlueprintGeneratedClass>(C->GetSuperClass()))
		if (C->SimpleConstructionScript) for (auto* N:C->SimpleConstructionScript->GetAllNodes())
		{
			const FName Name=N->GetVariableName(); if (Visited.Contains(Name)) continue; Visited.Add(Name);
			auto* From=N->GetActualComponentTemplate(SourceClass); if (!From || Skip(From)) continue;
			auto* const* Found=TargetNodes.Find(Name);
			if (!Found) {Errors.Add(MakeShared<FJsonValueString>(Name.ToString()+TEXT(": missing SCS node")));continue;}
			auto* TN=*Found; UActorComponent* To=TN->GetActualComponentTemplate(TargetClass);
			if (bApply && TN->GetSCS()->GetOwnerClass()!=TargetClass)
			{
				auto* Handler=Target->GetInheritableComponentHandler(true);const FComponentKey Key(TN);
				To=Handler->GetOverridenComponentTemplate(Key);
				if (!To) To=Handler->CreateOverridenComponentTemplate(Key);
			}
			Copy(From,To,Name.ToString());
		}
	const auto* S=CastChecked<AActor>(SourceClass->GetDefaultObject());auto* D=CastChecked<AActor>(TargetClass->GetDefaultObject());
	TInlineComponentArray<UActorComponent*> NativeSource(S),NativeTarget(D);
	for (auto* From:NativeSource)
	{
		if (Skip(From)) continue;
		auto* const* To=NativeTarget.FindByPredicate([&](auto* V){return V->GetFName()==From->GetFName();});
		Copy(From,To?*To:nullptr,From->GetName());
	}
	// Pawn possession is a separate authoring contract, independent of visual overrides.
	for (FName Name:{FName(TEXT("AIControllerClass")),FName(TEXT("AutoPossessAI"))})
		if (FProperty* P=FindFProperty<FProperty>(SourceClass,Name))
		{
			++Compared;
			if (!P->Identical_InContainer(S,D))
			{
				Differences.Add(MakeShared<FJsonValueString>(Name.ToString()));
				if (bApply) P->CopyCompleteValue_InContainer(D,S);
			}
		}
	if (bApply) {FBlueprintEditorUtils::MarkBlueprintAsModified(Target);Target->MarkPackageDirty();}
	Report->SetNumberField(TEXT("compared"),Compared);Report->SetArrayField(TEXT("differences"),Differences);Report->SetArrayField(TEXT("errors"),Errors);
	Report->SetBoolField(TEXT("applied"),bApply);
	FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Report,Writer);return Result;
}

AActor* UProjectCompatibilityInspectionLibrary::ReplaceOwnedNPCInLevel(AActor* Actor, UBlueprint* Replacement)
{
	if (!GEditor || !Actor || Actor->GetWorld()->IsGameWorld() || !Replacement ||
		!Actor->GetOutermost()->GetName().StartsWith(TEXT("/Game/_Game/")) ||
		!Replacement->GetOutermost()->GetName().StartsWith(TEXT("/Game/_Game/Characters/"))) return nullptr;
	const auto* Defaults=Cast<ACharacter>(Replacement->GeneratedClass->GetDefaultObject());
	if (!Defaults || !Cast<ACharacter>(Actor)) return nullptr;
	auto* Settings=GetMutableDefault<ULevelEditorMiscSettings>();
	const bool bOldCopy=Settings->bReplaceCopiesPropertiesWhenRelatedByNativeParent;
	const bool bOldScale=Settings->bReplaceRespectsScale;
	Settings->bReplaceCopiesPropertiesWhenRelatedByNativeParent=true;
	Settings->bReplaceRespectsScale=true;
	TArray<AActor*> Created;
	UEditorActorSubsystem::ReplaceActors(GEditor->FindActorFactoryByClass(UActorFactoryBlueprint::StaticClass()),FAssetData(Replacement),{Actor},&Created,true);
	Settings->bReplaceCopiesPropertiesWhenRelatedByNativeParent=bOldCopy;
	Settings->bReplaceRespectsScale=bOldScale;
	if (Created.Num()!=1) return nullptr;
	auto* New=CastChecked<ACharacter>(Created[0]);
	// Restore only the new presentation contract after preserving source instance state.
	New->GetMesh()->SetSkeletalMeshAsset(Defaults->GetMesh()->GetSkeletalMeshAsset());
	New->GetMesh()->OverrideMaterials=Defaults->GetMesh()->OverrideMaterials;
	New->GetMesh()->SetAnimInstanceClass(Defaults->GetMesh()->GetAnimClass());
	New->GetMesh()->SetRelativeTransform(Defaults->GetMesh()->GetRelativeTransform());
	if (auto* Init=New->FindComponentByClass<UACFCharacterInitializerComponent>())
		if (const auto* DefaultInit=Defaults->FindComponentByClass<UACFCharacterInitializerComponent>())
			if (auto* Property=FindFProperty<FProperty>(Init->GetClass(),TEXT("CharacterInitDataAsset"))) Property->CopyCompleteValue_InContainer(Init,DefaultInit);
	for (const FName Tag:Defaults->Tags) New->Tags.AddUnique(Tag);
	New->MarkPackageDirty();
	return New;
}

int32 UProjectCompatibilityInspectionLibrary::RemapOwnedNPCReferences(UObject* Asset, const TMap<UObject*, UObject*>& Replacements)
{
	if (!Asset || !Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/_Game/Characters/"))) return -1;
	for (const auto& Pair : Replacements)
		if (!Pair.Key || !Pair.Value || !Pair.Value->GetOutermost()->GetName().StartsWith(TEXT("/Game/_Game/Characters/"))) return -1;
	int32 Count = 0;
	TArray<UObject*> Objects;
	GetObjectsWithOuter(Asset->GetOutermost(), Objects, true);
	for (auto* Object : Objects)
	{
		FArchiveReplaceObjectRef<UObject> Archive(Object, Replacements,
			EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
		Count += static_cast<int32>(Archive.GetCount());
	}
	if (Count) Asset->MarkPackageDirty();
	return Count;
}

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

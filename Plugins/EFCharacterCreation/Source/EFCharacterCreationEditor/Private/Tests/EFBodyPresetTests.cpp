#include "EFBodyPresetCatalog.h"
#include "EFCharacterCustomizationComponent.h"
#include "EFCharacterCustomizationSaveGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFBodyPresetContracts, "NoShellForWinter.BodyPresets.CatalogAndState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFBodyPresetContracts::RunTest(const FString& Parameters)
{
	const auto* Catalog = UEFBodyPresetCatalog::Get();
	if (!TestNotNull(TEXT("Authored shared catalog loads"), Catalog)) return false;
	FString Error;
	TestTrue(*Error, Catalog->ValidateDefinition(Error));
	const auto* Male1 = Catalog->Find(TEXT("Male.01"));
	const auto* Male2 = Catalog->Find(TEXT("Male.02"));
	const auto* Female = Catalog->Find(TEXT("Female.01"));
	if (!TestNotNull(TEXT("Male1"),Male1) || !TestNotNull(TEXT("Male2"),Male2) || !TestNotNull(TEXT("Female"),Female)) return false;
	FRandomStream A(613), B(613);
	TSet<FName> Chosen;
	for (int32 I=0; I<1000; ++I)
	{
		const auto* Left = Catalog->ChooseNPC(ECharacterCreationGender::Male,A);
		const auto* Right = Catalog->ChooseNPC(ECharacterCreationGender::Male,B);
		if (!Left || !Right) { AddError(TEXT("Male pool empty")); return false; }
		if (Left->PresetId != Right->PresetId || Left->Gender != ECharacterCreationGender::Male) { AddError(TEXT("Determinism/gender violation")); return false; }
		Chosen.Add(Left->PresetId);
	}
	TestEqual(TEXT("Both Male variants are reachable"),Chosen.Num(),2);
	TestNull(TEXT("Unknown pool fails closed"),Catalog->ChooseNPC(ECharacterCreationGender::NotApplicable,A));
	TStrongObjectPtr<UEFBodyPresetCatalog> Invalid(DuplicateObject<UEFBodyPresetCatalog>(Catalog,GetTransientPackage()));
	const FEFBodyPresetDefinition Duplicate = Invalid->Bodies[0];
	Invalid->Bodies.Add(Duplicate);
	TestFalse(TEXT("Duplicate IDs rejected"),Invalid->ValidateDefinition(Error));
	Invalid->Bodies = Catalog->Bodies;
	for (auto& Body : Invalid->Bodies) Body.NPCWeight = 0;
	TestNull(TEXT("Zero weights do not select disabled pool"),Invalid->ChooseNPC(ECharacterCreationGender::Male,A));

	UWorld* World=GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("Initialized editor world"), World)) return false;
	FActorSpawnParameters Spawn; Spawn.ObjectFlags |= RF_Transient;
	ACharacter* Pawn=World->SpawnActor<ACharacter>(ACharacter::StaticClass(),FTransform::Identity,Spawn);
	if (!TestNotNull(TEXT("Transient appearance fixture"), Pawn)) return false;
	Pawn->GetMesh()->SetSkeletalMeshAsset(Female->Mesh.LoadSynchronous());
	auto* Component=NewObject<UEFCharacterCustomizationComponent>(Pawn);
	Pawn->AddInstanceComponent(Component); Component->RegisterComponent();
	Component->InitializeForActor(Pawn);
	TestEqual(TEXT("Female has one body option"),Component->GetAvailableBodyMeshOptions().Num(),1);
	TestTrue(TEXT("Select Male2"),Component->SelectBodyPreset(TEXT("Male.02")));
	TestEqual(TEXT("Male list contains two options"),Component->GetAvailableBodyMeshOptions().Num(),2);
	TestTrue(TEXT("Reselect gender succeeds"),Component->SetGender(ECharacterCreationGender::Male));
	TestEqual(TEXT("Reselect does not reset variant"),Component->GetBodyPresetId(),FName(TEXT("Male.02")));
	const auto Confirmed = Component->CaptureCurrentState();
	Component->SetGender(ECharacterCreationGender::Female);
	Component->SetGender(ECharacterCreationGender::Male);
	TestEqual(TEXT("Gender round trip remembers Male2"),Component->GetBodyPresetId(),FName(TEXT("Male.02")));
	Component->SelectBodyPreset(TEXT("Male.01"));
	Component->ApplyState(Confirmed);
	TestEqual(TEXT("Cancel state restores Male2"),Component->GetBodyPresetId(),FName(TEXT("Male.02")));
	TestFalse(TEXT("Unknown ID rejected without changing body"),Component->SelectBodyPreset(TEXT("missing")));
	TestEqual(TEXT("Failed selection atomic"),Component->GetBodyPresetId(),FName(TEXT("Male.02")));
	auto Legacy=Confirmed; Legacy.BodyPresetId=NAME_None; Legacy.LastBodyPresetByGender.Reset(); Legacy.AppearanceVersion=0;
	Component->SelectBodyPreset(TEXT("Female.01")); Component->ApplyState(Legacy);
	TestEqual(TEXT("Legacy path save migrates to stable ID"),Component->GetBodyPresetId(),FName(TEXT("Male.02")));
	auto Missing=Confirmed; Missing.BodyPresetId=TEXT("Removed.42"); Missing.SelectedBodyMeshAsset=FSoftObjectPath(TEXT("/Game/MissingBody.MissingBody"));
	Component->ApplyState(Missing);
	TestEqual(TEXT("Removed body has gender default fallback"),Component->GetBodyPresetId(),FName(TEXT("Male.01")));
	TStrongObjectPtr<UEFCharacterCustomizationSaveGame> Save(NewObject<UEFCharacterCustomizationSaveGame>());
	Save->bHasLastConfirmedState=true; Save->LastConfirmedState=Confirmed;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize appearance through actual SaveGame codec"),UGameplayStatics::SaveGameToMemory(Save.Get(),Bytes));
	const auto* Reloaded=Cast<UEFCharacterCustomizationSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (TestNotNull(TEXT("Deserialize appearance"),Reloaded))
	{
		Component->ApplyState(Reloaded->LastConfirmedState);
		TestEqual(TEXT("Reloaded stable body"),Component->GetBodyPresetId(),FName(TEXT("Male.02")));
		TestEqual(TEXT("Reloaded morph count"),Component->CaptureCurrentState().MorphValues.Num(),Confirmed.MorphValues.Num());
	}
	Pawn->Destroy();
	return true;
}
#endif

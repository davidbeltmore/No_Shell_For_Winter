#include "EFCharacterCustomizationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"

namespace EFCharacterCreationGenderRoutingTestsPrivate
{
	static constexpr const TCHAR* FemaleMeshPath = TEXT("/Game/DazToUnreal/Female/Female.Female");
	static constexpr const TCHAR* MaleMeshPath = TEXT("/Game/DazToUnreal/Male/Male.Male");

	static FString GetActiveBodyMeshPath(const UEFCharacterCustomizationComponent* Component)
	{
		const USkeletalMeshComponent* MeshComponent = Component ? Component->GetBodyMeshSelectionComponent() : nullptr;
		if (!IsValid(MeshComponent) && Component)
		{
			MeshComponent = Component->GetBodyMeshComponent();
		}

		const USkeletalMesh* Mesh = IsValid(MeshComponent) ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
		return IsValid(Mesh) ? Mesh->GetPathName() : FString();
	}

	static FString GetBodyMeshPath(const USkeletalMeshComponent* MeshComponent)
	{
		const USkeletalMesh* Mesh = IsValid(MeshComponent) ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
		return IsValid(Mesh) ? Mesh->GetPathName() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEFCharacterCreationGenderMeshRoutingTest,
	"NoShellForWinter.CharacterCreation.Identity.GenderMeshRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCharacterCreationGenderMeshRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EFCharacterCreationGenderRoutingTestsPrivate;

	USkeletalMesh* FemaleMesh = LoadObject<USkeletalMesh>(nullptr, FemaleMeshPath);
	USkeletalMesh* MaleMesh = LoadObject<USkeletalMesh>(nullptr, MaleMeshPath);
	TestNotNull(TEXT("Female mesh loads"), FemaleMesh);
	TestNotNull(TEXT("Male mesh loads"), MaleMesh);
	if (!FemaleMesh || !MaleMesh)
	{
		return false;
	}

	FAutomationEditorCommonUtils::CreateNewMap();
	UWorld* TestWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("Automation editor world is valid"), TestWorld);
	if (!TestWorld)
	{
		return false;
	}

	ACharacter* TestActor = TestWorld->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform::Identity);
	USkeletalMeshComponent* CharacterMesh = TestActor->GetMesh();
	CharacterMesh->SetSkeletalMeshAsset(FemaleMesh);
	CharacterMesh->SetVisibility(false);

	USkeletalMeshComponent* VisibleBodyMesh = NewObject<USkeletalMeshComponent>(TestActor, TEXT("SkeletalMesh"));
	VisibleBodyMesh->SetSkeletalMeshAsset(FemaleMesh);
	VisibleBodyMesh->SetVisibility(true);
	TestActor->AddInstanceComponent(VisibleBodyMesh);
	VisibleBodyMesh->RegisterComponent();

	UEFCharacterCustomizationComponent* Component = NewObject<UEFCharacterCustomizationComponent>(TestActor, TEXT("CharacterCustomization"));
	TestActor->AddInstanceComponent(Component);
	Component->RegisterComponent();

	TestTrue(TEXT("InitializeForActor succeeds"), Component->InitializeForActor(TestActor));
	TestTrue(TEXT("ACharacter Mesh and visible SkeletalMesh are discovered separately"),
		Component->GetBodyMeshComponent() == CharacterMesh
		&& Component->GetBodyMeshSelectionComponent() == VisibleBodyMesh);
	TestEqual(TEXT("Initial gender resolves to Female"), Component->GetGender(), ECharacterCreationGender::Female);
	TestEqual(TEXT("Female mesh starts active"), GetActiveBodyMeshPath(Component), FString(FemaleMeshPath));
	TestEqual(TEXT("ACharacter Mesh starts Female"), GetBodyMeshPath(CharacterMesh), FString(FemaleMeshPath));
	TestEqual(TEXT("Visible SkeletalMesh starts Female"), GetBodyMeshPath(VisibleBodyMesh), FString(FemaleMeshPath));
	const int32 InitialFemaleMorphCount = Component->GetAvailableMorphEntries().Num();
	TestTrue(TEXT("Female morph catalog is populated"), InitialFemaleMorphCount > 0);

	TestTrue(TEXT("Select Male succeeds"), Component->SelectGender(ECharacterCreationGender::Male));
	TestEqual(TEXT("Male enum is active"), Component->GetGender(), ECharacterCreationGender::Male);
	TestEqual(TEXT("Male mesh is active"), GetActiveBodyMeshPath(Component), FString(MaleMeshPath));
	TestEqual(TEXT("ACharacter Mesh changes to Male"), GetBodyMeshPath(CharacterMesh), FString(MaleMeshPath));
	TestEqual(TEXT("Visible SkeletalMesh changes to Male"), GetBodyMeshPath(VisibleBodyMesh), FString(MaleMeshPath));
	TestTrue(TEXT("Male morph catalog is rebuilt"), Component->GetAvailableMorphEntries().Num() > 0);
	TestEqual(TEXT("Male tag resolves"), Component->GetGenderGameplayTag().ToString(), FString(TEXT("Project.Gender.Male")));

	TestTrue(TEXT("Select Female succeeds"), Component->SelectGender(ECharacterCreationGender::Female));
	TestEqual(TEXT("Female enum is restored"), Component->GetGender(), ECharacterCreationGender::Female);
	TestEqual(TEXT("Female mesh is restored"), GetActiveBodyMeshPath(Component), FString(FemaleMeshPath));
	TestEqual(TEXT("ACharacter Mesh changes back to Female"), GetBodyMeshPath(CharacterMesh), FString(FemaleMeshPath));
	TestEqual(TEXT("Visible SkeletalMesh changes back to Female"), GetBodyMeshPath(VisibleBodyMesh), FString(FemaleMeshPath));
	TestEqual(TEXT("Female morph catalog is deterministically restored"), Component->GetAvailableMorphEntries().Num(), InitialFemaleMorphCount);
	TestEqual(TEXT("Female tag resolves"), Component->GetGenderGameplayTag().ToString(), FString(TEXT("Project.Gender.Female")));
	TestFalse(TEXT("Unknown is not selectable in Info contract"), Component->SelectGender(ECharacterCreationGender::NotApplicable));

	Component->SetCharacterName(TEXT("  Alia  "));
	TestEqual(TEXT("Character name is trimmed"), Component->GetCharacterName(), FString(TEXT("Alia")));
	Component->SetCharacterName(TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"));
	TestEqual(TEXT("Character name is capped at 32 characters"), Component->GetCharacterName().Len(), 32);
	Component->SetCharacterName(TEXT("   "));
	TestEqual(TEXT("Empty character name falls back"), Component->GetResolvedCharacterName(), FString(TEXT("Player")));

	Component->SetCharacterName(TEXT("Persisted Alia"));
	FCharacterCustomizationState CapturedState = Component->CaptureCurrentState();
	TestEqual(TEXT("Captured state stores name"), CapturedState.CharacterName, FString(TEXT("Persisted Alia")));
	TestEqual(TEXT("Captured state stores gender"), CapturedState.Gender, ECharacterCreationGender::Female);
	Component->SetCharacterName(TEXT("Temporary"));
	Component->SelectGender(ECharacterCreationGender::Male);
	Component->ApplyState(CapturedState);
	TestEqual(TEXT("ApplyState restores name"), Component->GetResolvedCharacterName(), FString(TEXT("Persisted Alia")));
	TestEqual(TEXT("ApplyState restores gender"), Component->GetGender(), ECharacterCreationGender::Female);
	TestEqual(TEXT("ApplyState restores visible mesh"), GetActiveBodyMeshPath(Component), FString(FemaleMeshPath));

	CharacterMesh->SetSkeletalMeshAsset(MaleMesh);
	VisibleBodyMesh->SetSkeletalMeshAsset(MaleMesh);
	TestTrue(TEXT("Reinitialize a Male-authored actor succeeds"), Component->InitializeForActor(TestActor));
	TestEqual(TEXT("Male-authored actor keeps Male identity"), Component->GetGender(), ECharacterCreationGender::Male);
	TestEqual(TEXT("Male-authored actor keeps Male body mesh"), GetActiveBodyMeshPath(Component), FString(MaleMeshPath));

	return true;
}

#endif

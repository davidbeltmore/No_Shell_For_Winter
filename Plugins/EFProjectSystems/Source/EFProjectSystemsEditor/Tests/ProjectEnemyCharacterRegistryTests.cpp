#include "Characters/ProjectCharacterIdentitySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Characters/ProjectEnemyVisualVariationSettings.h"
#include "Characters/ProjectEnemyVisualVariationSubsystem.h"
#include "EFProjectEnemySettings.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"
#include "Misc/AutomationTest.h"

namespace ProjectEnemyCharacterRegistryTestsPrivate
{
	static bool IsFemaleClassPath(const FString& Path)
	{
		return Path.Contains(TEXT("/_Game/Characters/Female/"));
	}

	static bool IsMaleClassPath(const FString& Path)
	{
		return Path.Contains(TEXT("/_Game/Characters/Male/"));
	}

	static bool IsCompanionClassPath(const FString& Path)
	{
		return Path.Contains(TEXT("Companion"));
	}

	static void ValidateFinalPathContract(FAutomationTestBase& Test, const FString& Label, const FString& Path)
	{
		Test.TestFalse(*FString::Printf(TEXT("%s does not reference legacy FullSample enemy classes"), *Label), Path.Contains(TEXT("/FullSample/Blueprints/Characters/Enemies/")));
		Test.TestFalse(*FString::Printf(TEXT("%s does not retain BPMale1"), *Label), Path.Contains(TEXT("BPMale1")));
		Test.TestTrue(*FString::Printf(TEXT("%s belongs to an organized project character folder"), *Label), IsMaleClassPath(Path) || IsFemaleClassPath(Path));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyCharacterRegistryCoverageTest,
	"NoShellForWinter.ProjectSystems.Enemies.CharacterRegistryCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyCharacterRegistryCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UEFProjectEnemySettings* EnemySettings = UEFProjectEnemySettings::Get();
	const UProjectEnemyLevelSettings* LevelSettings = UProjectEnemyLevelSettings::Get();
	const UProjectEnemyVisualVariationSettings* VisualSettings = UProjectEnemyVisualVariationSettings::Get();
	TestNotNull(TEXT("Enemy settings resolve"), EnemySettings);
	TestNotNull(TEXT("Enemy level settings resolve"), LevelSettings);
	TestNotNull(TEXT("Enemy visual settings resolve"), VisualSettings);
	if (!EnemySettings || !LevelSettings || !VisualSettings)
	{
		return false;
	}

	TestEqual(TEXT("Runtime registry contains sixteen hostiles"), EnemySettings->RuntimeEnemyClasses.Num(), 16);
	TestEqual(TEXT("Male identity registry contains eight hostiles and three companions"), EnemySettings->MaleCharacterClasses.Num(), 11);
	TestEqual(TEXT("Female identity registry contains eight hostiles and three companions"), EnemySettings->FemaleCharacterClasses.Num(), 11);
	TestEqual(TEXT("Level registry contains sixteen hostiles"), LevelSettings->TargetEnemyBaseClasses.Num(), 16);
	TestEqual(TEXT("Shared visual registry contains sixteen hostiles"), VisualSettings->TargetEnemyClasses.Num(), 16);
	TestEqual(
		TEXT("Optional mature morph registry contains eight Male hostiles and the HUB Intimacy companion"),
		VisualSettings->OptionalMatureMorphTargetEnemyClasses.Num(),
		9);
	TestTrue(
		TEXT("Optional mature morph capability is configured but remains subordinate to content policy"),
		VisualSettings->bEnableOptionalMatureMorphPresentation);
	TestFalse(TEXT("Neutral-base morph setting resolves"), VisualSettings->NeutralBaseMorphName.IsNone());
	TestFalse(TEXT("Active-presentation morph setting resolves"), VisualSettings->ActivePresentationMorphName.IsNone());
	USkeletalMesh* MaleMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/DazToUnreal/Male/Male.Male"));
	TestNotNull(TEXT("Current Male mesh resolves"), MaleMesh);
	if (MaleMesh)
	{
		TestNotNull(
			TEXT("Current Male mesh contains the configured neutral-base morph"),
			MaleMesh->FindMorphTarget(VisualSettings->NeutralBaseMorphName));
		TestNotNull(
			TEXT("Current Male mesh contains the configured active-presentation morph"),
			MaleMesh->FindMorphTarget(VisualSettings->ActivePresentationMorphName));
	}

	TSet<FString> RuntimePaths;
	int32 RuntimeMaleCount = 0;
	int32 RuntimeFemaleCount = 0;
	for (const FSoftClassPath& ClassPath : EnemySettings->RuntimeEnemyClasses)
	{
		const FString Path = ClassPath.ToString();
		ProjectEnemyCharacterRegistryTestsPrivate::ValidateFinalPathContract(*this, TEXT("Runtime class"), Path);
		TestFalse(TEXT("Companions stay outside hostile runtime systems"), ProjectEnemyCharacterRegistryTestsPrivate::IsCompanionClassPath(Path));
		RuntimeMaleCount += ProjectEnemyCharacterRegistryTestsPrivate::IsMaleClassPath(Path) ? 1 : 0;
		RuntimeFemaleCount += ProjectEnemyCharacterRegistryTestsPrivate::IsFemaleClassPath(Path) ? 1 : 0;
		RuntimePaths.Add(Path);
	}
	TestEqual(TEXT("Runtime registry has no duplicates"), RuntimePaths.Num(), 16);
	TestEqual(TEXT("Runtime registry contains eight Male hostiles"), RuntimeMaleCount, 8);
	TestEqual(TEXT("Runtime registry contains eight Female hostiles"), RuntimeFemaleCount, 8);

	int32 MaleCompanionCount = 0;
	for (const FSoftClassPath& ClassPath : EnemySettings->MaleCharacterClasses)
	{
		const FString Path = ClassPath.ToString();
		ProjectEnemyCharacterRegistryTestsPrivate::ValidateFinalPathContract(*this, TEXT("Male identity class"), Path);
		TestTrue(TEXT("Male identity class stays in Male folder"), ProjectEnemyCharacterRegistryTestsPrivate::IsMaleClassPath(Path));
		MaleCompanionCount += ProjectEnemyCharacterRegistryTestsPrivate::IsCompanionClassPath(Path) ? 1 : 0;
		const UClass* CharacterClass = ClassPath.TryLoadClass<ACharacter>();
		const ACharacter* CharacterDefaults = CharacterClass ? CharacterClass->GetDefaultObject<ACharacter>() : nullptr;
		const USkeletalMesh* AuthoredMesh = CharacterDefaults && CharacterDefaults->GetMesh()
			? CharacterDefaults->GetMesh()->GetSkeletalMeshAsset()
			: nullptr;
		TestNotNull(*FString::Printf(TEXT("Male identity class loads: %s"), *Path), CharacterDefaults);
		TestTrue(
			*FString::Printf(TEXT("Male identity class uses the authoritative Male mesh: %s"), *Path),
			AuthoredMesh && AuthoredMesh->GetPathName() == TEXT("/Game/DazToUnreal/Male/Male.Male"));
	}
	TestEqual(TEXT("Male registry includes three companions"), MaleCompanionCount, 3);

	int32 FemaleCompanionCount = 0;
	for (const FSoftClassPath& ClassPath : EnemySettings->FemaleCharacterClasses)
	{
		const FString Path = ClassPath.ToString();
		ProjectEnemyCharacterRegistryTestsPrivate::ValidateFinalPathContract(*this, TEXT("Female identity class"), Path);
		TestTrue(TEXT("Female identity class stays in Female folder"), ProjectEnemyCharacterRegistryTestsPrivate::IsFemaleClassPath(Path));
		TestTrue(TEXT("Female assets use a clean Female suffix"), Path.Contains(TEXT("BPFemale.")));
		FemaleCompanionCount += ProjectEnemyCharacterRegistryTestsPrivate::IsCompanionClassPath(Path) ? 1 : 0;
	}
	TestEqual(TEXT("Female registry includes three companions"), FemaleCompanionCount, 3);

	int32 OptionalMatureCompanionCount = 0;
	TArray<TSubclassOf<APawn>> LoadedVisualTargets;
	TArray<TSubclassOf<APawn>> LoadedOptionalMatureTargets;
	for (const TSoftClassPtr<APawn>& ClassPath : VisualSettings->TargetEnemyClasses)
	{
		if (UClass* LoadedClass = ClassPath.LoadSynchronous())
		{
			LoadedVisualTargets.Add(LoadedClass);
		}
	}
	for (const TSoftClassPtr<APawn>& ClassPath : VisualSettings->OptionalMatureMorphTargetEnemyClasses)
	{
		const FString Path = ClassPath.ToSoftObjectPath().ToString();
		TestTrue(
			TEXT("Optional mature morph targets stay in Male folder"),
			ProjectEnemyCharacterRegistryTestsPrivate::IsMaleClassPath(Path));
		OptionalMatureCompanionCount += ProjectEnemyCharacterRegistryTestsPrivate::IsCompanionClassPath(Path) ? 1 : 0;
		if (UClass* LoadedClass = ClassPath.LoadSynchronous())
		{
			LoadedOptionalMatureTargets.Add(LoadedClass);
		}
	}
	TestEqual(TEXT("Only the configured HUB Intimacy companion joins optional mature targets"), OptionalMatureCompanionCount, 1);

	const UClass* HubIntimacyCompanionClass = StaticLoadClass(
		APawn::StaticClass(),
		nullptr,
		TEXT("/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C"));
	TestNotNull(TEXT("HUB Intimacy companion class resolves"), HubIntimacyCompanionClass);
	TestTrue(
		TEXT("HUB Intimacy companion is eligible through the optional participant registry"),
		UProjectEnemyVisualVariationSubsystem::IsClassEligibleForVisualVariation(
			HubIntimacyCompanionClass,
			LoadedVisualTargets,
			LoadedOptionalMatureTargets));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyCharacterGenderApplicationTest,
	"NoShellForWinter.ProjectSystems.Enemies.GenderApplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyCharacterGenderApplicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	APawn* Pawn = NewObject<APawn>(GetTransientPackage());
	UAbilitySystemComponent* AbilitySystem = NewObject<UAbilitySystemComponent>(Pawn, TEXT("EnemyIdentityTestAbilitySystem"));
	Pawn->AddInstanceComponent(AbilitySystem);
	UProjectIntimacyPartnerComponent* Partner = NewObject<UProjectIntimacyPartnerComponent>(Pawn, TEXT("ProjectIntimacyPartnerComponent"));
	Pawn->AddInstanceComponent(Partner);

	const FGameplayTag MaleTag = FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Male"), false);
	const FGameplayTag FemaleTag = FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Female"), false);
	const FGameplayTag UnknownTag = FGameplayTag::RequestGameplayTag(TEXT("Project.Gender.Unknown"), false);

	TestTrue(TEXT("Male identity applies"), UProjectCharacterIdentitySubsystem::ApplyGenderIdentity(Pawn, MaleTag));
	TestEqual(TEXT("Male ASC tag is exclusive"), AbilitySystem->GetTagCount(MaleTag), 1);
	TestEqual(TEXT("Female ASC tag is cleared"), AbilitySystem->GetTagCount(FemaleTag), 0);
	TestEqual(TEXT("Unknown ASC tag is cleared"), AbilitySystem->GetTagCount(UnknownTag), 0);
	TestTrue(TEXT("Partner component receives Male identity"), Partner->GenderTag.MatchesTagExact(MaleTag));
	TestTrue(TEXT("Configured humanoid identities are explicitly marked adult"), Partner->bAdultVerified);
	TestFalse(TEXT("Adult verification never implies consent"), Partner->bExplicitConsent);

	TestTrue(TEXT("Female identity applies"), UProjectCharacterIdentitySubsystem::ApplyGenderIdentity(Pawn, FemaleTag));
	TestEqual(TEXT("Male ASC tag is removed"), AbilitySystem->GetTagCount(MaleTag), 0);
	TestEqual(TEXT("Female ASC tag is exclusive"), AbilitySystem->GetTagCount(FemaleTag), 1);
	TestTrue(TEXT("Partner component receives Female identity"), Partner->GenderTag.MatchesTagExact(FemaleTag));
	TestFalse(TEXT("Female is never treated as a Male morph identity"), UProjectCharacterIdentitySubsystem::IsMaleGenderTag(Partner->GenderTag));

	return true;
}

#endif

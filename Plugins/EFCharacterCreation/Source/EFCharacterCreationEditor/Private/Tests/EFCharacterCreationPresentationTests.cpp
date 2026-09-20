#include "EFMorphPresentation.h"
#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFMorphAnatomyTest, "EF.CharacterCreation.Rework.Anatomy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFMorphAnatomyTest::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Name; const TCHAR* Category; const TCHAR* Section; };
	const FCase Cases[] = {
		{TEXT("Body Pear Figure"), TEXT("Body"), TEXT("General & Proportions")},
		{TEXT("Skull Above Ear Width"), TEXT("Head"), TEXT("Skull")},
		{TEXT("Hip Back Dimples"), TEXT("Body"), TEXT("Hips & Glutes")},
		{TEXT("Base Anime Tongue Out Side"), TEXT("Head"), TEXT("Expressions")},
		{TEXT("Afraid"), TEXT("Head"), TEXT("Expressions")},
		{TEXT("Taper UpperArm B"), TEXT("Body"), TEXT("Arms")},
		{TEXT("Mass Forearms"), TEXT("Body"), TEXT("Arms")},
		{TEXT("Eyelid Upper Height"), TEXT("Head"), TEXT("Eyelids")},
		{TEXT("GPV_Vagina_Open_Labia Front Widen_Left"), TEXT("Body"), TEXT("Genitals")},
		{TEXT("DK_Shaft Root_Bend"), TEXT("Body"), TEXT("Genitals")}
	};
	for (const FCase& Case : Cases)
	{
		const auto Result = EFMorphPresentation::Classify(Case.Name);
		TestEqual(Case.Name, Result.Category, FName(Case.Category));
		TestEqual(Case.Name, Result.Section, FString(Case.Section));
	}
	TestFalse(TEXT("Unknown morph retained for review"), EFMorphPresentation::Classify(TEXT("Custom New Shape 09")).bRecognized);
	TestEqual(TEXT("Technical prefix removal preserves side and variant"), EFMorphPresentation::CleanLabel(TEXT("GPL_Minora_Pinch 2_Left"), TEXT("Genitals")), FString(TEXT("Minora Pinch 2 Left")));
	TestEqual(TEXT("Configured aliases"), EFMorphPresentation::NormalizeSection(TEXT("Brows")), FString(TEXT("Brow")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFMorphCoverageTest, "EF.CharacterCreation.Rework.MeshCoverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFMorphCoverageTest::RunTest(const FString& Parameters)
{
	FString Report = TEXT("Mesh\tMorph\tCategory\tSection\tLabel\tRecognized\n");
	for (const TCHAR* Name : {TEXT("Female"), TEXT("Male")})
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/DazToUnreal/%s/%s.%s"), Name, Name, Name));
		if (!TestNotNull(Name, Mesh)) continue;
		TSet<FName> Seen;
		for (const UMorphTarget* Morph : Mesh->GetMorphTargets())
		{
			if (!Morph) continue;
			const FName MorphName = Morph->GetFName();
			TestFalse(TEXT("Unique morph identity"), Seen.Contains(MorphName)); Seen.Add(MorphName);
			const auto Result = EFMorphPresentation::Classify(MorphName.ToString());
			TestTrue(MorphName.ToString(), EFMorphPresentation::Sections(Result.Category).Contains(Result.Section));
			Report += FString::Printf(TEXT("%s\t%s\t%s\t%s\t%s\t%s\n"), Name, *MorphName.ToString(), *Result.Category.ToString(), *Result.Section, *EFMorphPresentation::CleanLabel(MorphName.ToString(), Result.Section), Result.bRecognized ? TEXT("true") : TEXT("false"));
		}
		TestEqual(FString(Name) + TEXT(" inventory baseline"), Seen.Num(), FString(Name) == TEXT("Female") ? 446 : 415);
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Migration/CharacterCreationRework");
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Classification evidence written"), FFileHelper::SaveStringToFile(Report, *(Directory / TEXT("MorphClassification.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	return true;
}
#endif

#include "EFClothingMorphDirectorPolicy.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFClothingUnisexVariantsTest,
	"EF.ClothingMorph.Unisex.BodyVariants", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFClothingUnisexVariantsTest::RunTest(const FString& Parameters)
{
	UEFClothingMorphDirectorPolicy* Director = NewObject<UEFClothingMorphDirectorPolicy>();
	FEFClothingGarmentRow Row;
	Row.GarmentId = TEXT("OneUnisexGarment");
	Row.SourceGarment = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Test/Clothing.Clothing")));
	Row.BodySurface = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Test/BodyA.BodyA")));
	Row.BodySectionsToExclude.Add(TEXT("ReferenceAuxiliary"));
	Row.ExcludedBodySurfaceMaterialSlots = Row.BodySectionsToExclude;
	Row.bCoversGenitals = true;
	Row.ShellThicknessCm = 0.17f;
	Director->Garments.Add(Row);
	for (const TCHAR* Body : {TEXT("/Game/Test/BodyB.BodyB"), TEXT("/Game/Test/BodyC.BodyC")})
	{
		FEFClothingBodyTarget Target;
		Target.BodySurface = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Body));
		Target.MaterialSlotAliases.Add(TEXT("ReferenceAuxiliary"), NAME_None);
		Target.ExcludedFitBoneBranches.Add(TEXT("aux_root"));
		Target.GenitalBoneBranches.Add(TEXT("aux_root"));
		Director->Bodies.Add(Target);
	}
	const FEFClothingBodyTarget Duplicate = Director->Bodies[0];
	Director->Bodies.Add(Duplicate); // Registration cannot duplicate a fit.
	const TArray<FEFClothingGarmentRow> Variants = Director->BuildBodyVariants();
	TestEqual(TEXT("One row supports three bodies without duplicate registration"), Variants.Num(), 3);
	if (Variants.Num() != 3) { return false; }
	TestEqual(TEXT("Authored catalog stays one row"), Director->Garments.Num(), 1);
	TestEqual(TEXT("Reference binding identity stays stable"), Variants[0].GarmentId, Row.GarmentId);
	TestEqual(TEXT("Reference fit fingerprint stays stable"), Variants[0].BuildCompileFingerprint(), Row.BuildCompileFingerprint());
	TSet<FName> Ids;
	for (const FEFClothingGarmentRow& Variant : Variants)
	{
		Ids.Add(Variant.GarmentId);
		TestEqual(TEXT("Same clothing asset for every body"), Variant.SourceGarment.ToSoftObjectPath(), Row.SourceGarment.ToSoftObjectPath());
		TestEqual(TEXT("Same live fit control"), Variant.ShellThicknessCm, Row.ShellThicknessCm);
		TestEqual(TEXT("Stable gameplay identity"), Variant.AuthoredGarmentId, Row.GarmentId);
	}
	TestEqual(TEXT("Generated identities are unique"), Ids.Num(), 3);
	TestTrue(TEXT("Third body transports from reference shape"), Variants[2].ReferenceBodySurface == Row.BodySurface);
	TestTrue(TEXT("Unknown section removed instead of hiding unrelated skin"), Variants[1].BodySectionsToExclude.IsEmpty());
	TestTrue(TEXT("Covered auxiliary anatomy hides its branch"), Variants[1].BodyBoneBranchesToHide.Contains(TEXT("aux_root")));
	Director->Garments[0].bCoversGenitals = false;
	const auto Uncovered = Director->BuildBodyVariants();
	TestTrue(TEXT("Geometry exclusions alone do not hide anatomy"), Uncovered[1].BodyBoneBranchesToHide.IsEmpty());
	TestEqual(TEXT("Coverage toggle does not recompile fitting"), Uncovered[1].BuildCompileFingerprint(), Variants[1].BuildCompileFingerprint());
	return true;
}
#endif

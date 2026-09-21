#include "Calysto/EFCalystoDirectorEditorLibrary.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Calysto/EFCalystoDungeonDirectorAsset.h"
#include "Misc/AutomationTest.h"
#endif

namespace
{
	bool ExportValue(const FProperty* Property, const void* Address, FString& Output, int32 Depth, int32& Nodes)
	{
		if (!Property || !Address || Depth > 32 || ++Nodes > 200000) return false;
		if (const auto* Array = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Values(Array, Address); Output += TEXT("(");
			for (int32 I = 0; I < Values.Num(); ++I)
			{
				if (I) Output += TEXT(",");
				if (!ExportValue(Array->Inner, Values.GetRawPtr(I), Output, Depth + 1, Nodes)) return false;
			}
			Output += TEXT(")"); return true;
		}
		if (const auto* Struct = CastField<FStructProperty>(Property))
		{
			// Native atomic structures (SoftObjectPath, Guid, etc.) keep their engine text representation.
			const auto* Ops = Struct->Struct->GetCppStructOps();
			if ((!Ops || !Ops->HasExportTextItem()) && Struct->Struct->GetStructureSize() > 0 && TFieldIterator<FProperty>(Struct->Struct))
			{
				Output += TEXT("("); int32 Count = 0;
				for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
				{
					if (Count++) Output += TEXT(",");
					Output += It->GetName() + TEXT("=");
					if (!ExportValue(*It, It->ContainerPtrToValuePtr<void>(Address), Output, Depth + 1, Nodes)) return false;
				}
				Output += TEXT(")"); return true;
			}
		}
		Property->ExportTextItem_Direct(Output, Address, nullptr, nullptr, PPF_Delimited);
		return true;
	}
}

FString UEFCalystoDirectorEditorLibrary::ExportAuthoredField(UObject* Asset, const FName Field)
{
	if (!IsValid(Asset)) return {};
	const FString ClassPath = Asset->GetClass()->GetPathName();
	const bool Current = ClassPath == TEXT("/Script/EFProceduralRuntime.EFCalystoDungeonDirectorAsset");
	const bool MigrationSource = ClassPath == TEXT("/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset")
		&& Asset->GetPathName() == TEXT("/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy");
	if (!Current && !MigrationSource) return {};
	const TSet<FName> Allowed = Current
		? TSet<FName>{TEXT("Dungeon"), TEXT("Styles"), TEXT("RoomThemes"), TEXT("Advanced")}
		: TSet<FName>{TEXT("Styles"), TEXT("RoomThemes"), TEXT("PerformanceAndSafety")};
	if (!Allowed.Contains(Field)) return {};
	const FProperty* Property = Asset->GetClass()->FindPropertyByName(Field);
	if (!Property) return {};
	FString Result; int32 Nodes = 0;
	return ExportValue(Property, Property->ContainerPtrToValuePtr<void>(Asset), Result, 0, Nodes) ? Result : FString();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoDirectorCompleteExportTest,
	"NoShellForWinter.CalystoDungeon.Director.Editor.CompleteFieldExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoDirectorCompleteExportTest::RunTest(const FString&)
{
	auto* Asset = NewObject<UEFCalystoDungeonDirectorAsset>();
	auto& Style = Asset->Styles.AddDefaulted_GetRef();
	Style.Selection.Id = FGuid(1, 2, 3, 4);
	auto& Entry = Style.Architecture.Floor.AddDefaulted_GetRef();
	Entry.Selection.Id = FGuid(5, 6, 7, 8);
	Entry.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DirectorExportTest/Unloaded.Unloaded")));
	const FString Export = UEFCalystoDirectorEditorLibrary::ExportAuthoredField(Asset, TEXT("Styles"));
	TestTrue(TEXT("Nested default selection weight is explicit"), Export.Contains(TEXT("Weight=1.000000")));
	TestTrue(TEXT("Nested default enabled flag is explicit"), Export.Contains(TEXT("bEnabled=True")));
	TestTrue(TEXT("Unloaded soft path survives exactly"), Export.Contains(TEXT("/Game/DirectorExportTest/Unloaded.Unloaded")));
	TestFalse(TEXT("Export does not load a soft mesh"), Entry.Mesh.IsValid());
	TestTrue(TEXT("Empty native arrays retain tuple syntax"), Export.Contains(TEXT("Doorways=()")));
	TestTrue(TEXT("Unapproved metadata field is not exported"), UEFCalystoDirectorEditorLibrary::ExportAuthoredField(Asset, TEXT("SchemaVersion")).IsEmpty());
	auto& Theme = Asset->RoomThemes.AddDefaulted_GetRef();
	Theme.Description = TEXT("Authoring notes, with \"quotes\" and a second line.\nNo material effect.");
	Theme.PreviewColor = FLinearColor(1.0f, 0.32f, 0.02f, 0.5f);
	Theme.Decals.SourceFadeStartDistanceCm = 2500.0;
	const FString ThemesExport = UEFCalystoDirectorEditorLibrary::ExportAuthoredField(Asset, TEXT("RoomThemes"));
	TArray<FEFCalystoTheme> ImportedThemes;
	const FProperty* ThemesProperty = Asset->GetClass()->FindPropertyByName(TEXT("RoomThemes"));
	TestNotNull(TEXT("Native Theme metadata import accepts complete export"), ThemesProperty->ImportText_Direct(*ThemesExport, &ImportedThemes, nullptr, PPF_Delimited));
	if (TestEqual(TEXT("Theme export preserves array capacity"), ImportedThemes.Num(), 1))
	{
		TestEqual(TEXT("Author notes roundtrip exactly"), ImportedThemes[0].Description, Theme.Description);
		TestTrue(TEXT("All four preview channels roundtrip exactly"), ImportedThemes[0].PreviewColor == Theme.PreviewColor);
		TestEqual(TEXT("Retired distance approximation remains exact archive metadata"), ImportedThemes[0].Decals.SourceFadeStartDistanceCm, 2500.0);
	}
	const auto* Duplicate = DuplicateObject<UEFCalystoDungeonDirectorAsset>(Asset, GetTransientPackage());
	TestEqual(TEXT("Native duplication preserves Theme notes"), Duplicate->RoomThemes[0].Description, Theme.Description);
	TestTrue(TEXT("Native duplication preserves Theme swatch"), Duplicate->RoomThemes[0].PreviewColor == Theme.PreviewColor);
	const FProperty* PreviewProperty = FEFCalystoTheme::StaticStruct()->FindPropertyByName(TEXT("PreviewColor"));
	TestTrue(TEXT("Preview is stripped from cooked authoring data"), PreviewProperty->HasAnyPropertyFlags(CPF_EditorOnly));
	TestFalse(TEXT("Preview is not a misleading editable material control"), PreviewProperty->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible));
	const FProperty* SourceFade = FEFCalystoDecals::StaticStruct()->FindPropertyByName(TEXT("SourceFadeStartDistanceCm"));
	TestTrue(TEXT("Retired fade input is editor-only"), SourceFade->HasAnyPropertyFlags(CPF_EditorOnly));
	TestFalse(TEXT("Retired fade input cannot advertise an unimplemented gameplay control"), SourceFade->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible));
	return true;
}
#endif

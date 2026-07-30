#include "CodeWidgetSetTextFontSizeCommandlet.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace CodeWidgetSetTextFontSizeCommandlet
{
	static bool ReadCommandLineValue(const FString& Params, const TCHAR* Key, FString& OutValue)
	{
		const FString Prefix = FString::Printf(TEXT("-%s="), Key);
		int32 StartIndex = Params.Find(Prefix, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (StartIndex == INDEX_NONE)
		{
			return false;
		}

		StartIndex += Prefix.Len();
		if (!Params.IsValidIndex(StartIndex))
		{
			OutValue.Reset();
			return true;
		}

		if (Params[StartIndex] == TEXT('"'))
		{
			const int32 ValueStart = StartIndex + 1;
			const int32 ValueEnd = Params.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			if (ValueEnd != INDEX_NONE && ValueEnd >= ValueStart)
			{
				OutValue = Params.Mid(ValueStart, ValueEnd - ValueStart);
				return true;
			}
		}

		int32 EndIndex = StartIndex;
		while (Params.IsValidIndex(EndIndex) && !FChar::IsWhitespace(Params[EndIndex]))
		{
			++EndIndex;
		}

		OutValue = Params.Mid(StartIndex, EndIndex - StartIndex);
		return true;
	}

	static void SplitAssetList(const FString& AssetList, TArray<FString>& OutAssetPaths)
	{
		TArray<FString> SplitPaths;
		AssetList.ParseIntoArray(SplitPaths, TEXT(";"), true);
		if (SplitPaths.Num() <= 1)
		{
			SplitPaths.Reset();
			AssetList.ParseIntoArray(SplitPaths, TEXT(","), true);
		}

		for (FString& Path : SplitPaths)
		{
			Path.TrimStartAndEndInline();
			if (!Path.IsEmpty())
			{
				OutAssetPaths.Add(Path);
			}
		}
	}
}

UCodeWidgetSetTextFontSizeCommandlet::UCodeWidgetSetTextFontSizeCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodeWidgetSetTextFontSizeCommandlet::Main(const FString& Params)
{
	FString AssetList;
	FString WidgetName;
	FString SizeString;

	CodeWidgetSetTextFontSizeCommandlet::ReadCommandLineValue(Params, TEXT("Assets"), AssetList);
	CodeWidgetSetTextFontSizeCommandlet::ReadCommandLineValue(Params, TEXT("Asset"), AssetList);
	CodeWidgetSetTextFontSizeCommandlet::ReadCommandLineValue(Params, TEXT("WidgetName"), WidgetName);
	CodeWidgetSetTextFontSizeCommandlet::ReadCommandLineValue(Params, TEXT("Size"), SizeString);

	int32 FontSize = 0;
	LexFromString(FontSize, *SizeString);

	TArray<FString> AssetPaths;
	CodeWidgetSetTextFontSizeCommandlet::SplitAssetList(AssetList, AssetPaths);

	if (AssetPaths.IsEmpty() || WidgetName.IsEmpty() || FontSize <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=CodeWidgetSetTextFontSize -Assets=/Game/Folder/WBP_A;/Game/Folder/WBP_B -WidgetName=TextBlockName -Size=18 [-NoSave]"));
		return 1;
	}

	const bool bSave = !FParse::Param(*Params, TEXT("NoSave"));
	TArray<UPackage*> PackagesToSave;
	int32 UpdatedCount = 0;

	for (const FString& AssetPath : AssetPaths)
	{
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not load Widget Blueprint or WidgetTree: %s"), *AssetPath);
			continue;
		}

		UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
		UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
		if (!TextBlock)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not find TextBlock '%s' in %s."), *WidgetName, *AssetPath);
			continue;
		}

		FSlateFontInfo FontInfo = TextBlock->GetFont();
		FontInfo.Size = FontSize;

		TextBlock->Modify();
		TextBlock->SetFont(FontInfo);
		WidgetBlueprint->Modify();
		WidgetBlueprint->WidgetTree->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

		if (UPackage* Package = WidgetBlueprint->GetOutermost())
		{
			PackagesToSave.AddUnique(Package);
			if (!bSave)
			{
				Package->MarkPackageDirty();
			}
		}

		++UpdatedCount;
		UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_SET_TEXT_FONT_SIZE asset=%s widget=%s size=%d success=true"), *AssetPath, *WidgetName, FontSize);
	}

	if (bSave && !PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}

	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_SET_TEXT_FONT_SIZE_RESULT updated=%d requested=%d"), UpdatedCount, AssetPaths.Num());
	return UpdatedCount == AssetPaths.Num() ? 0 : 1;
}

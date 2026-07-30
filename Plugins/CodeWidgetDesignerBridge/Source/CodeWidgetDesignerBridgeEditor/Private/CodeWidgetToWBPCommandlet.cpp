#include "CodeWidgetToWBPCommandlet.h"

#include "Blueprint/UserWidget.h"
#include "CodeWidgetToWBPBridgeLibrary.h"
#include "Misc/Parse.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"

namespace CodeWidgetToWBPCommandlet
{
	static constexpr TCHAR DefaultWidgetBlueprintFolder[] = TEXT("/Game/_Game/Widgets");

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
			if (ValueEnd != INDEX_NONE && ValueEnd > ValueStart)
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
		int32 ContinuationIndex = EndIndex;
		while (Params.IsValidIndex(ContinuationIndex) && FChar::IsWhitespace(Params[ContinuationIndex]))
		{
			++ContinuationIndex;
		}

		if (Params.IsValidIndex(ContinuationIndex) && Params[ContinuationIndex] == TEXT('.'))
		{
			int32 ContinuationEnd = ContinuationIndex;
			while (Params.IsValidIndex(ContinuationEnd) && !FChar::IsWhitespace(Params[ContinuationEnd]))
			{
				++ContinuationEnd;
			}
			OutValue += Params.Mid(ContinuationIndex, ContinuationEnd - ContinuationIndex);
		}
		return true;
	}

	static FString MakeWidgetBlueprintAssetName(const UClass* NativeWidgetClass)
	{
		FString AssetBaseName = NativeWidgetClass ? NativeWidgetClass->GetName() : TEXT("CodeWidget");
		AssetBaseName.RemoveFromStart(TEXT("U"));
		AssetBaseName.RemoveFromEnd(TEXT("Widget"));

		if (!AssetBaseName.StartsWith(TEXT("WBP_")))
		{
			AssetBaseName = FString::Printf(TEXT("WBP_%s"), *AssetBaseName);
		}

		return AssetBaseName;
	}

	static FString MakeWidgetFolderName(const UClass* NativeWidgetClass)
	{
		FString FolderName = MakeWidgetBlueprintAssetName(NativeWidgetClass);
		FolderName.RemoveFromStart(TEXT("WBP_"));
		return FolderName;
	}

	static FString MakeDefaultTargetAssetPath(const UClass* NativeWidgetClass)
	{
		return FString::Printf(
			TEXT("%s/%s/Main/%s"),
			DefaultWidgetBlueprintFolder,
			*MakeWidgetFolderName(NativeWidgetClass),
			*MakeWidgetBlueprintAssetName(NativeWidgetClass));
	}
}

UCodeWidgetToWBPCommandlet::UCodeWidgetToWBPCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodeWidgetToWBPCommandlet::Main(const FString& Params)
{
	FString WidgetClassPath;
	if (!CodeWidgetToWBPCommandlet::ReadCommandLineValue(Params, TEXT("WidgetClass"), WidgetClassPath))
	{
		CodeWidgetToWBPCommandlet::ReadCommandLineValue(Params, TEXT("NativeWidgetClass"), WidgetClassPath);
	}

	FString TargetAssetPath;
	if (!CodeWidgetToWBPCommandlet::ReadCommandLineValue(Params, TEXT("Target"), TargetAssetPath))
	{
		CodeWidgetToWBPCommandlet::ReadCommandLineValue(Params, TEXT("TargetAsset"), TargetAssetPath);
	}

	if (WidgetClassPath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=CodeWidgetToWBP -WidgetClass=/Script/Module.WidgetClass [-Target=/Game/_Game/Widgets/WidgetName/Main/WBP_Name] [-Replace] [-NoSave]"));
		return 1;
	}

	UClass* NativeWidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetClassPath);
	if (!NativeWidgetClass)
	{
		const FString ShortName = WidgetClassPath.Replace(TEXT("U"), TEXT(""));
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (Candidate && Candidate->IsChildOf(UUserWidget::StaticClass()) &&
				(Candidate->GetName() == WidgetClassPath || Candidate->GetName() == ShortName))
			{
				NativeWidgetClass = Candidate;
				break;
			}
		}
	}

	if (!NativeWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not resolve native widget class '%s'."), *WidgetClassPath);
		return 1;
	}

	if (TargetAssetPath.IsEmpty())
	{
		TargetAssetPath = CodeWidgetToWBPCommandlet::MakeDefaultTargetAssetPath(NativeWidgetClass);
		UE_LOG(LogTemp, Display, TEXT("No -Target supplied; using project default Widget Blueprint folder: %s"), *TargetAssetPath);
	}
	else if (!TargetAssetPath.StartsWith(CodeWidgetToWBPCommandlet::DefaultWidgetBlueprintFolder))
	{
		UE_LOG(LogTemp, Warning, TEXT("Project convention: generated Widget Blueprints should live under %s unless the caller explicitly needs another folder. Current target: %s"),
			CodeWidgetToWBPCommandlet::DefaultWidgetBlueprintFolder,
			*TargetAssetPath);
	}

	const bool bReplace = FParse::Param(*Params, TEXT("Replace"));
	const bool bSave = !FParse::Param(*Params, TEXT("NoSave"));
	FString Report;
	UWidgetBlueprint* WidgetBlueprint = UCodeWidgetToWBPBridgeLibrary::CreateOrUpdateWidgetBlueprintFromCode(
		NativeWidgetClass,
		TargetAssetPath,
		bReplace,
		bSave,
		Report);

	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_TO_WBP_RESULT asset=%s success=%s\n%s"),
		WidgetBlueprint ? *WidgetBlueprint->GetPathName() : TEXT("<none>"),
		WidgetBlueprint ? TEXT("true") : TEXT("false"),
		*Report);
	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_BRIDGE_STATUS command=CodeWidgetToWBP bridgeSuccess=%s fatalForBridge=%s errors=%d warnings=%d"),
		WidgetBlueprint ? TEXT("true") : TEXT("false"),
		WidgetBlueprint ? TEXT("false") : TEXT("true"),
		WidgetBlueprint ? 0 : 1,
		-1);

	return WidgetBlueprint ? 0 : 1;
}

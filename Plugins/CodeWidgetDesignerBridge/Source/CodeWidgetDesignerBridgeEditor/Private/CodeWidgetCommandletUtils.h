#pragma once

#include "Blueprint/UserWidget.h"
#include "CodeWidgetDesignerTreeProvider.h"
#include "Misc/Parse.h"
#include "UObject/UObjectIterator.h"

namespace CodeWidgetCommandletUtils
{
	inline bool ReadCommandLineValue(const FString& Params, const TCHAR* Key, FString& OutValue)
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

	inline UClass* ResolveNativeWidgetClass(const FString& WidgetClassPath)
	{
		UClass* NativeWidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetClassPath);
		if (NativeWidgetClass)
		{
			return NativeWidgetClass;
		}

		const FString ShortName = WidgetClassPath.Replace(TEXT("U"), TEXT(""));
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (Candidate && Candidate->IsChildOf(UUserWidget::StaticClass()) &&
				(Candidate->GetName() == WidgetClassPath || Candidate->GetName() == ShortName))
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	inline bool ReadWidgetClassAndTarget(const FString& Params, FString& OutWidgetClassPath, FString& OutTargetAssetPath)
	{
		if (!ReadCommandLineValue(Params, TEXT("WidgetClass"), OutWidgetClassPath))
		{
			ReadCommandLineValue(Params, TEXT("NativeWidgetClass"), OutWidgetClassPath);
		}

		if (!ReadCommandLineValue(Params, TEXT("Target"), OutTargetAssetPath))
		{
			ReadCommandLineValue(Params, TEXT("TargetAsset"), OutTargetAssetPath);
		}

		return !OutWidgetClassPath.IsEmpty();
	}

	inline ECodeWidgetDesignerGenerationMode ParseGenerationMode(const FString& Params)
	{
		if (FParse::Param(*Params, TEXT("CleanRebuild")))
		{
			return ECodeWidgetDesignerGenerationMode::CleanRebuild;
		}
		if (FParse::Param(*Params, TEXT("ValidateOnly")))
		{
			return ECodeWidgetDesignerGenerationMode::ValidateOnly;
		}
		if (FParse::Param(*Params, TEXT("Replace")) || FParse::Param(*Params, TEXT("ReplaceDesigner")))
		{
			return ECodeWidgetDesignerGenerationMode::ReplaceDesigner;
		}

		return ECodeWidgetDesignerGenerationMode::PreserveManual;
	}

	inline void LogBridgeStatus(
		const TCHAR* CommandName,
		const bool bBridgeSuccess,
		const bool bFatalForBridge,
		const int32 ErrorCount = INDEX_NONE,
		const int32 WarningCount = INDEX_NONE)
	{
		UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_BRIDGE_STATUS command=%s bridgeSuccess=%s fatalForBridge=%s errors=%d warnings=%d"),
			CommandName ? CommandName : TEXT("<unknown>"),
			bBridgeSuccess ? TEXT("true") : TEXT("false"),
			bFatalForBridge ? TEXT("true") : TEXT("false"),
			ErrorCount,
			WarningCount);
	}
}

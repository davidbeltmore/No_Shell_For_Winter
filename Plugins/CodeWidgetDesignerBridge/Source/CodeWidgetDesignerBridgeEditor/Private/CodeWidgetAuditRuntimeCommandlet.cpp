#include "CodeWidgetAuditRuntimeCommandlet.h"

#include "CodeWidgetCommandletUtils.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace CodeWidgetAuditRuntimeCommandlet
{
	static constexpr TCHAR DefaultWidgetRootPath[] = TEXT("/Game/_Game/Widgets");

	struct FAuditCandidate
	{
		FString PackageName;
		UClass* WidgetClass = nullptr;
		bool bDirectNativeParent = false;
		int32 Score = 0;
		TArray<FString> Reasons;
	};

	static bool GetGeneratedClassObjectPath(const FAssetData& AssetData, FString& OutClassObjectPath)
	{
		FString GeneratedClassExportPath;
		if (!AssetData.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassExportPath)
			&& !AssetData.GetTagValue(FName(TEXT("GeneratedClassPath")), GeneratedClassExportPath))
		{
			return false;
		}

		OutClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassExportPath);
		return !OutClassObjectPath.IsEmpty();
	}

	static FString InferPreferredWidgetSystemFolder(const FString& ContextName)
	{
		if (ContextName.Contains(TEXT("ProjectGameplayDebug")))
		{
			return TEXT("Debug");
		}
		if (ContextName.Contains(TEXT("ProjectEmote")))
		{
			return TEXT("Y");
		}
		if (ContextName.Contains(TEXT("ProjectInnerState")) || ContextName.Contains(TEXT("ProjectSurvivalNeeds")))
		{
			return TEXT("InnerState");
		}
		const bool bDoctrineContext = ContextName.Contains(TEXT("ProjectInnerDoctrine"))
			|| ContextName.Contains(TEXT("InnerDoctrine"));
		const bool bAltarExchangeContext =
			ContextName.Contains(TEXT("Exchange"))
			|| ContextName.Contains(TEXT("Altar"))
			|| ContextName.Contains(TEXT("DxpExchange"));
		if (bDoctrineContext && bAltarExchangeContext)
		{
			return TEXT("InnerDoctrineAltar");
		}
		if (bDoctrineContext || ContextName.Contains(TEXT("ProjectAttribute")))
		{
			return TEXT("Attributes");
		}
		if (ContextName.Contains(TEXT("ProjectChronicle")) || ContextName.Contains(TEXT("ActivityFeed")))
		{
			return TEXT("Chronicle");
		}
		if (ContextName.Contains(TEXT("Lockpick")))
		{
			return TEXT("LockPick");
		}
		return FString();
	}

	static bool PackageIsUnderSystemFolder(const FString& PackageName, const FString& SystemFolder)
	{
		return !SystemFolder.IsEmpty()
			&& PackageName.Contains(FString::Printf(TEXT("/%s/"), *SystemFolder));
	}

	static int32 ScoreCandidate(
		const FString& PackageName,
		const UClass* GeneratedClass,
		const UClass* NativeWidgetClass,
		const FString& ContextName,
		TArray<FString>& OutReasons)
	{
		int32 Score = 0;
		const FString PreferredSystemFolder = InferPreferredWidgetSystemFolder(ContextName);
		if (!PreferredSystemFolder.IsEmpty())
		{
			if (PackageIsUnderSystemFolder(PackageName, PreferredSystemFolder))
			{
				Score += 250000;
				OutReasons.Add(FString::Printf(TEXT("preferred system folder '%s'"), *PreferredSystemFolder));
			}
			else if (PackageName.StartsWith(DefaultWidgetRootPath))
			{
				Score -= 250000;
				OutReasons.Add(FString::Printf(TEXT("different system folder; preferred '%s'"), *PreferredSystemFolder));
			}
		}
		if (PackageName.Contains(TEXT("/Global/")))
		{
			Score += 5000;
			OutReasons.Add(TEXT("Global folder"));
		}
		else if (PackageName.Contains(TEXT("/Globals/")))
		{
			Score += 5000;
			OutReasons.Add(TEXT("Globals folder"));
		}
		if (PackageName.Contains(TEXT("Global")))
		{
			Score += 1500;
			OutReasons.Add(TEXT("asset name contains Global"));
		}
		if (PackageName.Contains(TEXT("/Main/")))
		{
			Score += 600;
			OutReasons.Add(TEXT("Main folder"));
		}
		if (GeneratedClass && NativeWidgetClass && GeneratedClass->GetSuperClass() == NativeWidgetClass)
		{
			Score += 500;
			OutReasons.Add(TEXT("direct native parent"));
		}
		if (!ContextName.IsEmpty() && PackageName.Contains(ContextName))
		{
			Score += 250;
			OutReasons.Add(TEXT("package contains context"));
		}
		return Score;
	}

	static FString EscapeAuditJsonString(const FString& Input)
	{
		FString Output = Input.Replace(TEXT("\\"), TEXT("\\\\"));
		Output.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Output.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Output.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Output.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Output;
	}

	static void AppendJsonStringArray(FString& Json, const TCHAR* Name, const TArray<FString>& Values)
	{
		Json += FString::Printf(TEXT("\"%s\": ["), Name);
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0)
			{
				Json += TEXT(", ");
			}
			Json += FString::Printf(TEXT("\"%s\""), *EscapeAuditJsonString(Values[Index]));
		}
		Json += TEXT("]");
	}

	static bool SaveAuditJson(
		const UClass* NativeWidgetClass,
		const FString& RootPath,
		const FString& ContextName,
		const TArray<FAuditCandidate>& Candidates,
		FString& OutReportPath)
	{
		const FString ReportDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodeWidgetDesignerBridge"), TEXT("Reports"));
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*ReportDirectory);

		const FString ContextSuffix = ContextName.IsEmpty() ? FString() : FString::Printf(TEXT("_%s"), *ContextName);
		FString FileName = FString::Printf(
			TEXT("RuntimeAudit_%s%s"),
			*GetNameSafe(NativeWidgetClass),
			*ContextSuffix);
		FileName = FPaths::MakeValidFileName(FileName);
		if (FileName.IsEmpty())
		{
			FileName = TEXT("RuntimeAudit");
		}

		OutReportPath = FPaths::Combine(ReportDirectory, FileName + TEXT(".json"));

		FString Json = TEXT("{");
		Json += FString::Printf(TEXT("\n  \"nativeClass\": \"%s\","), *EscapeAuditJsonString(GetNameSafe(NativeWidgetClass)));
		Json += FString::Printf(TEXT("\n  \"rootPath\": \"%s\","), *EscapeAuditJsonString(RootPath));
		Json += FString::Printf(TEXT("\n  \"context\": \"%s\","), *EscapeAuditJsonString(ContextName));
		Json += FString::Printf(TEXT("\n  \"candidateCount\": %d,"), Candidates.Num());
		const FString WinnerPackage = Candidates.IsEmpty() ? FString() : EscapeAuditJsonString(Candidates[0].PackageName);
		Json += FString::Printf(TEXT("\n  \"winnerPackage\": \"%s\","), *WinnerPackage);
		Json += TEXT("\n  \"candidates\": [");
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FAuditCandidate& Candidate = Candidates[Index];
			if (Index > 0)
			{
				Json += TEXT(",");
			}
			Json += TEXT("\n    {");
			Json += FString::Printf(TEXT("\"rank\": %d, "), Index + 1);
			Json += FString::Printf(TEXT("\"score\": %d, "), Candidate.Score);
			Json += FString::Printf(TEXT("\"class\": \"%s\", "), *EscapeAuditJsonString(GetNameSafe(Candidate.WidgetClass)));
			Json += FString::Printf(TEXT("\"package\": \"%s\", "), *EscapeAuditJsonString(Candidate.PackageName));
			Json += FString::Printf(TEXT("\"directParent\": %s, "), Candidate.bDirectNativeParent ? TEXT("true") : TEXT("false"));
			AppendJsonStringArray(Json, TEXT("reasons"), Candidate.Reasons);
			Json += TEXT("}");
		}
		Json += TEXT("\n  ]\n}\n");

		return FFileHelper::SaveStringToFile(Json, *OutReportPath);
	}
}

UCodeWidgetAuditRuntimeCommandlet::UCodeWidgetAuditRuntimeCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodeWidgetAuditRuntimeCommandlet::Main(const FString& Params)
{
	FString WidgetClassPath;
	FString TargetAssetPath;
	if (!CodeWidgetCommandletUtils::ReadWidgetClassAndTarget(Params, WidgetClassPath, TargetAssetPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=CodeWidgetAuditRuntime -WidgetClass=/Script/Module.WidgetClass [-Root=/Game/_Game/Widgets] [-Context=ProjectWidgetContext]"));
		return 1;
	}

	UClass* NativeWidgetClass = CodeWidgetCommandletUtils::ResolveNativeWidgetClass(WidgetClassPath);
	if (!NativeWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not resolve native widget class '%s'."), *WidgetClassPath);
		return 1;
	}

	FString RootPath;
	if (!CodeWidgetCommandletUtils::ReadCommandLineValue(Params, TEXT("Root"), RootPath) || RootPath.IsEmpty())
	{
		RootPath = CodeWidgetAuditRuntimeCommandlet::DefaultWidgetRootPath;
	}

	FString ContextName;
	CodeWidgetCommandletUtils::ReadCommandLineValue(Params, TEXT("Context"), ContextName);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	TArray<FString> RootPaths = { RootPath };
	AssetRegistry.ScanPathsSynchronous(RootPaths, true);

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*RootPath));
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TArray<CodeWidgetAuditRuntimeCommandlet::FAuditCandidate> Candidates;
	for (const FAssetData& AssetData : Assets)
	{
		FString GeneratedClassObjectPath;
		if (!CodeWidgetAuditRuntimeCommandlet::GetGeneratedClassObjectPath(AssetData, GeneratedClassObjectPath))
		{
			continue;
		}

		UClass* GeneratedClass = LoadObject<UClass>(nullptr, *GeneratedClassObjectPath);
		if (!GeneratedClass || !GeneratedClass->IsChildOf(NativeWidgetClass) || GeneratedClass == NativeWidgetClass)
		{
			continue;
		}

		CodeWidgetAuditRuntimeCommandlet::FAuditCandidate Candidate;
		Candidate.PackageName = AssetData.PackageName.ToString();
		Candidate.WidgetClass = GeneratedClass;
		Candidate.bDirectNativeParent = GeneratedClass->GetSuperClass() == NativeWidgetClass;
		Candidate.Score = CodeWidgetAuditRuntimeCommandlet::ScoreCandidate(Candidate.PackageName, GeneratedClass, NativeWidgetClass, ContextName, Candidate.Reasons);
		Candidates.Add(Candidate);
	}

	Candidates.Sort([](const CodeWidgetAuditRuntimeCommandlet::FAuditCandidate& Left, const CodeWidgetAuditRuntimeCommandlet::FAuditCandidate& Right)
	{
		if (Left.Score != Right.Score)
		{
			return Left.Score > Right.Score;
		}
		if (Left.bDirectNativeParent != Right.bDirectNativeParent)
		{
			return Left.bDirectNativeParent && !Right.bDirectNativeParent;
		}
		return Left.PackageName < Right.PackageName;
	});

	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_AUDIT_RUNTIME native=%s root=%s context=%s candidates=%d"),
		*NativeWidgetClass->GetName(),
		*RootPath,
		ContextName.IsEmpty() ? TEXT("<none>") : *ContextName,
		Candidates.Num());

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const CodeWidgetAuditRuntimeCommandlet::FAuditCandidate& Candidate = Candidates[Index];
		UE_LOG(LogTemp, Display, TEXT("%s rank=%d score=%d class=%s package=%s directParent=%s"),
			Index == 0 ? TEXT("WINNER") : TEXT("CANDIDATE"),
			Index + 1,
			Candidate.Score,
			*GetNameSafe(Candidate.WidgetClass),
			*Candidate.PackageName,
			Candidate.bDirectNativeParent ? TEXT("true") : TEXT("false"));
	}

	FString AuditReportPath;
	const bool bSavedAuditReport = CodeWidgetAuditRuntimeCommandlet::SaveAuditJson(
		NativeWidgetClass,
		RootPath,
		ContextName,
		Candidates,
		AuditReportPath);
	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_AUDIT_RUNTIME_JSON saved=%s path=%s"),
		bSavedAuditReport ? TEXT("true") : TEXT("false"),
		*AuditReportPath);
	CodeWidgetCommandletUtils::LogBridgeStatus(
		TEXT("CodeWidgetAuditRuntime"),
		!Candidates.IsEmpty(),
		Candidates.IsEmpty(),
		Candidates.IsEmpty() ? 1 : 0,
		0);

	return Candidates.IsEmpty() ? 1 : 0;
}

#include "CodeWidgetPreflightWBPCommandlet.h"

#include "CodeWidgetCommandletUtils.h"
#include "CodeWidgetToWBPBridgeLibrary.h"
#include "Misc/Parse.h"

UCodeWidgetPreflightWBPCommandlet::UCodeWidgetPreflightWBPCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodeWidgetPreflightWBPCommandlet::Main(const FString& Params)
{
	FString WidgetClassPath;
	FString TargetAssetPath;
	if (!CodeWidgetCommandletUtils::ReadWidgetClassAndTarget(Params, WidgetClassPath, TargetAssetPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=CodeWidgetPreflightWBP -WidgetClass=/Script/Module.WidgetClass [-Target=/Game/_Game/Widgets/System/Main/WBP_Name] [-Replace|-CleanRebuild|-ValidateOnly] [-Strict]"));
		return 1;
	}

	UClass* NativeWidgetClass = CodeWidgetCommandletUtils::ResolveNativeWidgetClass(WidgetClassPath);
	if (!NativeWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not resolve native widget class '%s'."), *WidgetClassPath);
		return 1;
	}

	FCodeWidgetDesignerValidationReport ValidationReport;
	FString Report;
	const bool bSuccess = UCodeWidgetToWBPBridgeLibrary::PreflightWidgetBlueprintConversion(
		NativeWidgetClass,
		TargetAssetPath,
		CodeWidgetCommandletUtils::ParseGenerationMode(Params),
		FParse::Param(*Params, TEXT("Strict")),
		ValidationReport,
		Report);

	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_PREFLIGHT_WBP_RESULT success=%s\n%s"), bSuccess ? TEXT("true") : TEXT("false"), *Report);
	CodeWidgetCommandletUtils::LogBridgeStatus(
		TEXT("CodeWidgetPreflightWBP"),
		bSuccess,
		!bSuccess,
		ValidationReport.Errors.Num(),
		ValidationReport.Warnings.Num());
	return bSuccess ? 0 : 1;
}

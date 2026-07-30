#include "CodeWidgetValidateWBPCommandlet.h"

#include "CodeWidgetCommandletUtils.h"
#include "CodeWidgetToWBPBridgeLibrary.h"

UCodeWidgetValidateWBPCommandlet::UCodeWidgetValidateWBPCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodeWidgetValidateWBPCommandlet::Main(const FString& Params)
{
	FString WidgetClassPath;
	FString TargetAssetPath;
	if (!CodeWidgetCommandletUtils::ReadWidgetClassAndTarget(Params, WidgetClassPath, TargetAssetPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=CodeWidgetValidateWBP -WidgetClass=/Script/Module.WidgetClass [-Target=/Game/_Game/Widgets/System/Main/WBP_Name]"));
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
	const bool bSuccess = UCodeWidgetToWBPBridgeLibrary::ValidateWidgetBlueprintConversion(
		NativeWidgetClass,
		TargetAssetPath,
		ValidationReport,
		Report);

	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_VALIDATE_WBP_RESULT success=%s\n%s"), bSuccess ? TEXT("true") : TEXT("false"), *Report);
	CodeWidgetCommandletUtils::LogBridgeStatus(
		TEXT("CodeWidgetValidateWBP"),
		bSuccess,
		!bSuccess,
		ValidationReport.Errors.Num(),
		ValidationReport.Warnings.Num());
	return bSuccess ? 0 : 1;
}

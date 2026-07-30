#include "CodeWidgetCleanRebuildCommandlet.h"

#include "CodeWidgetCommandletUtils.h"
#include "CodeWidgetToWBPBridgeLibrary.h"
#include "Misc/Parse.h"

UCodeWidgetCleanRebuildCommandlet::UCodeWidgetCleanRebuildCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCodeWidgetCleanRebuildCommandlet::Main(const FString& Params)
{
	FString WidgetClassPath;
	FString TargetAssetPath;
	if (!CodeWidgetCommandletUtils::ReadWidgetClassAndTarget(Params, WidgetClassPath, TargetAssetPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: -run=CodeWidgetCleanRebuild -WidgetClass=/Script/Module.WidgetClass [-Target=/Game/_Game/Widgets/System/Main/WBP_Name] [-NoSave]"));
		return 1;
	}

	UClass* NativeWidgetClass = CodeWidgetCommandletUtils::ResolveNativeWidgetClass(WidgetClassPath);
	if (!NativeWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Could not resolve native widget class '%s'."), *WidgetClassPath);
		return 1;
	}

	FString Report;
	const bool bSuccess = UCodeWidgetToWBPBridgeLibrary::CreateOrUpdateWidgetBlueprintsFromManifest(
		NativeWidgetClass,
		TargetAssetPath,
		ECodeWidgetDesignerGenerationMode::CleanRebuild,
		!FParse::Param(*Params, TEXT("NoSave")),
		Report);

	UE_LOG(LogTemp, Display, TEXT("CODE_WIDGET_CLEAN_REBUILD_RESULT success=%s\n%s"), bSuccess ? TEXT("true") : TEXT("false"), *Report);
	CodeWidgetCommandletUtils::LogBridgeStatus(TEXT("CodeWidgetCleanRebuild"), bSuccess, !bSuccess);
	return bSuccess ? 0 : 1;
}

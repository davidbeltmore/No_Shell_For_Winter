#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"
#include "CodeWidgetDesignerTreeProvider.generated.h"

class UUserWidget;
class UWidgetTree;

UENUM(BlueprintType)
enum class ECodeWidgetDesignerAssetRole : uint8
{
	Host UMETA(DisplayName = "Host"),
	GlobalPanel UMETA(DisplayName = "Global Panel"),
	GlobalTemplate UMETA(DisplayName = "Global Template"),
	Individual UMETA(DisplayName = "Individual"),
	State UMETA(DisplayName = "State"),
	Asset UMETA(DisplayName = "Asset"),
	MainBase UMETA(DisplayName = "Main Base"),
	NativeFallback UMETA(DisplayName = "Native Fallback")
};

UENUM(BlueprintType)
enum class ECodeWidgetDesignerGenerationMode : uint8
{
	PreserveManual UMETA(DisplayName = "Preserve Manual Designer"),
	ReplaceDesigner UMETA(DisplayName = "Replace Designer"),
	CleanRebuild UMETA(DisplayName = "Clean Rebuild"),
	ValidateOnly UMETA(DisplayName = "Validate Only")
};

USTRUCT(BlueprintType)
struct CODEWIDGETDESIGNERBRIDGE_API FCodeWidgetDesignerChildWidgetSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TSubclassOf<UUserWidget> WidgetClass;

	// Folder below the parent widget system root. Use values such as Main, Global,
	// Normal, Expanded, Prompt, Cards, or a row-family folder to keep generated assets organized.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString RelativeFolder;

	// Designer-facing asset name for global panel widgets, fixed rows, or variants
	// when the native class name is only an implementation detail.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString AssetNameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	ECodeWidgetDesignerAssetRole Role = ECodeWidgetDesignerAssetRole::Individual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FName PriorityGroup = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	int32 PriorityRank = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bRuntimeDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bDesignerOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bRequiresStableRootWrapper = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FName> ExpectedWidgetNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FName> ExpectedBlueprintEvents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FName> ExpectedVisualStates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FString> ExpectedPreviewTexts;
};

USTRUCT(BlueprintType)
struct CODEWIDGETDESIGNERBRIDGE_API FCodeWidgetDesignerWidgetAssetSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TSubclassOf<UUserWidget> WidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString TargetAssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString RelativeFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString AssetNameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	ECodeWidgetDesignerAssetRole Role = ECodeWidgetDesignerAssetRole::Individual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FName PriorityGroup = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	int32 PriorityRank = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bRuntimeDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bDesignerOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bRequiresStableRootWrapper = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FName> ExpectedWidgetNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FName> ExpectedBlueprintEvents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FName> ExpectedVisualStates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FString> ExpectedPreviewTexts;
};

USTRUCT(BlueprintType)
struct CODEWIDGETDESIGNERBRIDGE_API FCodeWidgetDesignerBuildContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString TargetAssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString RelativeFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString AssetNameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	ECodeWidgetDesignerAssetRole Role = ECodeWidgetDesignerAssetRole::Individual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FName PriorityGroup = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	int32 PriorityRank = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	bool bRuntimeDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FString> ExpectedPreviewTexts;
};

USTRUCT(BlueprintType)
struct CODEWIDGETDESIGNERBRIDGE_API FCodeWidgetDesignerConversionManifest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString SystemName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString RootPath = TEXT("/Game/_Game/Widgets");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString MainFolder = TEXT("Main");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FString GlobalFolder = TEXT("Global");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FString> AssetFolders;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	FCodeWidgetDesignerWidgetAssetSpec HostWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	TArray<FCodeWidgetDesignerWidgetAssetSpec> WidgetAssets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Widget Designer Bridge")
	ECodeWidgetDesignerGenerationMode GenerationMode = ECodeWidgetDesignerGenerationMode::PreserveManual;
};

USTRUCT(BlueprintType)
struct CODEWIDGETDESIGNERBRIDGE_API FCodeWidgetDesignerValidationReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	bool bPassed = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	FString SystemName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	FString RootPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> Errors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> Warnings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> Info;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> ExpectedAssets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> ExistingAssets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> MissingAssets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> StaleDuplicateAssets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Code Widget Designer Bridge")
	TArray<FString> RuntimeWinners;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UCodeWidgetDesignerTreeProvider : public UInterface
{
	GENERATED_BODY()
};

class CODEWIDGETDESIGNERBRIDGE_API ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	virtual void SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext)
	{
	}

	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
	{
		return false;
	}

	virtual bool GatherCodeWidgetDesignerConversionManifest(FCodeWidgetDesignerConversionManifest& OutManifest) const
	{
		return false;
	}

	virtual void GatherCodeWidgetDesignerChildWidgetClasses(TArray<TSubclassOf<UUserWidget>>& OutWidgetClasses) const
	{
	}

	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
	{
		TArray<TSubclassOf<UUserWidget>> WidgetClasses;
		GatherCodeWidgetDesignerChildWidgetClasses(WidgetClasses);

		for (const TSubclassOf<UUserWidget>& WidgetClass : WidgetClasses)
		{
			FCodeWidgetDesignerChildWidgetSpec Spec;
			Spec.WidgetClass = WidgetClass;
			OutWidgetSpecs.Add(Spec);
		}
	}
};

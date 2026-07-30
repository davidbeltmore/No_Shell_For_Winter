#pragma once

#include "CoreMinimal.h"
#include "EFProjectThemedUserWidget.h"
#include "UI/ProjectEmoteMenuOptionWidget.h"
#include "UI/ProjectEmoteMenuWidget.h"
#include "ProjectGameplayDebugMenuWidget.generated.h"

class UWidgetTree;

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMenuOptionRowWidget : public UProjectEmoteMenuOptionWidget
{
	GENERATED_BODY()

public:
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;

protected:
	virtual void RefreshOptionData() override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMenuOptionRowGlobalWidget : public UProjectGameplayDebugMenuOptionRowWidget
{
	GENERATED_BODY()
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMenuWidget : public UProjectEmoteMenuWidget
{
	GENERATED_BODY()

public:
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
	virtual bool GatherCodeWidgetDesignerConversionManifest(FCodeWidgetDesignerConversionManifest& OutManifest) const override;
	virtual void GatherCodeWidgetDesignerChildWidgetClasses(TArray<TSubclassOf<UUserWidget>>& OutWidgetClasses) const override;
	virtual void GatherCodeWidgetDesignerChildWidgetSpecs(TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const override;

protected:
	virtual TSubclassOf<UProjectEmoteMenuOptionWidget> ResolveOptionRowWidgetClass() const override;
	virtual float ResolveOptionHeight() const override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMenuGlobalWidget : public UProjectGameplayDebugMenuWidget
{
	GENERATED_BODY()
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMenuRowsGlobalWidget : public UEFProjectThemedUserWidget, public ICodeWidgetDesignerTreeProvider
{
	GENERATED_BODY()

public:
	virtual bool BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree) override;
};

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugRootDebugRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugRootTestRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugRootCancelRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugImmediateDefeatRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugDownedModeRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugRestoreAcfHealthRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugRestoreNeedsSensationsRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugSetTo100RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugSetTo50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugSetTo0RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugApplyStatusRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugBackRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMadness100RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCurse100RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugPain100RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugHunger50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugThirst50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugSleep50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugMadness50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCurse50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugPain50RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugHunger0RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugThirst0RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugSleep0RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusStarvingRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusThirstRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusSleepDeprivedRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusExhaustedRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusExhaustedRecoveryRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusFrenzyRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusCursedRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusExtremePainRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusGraceStepRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusKnockedOutRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusBleedingRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusDizzyRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugStatusFearRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugRuntimeFpsBenchmarkRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugFullStackOverloadBenchmarkRowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugWillpowerLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugOffensiveLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugDefensiveLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugFaithLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCunningLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCelerityLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCharismaLevel5RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugWillpowerLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugOffensiveLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugDefensiveLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugFaithLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCunningLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCelerityLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };
UCLASS(BlueprintType, Blueprintable)
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectGameplayDebugCharismaLevel10RowWidget : public UProjectGameplayDebugMenuOptionRowWidget { GENERATED_BODY() };

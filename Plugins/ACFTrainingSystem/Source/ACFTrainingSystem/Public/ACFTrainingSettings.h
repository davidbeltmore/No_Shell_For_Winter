#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ACFTrainingTypes.h"
#include "ACFTrainingSettings.generated.h"

class UGameplayEffect;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ACF Training System"))
class ACFTRAININGSYSTEM_API UACFTrainingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UACFTrainingSettings();

	static TArray<FACFTrainingDefinition> MakeDefaultTrainingDefinitions();
	static FACFTrainingFutureRequirements MakeDefaultFutureRequirements();

	const TArray<FACFTrainingDefinition>& GetTrainingDefinitions() const;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ACF Training")
	bool bUseBuiltInDefinitionsWhenEmpty;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ACF Training|ACF")
	TSoftClassPtr<UGameplayEffect> AttributeModifierGameplayEffect;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, meta = (TitleProperty = "TrainingId"), Category = "ACF Training")
	TArray<FACFTrainingDefinition> TrainingDefinitions;

	virtual FName GetCategoryName() const override;
};

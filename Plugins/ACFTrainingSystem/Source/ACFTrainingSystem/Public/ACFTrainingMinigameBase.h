#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACFTrainingTypes.h"
#include "ACFTrainingMinigameBase.generated.h"

class UACFTrainingComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FACFTrainingMinigameResultSignature, UACFTrainingMinigameBase*, Minigame, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FACFTrainingMinigameCancelledSignature, UACFTrainingMinigameBase*, Minigame);

UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class ACFTRAININGSYSTEM_API UACFTrainingMinigameBase : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "ACF Training|Minigame")
	virtual void InitializeMinigame(UACFTrainingComponent* InTrainingComponent, const FACFTrainingDefinition& InTrainingDefinition);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ACF Training|Minigame")
	void StartMinigame();
	virtual void StartMinigame_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ACF Training|Minigame")
	void CancelMinigame();
	virtual void CancelMinigame_Implementation();

	UFUNCTION(BlueprintCallable, Category = "ACF Training|Minigame")
	void SubmitResult(bool bSuccess);

	UFUNCTION(BlueprintPure, Category = "ACF Training|Minigame")
	UACFTrainingComponent* GetTrainingComponent() const;

	UFUNCTION(BlueprintPure, Category = "ACF Training|Minigame")
	FACFTrainingDefinition GetTrainingDefinition() const;

	UPROPERTY(BlueprintAssignable, Category = "ACF Training|Minigame")
	FACFTrainingMinigameResultSignature OnMinigameResult;

	UPROPERTY(BlueprintAssignable, Category = "ACF Training|Minigame")
	FACFTrainingMinigameCancelledSignature OnMinigameCancelled;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UACFTrainingComponent> TrainingComponent;

	UPROPERTY(Transient)
	FACFTrainingDefinition TrainingDefinition;
};

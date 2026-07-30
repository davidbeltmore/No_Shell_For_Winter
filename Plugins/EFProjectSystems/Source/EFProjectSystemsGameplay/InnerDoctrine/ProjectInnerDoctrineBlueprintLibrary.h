#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"
#include "ProjectInnerDoctrineBlueprintLibrary.generated.h"

class UProjectInnerDoctrineComponent;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerDoctrineBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine", meta = (DefaultToSelf = "Actor"))
	static UProjectInnerDoctrineComponent* FindInnerDoctrineComponent(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine", meta = (DefaultToSelf = "Actor"))
	static int32 GrantDoctrineDxp(
		AActor* Actor,
		FName ReasonId,
		int32 Amount,
		EProjectDoctrineExperienceSource Source = EProjectDoctrineExperienceSource::Utility);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine", meta = (DefaultToSelf = "Actor"))
	static bool SpendDoctrineDxpOnAttribute(AActor* Actor, EProjectDoctrineAttribute Attribute);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine", meta = (DefaultToSelf = "Actor"))
	static int32 WithdrawDoctrineMetaDxp(AActor* Actor, int32 RequestedAmount);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI", meta = (DefaultToSelf = "Actor"))
	static bool OpenInnerDoctrineExchangeMenu(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Inner Doctrine|UI", meta = (DefaultToSelf = "Actor"))
	static void CloseInnerDoctrineExchangeMenu(AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Inner Doctrine|UI", meta = (DefaultToSelf = "Actor"))
	static bool IsInnerDoctrineExchangeMenuOpen(AActor* Actor);
};

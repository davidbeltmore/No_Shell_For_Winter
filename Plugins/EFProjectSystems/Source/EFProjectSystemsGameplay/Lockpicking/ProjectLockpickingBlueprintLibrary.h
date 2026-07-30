#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProjectLockpickingBlueprintLibrary.generated.h"

class AActor;
class APawn;
class UProjectLockpickableComponent;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectLockpickingBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Lockpicking")
	static UProjectLockpickableComponent* FindLockpickableComponent(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|ACF")
	static bool ConsumeActorACFInteractionIfLocked(AActor* Actor, APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintCallable, Category = "Lockpicking|ACF")
	static bool ConsumeActorACFLocalInteractionIfLocked(AActor* Actor, APawn* Pawn, const FString& InteractionType);

	UFUNCTION(BlueprintPure, Category = "Lockpicking|ACF")
	static bool ShouldAllowActorACFInteraction(AActor* Actor, APawn* Pawn, bool bOriginalCanInteract = true);
};

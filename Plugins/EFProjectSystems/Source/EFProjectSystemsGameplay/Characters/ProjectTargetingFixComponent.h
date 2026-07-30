#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectTargetingFixComponent.generated.h"

class APlayerController;
class APawn;
class UActorComponent;
class UProjectEnemyTargetInfoComponent;
class UProjectSocialCardWidget;
class UProjectTargetLevelWidget;
class USceneComponent;

UCLASS(ClassGroup = (Project), meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectTargetingFixComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectTargetingFixComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	bool HasResolvedTargetingComponent();

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	AActor* GetCurrentTargetActor() const;

	UFUNCTION(BlueprintCallable, Category = "Project|Target|Debug")
	bool DebugSetCurrentTargetActor(AActor* TargetActor);

	/** Re-establishes a target temporarily released for a project-owned interaction. */
	bool RestoreCurrentTargetActor(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	bool DeactivateCurrentTargetingLock();

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	bool ShowSocialCardForActor(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Project|Target")
	void HideSocialCard();

	UFUNCTION(BlueprintPure, Category = "Project|Target")
	bool IsSocialCardVisible() const;

private:
	bool EnsureRuntimeContext();
	UActorComponent* ResolveTargetingComponent();
	bool TryRepairCurrentTarget(UActorComponent* TargetingComponent, AActor*& OutTargetActor, USceneComponent*& OutTargetPointComponent);
	void EnsureScreenWidget(APlayerController* PlayerController);
	void UpdateScreenWidget(AActor* TargetActor, USceneComponent* TargetPointComponent);
	UProjectSocialCardWidget* EnsureSocialCardWidget(APlayerController* PlayerController);
	bool RefreshSocialCardForActor(AActor* TargetActor, bool bManualRequest);
	bool ShouldShowSocialCard() const;
	void ShowTargetInfoForActor(AActor* TargetActor);
	void HideTargetInfoForActor(AActor* TargetActor);
	void HideScreenWidget();
	bool IsValidTargetPointForActor(const AActor* TargetActor, const USceneComponent* TargetPointComponent) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<APawn> CachedOwnerPawn;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> CachedPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<UActorComponent> CachedTargetingComponent;

	UPROPERTY(Transient)
	TObjectPtr<UProjectTargetLevelWidget> LevelWidget;

	UPROPERTY(Transient)
	TObjectPtr<UProjectSocialCardWidget> SocialCardWidget;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CachedTargetActor;

	bool bManualSocialCardPreview = false;
};

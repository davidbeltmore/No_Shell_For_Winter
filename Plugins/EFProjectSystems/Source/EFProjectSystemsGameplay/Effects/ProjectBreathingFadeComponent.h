#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProjectBreathingFadeComponent.generated.h"

class UMeshComponent;
class UChildActorComponent;
class UTexture;
struct FPropertyChangedEvent;

UCLASS(ClassGroup = (Project), meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectBreathingFadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectBreathingFadeComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Project|Effects|Breathing Fade")
	void StartBreathingFade();

	UFUNCTION(BlueprintCallable, Category = "Project|Effects|Breathing Fade")
	void StopBreathingFade(bool bRestoreFullIntensity = true);

	UFUNCTION(BlueprintCallable, Category = "Project|Effects|Breathing Fade")
	void RestartBreathingFade();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	bool bUseSmoothFade = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade", meta = (ClampMin = "0.0"))
	float VisibleHoldSeconds = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade", meta = (ClampMin = "0.0"))
	float InvisibleHoldSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	FName TargetComponentName = TEXT("FogCard");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	FName TargetChildActorComponentName = TEXT("HeavyBreathing");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	FName FogDensityParameterName = TEXT("Fog Density");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade")
	bool bUseSceneChildTemplateSettings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade", meta = (ClampMin = "0.0"))
	float InvisibleFogDensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Effects|Breathing Fade", meta = (ClampMin = "0.0"))
	float VisibleFogDensity = 1.0f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	enum class EBreathingFadePhase : uint8
	{
		FadeIn,
		VisibleHold,
		FadeOut,
		InvisibleHold
	};

	UMeshComponent* ResolveFogMesh();
	UMeshComponent* ResolveFogMeshOnActor(AActor* Actor) const;
	UChildActorComponent* ResolveTargetChildActorComponent() const;
	UChildActorComponent* ResolveOwningChildActorComponent(const UMeshComponent* MeshComponent) const;
	AActor* ResolveSceneChildTemplateActor(const UMeshComponent* MeshComponent) const;
	void SyncFromSceneChildTemplate(UMeshComponent* MeshComponent);
	void CopyEditableFogSettings(AActor* TemplateActor, AActor* RuntimeActor, UMeshComponent* RuntimeMesh);
	bool ShouldCopyTemplateProperty(const FProperty* Property) const;
	void ApplyCopiedPropertyToFogMaterial(const FProperty* Property, const void* SourceContainer, UMeshComponent* RuntimeMesh);
	void ApplyTextureParameterToFogMaterials(UMeshComponent* RuntimeMesh, FName ParameterName, UTexture* Texture);
	void CopyTemplateMaterials(AActor* TemplateActor, UMeshComponent* RuntimeMesh);
	void InvokeFogRefreshFunction(AActor* RuntimeActor);
	bool TryReadFloatProperty(const UObject* Object, FName PropertyName, float& OutValue) const;
	void ApplyCurrentFogDensity();
	void ApplyFogDensity(float FogDensity);
	void AdvancePhase();
	float GetCurrentPhaseDuration() const;
	float CalculateCurrentAlpha() const;
	float ShapeAlpha(float Alpha) const;
	float CalculateCurrentFogDensity() const;
	float GetVisibleFogDensity() const;
	bool ComponentNameMatches(const UMeshComponent* Component) const;
	bool ChildActorComponentNameMatches(const UChildActorComponent* Component) const;

	TWeakObjectPtr<UMeshComponent> FogMesh;
	TWeakObjectPtr<UChildActorComponent> ChildActorComponent;
	EBreathingFadePhase Phase = EBreathingFadePhase::FadeIn;
	float PhaseTimeSeconds = 0.0f;
	float ActiveVisibleFogDensity = 1.0f;
	bool bIsRunning = false;
	bool bHasActiveVisibleFogDensity = false;
	bool bTemplateSettingsSynced = false;
};

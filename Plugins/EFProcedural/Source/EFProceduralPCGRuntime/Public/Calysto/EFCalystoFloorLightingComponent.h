#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Calysto/EFCalystoLightingResolver.h"
#include "EFCalystoFloorLightingComponent.generated.h"

class UPointLightComponent;

struct FEFCalystoNativeLightBinding
{
	FGuid Id;
	TWeakObjectPtr<AActor> Torch;
	TWeakObjectPtr<UPointLightComponent> Light;
};

/** One transient floor owner controls exact PCG-owned native lights. No per-torch MID or tick owner. */
UCLASS(Transient, NotBlueprintable)
class EFPROCEDURALPCGRUNTIME_API UEFCalystoFloorLightingComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	UEFCalystoFloorLightingComponent();
	bool Initialize(const FEFCalystoRandomKey& Key, const FEFCalystoResolvedLighting& Lighting,
		TConstArrayView<FEFCalystoNativeLightBinding> NativeLights, FString& Error);
	bool Verify(FString& Error) const;
	void Release();
	int32 GetBoundLightCount() const { return Bindings.Num(); }
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FEFCalystoNativeLightingControllerTest;
	struct FBinding
	{
		FEFCalystoNativeLightBinding Native;
		float Baseline = 0;
		float Expected = 0;
	};
	bool ApplyAtElapsed(double Seconds, FString& Error);
	TArray<FBinding> Bindings;
	FEFCalystoRandomKey Random;
	FEFCalystoResolvedLighting Resolved;
	double StartedAt = 0.0;
	double LastElapsed = 0.0;
	bool bInitialized = false;
	FString Failure;
};

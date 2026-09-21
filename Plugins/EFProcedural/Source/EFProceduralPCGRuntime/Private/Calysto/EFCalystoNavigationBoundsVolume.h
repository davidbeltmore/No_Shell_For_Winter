#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EFCalystoNavigationBoundsVolume.generated.h"

class UBoxComponent;
class UNavigationSystemV1;
class ANavigationData;

/** Runtime-only nav bounds whose extent exists without an editor-built BSP brush. */
UCLASS(NotBlueprintable, Transient)
class AEFCalystoNavigationBoundsVolume final : public ANavMeshBoundsVolume
{
	GENERATED_BODY()

public:
	AEFCalystoNavigationBoundsVolume(const FObjectInitializer& ObjectInitializer);
	bool SetGeneratedGeometryBounds(const FBox& WorldBounds);
	void ObserveNavigationCompletion(UNavigationSystemV1* Navigation, ANavigationData* RelevantData,
		const FNavAgentProperties* QueryAgent = nullptr, const FVector& QueryOrigin = FVector::ZeroVector);
	void StopObservingNavigationCompletion();
	uint64 GetNavigationCompletionRevision() const { return NavigationCompletionRevision; }
	double GetLastNavigationCompletionSeconds() const { return LastNavigationCompletionSeconds; }
	virtual void Destroyed() override;

private:
	UFUNCTION()
	void NavigationGenerationFinished(ANavigationData* Data);
	TWeakObjectPtr<UNavigationSystemV1> ObservedNavigation;
	TWeakObjectPtr<ANavigationData> ObservedData;
	FNavAgentProperties ObservedAgent;
	FVector ObservedQueryOrigin = FVector::ZeroVector;
	bool bResolveQueryAgent = false;
	uint64 NavigationCompletionRevision = 0;
	double LastNavigationCompletionSeconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> GeneratedGeometryBounds;
};

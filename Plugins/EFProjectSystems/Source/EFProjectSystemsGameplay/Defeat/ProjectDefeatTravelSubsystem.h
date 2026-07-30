#pragma once

#include "CoreMinimal.h"
#include "Defeat/ProjectDefeatInventoryBridge.h"
#include "Defeat/ProjectDefeatTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectDefeatTravelSubsystem.generated.h"

class UObject;
class UProjectDefeatFlowSettings;
class UWorld;

UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectDefeatTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool BeginDefeatedTravel(UObject* WorldContextObject, const FProjectDefeatTransferPayload& Payload, const FProjectDefeatInventorySnapshot& RetainedInventorySnapshot);
	bool HasPendingTransfer() const;
	void ClearPendingTransfer();

private:
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void PollArrivalReady();
	bool ShouldUseSameMapFastPath(UObject* WorldContextObject, const UProjectDefeatFlowSettings& Settings) const;

private:
	FDelegateHandle PostLoadMapHandle;
	FTimerHandle ArrivalPollTimerHandle;
	TWeakObjectPtr<UWorld> PendingWorld;
	FProjectDefeatTransferPayload PendingPayload;
	FProjectDefeatInventorySnapshot PendingRetainedInventorySnapshot;
	bool bHasPendingTransfer = false;
};

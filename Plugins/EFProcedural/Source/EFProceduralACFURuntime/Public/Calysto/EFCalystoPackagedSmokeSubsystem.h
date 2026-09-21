#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Calysto/EFCalystoPopulationPlannerV6.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCalystoPackagedSmokeSubsystem.generated.h"

class AActor;
class UEFCalystoDungeonSubsystem;
class UWorld;

/**
 * Opt-in cooked-build acceptance driver for the definitive Calysto V6 runtime.
 *
 * The driver enters through the real DoorToLevel actor, restarts the accepted
 * dungeon world with an explicit seed, traverses real generated floor doors,
 * and writes project-owned evidence. It never loads an object synchronously.
 */
UCLASS()
class EFPROCEDURALACFURUNTIME_API UEFCalystoPackagedSmokeSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 ProjectTelemetrySchemaVersion = 2;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Persists the exact transition trace emitted by PCGRuntime. */
	bool RecordRuntimeReadinessTrace(
		UWorld* World,
		int64 FloorNumber,
		int64 GenerationSerial,
		const TArray<FName>& ReadinessTrace);

private:
	struct FReadyFloorRecord
	{
		int32 SchemaVersion = 6;
		int32 GeneratorVersion = 6;
		int64 FloorNumber = 0;
		int64 GenerationSerial = 0;
		int32 PCGSeed = 0;
		FName StyleId = NAME_None;
		FIntVector DungeonSize = FIntVector::ZeroValue;
		int32 RoomCount = 0;
		int32 EligibleRoomCount = 0;
		int32 ThemedRoomCount = 0;
		TArray<FName> RoomThemeIds;
		int32 CandidateAnchorCount = 0;
		int32 ActorDecisionCount = 0;
		int32 ChestContentDecisionCount = 0;
		int32 EnemyCount = 0;
		int32 LooseFoodCount = 0;
		int32 ChestCount = 0;
		int32 LootActorCount = 0;
		int32 SpecialEventCount = 0;
		int32 SpawnedActorCount = 0;
		float RealizedThreatCost = 0.0f;
		float RealizedResourceCost = 0.0f;
		FString PolicyHash;
		FString EcologyHash;
		FString IntentHash;
		FString FloorPlanHash;
		FString RoomManifestHash;
		FString PopulationPlanHash;
		FString AnchorTopologyHash;
		FString CompanionSnapshotHash;
		FString RealizedManifestHash;
	};

	bool ConfigureFromCommandLine(FString& OutError);
	bool InitializeProjectTelemetry(FString& OutError);
	bool AppendProjectTelemetry(const FString& EventPayload);
	bool AppendReadyFloorProjectTelemetry(
		const FReadyFloorRecord& Record,
		const FEFCalystoRoomManifestV6& RoomManifest);
	bool HandleBootstrapTick(float DeltaTime);
	bool HandleDoorSelectionTick(float DeltaTime);
	bool HandleTimeoutTick(float DeltaTime);
	bool HandleExitTick(float DeltaTime);
	void HandleFloorReady(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV6& Intent,
		const FEFCalystoRealizedFloorManifestV6& Manifest);
	void HandleFloorTravelFailed();
	bool ValidateEntryProbe(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV6& Intent,
		const FEFCalystoRealizedFloorManifestV6& Manifest,
		FString& OutError) const;
	bool ValidateReadyFloor(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV6& Intent,
		const FEFCalystoRealizedFloorManifestV6& Manifest,
		const FEFCalystoRoomManifestV6& RoomManifest,
		const FEFCalystoPopulationPlanV6& PopulationPlan,
		FString& OutError) const;
	bool IsConfiguredDungeonWorld(const UWorld* World) const;
	AActor* FindUniqueEntranceDoor(UWorld* World, FString& OutError) const;
	bool TrySelectDoor(UWorld* World, AActor* Door, FString& OutError);
	void ScheduleFloorDoorInspection();
	void Finish(bool bSuccess, const FString& Reason);
	bool WriteReceipt(bool bSuccess, const FString& Reason, double ElapsedSeconds);
	void CancelTicker(FTSTicker::FDelegateHandle& Handle);

	UPROPERTY(Transient)
	TObjectPtr<UEFCalystoDungeonSubsystem> DungeonSubsystem;

	int64 RunSeed = 202609040006LL;
	int64 ExpectedFloor = 1;
	int64 PreviousGenerationSerial = -1;
	int32 MaximumFloor = 10;
	int32 CompletedFloorCount = 0;
	int32 FloorDoorInteractionCount = 0;
	int32 ForcedDungeonEdge = 0;
	FName Scenario = TEXT("Natural");
	FString ConfigurationName;
	FString RunTag;
	FString ScreenshotPath;
	FString ReceiptPath;
	FString ProjectTelemetryPath;
	FString EntranceWorldPath;
	TArray<FReadyFloorRecord> ReadyFloorRecords;
	double StartedAtSeconds = 0.0;
	double EntranceDoorInteractionAtSeconds = 0.0;
	double DoorSelectionStartedAtSeconds = 0.0;
	double DoorInspectionNotBeforeSeconds = 0.0;
	float TimeoutSeconds = 360.0f;
	bool bCaptureVisual = false;
	bool bScreenshotRequested = false;
	bool bAuthorityValidated = false;
	bool bEntryDoorPositioned = false;
	bool bEntryDoorSelected = false;
	bool bEntryDoorInteracted = false;
	bool bDungeonWorldObserved = false;
	bool bEntryProbeAccepted = false;
	bool bSeededRunRequested = false;
	bool bFloorDoorPositioned = false;
	bool bFinished = false;
	bool bFinalSuccess = false;
	bool bProjectTelemetryInitialized = false;
	bool bProjectTelemetryHealthy = false;
	uint64 ProjectTelemetrySequence = 0;
	int32 EntryProbeReadinessTraceCount = 0;
	int32 ProjectTelemetryReadySequenceCount = 0;
	int32 ProjectTelemetryEntryDoorSelectedCount = 0;
	int32 ProjectTelemetryEntryDoorInteractedCount = 0;
	int32 ProjectTelemetryFloorDoorSelectedCount = 0;
	int32 ProjectTelemetryFloorDoorInteractedCount = 0;
	int32 ProjectTelemetryFailureEventCount = 0;
	int32 ProjectTelemetryCompleteEventCount = 0;

	FTSTicker::FDelegateHandle BootstrapTickerHandle;
	FTSTicker::FDelegateHandle DoorSelectionTickerHandle;
	FTSTicker::FDelegateHandle TimeoutTickerHandle;
	FTSTicker::FDelegateHandle ExitTickerHandle;
};

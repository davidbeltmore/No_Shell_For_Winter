#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCalystoPackagedSmokeSubsystem.generated.h"

class UEFCalystoDungeonSubsystem;
class UWorld;

/**
 * Explicit command-line acceptance driver for cooked builds.
 *
 * It is never created during normal play. Shipping can exercise only a natural,
 * seeded run through the real ACF floor door. Exact population scenarios and the
 * forced size used to prove their caps remain compiled exclusively in Development.
 */
UCLASS()
class EFPROCEDURALACFURUNTIME_API UEFCalystoPackagedSmokeSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Stable, project-owned telemetry contract used when installed-engine Shipping
	// builds compile UE_LOG out. Keep this independent from Engine log settings.
	static constexpr int32 ProjectTelemetrySchemaVersion = 1;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Persists the exact milestone trace captured by PCGRuntime at the transitions. */
	bool RecordRuntimeReadinessTrace(
		UWorld* World,
		int64 FloorNumber,
		int64 GenerationSerial,
		const TArray<FName>& ReadinessTrace);

private:
	struct FReadyFloorRecord
	{
		int32 GeneratorVersion = 4;
		int64 FloorNumber = 0;
		int64 GenerationSerial = 0;
		int32 PCGSeed = 0;
		EEFCalystoStyleV4 Style = EEFCalystoStyleV4::Standard;
		EEFCalystoThemeV4 Theme = EEFCalystoThemeV4::Default;
		FIntVector DungeonSize = FIntVector::ZeroValue;
		int32 CandidateAnchorCount = 0;
		int32 EnemyCount = 0;
		int32 NPCCount = 0;
		int32 FoodCount = 0;
		int32 ChestCount = 0;
		int32 LooseLootCount = 0;
		int32 ClothingCount = 0;
		int32 SpecialEventCount = 0;
		int32 SpawnedActorCount = 0;
		float RealizedThreatCost = 0.0f;
		float RealizedResourceCost = 0.0f;
		FString PolicyHash;
		FString EcologyHash;
		FString OutcomeHash;
		bool bHasFrozenOutcome = false;
		FEFCalystoFloorOutcomeV4 FrozenOutcome;
		FString IntentHash;
		FString AnchorTopologyHash;
		FString PopulationHash;
		FString ResourceHash;
		FString CompanionSnapshotHash;
		FString ManifestHash;
	};

	bool ConfigureFromCommandLine(FString& OutError);
	bool InitializeProjectTelemetry(FString& OutError);
	bool AppendProjectTelemetry(const FString& EventPayload);
	bool AppendReadyFloorProjectTelemetry(
		int64 FloorNumber,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoRealizedFloorManifestV4& Manifest);
	bool HandleBootstrapTick(float DeltaTime);
	bool HandleDoorSelectionTick(float DeltaTime);
	bool HandleTimeoutTick(float DeltaTime);
	bool HandleExitTick(float DeltaTime);
	void HandleFloorReady(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoRealizedFloorManifestV4& Manifest);
	void HandleFloorTravelFailed();
	bool ValidateReadyFloor(
		int64 FloorNumber,
		int32 PCGSeed,
		const FEFCalystoResolvedFloorIntentV4& Intent,
		const FEFCalystoRealizedFloorManifestV4& Manifest,
		FString& OutError) const;
	void Finish(bool bSuccess, const FString& Reason);
	bool WriteReceipt(bool bSuccess, const FString& Reason, double ElapsedSeconds);
	void CancelTicker(FTSTicker::FDelegateHandle& Handle);

	UPROPERTY(Transient)
	TObjectPtr<UEFCalystoDungeonSubsystem> DungeonSubsystem;

	int64 RunSeed = 202608140058LL;
	int64 ExpectedFloor = 1;
	int64 PreviousGenerationSerial = 0;
	int32 MaximumFloor = 10;
	int32 CompletedFloorCount = 0;
	int32 DoorInteractionCount = 0;
	FName Scenario = TEXT("Natural");
	FString ConfigurationName;
	FString RunTag;
	FString ScreenshotPath;
	FString ReceiptPath;
	FString ProjectTelemetryPath;
	TArray<FReadyFloorRecord> ReadyFloorRecords;
	double StartedAtSeconds = 0.0;
	double BootstrapGameplayPawnReadyNotBeforeSeconds = 0.0;
	double DoorSelectionStartedAtSeconds = 0.0;
	double DoorInspectionNotBeforeSeconds = 0.0;
	float TimeoutSeconds = 360.0f;
	bool bCaptureVisual = false;
	bool bOutcomeTelemetryDisabled = false;
	bool bScreenshotRequested = false;
	bool bBootstrapDispatched = false;
	bool bBootstrapGameplayPawnRequested = false;
	bool bFinished = false;
	bool bDoorPositioned = false;
	bool bProjectTelemetryInitialized = false;
	bool bProjectTelemetryHealthy = false;
	uint64 ProjectTelemetrySequence = 0;
	int32 ProjectTelemetryReadySequenceCount = 0;
	int32 ProjectTelemetryDoorSelectedCount = 0;
	int32 ProjectTelemetryDoorInteractedCount = 0;
	int32 ProjectTelemetryFailureEventCount = 0;
	int32 ProjectTelemetryCompleteEventCount = 0;

	FTSTicker::FDelegateHandle BootstrapTickerHandle;
	FTSTicker::FDelegateHandle DoorSelectionTickerHandle;
	FTSTicker::FDelegateHandle TimeoutTickerHandle;
	FTSTicker::FDelegateHandle ExitTickerHandle;
};

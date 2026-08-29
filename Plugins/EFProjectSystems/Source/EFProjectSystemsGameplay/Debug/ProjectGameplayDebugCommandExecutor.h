#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "InnerDoctrine/ProjectInnerDoctrineTypes.h"

class AActor;

class EFPROJECTSYSTEMSGAMEPLAY_API FProjectGameplayDebugCommandExecutor
{
public:
	static bool TriggerImmediateDefeat(AActor* OwnerActor);
	static bool TriggerDownedMode(AActor* OwnerActor);
	static bool RestoreAcfHealth(AActor* OwnerActor);
	static bool RestoreNeedsAndSensations(AActor* OwnerActor);
	static bool SetSensationToMax(AActor* OwnerActor, FName SensationName);
	static bool SetNeedToZero(AActor* OwnerActor, FName NeedName);
	static bool SetNeedOrSensationToPercent(AActor* OwnerActor, FName EntryName, float Percent);
	static bool SetNeedToPercent(AActor* OwnerActor, FName NeedName, float Percent);
	static bool SetSensationToPercent(AActor* OwnerActor, FName SensationName, float Percent);
	static bool ForceApplyStatus(AActor* OwnerActor, FName StatusName);
	static bool RaiseDoctrineAttributeToLevel(AActor* OwnerActor, EProjectDoctrineAttribute Attribute, int32 TargetLevel);
	static bool StartRuntimeFpsBenchmark(AActor* OwnerActor);
	static bool StartFullStackOverloadBenchmark(AActor* OwnerActor);
	static FText GetDungeonHarnessStatusLabel(AActor* OwnerActor);
	static FText GetDungeonHarnessStatusDescription(AActor* OwnerActor);
	static FText GetDungeonHarnessFloorChoiceLabel(AActor* OwnerActor, int64 FloorNumber);
	static FText GetDungeonHarnessStyleChoiceLabel(AActor* OwnerActor, bool bAuto, EEFCalystoStyleV4 Style);
	static FText GetDungeonHarnessThemeChoiceLabel(AActor* OwnerActor, bool bAuto, EEFCalystoThemeV4 Theme);
	static FText GetDungeonHarnessBiasChoiceLabel(AActor* OwnerActor, FName BiasName, float Bias);
	static FText GetDungeonHarnessVolatilityChoiceLabel(AActor* OwnerActor, float Volatility);
	static bool RefreshDungeonHarnessStatus(AActor* OwnerActor);
	static bool RequestAdvanceDungeonFloor(AActor* OwnerActor);
	static bool RequestTravelToDungeonFloor(AActor* OwnerActor, int64 FloorNumber);
	static bool RequestReplayDungeonFloor(AActor* OwnerActor);
	static bool RequestRerollDungeonFloor(AActor* OwnerActor);
	static bool RequestStartNewDungeonRun(AActor* OwnerActor);
	static bool RequestStartDungeonTestRun(AActor* OwnerActor);
	static bool SetDungeonHarnessPreferredStyle(AActor* OwnerActor, bool bAuto, EEFCalystoStyleV4 Style);
	static bool SetDungeonHarnessPreferredTheme(AActor* OwnerActor, bool bAuto, EEFCalystoThemeV4 Theme);
	static bool SetDungeonHarnessIntentBias(AActor* OwnerActor, FName BiasName, float Bias);
	static bool SetDungeonHarnessIntentVolatility(AActor* OwnerActor, float Volatility);
	static bool ClearDungeonHarnessDirectorIntent(AActor* OwnerActor);
	static bool IsDungeonHarnessPersistentCommand(FName OptionId);
	static TArray<FName> GetAutomaticTattooDebugRowNames(AActor* OwnerActor);
	static FText GetAutomaticTattooDebugRowLabel(AActor* OwnerActor, FName RowName);
	static FText GetAutomaticTattooDebugRowDescription(AActor* OwnerActor, FName RowName);
	static bool AdjustAutomaticTattooPlacement(AActor* OwnerActor, FName RowName, float DeltaOffsetX, float DeltaOffsetY, float DeltaSize, float DeltaRotationDegrees, float DeltaProjectionDistance);
	static bool ResetAutomaticTattooPlacement(AActor* OwnerActor, FName RowName);
	static bool ToggleAutomaticTattooForcedActive(AActor* OwnerActor, FName RowName);
	static bool CopyAutomaticTattooPlacementValues(AActor* OwnerActor, FName RowName);

	static TArray<FName> GetAvailableStatusNames();
	static TArray<EProjectDoctrineAttribute> GetDebugAttributes();
	static FName GetAttributeId(EProjectDoctrineAttribute Attribute);
	static FText GetAttributeDisplayName(EProjectDoctrineAttribute Attribute);
};

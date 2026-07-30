#pragma once

#include "CoreMinimal.h"
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

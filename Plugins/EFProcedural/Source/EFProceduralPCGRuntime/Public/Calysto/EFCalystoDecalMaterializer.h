#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoDecalReservation.h"
#include "UObject/GCObject.h"

class UWorld;
struct FStreamableHandle;

enum class EEFCalystoDecalRealizationState : uint8
{ Idle, Loading, Staging, Prepared, Published, Committed, Releasing, Released };

struct EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalRealizationObservation
{
	FEFCalystoAttemptToken Token;
	EEFCalystoDecalRealizationState State = EEFCalystoDecalRealizationState::Idle;
	EEFCalystoAttemptFailure Failure = EEFCalystoAttemptFailure::Configuration;
	FName FailureCode;
	FString Message;
	FString ManifestHash;
	FString RealizationHash;
	TSet<FGuid> VerifiedElements;
	int32 OwnedPoolActors = 0;
	int32 RetainedLeases = 0;
};

/** Retained selected-decal load/staging owner.  It has no random selection or PCG work:
 * pool staging is hidden until coordinator acceptance and every selected resource is released
 * before the next topology attempt begins. */
class EFPROCEDURALPCGRUNTIME_API FEFCalystoDecalMaterializer final : public FGCObject
{
public:
	FEFCalystoDecalMaterializer() = default;
	virtual ~FEFCalystoDecalMaterializer() override;
	FEFCalystoDecalMaterializer(const FEFCalystoDecalMaterializer&) = delete;
	FEFCalystoDecalMaterializer& operator=(const FEFCalystoDecalMaterializer&) = delete;
	bool Begin(const FEFCalystoAttemptToken& Token, TSharedRef<const FEFCalystoCompiledDirector> Configuration,
		TSharedRef<FEFCalystoNativeAdapter> Native, UWorld* World,
		TSharedRef<const FEFCalystoReservedDecalManifest> Manifest, double DeadlineSeconds, double Now, FString& Error);
	FEFCalystoDecalRealizationObservation Observe(double Now);
	bool Commit(const FEFCalystoAttemptToken& Token, double Now, FString& Error);
	EEFCalystoActivationResult ActivateCommitted(const FEFCalystoAttemptToken& Token, FString& Error);
	void Release(const FEFCalystoAttemptToken& Token);
	FEFCalystoRollbackEvidence GetReleaseEvidence(const FEFCalystoAttemptToken& Token) const;
	const FEFCalystoDecalRealizationObservation& GetObservation() const { return Observation; }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FEFCalystoDecalMaterializer"); }
private:
	bool AcceptClock(double Now, FString& Error);
	bool Fail(EEFCalystoAttemptFailure Failure, FName Code, const FString& Message);
	bool Stage(FString& Error);
	void StepRelease();
	void UpdateCounts();
	FEFCalystoAttemptToken Token;
	TSharedPtr<const FEFCalystoCompiledDirector> Configuration;
	TSharedPtr<FEFCalystoNativeAdapter> Native;
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<const FEFCalystoReservedDecalManifest> Manifest;
	TSharedPtr<FStreamableHandle> LoadHandle;
	TArray<FSoftObjectPath> Dependencies;
	TArray<TObjectPtr<UObject>> Resources;
	TObjectPtr<AEFCalystoDecalPoolOwner> Pool = nullptr;
	FEFCalystoDecalRealizationObservation Observation;
	double DeadlineSeconds = 0;
	double LastNow = -1;
	bool bActivated = false;
};

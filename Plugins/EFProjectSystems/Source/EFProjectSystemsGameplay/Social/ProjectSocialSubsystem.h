#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoFloorTransaction.h"
#include "Social/ProjectSocialTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/ObjectKey.h"
#include "ProjectSocialSubsystem.generated.h"

class AActor;
class UWorld;

DECLARE_MULTICAST_DELEGATE_OneParam(FProjectSocialParticipantChangedNativeSignature, AActor*);
DECLARE_MULTICAST_DELEGATE_OneParam(FProjectSocialParticipantUnregisteredNativeSignature, AActor*);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectSocialParticipantChangedSignature,
	AActor*,
	Participant);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectSocialParticipantUnregisteredSignature,
	AActor*,
	Participant);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FProjectSocialIntimacyConsentChangedSignature,
	AActor*,
	GrantingParticipant,
	AActor*,
	OtherParticipant,
	bool,
	bConsented);

/**
 * Neutral social authority for adult verification, bilateral consent,
 * dialogue/service availability, affinity and recruited companions.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectSocialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** One exact attempt may reserve at most 512 new actors/participant IDs. Existing identities
	 * are never replaced. Staging is invisible to ordinary social reads and emits no events. */
	bool StageParticipant(const FEFCalystoAttemptToken& Token, AActor* Participant,
		const FProjectSocialParticipantState& State, FString& Error);
	bool ObserveStagedParticipant(const FEFCalystoAttemptToken& Token, AActor* Participant,
		FName ParticipantId, FProjectSocialParticipantState& OutState, FString& Error) const;
	/** Allocates hidden map entries and destruction bindings before the publication boundary. */
	bool PrepareStagedParticipants(const FEFCalystoAttemptToken& Token, FString& Error);
	/** Validates the complete batch before making all prepared entries readable; no allocation/events. */
	bool PublishStagedParticipantsWithoutEvents(const FEFCalystoAttemptToken& Token, FString& Error);
	/** Final acceptance broadcasts once per participant and retains mutation locks for the whole batch. */
	bool ConfirmStagedParticipants(const FEFCalystoAttemptToken& Token, FString& Error);
	/** Rejection removes all unpublished/unconfirmed entries without events. Accepted release only
	 * retires the staging ledger; participants continue under ordinary social ownership. */
	bool ReleaseStagedParticipants(const FEFCalystoAttemptToken& Token, bool bAccepted, FString& Error);
	FProjectSocialParticipantChangedNativeSignature& OnNativeParticipantChanged() { return NativeParticipantChanged; }
	FProjectSocialParticipantUnregisteredNativeSignature& OnNativeParticipantUnregistered() { return NativeParticipantUnregistered; }

	UFUNCTION(BlueprintCallable, Category = "Project|Social")
	bool RegisterOrUpdateParticipant(AActor* Participant, const FProjectSocialParticipantState& State);

	UFUNCTION(BlueprintCallable, Category = "Project|Social")
	void UnregisterParticipant(AActor* Participant);

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	bool TryGetParticipantState(AActor* Participant, FProjectSocialParticipantState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	AActor* FindParticipantById(FName ParticipantId) const;

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	bool IsVerifiedAdult(AActor* Participant) const;

	UFUNCTION(BlueprintCallable, Category = "Project|Social|Consent")
	bool SetExplicitIntimacyConsent(AActor* GrantingParticipant, AActor* OtherParticipant, bool bConsented);

	UFUNCTION(BlueprintPure, Category = "Project|Social|Consent")
	bool HasExplicitIntimacyConsent(AActor* GrantingParticipant, AActor* OtherParticipant) const;

	UFUNCTION(BlueprintCallable, Category = "Project|Social|Consent")
	void ClearIntimacyConsentForParticipant(AActor* Participant);

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	FProjectSocialEligibilityResult EvaluateIntimacyEligibility(AActor* Initiator, AActor* Participant) const;

	UFUNCTION(BlueprintPure, Category = "Project|Social|Consent")
	FProjectSocialEligibilityResult EvaluateIntimacyConsentOffer(
		AActor* Initiator,
		AActor* Participant,
		int32 MinimumAffinity) const;

	/**
	 * Establishes both directional consent records atomically for one
	 * player-initiated request. The caller must clear them when the request
	 * ends. Failure never leaves one-sided consent behind.
	 */
	UFUNCTION(BlueprintCallable, Category = "Project|Social|Consent")
	FProjectSocialEligibilityResult TryEstablishBilateralIntimacyConsent(
		AActor* Initiator,
		AActor* Participant,
		int32 MinimumAffinity);

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	bool CanStartDialogue(AActor* Participant) const;

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	bool CanUseServices(AActor* Participant) const;

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	bool CanRecruitParticipant(AActor* Participant) const;

	UFUNCTION(BlueprintCallable, Category = "Project|Social")
	bool SetRecruitedCompanion(AActor* Participant, bool bRecruited);

	UFUNCTION(BlueprintCallable, Category = "Project|Social")
	bool AdjustAffinity(AActor* Participant, int32 Delta, int32& OutAffinity);

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	bool HasLivingCompanionWithin(AActor* SourceActor, float Radius = 1200.0f) const;

	/** Stable no-tick query used by neutral companion-based Doctrine bonuses. */
	bool HasLivingRecruitedCompanionWithin(const AActor* Subject, float Radius = 1200.0f) const;

	UFUNCTION(BlueprintPure, Category = "Project|Social")
	TArray<AActor*> GetLivingCompanionsWithin(AActor* SourceActor, float Radius = 1200.0f) const;

	UPROPERTY(BlueprintAssignable, Category = "Project|Social")
	FProjectSocialParticipantChangedSignature OnParticipantChanged;

	UPROPERTY(BlueprintAssignable, Category = "Project|Social")
	FProjectSocialParticipantUnregisteredSignature OnParticipantUnregistered;

	UPROPERTY(BlueprintAssignable, Category = "Project|Social|Consent")
	FProjectSocialIntimacyConsentChangedSignature OnIntimacyConsentChanged;

private:
	struct FParticipantRecord
	{
		TWeakObjectPtr<AActor> Actor;
		FProjectSocialParticipantState State;
		bool bUnpublished = false;
	};
	struct FStagedParticipant
	{
		TObjectKey<AActor> ActorKey;
		TWeakObjectPtr<AActor> Actor;
		FProjectSocialParticipantState State;
	};
	enum class EParticipantStagePhase : uint8 { Empty, Staged, Prepared, Published, Confirming, Confirmed, Released };

	struct FConsentKey
	{
		TObjectKey<AActor> GrantingParticipant;
		TObjectKey<AActor> OtherParticipant;

		bool operator==(const FConsentKey& Other) const
		{
			return GrantingParticipant == Other.GrantingParticipant
				&& OtherParticipant == Other.OtherParticipant;
		}

		friend uint32 GetTypeHash(const FConsentKey& Key)
		{
			return HashCombine(GetTypeHash(Key.GrantingParticipant), GetTypeHash(Key.OtherParticipant));
		}
	};

	FParticipantRecord* FindMutableRecord(AActor* Participant);
	const FParticipantRecord* FindRecord(AActor* Participant) const;

	UFUNCTION()
	void HandleParticipantDestroyed(AActor* DestroyedActor);

	void PruneInvalidParticipants();
	void RemoveConsentForKey(const TObjectKey<AActor>& ParticipantKey);
	void BroadcastParticipantChanged(const FParticipantRecord& Record);
	bool IsStageLocked(const TObjectKey<AActor>& ActorKey) const;
	bool IsStageIdentityLocked(FName ParticipantId) const;
	bool ValidateStagedParticipants(FString& Error) const;
	void RemoveParticipantRecordWithoutEvents(const TObjectKey<AActor>& ActorKey);
	static bool SameParticipantState(const FProjectSocialParticipantState& A, const FProjectSocialParticipantState& B);

private:
	TMap<TObjectKey<AActor>, FParticipantRecord> ParticipantRecords;
	TMap<FName, TObjectKey<AActor>> ParticipantsById;
	TSet<FConsentKey> ExplicitIntimacyConsent;
	FEFCalystoAttemptToken ParticipantStageToken;
	EParticipantStagePhase ParticipantStagePhase = EParticipantStagePhase::Empty;
	TArray<FStagedParticipant> StagedParticipants;
	TMap<TObjectKey<AActor>, int32> StagedParticipantsByActor;
	TMap<FName, int32> StagedParticipantsById;
	TWeakObjectPtr<UWorld> ParticipantStageWorld;
	bool bParticipantStageInvalidated = false;
	bool bParticipantStageOperation = false;
	bool bParticipantStageReleaseRequested = false;
	bool bParticipantStageAcceptedRelease = false;
	FProjectSocialParticipantChangedNativeSignature NativeParticipantChanged;
	FProjectSocialParticipantUnregisteredNativeSignature NativeParticipantUnregistered;
};

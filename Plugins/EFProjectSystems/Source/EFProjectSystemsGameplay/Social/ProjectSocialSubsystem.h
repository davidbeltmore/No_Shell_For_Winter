#pragma once

#include "CoreMinimal.h"
#include "Social/ProjectSocialTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/ObjectKey.h"
#include "ProjectSocialSubsystem.generated.h"

class AActor;

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
	};

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

private:
	TMap<TObjectKey<AActor>, FParticipantRecord> ParticipantRecords;
	TMap<FName, TObjectKey<AActor>> ParticipantsById;
	TSet<FConsentKey> ExplicitIntimacyConsent;
};

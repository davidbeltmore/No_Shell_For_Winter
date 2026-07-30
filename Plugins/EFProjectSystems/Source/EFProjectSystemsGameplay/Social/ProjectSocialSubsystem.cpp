#include "Social/ProjectSocialSubsystem.h"

#include "GameFramework/Actor.h"

void UProjectSocialSubsystem::Deinitialize()
{
	for (const TPair<TObjectKey<AActor>, FParticipantRecord>& Pair : ParticipantRecords)
	{
		if (AActor* Participant = Pair.Value.Actor.Get())
		{
			Participant->OnDestroyed.RemoveAll(this);
		}
	}

	ExplicitIntimacyConsent.Reset();
	ParticipantsById.Reset();
	ParticipantRecords.Reset();
	Super::Deinitialize();
}

bool UProjectSocialSubsystem::RegisterOrUpdateParticipant(
	AActor* Participant,
	const FProjectSocialParticipantState& State)
{
	if (!IsValid(Participant) || State.ParticipantId.IsNone())
	{
		return false;
	}

	PruneInvalidParticipants();
	const TObjectKey<AActor> ParticipantKey(Participant);

	if (const TObjectKey<AActor>* ExistingKey = ParticipantsById.Find(State.ParticipantId);
		ExistingKey && *ExistingKey != ParticipantKey)
	{
		if (FParticipantRecord* ExistingRecord = ParticipantRecords.Find(*ExistingKey))
		{
			if (AActor* ExistingActor = ExistingRecord->Actor.Get())
			{
				UnregisterParticipant(ExistingActor);
			}
			else
			{
				RemoveConsentForKey(*ExistingKey);
				ParticipantRecords.Remove(*ExistingKey);
			}
		}
	}

	FParticipantRecord& Record = ParticipantRecords.FindOrAdd(ParticipantKey);
	const bool bIdentityChanged = !Record.State.ParticipantId.IsNone()
		&& Record.State.ParticipantId != State.ParticipantId;
	if (bIdentityChanged)
	{
		ParticipantsById.Remove(Record.State.ParticipantId);
	}

	Record.Actor = Participant;
	Record.State = State;
	Record.State.Affinity = FProjectSocialRules::ClampAffinity(Record.State.Affinity);
	if (bIdentityChanged
		|| !Record.State.bVerifiedAdult
		|| !Record.State.bAlive
		|| !Record.State.bConscious
		|| Record.State.bHostile
		|| Record.State.bInCombat
		|| !Record.State.bInSafeLocation
		|| (Record.State.bRecruitedCompanion && !Record.State.bOffersPlayerInitiatedIntimacy))
	{
		RemoveConsentForKey(ParticipantKey);
	}

	Participant->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleParticipantDestroyed);
	ParticipantsById.Add(Record.State.ParticipantId, ParticipantKey);
	BroadcastParticipantChanged(Record);
	return true;
}

void UProjectSocialSubsystem::UnregisterParticipant(AActor* Participant)
{
	if (!Participant)
	{
		return;
	}

	const TObjectKey<AActor> ParticipantKey(Participant);
	Participant->OnDestroyed.RemoveAll(this);
	if (const FParticipantRecord* Record = ParticipantRecords.Find(ParticipantKey))
	{
		ParticipantsById.Remove(Record->State.ParticipantId);
	}

	RemoveConsentForKey(ParticipantKey);
	ParticipantRecords.Remove(ParticipantKey);
	OnParticipantUnregistered.Broadcast(Participant);
}

bool UProjectSocialSubsystem::TryGetParticipantState(
	AActor* Participant,
	FProjectSocialParticipantState& OutState) const
{
	if (const FParticipantRecord* Record = FindRecord(Participant))
	{
		OutState = Record->State;
		return true;
	}

	OutState = FProjectSocialParticipantState();
	return false;
}

AActor* UProjectSocialSubsystem::FindParticipantById(const FName ParticipantId) const
{
	const TObjectKey<AActor>* ParticipantKey = ParticipantsById.Find(ParticipantId);
	const FParticipantRecord* Record = ParticipantKey ? ParticipantRecords.Find(*ParticipantKey) : nullptr;
	return Record ? Record->Actor.Get() : nullptr;
}

bool UProjectSocialSubsystem::IsVerifiedAdult(AActor* Participant) const
{
	const FParticipantRecord* Record = FindRecord(Participant);
	return Record && Record->State.bVerifiedAdult;
}

bool UProjectSocialSubsystem::SetExplicitIntimacyConsent(
	AActor* GrantingParticipant,
	AActor* OtherParticipant,
	const bool bConsented)
{
	if (!IsValid(GrantingParticipant)
		|| !IsValid(OtherParticipant)
		|| GrantingParticipant == OtherParticipant)
	{
		return false;
	}

	const FConsentKey ConsentKey{
		TObjectKey<AActor>(GrantingParticipant),
		TObjectKey<AActor>(OtherParticipant)
	};

	if (!bConsented)
	{
		if (ExplicitIntimacyConsent.Remove(ConsentKey) > 0)
		{
			OnIntimacyConsentChanged.Broadcast(GrantingParticipant, OtherParticipant, false);
		}
		return true;
	}

	const FParticipantRecord* GrantingRecord = FindRecord(GrantingParticipant);
	const FParticipantRecord* OtherRecord = FindRecord(OtherParticipant);
	if (!GrantingRecord
		|| !OtherRecord
		|| !GrantingRecord->State.bVerifiedAdult
		|| !OtherRecord->State.bVerifiedAdult
		|| !GrantingRecord->State.bAlive
		|| !OtherRecord->State.bAlive
		|| !GrantingRecord->State.bConscious
		|| !OtherRecord->State.bConscious
		|| GrantingRecord->State.bHostile
		|| OtherRecord->State.bHostile)
	{
		return false;
	}

	if (!ExplicitIntimacyConsent.Contains(ConsentKey))
	{
		ExplicitIntimacyConsent.Add(ConsentKey);
		OnIntimacyConsentChanged.Broadcast(GrantingParticipant, OtherParticipant, true);
	}
	return true;
}

bool UProjectSocialSubsystem::HasExplicitIntimacyConsent(
	AActor* GrantingParticipant,
	AActor* OtherParticipant) const
{
	if (!IsValid(GrantingParticipant)
		|| !IsValid(OtherParticipant)
		|| GrantingParticipant == OtherParticipant)
	{
		return false;
	}

	return ExplicitIntimacyConsent.Contains(FConsentKey{
		TObjectKey<AActor>(GrantingParticipant),
		TObjectKey<AActor>(OtherParticipant)
	});
}

void UProjectSocialSubsystem::ClearIntimacyConsentForParticipant(AActor* Participant)
{
	if (Participant)
	{
		RemoveConsentForKey(TObjectKey<AActor>(Participant));
		OnParticipantChanged.Broadcast(Participant);
	}
}

FProjectSocialEligibilityResult UProjectSocialSubsystem::EvaluateIntimacyEligibility(
	AActor* Initiator,
	AActor* Participant) const
{
	const FParticipantRecord* InitiatorRecord = FindRecord(Initiator);
	const FParticipantRecord* ParticipantRecord = FindRecord(Participant);
	if (!InitiatorRecord || !ParticipantRecord)
	{
		return FProjectSocialEligibilityResult();
	}

	return FProjectSocialRules::EvaluateIntimacyEligibility(
		InitiatorRecord->State,
		ParticipantRecord->State,
		HasExplicitIntimacyConsent(Initiator, Participant),
		HasExplicitIntimacyConsent(Participant, Initiator));
}

FProjectSocialEligibilityResult UProjectSocialSubsystem::EvaluateIntimacyConsentOffer(
	AActor* Initiator,
	AActor* Participant,
	const int32 MinimumAffinity) const
{
	const FParticipantRecord* InitiatorRecord = FindRecord(Initiator);
	const FParticipantRecord* ParticipantRecord = FindRecord(Participant);
	if (!InitiatorRecord || !ParticipantRecord)
	{
		return FProjectSocialEligibilityResult();
	}

	return FProjectSocialRules::EvaluateConsentOfferEligibility(
		InitiatorRecord->State,
		ParticipantRecord->State,
		MinimumAffinity);
}

FProjectSocialEligibilityResult UProjectSocialSubsystem::TryEstablishBilateralIntimacyConsent(
	AActor* Initiator,
	AActor* Participant,
	const int32 MinimumAffinity)
{
	SetExplicitIntimacyConsent(Initiator, Participant, false);
	SetExplicitIntimacyConsent(Participant, Initiator, false);

	const FProjectSocialEligibilityResult OfferResult =
		EvaluateIntimacyConsentOffer(Initiator, Participant, MinimumAffinity);
	if (!OfferResult.bEligible)
	{
		return OfferResult;
	}

	const bool bInitiatorRecorded =
		SetExplicitIntimacyConsent(Initiator, Participant, true);
	const bool bParticipantRecorded =
		bInitiatorRecorded && SetExplicitIntimacyConsent(Participant, Initiator, true);
	if (!bInitiatorRecorded || !bParticipantRecorded)
	{
		SetExplicitIntimacyConsent(Initiator, Participant, false);
		SetExplicitIntimacyConsent(Participant, Initiator, false);
		return FProjectSocialEligibilityResult();
	}

	return EvaluateIntimacyEligibility(Initiator, Participant);
}

bool UProjectSocialSubsystem::CanStartDialogue(AActor* Participant) const
{
	const FParticipantRecord* Record = FindRecord(Participant);
	return Record && FProjectSocialRules::CanStartDialogue(Record->State);
}

bool UProjectSocialSubsystem::CanUseServices(AActor* Participant) const
{
	return CanStartDialogue(Participant);
}

bool UProjectSocialSubsystem::CanRecruitParticipant(AActor* Participant) const
{
	const FParticipantRecord* Record = FindRecord(Participant);
	return Record && FProjectSocialRules::CanRecruit(Record->State);
}

bool UProjectSocialSubsystem::SetRecruitedCompanion(AActor* Participant, const bool bRecruited)
{
	FParticipantRecord* Record = FindMutableRecord(Participant);
	if (!Record || (bRecruited && !FProjectSocialRules::CanRecruit(Record->State)))
	{
		return false;
	}

	Record->State.bRecruitedCompanion = bRecruited;
	BroadcastParticipantChanged(*Record);
	return true;
}

bool UProjectSocialSubsystem::AdjustAffinity(
	AActor* Participant,
	const int32 Delta,
	int32& OutAffinity)
{
	FParticipantRecord* Record = FindMutableRecord(Participant);
	if (!Record)
	{
		OutAffinity = 0;
		return false;
	}

	Record->State.Affinity = FProjectSocialRules::ClampAffinity(Record->State.Affinity + Delta);
	OutAffinity = Record->State.Affinity;
	BroadcastParticipantChanged(*Record);
	return true;
}

bool UProjectSocialSubsystem::HasLivingCompanionWithin(
	AActor* SourceActor,
	const float Radius) const
{
	return !GetLivingCompanionsWithin(SourceActor, Radius).IsEmpty();
}

bool UProjectSocialSubsystem::HasLivingRecruitedCompanionWithin(
	const AActor* Subject,
	const float Radius) const
{
	return Subject
		&& HasLivingCompanionWithin(const_cast<AActor*>(Subject), Radius);
}

TArray<AActor*> UProjectSocialSubsystem::GetLivingCompanionsWithin(
	AActor* SourceActor,
	const float Radius) const
{
	TArray<AActor*> Result;
	if (!IsValid(SourceActor) || Radius < 0.0f)
	{
		return Result;
	}

	const float RadiusSquared = FMath::Square(Radius);
	for (const TPair<TObjectKey<AActor>, FParticipantRecord>& Pair : ParticipantRecords)
	{
		AActor* Candidate = Pair.Value.Actor.Get();
		if (!IsValid(Candidate)
			|| Candidate == SourceActor
			|| !FProjectSocialRules::IsLivingCompanion(Pair.Value.State))
		{
			continue;
		}

		if (FVector::DistSquared(SourceActor->GetActorLocation(), Candidate->GetActorLocation()) <= RadiusSquared)
		{
			Result.Add(Candidate);
		}
	}

	return Result;
}

UProjectSocialSubsystem::FParticipantRecord* UProjectSocialSubsystem::FindMutableRecord(AActor* Participant)
{
	return IsValid(Participant)
		? ParticipantRecords.Find(TObjectKey<AActor>(Participant))
		: nullptr;
}

const UProjectSocialSubsystem::FParticipantRecord* UProjectSocialSubsystem::FindRecord(AActor* Participant) const
{
	const FParticipantRecord* Record = IsValid(Participant)
		? ParticipantRecords.Find(TObjectKey<AActor>(Participant))
		: nullptr;
	return Record && Record->Actor.IsValid() ? Record : nullptr;
}

void UProjectSocialSubsystem::HandleParticipantDestroyed(AActor* DestroyedActor)
{
	UnregisterParticipant(DestroyedActor);
}

void UProjectSocialSubsystem::PruneInvalidParticipants()
{
	TArray<TObjectKey<AActor>> InvalidKeys;
	for (const TPair<TObjectKey<AActor>, FParticipantRecord>& Pair : ParticipantRecords)
	{
		if (!Pair.Value.Actor.IsValid())
		{
			InvalidKeys.Add(Pair.Key);
			ParticipantsById.Remove(Pair.Value.State.ParticipantId);
		}
	}

	for (const TObjectKey<AActor>& InvalidKey : InvalidKeys)
	{
		RemoveConsentForKey(InvalidKey);
		ParticipantRecords.Remove(InvalidKey);
	}
}

void UProjectSocialSubsystem::RemoveConsentForKey(const TObjectKey<AActor>& ParticipantKey)
{
	for (auto It = ExplicitIntimacyConsent.CreateIterator(); It; ++It)
	{
		if (It->GrantingParticipant == ParticipantKey || It->OtherParticipant == ParticipantKey)
		{
			It.RemoveCurrent();
		}
	}
}

void UProjectSocialSubsystem::BroadcastParticipantChanged(const FParticipantRecord& Record)
{
	if (AActor* Participant = Record.Actor.Get())
	{
		OnParticipantChanged.Broadcast(Participant);
	}
}

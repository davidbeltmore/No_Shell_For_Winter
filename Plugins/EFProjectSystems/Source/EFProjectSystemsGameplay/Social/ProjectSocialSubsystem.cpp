#include "Social/ProjectSocialSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

bool UProjectSocialSubsystem::SameParticipantState(const FProjectSocialParticipantState& A, const FProjectSocialParticipantState& B)
{
	return A.ParticipantId==B.ParticipantId && A.bVerifiedAdult==B.bVerifiedAdult && A.bAlive==B.bAlive
		&& A.bConscious==B.bConscious && A.bHostile==B.bHostile && A.bInCombat==B.bInCombat
		&& A.bInSafeLocation==B.bInSafeLocation && A.bRecruitable==B.bRecruitable
		&& A.bRecruitedCompanion==B.bRecruitedCompanion && A.bOffersPlayerInitiatedIntimacy==B.bOffersPlayerInitiatedIntimacy
		&& A.MinimumIntimacyAffinity==B.MinimumIntimacyAffinity && A.Affinity==B.Affinity;
}

bool UProjectSocialSubsystem::IsStageLocked(const TObjectKey<AActor>& ActorKey) const
{
	return ParticipantStagePhase!=EParticipantStagePhase::Empty && ParticipantStagePhase!=EParticipantStagePhase::Released
		&& ParticipantStagePhase!=EParticipantStagePhase::Confirmed && StagedParticipantsByActor.Contains(ActorKey);
}

bool UProjectSocialSubsystem::IsStageIdentityLocked(FName ParticipantId) const
{
	return ParticipantStagePhase!=EParticipantStagePhase::Empty && ParticipantStagePhase!=EParticipantStagePhase::Released
		&& ParticipantStagePhase!=EParticipantStagePhase::Confirmed && StagedParticipantsById.Contains(ParticipantId);
}

bool UProjectSocialSubsystem::StageParticipant(const FEFCalystoAttemptToken& Token, AActor* Participant,
	const FProjectSocialParticipantState& State, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bParticipantStageOperation || bParticipantStageReleaseRequested
		|| !Token.Request.IsValid() || !Token.Attempt.IsValid() || !IsValid(Participant) || Participant->IsActorBeingDestroyed()
		|| !Participant->GetWorld() || (GetWorld() && GetWorld()!=Participant->GetWorld()) || State.ParticipantId.IsNone()
		|| State.Affinity!=FProjectSocialRules::ClampAffinity(State.Affinity))
	{ Error=TEXT("Social staging requires an exclusive valid token, live actor and exact bounded native state."); return false; }
	if (ParticipantStagePhase==EParticipantStagePhase::Released && ParticipantStageToken==Token)
	{ Error=TEXT("A retired social attempt token cannot reopen."); return false; }
	const bool bNewAttempt=ParticipantStagePhase==EParticipantStagePhase::Empty || ParticipantStagePhase==EParticipantStagePhase::Released;
	if (!bNewAttempt && (!(ParticipantStageToken==Token) || bParticipantStageInvalidated || ParticipantStageWorld.Get()!=Participant->GetWorld()))
	{ Error=TEXT("Another or invalidated attempt retains the social identity ledger."); return false; }
	const TObjectKey<AActor> ActorKey(Participant);
	if (!bNewAttempt)
	{
		if (const int32* Index=StagedParticipantsByActor.Find(ActorKey))
		{
			if (ParticipantStagePhase!=EParticipantStagePhase::Confirmed && SameParticipantState(StagedParticipants[*Index].State,State)) return true;
			Error=TEXT("A staged actor cannot change its exact participant identity or native state."); return false;
		}
		if (ParticipantStagePhase!=EParticipantStagePhase::Staged)
		{ Error=TEXT("A prepared social batch cannot accept another participant."); return false; }
	}
	if (ParticipantRecords.Contains(ActorKey) || ParticipantsById.Contains(State.ParticipantId)
		|| (!bNewAttempt && (StagedParticipantsById.Contains(State.ParticipantId) || StagedParticipants.Num()>=512)))
	{ Error=TEXT("Social staging cannot evict a registered/reserved identity or exceed 512 participants."); return false; }
	if (bNewAttempt)
	{
		ParticipantStageToken=Token; ParticipantStageWorld=Participant->GetWorld(); ParticipantStagePhase=EParticipantStagePhase::Staged;
		bParticipantStageInvalidated=false; bParticipantStageReleaseRequested=false; bParticipantStageAcceptedRelease=false;
	}
	FStagedParticipant Record; Record.ActorKey=ActorKey; Record.Actor=Participant; Record.State=State;
	const int32 Index=StagedParticipants.Add(MoveTemp(Record));
	StagedParticipantsByActor.Add(ActorKey,Index); StagedParticipantsById.Add(State.ParticipantId,Index);
	return true;
}

bool UProjectSocialSubsystem::ObserveStagedParticipant(const FEFCalystoAttemptToken& Token, AActor* Participant,
	FName ParticipantId, FProjectSocialParticipantState& OutState, FString& Error) const
{
	OutState={}; Error.Reset();
	if (!IsInGameThread() || !(ParticipantStageToken==Token) || bParticipantStageInvalidated || bParticipantStageReleaseRequested
		|| ParticipantStagePhase==EParticipantStagePhase::Empty || ParticipantStagePhase==EParticipantStagePhase::Released
		|| !IsValid(Participant) || Participant->IsActorBeingDestroyed())
	{ Error=TEXT("The exact social stage is unavailable, invalidated or releasing."); return false; }
	const int32* Index=StagedParticipantsByActor.Find(TObjectKey<AActor>(Participant));
	if (!Index || StagedParticipants[*Index].State.ParticipantId!=ParticipantId || StagedParticipants[*Index].Actor.Get()!=Participant)
	{ Error=TEXT("Social observation does not bind the staged actor and participant ID."); return false; }
	if (!ValidateStagedParticipants(Error)) return false;
	OutState=StagedParticipants[*Index].State; return true;
}

bool UProjectSocialSubsystem::ValidateStagedParticipants(FString& Error) const
{
	if (bParticipantStageInvalidated || !ParticipantStageWorld.IsValid() || StagedParticipants.IsEmpty() || StagedParticipants.Num()>512)
	{ Error=TEXT("The retained social attempt has lost a participant or its world."); return false; }
	for (const FStagedParticipant& Staged:StagedParticipants)
	{
		const AActor* Actor=Staged.Actor.Get();
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->GetWorld()!=ParticipantStageWorld.Get())
		{ Error=TEXT("A reserved social actor disappeared before acceptance."); return false; }
		const FParticipantRecord* Actual=ParticipantRecords.Find(Staged.ActorKey);
		const TObjectKey<AActor>* ActualId=ParticipantsById.Find(Staged.State.ParticipantId);
		if (ParticipantStagePhase==EParticipantStagePhase::Staged)
		{
			if (Actual || ActualId) { Error=TEXT("An existing social record conflicts with a reserved identity."); return false; }
		}
		else if (!Actual || !ActualId || *ActualId!=Staged.ActorKey || Actual->Actor.Get()!=Actor
			|| !SameParticipantState(Actual->State,Staged.State)
			|| Actual->bUnpublished!=(ParticipantStagePhase==EParticipantStagePhase::Prepared))
		{ Error=TEXT("The prepared social records differ from their exact reserved actors and state."); return false; }
	}
	return true;
}

bool UProjectSocialSubsystem::PrepareStagedParticipants(const FEFCalystoAttemptToken& Token, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bParticipantStageOperation || bParticipantStageReleaseRequested || !(ParticipantStageToken==Token)
		|| (ParticipantStagePhase!=EParticipantStagePhase::Staged && ParticipantStagePhase!=EParticipantStagePhase::Prepared))
	{ Error=TEXT("Only the current staged social batch may prepare publication."); return false; }
	if (!ValidateStagedParticipants(Error)) return false;
	if (ParticipantStagePhase==EParticipantStagePhase::Prepared) return true;
	TGuardValue<bool> Operation(bParticipantStageOperation,true);
	ParticipantRecords.Reserve(ParticipantRecords.Num()+StagedParticipants.Num());
	ParticipantsById.Reserve(ParticipantsById.Num()+StagedParticipants.Num());
	for (const FStagedParticipant& Staged:StagedParticipants)
	{
		FParticipantRecord Record; Record.Actor=Staged.Actor; Record.State=Staged.State; Record.bUnpublished=true;
		ParticipantRecords.Add(Staged.ActorKey,MoveTemp(Record)); ParticipantsById.Add(Staged.State.ParticipantId,Staged.ActorKey);
		Staged.Actor->OnDestroyed.AddUniqueDynamic(this,&ThisClass::HandleParticipantDestroyed);
	}
	ParticipantStagePhase=EParticipantStagePhase::Prepared; return true;
}

bool UProjectSocialSubsystem::PublishStagedParticipantsWithoutEvents(const FEFCalystoAttemptToken& Token, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bParticipantStageOperation || bParticipantStageReleaseRequested || !(ParticipantStageToken==Token)
		|| (ParticipantStagePhase!=EParticipantStagePhase::Prepared && ParticipantStagePhase!=EParticipantStagePhase::Published))
	{ Error=TEXT("Only the exact prepared social batch may publish without events."); return false; }
	if (!ValidateStagedParticipants(Error)) return false;
	if (ParticipantStagePhase==EParticipantStagePhase::Published) return true;
	// All identities, map storage, delegate bindings and live actors were proved above. No callbacks occur here.
	for (const FStagedParticipant& Staged:StagedParticipants) ParticipantRecords.FindChecked(Staged.ActorKey).bUnpublished=false;
	ParticipantStagePhase=EParticipantStagePhase::Published; return true;
}

bool UProjectSocialSubsystem::ConfirmStagedParticipants(const FEFCalystoAttemptToken& Token, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bParticipantStageOperation || bParticipantStageReleaseRequested || !(ParticipantStageToken==Token))
	{ Error=TEXT("Social confirmation does not own the exact published batch."); return false; }
	if (ParticipantStagePhase==EParticipantStagePhase::Confirmed) return true;
	if (ParticipantStagePhase!=EParticipantStagePhase::Published || !ValidateStagedParticipants(Error))
	{ if (Error.IsEmpty()) Error=TEXT("Social confirmation requires a complete published batch."); return false; }
	{
		TGuardValue<bool> Operation(bParticipantStageOperation,true);
		ParticipantStagePhase=EParticipantStagePhase::Confirming;
		for (const FStagedParticipant& Staged:StagedParticipants)
		{
			if (const FParticipantRecord* Record=ParticipantRecords.Find(Staged.ActorKey)) BroadcastParticipantChanged(*Record);
		}
		ParticipantStagePhase=EParticipantStagePhase::Confirmed;
	}
	if (bParticipantStageReleaseRequested)
	{
		const bool bAccepted=bParticipantStageAcceptedRelease;
		bParticipantStageReleaseRequested=false;
		return ReleaseStagedParticipants(Token,bAccepted,Error);
	}
	return true;
}

void UProjectSocialSubsystem::RemoveParticipantRecordWithoutEvents(const TObjectKey<AActor>& ActorKey)
{
	if (const FParticipantRecord* Record=ParticipantRecords.Find(ActorKey))
	{
		if (AActor* Actor=Record->Actor.Get()) Actor->OnDestroyed.RemoveDynamic(this,&ThisClass::HandleParticipantDestroyed);
		if (const TObjectKey<AActor>* Key=ParticipantsById.Find(Record->State.ParticipantId); Key && *Key==ActorKey)
			ParticipantsById.Remove(Record->State.ParticipantId);
	}
	RemoveConsentForKey(ActorKey); ParticipantRecords.Remove(ActorKey);
}

bool UProjectSocialSubsystem::ReleaseStagedParticipants(const FEFCalystoAttemptToken& Token, bool bAccepted, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !(ParticipantStageToken==Token) || ParticipantStagePhase==EParticipantStagePhase::Empty)
	{ Error=TEXT("A stale social token cannot release another identity ledger."); return false; }
	if (ParticipantStagePhase==EParticipantStagePhase::Released)
	{
		if (bAccepted==bParticipantStageAcceptedRelease) return true;
		Error=TEXT("A retired social release cannot change its acceptance intent."); return false;
	}
	const bool bWasAccepted=ParticipantStagePhase==EParticipantStagePhase::Confirming || ParticipantStagePhase==EParticipantStagePhase::Confirmed;
	if (bAccepted!=bWasAccepted) { Error=TEXT("Social release intent differs from its actual acceptance boundary."); return false; }
	if (bParticipantStageOperation)
	{
		bParticipantStageReleaseRequested=true; bParticipantStageAcceptedRelease=bAccepted;
		Error=TEXT("Social release is retained until the active confirmation callback unwinds."); return false;
	}
	if (!bAccepted) for (const FStagedParticipant& Staged:StagedParticipants) RemoveParticipantRecordWithoutEvents(Staged.ActorKey);
	StagedParticipants.Reset(); StagedParticipantsByActor.Reset(); StagedParticipantsById.Reset(); ParticipantStageWorld.Reset();
	bParticipantStageReleaseRequested=false; bParticipantStageAcceptedRelease=bAccepted;
	ParticipantStagePhase=EParticipantStagePhase::Released; return true;
}

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
	StagedParticipants.Reset(); StagedParticipantsByActor.Reset(); StagedParticipantsById.Reset(); ParticipantStageWorld.Reset();
	ParticipantStagePhase=EParticipantStagePhase::Released; bParticipantStageInvalidated=true;
	NativeParticipantChanged.Clear();
	NativeParticipantUnregistered.Clear();
	Super::Deinitialize();
}

bool UProjectSocialSubsystem::RegisterOrUpdateParticipant(
	AActor* Participant,
	const FProjectSocialParticipantState& State)
{
	if (!IsValid(Participant) || State.ParticipantId.IsNone()
		|| IsStageLocked(TObjectKey<AActor>(Participant)) || IsStageIdentityLocked(State.ParticipantId))
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

	// An eviction notification may synchronously reserve the actor/ID for an attempt.
	if (!IsValid(Participant) || IsStageLocked(ParticipantKey) || IsStageIdentityLocked(State.ParticipantId)) return false;
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
	if (IsStageLocked(ParticipantKey)) return;
	Participant->OnDestroyed.RemoveAll(this);
	if (const FParticipantRecord* Record = ParticipantRecords.Find(ParticipantKey))
	{
		ParticipantsById.Remove(Record->State.ParticipantId);
	}

	RemoveConsentForKey(ParticipantKey);
	ParticipantRecords.Remove(ParticipantKey);
	OnParticipantUnregistered.Broadcast(Participant);
	NativeParticipantUnregistered.Broadcast(Participant);
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
	return Record && !Record->bUnpublished ? Record->Actor.Get() : nullptr;
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
		|| GrantingParticipant == OtherParticipant
		|| IsStageLocked(TObjectKey<AActor>(GrantingParticipant)) || IsStageLocked(TObjectKey<AActor>(OtherParticipant)))
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
	if (Participant && !IsStageLocked(TObjectKey<AActor>(Participant)))
	{
		RemoveConsentForKey(TObjectKey<AActor>(Participant));
		OnParticipantChanged.Broadcast(Participant);
		NativeParticipantChanged.Broadcast(Participant);
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
		if (Pair.Value.bUnpublished || !IsValid(Candidate)
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
	return IsValid(Participant) && !IsStageLocked(TObjectKey<AActor>(Participant))
		? ParticipantRecords.Find(TObjectKey<AActor>(Participant))
		: nullptr;
}

const UProjectSocialSubsystem::FParticipantRecord* UProjectSocialSubsystem::FindRecord(AActor* Participant) const
{
	const FParticipantRecord* Record = IsValid(Participant)
		? ParticipantRecords.Find(TObjectKey<AActor>(Participant))
		: nullptr;
	return Record && !Record->bUnpublished && Record->Actor.IsValid() ? Record : nullptr;
}

void UProjectSocialSubsystem::HandleParticipantDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor && IsStageLocked(TObjectKey<AActor>(DestroyedActor)))
	{
		bParticipantStageInvalidated=true;
		RemoveParticipantRecordWithoutEvents(TObjectKey<AActor>(DestroyedActor));
		return;
	}
	UnregisterParticipant(DestroyedActor);
}

void UProjectSocialSubsystem::PruneInvalidParticipants()
{
	TArray<TObjectKey<AActor>> InvalidKeys;
	for (const TPair<TObjectKey<AActor>, FParticipantRecord>& Pair : ParticipantRecords)
	{
		if (!Pair.Value.Actor.IsValid())
		{
			if (IsStageLocked(Pair.Key)) bParticipantStageInvalidated=true;
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
		NativeParticipantChanged.Broadcast(Participant);
	}
}

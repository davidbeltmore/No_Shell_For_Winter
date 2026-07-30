#include "Characters/ProjectCharacterIdentitySubsystem.h"

#include "AbilitySystemComponent.h"
#include "EFProjectEnemySettings.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Intimacy/ProjectIntimacyPartnerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCharacterIdentity, Log, All);

namespace ProjectCharacterIdentitySubsystemPrivate
{
	constexpr float IdentityAuditIntervalSeconds = 1.0f;

	static FGameplayTag GenderTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(TagName, false);
	}
}

void UProjectCharacterIdentitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadConfiguredClasses();
	bInitialPawnScanPending = true;

	if (UWorld* World = GetWorld(); IsValid(World) && World->IsGameWorld())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
	}
}

void UProjectCharacterIdentitySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); IsValid(World) && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	ActorSpawnedHandle.Reset();
	MaleCharacterClasses.Reset();
	FemaleCharacterClasses.Reset();
	PendingPawns.Reset();
	SecondsUntilNextIdentityAudit = 0.0f;
	bInitialPawnScanPending = false;
	Super::Deinitialize();
}

void UProjectCharacterIdentitySubsystem::Tick(const float DeltaTime)
{
	SecondsUntilNextIdentityAudit -= FMath::Max(0.0f, DeltaTime);
	if (bInitialPawnScanPending || SecondsUntilNextIdentityAudit <= 0.0f)
	{
		ProcessExistingPawns();
		bInitialPawnScanPending = false;
		SecondsUntilNextIdentityAudit = ProjectCharacterIdentitySubsystemPrivate::IdentityAuditIntervalSeconds;
	}

	TArray<TWeakObjectPtr<APawn>> PawnsToProcess = MoveTemp(PendingPawns);
	PendingPawns.Reset();
	for (const TWeakObjectPtr<APawn>& Pawn : PawnsToProcess)
	{
		ProcessPawn(Pawn.Get());
	}
}

TStatId UProjectCharacterIdentitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectCharacterIdentitySubsystem, STATGROUP_Tickables);
}

bool UProjectCharacterIdentitySubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

bool UProjectCharacterIdentitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UProjectCharacterIdentitySubsystem::ApplyGenderIdentity(APawn* Pawn, const FGameplayTag GenderTag)
{
	if (!IsValid(Pawn) || !GenderTag.IsValid())
	{
		return false;
	}

	const FGameplayTag MaleTag = ProjectCharacterIdentitySubsystemPrivate::GenderTag(TEXT("Project.Gender.Male"));
	const FGameplayTag FemaleTag = ProjectCharacterIdentitySubsystemPrivate::GenderTag(TEXT("Project.Gender.Female"));
	const FGameplayTag UnknownTag = ProjectCharacterIdentitySubsystemPrivate::GenderTag(TEXT("Project.Gender.Unknown"));

	if (UAbilitySystemComponent* AbilitySystem = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		for (const FGameplayTag CandidateTag : { MaleTag, FemaleTag, UnknownTag })
		{
			if (CandidateTag.IsValid())
			{
				AbilitySystem->SetLooseGameplayTagCount(CandidateTag, CandidateTag == GenderTag ? 1 : 0);
			}
		}
	}

	TInlineComponentArray<UProjectIntimacyPartnerComponent*> PartnerComponents(Pawn);
	if (PartnerComponents.IsEmpty())
	{
		if (UProjectIntimacyPartnerComponent* NewPartnerComponent = UProjectIntimacyPartnerComponent::FindOrCreateForActor(Pawn))
		{
			PartnerComponents.Add(NewPartnerComponent);
		}
	}

	for (UProjectIntimacyPartnerComponent* PartnerComponent : PartnerComponents)
	{
		if (IsValid(PartnerComponent))
		{
			PartnerComponent->GenderTag = GenderTag;
			// Every humanoid class in the project-owned identity registry is
			// authored as an adult. Consent remains a separate, opt-in flag.
			PartnerComponent->bAdultVerified = true;
		}
	}
	return !PartnerComponents.IsEmpty();
}

bool UProjectCharacterIdentitySubsystem::IsMaleGenderTag(const FGameplayTag GenderTag)
{
	const FGameplayTag MaleTag = ProjectCharacterIdentitySubsystemPrivate::GenderTag(TEXT("Project.Gender.Male"));
	return MaleTag.IsValid() && GenderTag.MatchesTagExact(MaleTag);
}

void UProjectCharacterIdentitySubsystem::LoadConfiguredClasses()
{
	MaleCharacterClasses.Reset();
	FemaleCharacterClasses.Reset();

	const UEFProjectEnemySettings* Settings = UEFProjectEnemySettings::Get();
	if (!Settings)
	{
		return;
	}

	auto LoadClasses = [](const TArray<FSoftClassPath>& ClassPaths, TArray<TSubclassOf<APawn>>& OutClasses, const TCHAR* RegistryLabel)
	{
		for (const FSoftClassPath& ClassPath : ClassPaths)
		{
			if (UClass* ResolvedClass = ClassPath.TryLoadClass<APawn>())
			{
				OutClasses.AddUnique(ResolvedClass);
			}
			else
			{
				UE_LOG(LogProjectCharacterIdentity, Warning, TEXT("Could not resolve %s identity class '%s'."), RegistryLabel, *ClassPath.ToString());
			}
		}
	};

	LoadClasses(Settings->MaleCharacterClasses, MaleCharacterClasses, TEXT("Male"));
	LoadClasses(Settings->FemaleCharacterClasses, FemaleCharacterClasses, TEXT("Female"));
}

void UProjectCharacterIdentitySubsystem::ProcessExistingPawns()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		ProcessPawn(*It);
	}
}

void UProjectCharacterIdentitySubsystem::HandleActorSpawned(AActor* SpawnedActor)
{
	if (APawn* Pawn = Cast<APawn>(SpawnedActor))
	{
		// OnActorSpawned fires before Blueprint construction and component registration finish.
		// Defer identity until the next world tick so we update the authoritative component.
		PendingPawns.AddUnique(Pawn);
	}
}

bool UProjectCharacterIdentitySubsystem::ProcessPawn(APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->IsTemplate() || Pawn->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Pawn->GetNetMode() == NM_Client)
	{
		return false;
	}

	const FGameplayTag GenderTag = ResolveConfiguredGenderTag(Pawn->GetClass());
	return GenderTag.IsValid() && ApplyGenderIdentity(Pawn, GenderTag);
}

FGameplayTag UProjectCharacterIdentitySubsystem::ResolveConfiguredGenderTag(const UClass* ActorClass) const
{
	const bool bMale = IsConfiguredClass(ActorClass, MaleCharacterClasses);
	const bool bFemale = IsConfiguredClass(ActorClass, FemaleCharacterClasses);
	if (bMale == bFemale)
	{
		if (bMale)
		{
			UE_LOG(LogProjectCharacterIdentity, Error, TEXT("Character class '%s' is registered as both Male and Female."), *GetNameSafe(ActorClass));
		}
		return FGameplayTag();
	}

	return bMale
		? ProjectCharacterIdentitySubsystemPrivate::GenderTag(TEXT("Project.Gender.Male"))
		: ProjectCharacterIdentitySubsystemPrivate::GenderTag(TEXT("Project.Gender.Female"));
}

bool UProjectCharacterIdentitySubsystem::IsConfiguredClass(
	const UClass* ActorClass,
	const TArray<TSubclassOf<APawn>>& RegisteredClasses) const
{
	if (!IsValid(ActorClass))
	{
		return false;
	}

	for (const TSubclassOf<APawn>& RegisteredClass : RegisteredClasses)
	{
		if (RegisteredClass && ActorClass->IsChildOf(RegisteredClass.Get()))
		{
			return true;
		}
	}

	return false;
}

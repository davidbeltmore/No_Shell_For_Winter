#include "Companions/ProjectCompanionRuntimeAdapter.h"

#include "ACFAIController.h"
#include "ACFAITypes.h"
#include "ACFGASAttributesComponent.h"
#include "Actors/ACFCharacter.h"
#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Components/ACFCompanionGroupAIComponent.h"
#include "Components/ACFTeamComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/ACFCharacterDataAsset.h"
#include "Data/ACFCharacterInitializerComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Groups/ACFCompanionsPlayerController.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Social/ProjectSocialSubsystem.h"
#include "Social/ProjectSocialTypes.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCompanionRuntime, Log, All);

namespace ProjectCompanionRuntimeAdapterPrivate
{
	FProjectCompanionSpawnResult Failure(const EProjectCompanionSpawnFailure Code, const FString& Diagnostic)
	{
		FProjectCompanionSpawnResult Result;
		Result.Failure = Code;
		Result.Diagnostic = Diagnostic;
		return Result;
	}

	uint32 StableGuidHash(const FGuid& Guid)
	{
		const FString Text = Guid.ToString(EGuidFormats::Digits);
		uint32 Hash = 2166136261u;
		for (const TCHAR Character : Text)
		{
			Hash ^= static_cast<uint32>(Character);
			Hash *= 16777619u;
		}
		return Hash;
	}

	bool SetInitializerLevel(UACFCharacterInitializerComponent* Initializer, const int32 PhysicalLevel)
	{
		FIntProperty* LevelProperty = FindFProperty<FIntProperty>(Initializer->GetClass(), TEXT("CharacterInitLevel"));
		if (!LevelProperty)
		{
			return false;
		}
		LevelProperty->SetPropertyValue_InContainer(Initializer, PhysicalLevel);
		return Initializer->GetAutoInitLevel() == PhysicalLevel;
	}

	bool SetInitializerDataAsset(
		UACFCharacterInitializerComponent* Initializer,
		UACFCharacterDataAsset* DataAsset)
	{
		FObjectPropertyBase* DataProperty = FindFProperty<FObjectPropertyBase>(
			Initializer->GetClass(), TEXT("CharacterInitDataAsset"));
		if (!DataProperty)
		{
			return false;
		}
		DataProperty->SetObjectPropertyValue_InContainer(Initializer, DataAsset);
		return Initializer->GetAutoInitDataAsset() == DataAsset;
	}

	bool ConfigureInstanceOnlyStatisticsRepair(
		AACFCharacter* Character,
		UACFCharacterInitializerComponent* Initializer,
		const FProjectCompanionDefinition& Definition,
		FString& OutError,
		bool& bOutRepairApplied)
	{
		OutError.Reset();
		bOutRepairApplied = false;
		UACFCharacterDataAsset* SourceData = Initializer->GetAutoInitDataAsset();
		if (!SourceData)
		{
			// The migrated companion Blueprints predate ACF 4.3's DataAsset-based
			// initializer. Preserve that legacy instance contract: validate its
			// attributes component directly and repair only a missing row. Creating
			// an empty CharacterDataAsset here would clear authored abilities,
			// equipment and appearance during BeginPlay.
			UACFGASAttributesComponent* Attributes =
				Character->FindComponentByClass<UACFGASAttributesComponent>();
			if (!Attributes)
			{
				OutError = TEXT("The legacy ACF companion has neither CharacterInitDataAsset nor attributes component.");
				return false;
			}
			const FDataTableRowHandle ExistingRow = Attributes->GetCharacterRow();
			if (ExistingRow.DataTable && !ExistingRow.RowName.IsNone())
			{
				return true;
			}
			if (!Definition.bRepairMissingStatisticsRow)
			{
				OutError = TEXT("The legacy ACF statistics row is missing and instance repair is disabled.");
				return false;
			}
			UDataTable* RepairTable = Definition.StatisticsRepairDataTable.Get();
			if (!RepairTable)
			{
				OutError = FString::Printf(
					TEXT("The preloaded legacy statistics repair table is unavailable: %s"),
					*Definition.StatisticsRepairDataTable.ToSoftObjectPath().ToString());
				return false;
			}
			if (!RepairTable->GetRowMap().Contains(Definition.StatisticsRepairRow))
			{
				OutError = FString::Printf(
					TEXT("Legacy statistics repair row '%s' is absent from %s."),
					*Definition.StatisticsRepairRow.ToString(),
					*RepairTable->GetPathName());
				return false;
			}
			FDataTableRowHandle RepairedRow;
			RepairedRow.DataTable = RepairTable;
			RepairedRow.RowName = Definition.StatisticsRepairRow;
			Attributes->SetCharacterRow(RepairedRow);
			const FDataTableRowHandle AppliedRow = Attributes->GetCharacterRow();
			if (AppliedRow.DataTable != RepairTable
				|| AppliedRow.RowName != Definition.StatisticsRepairRow)
			{
				OutError = TEXT("The legacy ACF statistics row did not accept its instance-only repair.");
				return false;
			}
			bOutRepairApplied = true;
			return true;
		}

		if (SourceData->CharacterRow.DataTable && !SourceData->CharacterRow.RowName.IsNone())
		{
			return true;
		}
		if (!Definition.bRepairMissingStatisticsRow)
		{
			OutError = TEXT("The ACF statistics row is missing and instance repair is disabled.");
			return false;
		}

		UDataTable* RepairTable = Definition.StatisticsRepairDataTable.Get();
		if (!RepairTable)
		{
			OutError = FString::Printf(
				TEXT("The preloaded statistics repair table is unavailable: %s"),
				*Definition.StatisticsRepairDataTable.ToSoftObjectPath().ToString());
			return false;
		}
		if (!RepairTable->GetRowMap().Contains(Definition.StatisticsRepairRow))
		{
			OutError = FString::Printf(
				TEXT("Statistics repair row '%s' is absent from %s."),
				*Definition.StatisticsRepairRow.ToString(), *RepairTable->GetPathName());
			return false;
		}

		UACFCharacterDataAsset* InstanceData = DuplicateObject<UACFCharacterDataAsset>(SourceData, Character);
		if (!InstanceData)
		{
			OutError = TEXT("Could not create the transient per-companion CharacterDataAsset clone.");
			return false;
		}
		InstanceData->ClearFlags(RF_Public | RF_Standalone);
		InstanceData->SetFlags(RF_Transient);
		InstanceData->CharacterRow.DataTable = RepairTable;
		InstanceData->CharacterRow.RowName = Definition.StatisticsRepairRow;

		if (!SetInitializerDataAsset(Initializer, InstanceData))
		{
			OutError = TEXT("Could not bind the transient statistics clone to the ACF initializer instance.");
			return false;
		}
		bOutRepairApplied = true;
		return true;
	}

	bool RegisterSocialActor(
		AACFCharacter* Character,
		const FProjectCompanionDefinition& Definition,
		const bool bRegisterAsRecruited)
	{
		UGameInstance* GameInstance = Character && Character->GetWorld()
			? Character->GetWorld()->GetGameInstance()
			: nullptr;
		UProjectSocialSubsystem* Social = GameInstance
			? GameInstance->GetSubsystem<UProjectSocialSubsystem>()
			: nullptr;
		if (!Social)
		{
			return false;
		}

		FProjectSocialParticipantState State;
		Social->TryGetParticipantState(Character, State);
		State.ParticipantId = FName(*FString::Printf(
			TEXT("Companion.%s"), *Definition.StableCompanionId.ToString(EGuidFormats::Digits)));
		State.bAlive = true;
		State.bConscious = true;
		State.bHostile = false;
		State.bInCombat = false;
		State.bInSafeLocation = true;
		State.bRecruitable = Definition.Lifecycle == EProjectCompanionLifecycle::Recruitable;
		State.bRecruitedCompanion = bRegisterAsRecruited;
		return Social->RegisterOrUpdateParticipant(Character, State);
	}
}

bool UProjectCompanionRuntimeAdapter::PrepareDeferredCompanion(
	AACFCharacter* DeferredCharacter,
	const FProjectCompanionDefinition& Definition,
	FString& OutError,
	bool* OutStatisticsRepairApplied)
{
	using namespace ProjectCompanionRuntimeAdapterPrivate;

	OutError.Reset();
	if (OutStatisticsRepairApplied)
	{
		*OutStatisticsRepairApplied = false;
	}
	FString DefinitionError;
	if (!DeferredCharacter || DeferredCharacter->HasActorBegunPlay()
		|| !Definition.IsValid(DefinitionError))
	{
		OutError = DefinitionError.IsEmpty()
			? TEXT("The companion actor is null or has already begun play.")
			: DefinitionError;
		return false;
	}
	UWorld* World = DeferredCharacter->GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !DeferredCharacter->HasAuthority())
	{
		OutError = TEXT("Deferred companion configuration requires the authoritative world.");
		return false;
	}

	UClass* ExpectedClass = Definition.CharacterClass.Get();
	if (!ExpectedClass || !ExpectedClass->IsChildOf(AACFCharacter::StaticClass())
		|| !DeferredCharacter->IsA(ExpectedClass))
	{
		OutError = TEXT("The deferred actor does not match the preloaded companion class.");
		return false;
	}

	UACFCharacterInitializerComponent* Initializer =
		DeferredCharacter->FindComponentByClass<UACFCharacterInitializerComponent>();
	if (!Initializer)
	{
		OutError = TEXT("The companion class has no UACFCharacterInitializerComponent.");
		return false;
	}

	const int32 PhysicalACFLevel = FMath::Clamp(Definition.ResolvedLevel, 1, 100);
	if (!SetInitializerLevel(Initializer, PhysicalACFLevel))
	{
		OutError = TEXT("The adapter could not set CharacterInitLevel on the deferred instance.");
		return false;
	}
	bool bRepairApplied = false;
	const bool bConfigured = ConfigureInstanceOnlyStatisticsRepair(
		DeferredCharacter, Initializer, Definition, OutError, bRepairApplied);
	if (OutStatisticsRepairApplied)
	{
		*OutStatisticsRepairApplied = bRepairApplied;
	}
	return bConfigured;
}

FProjectCompanionSpawnResult UProjectCompanionRuntimeAdapter::FinalizeDeferredCompanion(
	AACFCharacter* Character,
	const FProjectCompanionDefinition& Definition,
	UACFCompanionGroupAIComponent* CompanionGroup,
	const bool bRegisterAsRecruited)
{
	using namespace ProjectCompanionRuntimeAdapterPrivate;

	if (!IsValid(Character) || !Character->HasActorBegunPlay())
	{
		return Failure(EProjectCompanionSpawnFailure::SpawnFailed,
			TEXT("Deferred companion did not survive FinishSpawning/BeginPlay."));
	}
	if (bRegisterAsRecruited && !CompanionGroup)
	{
		return Failure(EProjectCompanionSpawnFailure::CompanionGroupMissing,
			TEXT("A persistent roster projection requires the ACF companion group."));
	}

	UACFGASAttributesComponent* Attributes = Character->FindComponentByClass<UACFGASAttributesComponent>();
	const FDataTableRowHandle RealizedRow = Attributes ? Attributes->GetCharacterRow() : FDataTableRowHandle();
	if (!Attributes || !RealizedRow.DataTable || RealizedRow.RowName.IsNone())
	{
		return Failure(EProjectCompanionSpawnFailure::StatisticsRepairFailed,
			TEXT("The realized companion has no valid instance statistics row after FinishSpawning."));
	}

	UProjectEnemyLevelComponent* LogicalLevel = Character->FindComponentByClass<UProjectEnemyLevelComponent>();
	if (!LogicalLevel)
	{
		LogicalLevel = NewObject<UProjectEnemyLevelComponent>(
			Character, TEXT("ProjectCompanionLogicalLevel"), RF_Transient);
		if (!LogicalLevel)
		{
			return Failure(EProjectCompanionSpawnFailure::LogicalLevelComponentFailed,
				TEXT("Could not allocate project logical-level metadata."));
		}
		Character->AddInstanceComponent(LogicalLevel);
		LogicalLevel->RegisterComponent();
	}
	LogicalLevel->SetAssignedLevelData(
		FMath::Max(1, ((Definition.ResolvedLevel - 1) / 10) + 1),
		Definition.ResolvedLevel,
		Definition.ResolvedLevel,
		Definition.ResolvedLevel,
		FMath::Clamp((static_cast<float>(FMath::Min(Definition.ResolvedLevel, 100)) - 1.0f) / 99.0f, 0.0f, 1.0f));

	const UProjectEnemyLevelSettings* LevelSettings = UProjectEnemyLevelSettings::Get();
	FString ScalingFailure;
	FString AscentSyncDiagnostic;
	LogicalLevel->SyncAssignedLevelToAscent(AscentSyncDiagnostic);
	if (!LevelSettings
		|| !LogicalLevel->CaptureGameplayScalingBaseline(*LevelSettings, ScalingFailure)
		|| !LogicalLevel->ApplyGameplayScaling(*LevelSettings, ScalingFailure))
	{
		return Failure(
			EProjectCompanionSpawnFailure::LogicalLevelComponentFailed,
			ScalingFailure.IsEmpty()
				? TEXT("Project companion gameplay scaling settings are unavailable.")
				: ScalingFailure);
	}

	if (bRegisterAsRecruited)
	{
		if (!CompanionGroup->AddExistingCharacterToGroup(Character))
		{
			return Failure(EProjectCompanionSpawnFailure::GroupRegistrationFailed,
				TEXT("ACF rejected AddExistingCharacterToGroup."));
		}
		TArray<FAIAgentsInfo> Agents;
		CompanionGroup->GetGroupAgents(Agents);
		if (!Agents.Contains(Character))
		{
			CompanionGroup->RemoveAgentFromGroup(Character);
			return Failure(EProjectCompanionSpawnFailure::GroupRegistrationFailed,
				TEXT("The companion was not present in the ACF group after registration."));
		}
	}
	else if (!Character->GetController())
	{
		Character->SpawnDefaultController();
	}

	if (!Cast<AACFAIController>(Character->GetController()))
	{
		if (bRegisterAsRecruited && CompanionGroup)
		{
			CompanionGroup->RemoveAgentFromGroup(Character);
		}
		return Failure(EProjectCompanionSpawnFailure::ControllerMissing,
			TEXT("ACF did not create a compatible AI controller."));
	}
	if (!Character->FindComponentByClass<UACFTeamComponent>())
	{
		return Failure(EProjectCompanionSpawnFailure::TeamComponentMissing,
			TEXT("The realized companion has no ACF team component."));
	}
	if (!Character->FindComponentByClass<UAIPerceptionStimuliSourceComponent>())
	{
		return Failure(EProjectCompanionSpawnFailure::PerceptionComponentMissing,
			TEXT("The realized companion has no perception stimuli source."));
	}
	if (!RegisterSocialActor(Character, Definition, bRegisterAsRecruited))
	{
		return Failure(EProjectCompanionSpawnFailure::SocialRegistrationFailed,
			TEXT("ProjectSocialSubsystem rejected the live companion projection."));
	}

	FProjectCompanionSpawnResult Result;
	Result.bSucceeded = true;
	Result.Failure = EProjectCompanionSpawnFailure::None;
	Result.SpawnedCharacter = Character;
	Result.Diagnostic = FString::Printf(
		TEXT("catalog=%s class=%s controller=%s stats_table=%s stats_row=%s logical=%d physical=%d team=true perception=true social=true"),
		*Definition.ContentId.ToString(),
		*Character->GetClass()->GetPathName(),
		*Character->GetController()->GetClass()->GetPathName(),
		*GetPathNameSafe(RealizedRow.DataTable),
		*RealizedRow.RowName.ToString(),
		Definition.ResolvedLevel,
		FMath::Clamp(Definition.ResolvedLevel, 1, 100));
	return Result;
}

FProjectCompanionSpawnResult UProjectCompanionRuntimeAdapter::SpawnAndRegisterCompanion(
	UObject* WorldContextObject,
	const FProjectCompanionDefinition& Definition,
	const FTransform& SpawnTransform,
	UACFCompanionGroupAIComponent* CompanionGroup)
{
	using namespace ProjectCompanionRuntimeAdapterPrivate;

	FString DefinitionError;
	if (!Definition.IsValid(DefinitionError) || !WorldContextObject || !CompanionGroup)
	{
		return Failure(EProjectCompanionSpawnFailure::InvalidRequest,
			DefinitionError.IsEmpty() ? TEXT("World context or companion group is invalid.") : DefinitionError);
	}

	UWorld* World = WorldContextObject->GetWorld();
	UClass* LoadedClass = Definition.CharacterClass.Get();
	if (!World || !LoadedClass || !LoadedClass->IsChildOf(AACFCharacter::StaticClass()))
	{
		return Failure(EProjectCompanionSpawnFailure::ClassNotPreloaded,
			TEXT("The companion class must be valid, loaded and derived from AACFCharacter."));
	}
	if (World->GetNetMode() == NM_Client || !CompanionGroup->GetOwner() || !CompanionGroup->GetOwner()->HasAuthority())
	{
		return Failure(EProjectCompanionSpawnFailure::NoAuthority,
			TEXT("Companions may only be materialized by the authoritative world."));
	}

	AACFCharacter* Character = World->SpawnActorDeferred<AACFCharacter>(
		LoadedClass,
		SpawnTransform,
		CompanionGroup->GetOwner(),
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Character)
	{
		return Failure(EProjectCompanionSpawnFailure::SpawnFailed, TEXT("SpawnActorDeferred returned null."));
	}

	FString RepairError;
	if (!PrepareDeferredCompanion(Character, Definition, RepairError))
	{
		Character->Destroy();
		const bool bMissingAsset = RepairError.Contains(TEXT("unavailable"));
		const bool bMissingRow = RepairError.Contains(TEXT("absent"));
		return Failure(
			bMissingAsset ? EProjectCompanionSpawnFailure::StatisticsRepairAssetMissing
				: bMissingRow ? EProjectCompanionSpawnFailure::StatisticsRepairRowMissing
				: EProjectCompanionSpawnFailure::StatisticsRepairFailed,
			RepairError);
	}

	Character->FinishSpawning(SpawnTransform, true);
	FProjectCompanionSpawnResult Result = FinalizeDeferredCompanion(
		Character, Definition, CompanionGroup, true);
	if (!Result.bSucceeded)
	{
		RollbackSpawnedCompanion(Character, CompanionGroup);
	}
	return Result;
}

bool UProjectCompanionRuntimeAdapter::FindDeterministicSafeSpawnTransform(
	UWorld* World,
	const APawn* PlayerPawn,
	const FGuid& StableCompanionId,
	TSubclassOf<AACFCharacter> CharacterClass,
	const TArray<FVector>& ReservedLocations,
	FTransform& OutTransform,
	FString& OutError)
{
	OutTransform = FTransform::Identity;
	OutError.Reset();
	if (!World || !PlayerPawn || !StableCompanionId.IsValid() || !CharacterClass)
	{
		OutError = TEXT("Safe-spawn input is invalid.");
		return false;
	}

	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Navigation)
	{
		OutError = TEXT("No NavigationSystemV1 is available.");
		return false;
	}

	const AACFCharacter* CharacterCDO = CharacterClass->GetDefaultObject<AACFCharacter>();
	const UCapsuleComponent* Capsule = CharacterCDO ? CharacterCDO->GetCapsuleComponent() : nullptr;
	const float Radius = Capsule ? FMath::Max(20.0f, Capsule->GetUnscaledCapsuleRadius()) : 42.0f;
	const float HalfHeight = Capsule ? FMath::Max(Radius, Capsule->GetUnscaledCapsuleHalfHeight()) : 96.0f;
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const TArray<float> RingRadii = { 180.0f, 260.0f, 340.0f, 440.0f, 560.0f };
	constexpr int32 SpokesPerRing = 12;
	const double InitialAngle = static_cast<double>(ProjectCompanionRuntimeAdapterPrivate::StableGuidHash(StableCompanionId) % 360u);

	FCollisionQueryParams CollisionParams(SCENE_QUERY_STAT(ProjectCompanionSafeSpawn), false);
	CollisionParams.AddIgnoredActor(PlayerPawn);

	for (int32 RingIndex = 0; RingIndex < RingRadii.Num(); ++RingIndex)
	{
		for (int32 SpokeIndex = 0; SpokeIndex < SpokesPerRing; ++SpokeIndex)
		{
			const double Degrees = InitialAngle + (30.0 * SpokeIndex) + (15.0 * RingIndex);
			const double Radians = FMath::DegreesToRadians(Degrees);
			const FVector RawCandidate = PlayerLocation + FVector(
				FMath::Cos(Radians) * RingRadii[RingIndex],
				FMath::Sin(Radians) * RingRadii[RingIndex],
				0.0);

			FNavLocation Projected;
			if (!Navigation->ProjectPointToNavigation(RawCandidate, Projected, FVector(100.0f, 100.0f, 300.0f)))
			{
				continue;
			}

			bool bReserved = false;
			for (const FVector& Reserved : ReservedLocations)
			{
				if (FVector::DistSquared2D(Reserved, Projected.Location) < FMath::Square((Radius * 2.0f) + 20.0f))
				{
					bReserved = true;
					break;
				}
			}
			if (bReserved)
			{
				continue;
			}

			UNavigationPath* Path = Navigation->FindPathToLocationSynchronously(
				World, PlayerLocation, Projected.Location, const_cast<APawn*>(PlayerPawn));
			if (!Path || !Path->IsValid() || Path->IsPartial())
			{
				continue;
			}

			const FVector SpawnLocation = Projected.Location + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
			if (World->OverlapBlockingTestByChannel(
				SpawnLocation,
				FQuat::Identity,
				ECC_Pawn,
				FCollisionShape::MakeCapsule(Radius, HalfHeight),
				CollisionParams))
			{
				continue;
			}

			const FRotator Facing = (PlayerLocation - SpawnLocation).Rotation();
			OutTransform = FTransform(FRotator(0.0f, Facing.Yaw, 0.0f), SpawnLocation);
			return true;
		}
	}

	OutError = TEXT("The deterministic NavMesh ring search found no collision-free reachable location.");
	return false;
}

UACFCompanionGroupAIComponent* UProjectCompanionRuntimeAdapter::ResolveCompanionGroup(const APawn* PlayerPawn)
{
	if (!PlayerPawn)
	{
		return nullptr;
	}
	if (const AACFCompanionsPlayerController* CompanionController =
		Cast<AACFCompanionsPlayerController>(PlayerPawn->GetController()))
	{
		return CompanionController->GetCompanionsComponent();
	}
	if (AController* Controller = PlayerPawn->GetController())
	{
		if (UACFCompanionGroupAIComponent* Component =
			Controller->FindComponentByClass<UACFCompanionGroupAIComponent>())
		{
			return Component;
		}
	}
	return nullptr;
}

void UProjectCompanionRuntimeAdapter::RollbackSpawnedCompanion(
	AACFCharacter* Character,
	UACFCompanionGroupAIComponent* CompanionGroup)
{
	if (!Character)
	{
		return;
	}
	if (UWorld* World = Character->GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UProjectSocialSubsystem* Social = GameInstance->GetSubsystem<UProjectSocialSubsystem>())
			{
				Social->UnregisterParticipant(Character);
			}
		}
	}
	if (CompanionGroup)
	{
		CompanionGroup->RemoveAgentFromGroup(Character);
	}
	Character->Destroy();
}

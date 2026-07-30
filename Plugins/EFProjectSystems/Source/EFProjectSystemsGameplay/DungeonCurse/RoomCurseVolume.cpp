#include "DungeonCurse/RoomCurseVolume.h"

#include "Characters/ProjectEnemyLevelComponent.h"
#include "Characters/ProjectEnemyLevelLogic.h"
#include "Characters/ProjectEnemyLevelSettings.h"
#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "DrawDebugHelpers.h"
#include "DungeonCurse/DungeonCurseComponent.h"
#include "DungeonCurse/DungeonCurseEnemyInterface.h"
#include "DungeonCurse/DungeonCurseListenerInterface.h"
#include "DungeonCurse/DungeonCurseManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ProjectActivityFeedSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoomCurseVolume, Log, All);

ARoomCurseVolume::ARoomCurseVolume()
{
	PrimaryActorTick.bCanEverTick = true;

	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("CurseTrigger"));
	SetRootComponent(BoxComponent);
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxComponent->SetCollisionObjectType(ECC_WorldDynamic);
	BoxComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BoxComponent->SetGenerateOverlapEvents(true);
	BoxComponent->SetHiddenInGame(true);

	RoomType = EGeneratedRoomType::Unknown;
	bActive = false;
	bDebugDraw = false;
	DebugColor = FColor(180, 40, 255);
	VolumeExtent = FVector(600.0f, 600.0f, 180.0f);
	TickInterval = 1.0f;
	bEnableMaxLevelEnemyCurse = true;
	MaxEnemyLevelForCurrentFloor = 0;
}

void ARoomCurseVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateBoxExtent();
}

void ARoomCurseVolume::BeginPlay()
{
	Super::BeginPlay();
	UpdateBoxExtent();

	if (BoxComponent)
	{
		BoxComponent->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleBeginOverlap);
		BoxComponent->OnComponentEndOverlap.AddDynamic(this, &ThisClass::HandleEndOverlap);
	}

	NotifyListenersDiscovered();

	if (WantsEnemyLevelProcessing())
	{
		ApplyEnemyLevelCurseToCurrentEnemies();
	}
}

void ARoomCurseVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const FName SourceID = GetCurseSourceID();
	for (TPair<TObjectKey<AActor>, TWeakObjectPtr<AActor>>& Pair : ActiveTargets)
	{
		if (AActor* TargetActor = Pair.Value.Get())
		{
			if (UDungeonCurseComponent* CurseComponent = TargetActor->FindComponentByClass<UDungeonCurseComponent>())
			{
				CurseComponent->RemoveRoomCurse(SourceID);
			}
		}
	}

	RestoreModifiedLights();
	ActiveTargets.Reset();
	Super::EndPlay(EndPlayReason);
}

void ARoomCurseVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDebugDraw || AssignedCurse.bDebugEnabled)
	{
		DrawVolumeDebug();
	}
}

void ARoomCurseVolume::InitializeFromDetectedRoom(const FDetectedDungeonCurseRoom& RoomData, const FRoomCurseDefinition& Curse, ADungeonCurseManager* InManager)
{
	AssignedCurse = Curse;
	RoomType = RoomData.RoomType;
	VolumeExtent = RoomData.Extent.ComponentMax(FVector(1.0f, 1.0f, 1.0f));
	SourceActor = RoomData.SourceActor;
	SourceMarker = RoomData.SourceMarker;
	OwningManager = InManager;
	CurseSourceID = UDungeonCurseComponent::MakeFallbackSourceID(this, AssignedCurse);

	if (InManager)
	{
		PlayerClassFilter = InManager->PlayerClassFilter;
		bEnableMaxLevelEnemyCurse = InManager->bEnableMaxLevelEnemyCurse;
		MaxEnemyLevelForCurrentFloor = InManager->MaxEnemyLevelForCurrentFloor;
		bDebugDraw = InManager->bDebugDraw;
	}

	SetActorLocationAndRotation(RoomData.Center, RoomData.Rotation);
	UpdateBoxExtent();
}

FName ARoomCurseVolume::GetCurseSourceID() const
{
	return !CurseSourceID.IsNone() ? CurseSourceID : UDungeonCurseComponent::MakeFallbackSourceID(this, AssignedCurse);
}

bool ARoomCurseVolume::ContainsActor(AActor* Actor) const
{
	return IsValid(Actor) && GetCurseBounds().IsInsideOrOn(Actor->GetActorLocation());
}

void ARoomCurseVolume::ApplyEnemyLevelCurseToCurrentEnemies()
{
	UWorld* World = GetWorld();
	if (!World || !WantsEnemyLevelProcessing())
	{
		return;
	}

	int32 AppliedCount = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!IsValid(Pawn) || Pawn->IsPlayerControlled() || !ContainsActor(Pawn))
		{
			continue;
		}

		if (ApplyEnemyLevelCurseToActor(Pawn))
		{
			++AppliedCount;
		}
	}

	if (AppliedCount == 0 && !bWarnedNoEnemies)
	{
		bWarnedNoEnemies = true;
		UE_LOG(LogRoomCurseVolume, Warning, TEXT("MaxLevelEnemies curse %s found no enemies inside %s."),
			*ResolveDisplayCurseID().ToString(),
			*GetNameSafe(this));
	}
}

bool ARoomCurseVolume::ApplyEnemyLevelCurseToActor(AActor* EnemyActor)
{
	if (!WantsEnemyLevelProcessing() || !IsValid(EnemyActor) || !ContainsActor(EnemyActor))
	{
		return false;
	}

	APawn* EnemyPawn = Cast<APawn>(EnemyActor);
	if (EnemyPawn && EnemyPawn->IsPlayerControlled())
	{
		return false;
	}

	const UProjectEnemyLevelSettings* Settings = UProjectEnemyLevelSettings::Get();
	const int32 SettingsDefaultTier = Settings ? FMath::Max(Settings->DefaultWorldTier, 1) : 1;
	FProjectEnemyLevelContext LevelContext;
	if (Settings)
	{
		LevelContext = FProjectEnemyLevelLogic::BuildLevelContext(SettingsDefaultTier, *Settings);
	}

	UProjectEnemyLevelComponent* LevelComponent = EnemyActor->FindComponentByClass<UProjectEnemyLevelComponent>();
	const int32 WorldTier = LevelComponent && LevelComponent->HasAssignedLevel() ? LevelComponent->GetWorldTier() : LevelContext.WorldTier;
	const int32 MinLevel = LevelComponent && LevelComponent->HasAssignedLevel() ? LevelComponent->GetMinRolledLevel() : LevelContext.MinLevel;
	const int32 ExistingMaxLevel = LevelComponent && LevelComponent->HasAssignedLevel() ? LevelComponent->GetMaxRolledLevel() : LevelContext.MaxLevel;
	const int32 DesiredLevel = MaxEnemyLevelForCurrentFloor > 0 ? MaxEnemyLevelForCurrentFloor : ExistingMaxLevel;
	const int32 MaxLevel = FMath::Max(DesiredLevel, ExistingMaxLevel);
	const int32 ClampedDesiredLevel = FMath::Clamp(DesiredLevel, FMath::Max(MinLevel, 1), FMath::Max(MaxLevel, 1));

	bool bApplied = false;
	if (EnemyActor->GetClass()->ImplementsInterface(UDungeonCurseEnemyInterface::StaticClass()))
	{
		IDungeonCurseEnemyInterface::Execute_SetEnemyMaxLevelForFloor(EnemyActor, true);
		IDungeonCurseEnemyInterface::Execute_ApplyEnemyLevelOverride(EnemyActor, ClampedDesiredLevel);
		bApplied = true;
	}

	if (LevelComponent && Settings)
	{
		LevelComponent->ResetGameplayScalingState();
		LevelComponent->SetAssignedLevelData(
			WorldTier,
			FMath::Max(MinLevel, 1),
			FMath::Max(MaxLevel, FMath::Max(MinLevel, 1)),
			ClampedDesiredLevel,
			FProjectEnemyLevelLogic::NormalizeEnemyLevel(ClampedDesiredLevel, *Settings));

		FString DiagnosticMessage;
		LevelComponent->SyncAssignedLevelToAscent(DiagnosticMessage);

		FString BaselineFailureReason;
		if (!LevelComponent->CaptureGameplayScalingBaseline(*Settings, BaselineFailureReason))
		{
			UE_LOG(LogRoomCurseVolume, Warning, TEXT("MaxLevelEnemies could not capture scaling baseline for %s: %s"),
				*GetNameSafe(EnemyActor),
				*BaselineFailureReason);
		}

		FString ScalingFailureReason;
		if (!LevelComponent->ApplyGameplayScaling(*Settings, ScalingFailureReason))
		{
			UE_LOG(LogRoomCurseVolume, Warning, TEXT("MaxLevelEnemies could not apply scaling to %s: %s"),
				*GetNameSafe(EnemyActor),
				*ScalingFailureReason);
		}

		UE_LOG(LogRoomCurseVolume, Log, TEXT("MaxLevelEnemies forced %s to level %d. %s"),
			*GetNameSafe(EnemyActor),
			ClampedDesiredLevel,
			DiagnosticMessage.IsEmpty() ? TEXT("No ARS sync details.") : *DiagnosticMessage);

		bApplied = true;
	}

	if (!bApplied)
	{
		UE_LOG(LogRoomCurseVolume, Warning, TEXT("MaxLevelEnemies found %s inside %s, but no enemy level component or curse enemy interface was available."),
			*GetNameSafe(EnemyActor),
			*GetNameSafe(this));
	}

	return bApplied;
}

bool ARoomCurseVolume::WantsEnemyLevelProcessing() const
{
	return bEnableMaxLevelEnemyCurse
		&& (AssignedCurse.CurseType == ERoomCurseType::MaxLevelEnemies || AssignedCurse.bForceMaxLevelEnemies);
}

void ARoomCurseVolume::RestoreModifiedLights()
{
	for (TPair<TWeakObjectPtr<ULightComponent>, float>& Pair : ModifiedLightIntensities)
	{
		if (ULightComponent* LightComponent = Pair.Key.Get())
		{
			LightComponent->SetIntensity(Pair.Value);
		}
	}

	ModifiedLightIntensities.Reset();
	bLightReductionApplied = false;
}

void ARoomCurseVolume::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!IsValidTarget(OtherActor))
	{
		return;
	}

	const TObjectKey<AActor> TargetKey(OtherActor);
	if (ActiveTargets.Contains(TargetKey))
	{
		return;
	}

	ActiveTargets.Add(TargetKey, OtherActor);
	bActive = ActiveTargets.Num() > 0;

	if (UDungeonCurseComponent* CurseComponent = ResolveOrCreateCurseComponent(OtherActor))
	{
		CurseComponent->ApplyRoomCurse(this, AssignedCurse, TickInterval);
	}

	ApplyVolumeOnlyEffects(OtherActor);
	NotifyListenersEntered();
}

void ARoomCurseVolume::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!IsValid(OtherActor))
	{
		return;
	}

	const TObjectKey<AActor> TargetKey(OtherActor);
	if (!ActiveTargets.Remove(TargetKey))
	{
		return;
	}

	if (UDungeonCurseComponent* CurseComponent = OtherActor->FindComponentByClass<UDungeonCurseComponent>())
	{
		if (AssignedCurse.bRemoveOnExit)
		{
			CurseComponent->RemoveRoomCurse(GetCurseSourceID());
		}
	}

	bActive = ActiveTargets.Num() > 0;
	if (!bActive && AssignedCurse.bRemoveOnExit)
	{
		RemoveVolumeOnlyEffects();
	}

	NotifyListenersExited();
}

void ARoomCurseVolume::UpdateBoxExtent()
{
	VolumeExtent = VolumeExtent.ComponentMax(FVector(1.0f, 1.0f, 1.0f));
	if (BoxComponent)
	{
		BoxComponent->SetBoxExtent(VolumeExtent);
	}
}

bool ARoomCurseVolume::IsValidTarget(AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	if (PlayerClassFilter && !Actor->IsA(PlayerClassFilter))
	{
		return false;
	}

	if (Cast<APawn>(Actor))
	{
		return true;
	}

	return Actor->GetClass()->ImplementsInterface(UDungeonCurseListenerInterface::StaticClass())
		|| Actor->FindComponentByClass<UDungeonCurseComponent>() != nullptr;
}

UDungeonCurseComponent* ARoomCurseVolume::ResolveOrCreateCurseComponent(AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}

	if (UDungeonCurseComponent* ExistingComponent = Actor->FindComponentByClass<UDungeonCurseComponent>())
	{
		if (!ExistingComponent->IsRegistered())
		{
			ExistingComponent->RegisterComponent();
		}
		return ExistingComponent;
	}

	UDungeonCurseComponent* NewComponent = NewObject<UDungeonCurseComponent>(Actor, UDungeonCurseComponent::StaticClass(), TEXT("DungeonCurseComponent"));
	if (!NewComponent)
	{
		return nullptr;
	}

	Actor->AddInstanceComponent(NewComponent);
	NewComponent->OnComponentCreated();
	NewComponent->RegisterComponent();
	NewComponent->Activate(true);
	return NewComponent;
}

void ARoomCurseVolume::ApplyVolumeOnlyEffects(AActor* TargetActor)
{
	switch (AssignedCurse.CurseType)
	{
	case ERoomCurseType::LightReduction:
		ApplyLightReduction();
		break;
	case ERoomCurseType::MaxLevelEnemies:
		ApplyEnemyLevelCurseToCurrentEnemies();
		break;
	case ERoomCurseType::TynaWhisper:
		PlayWhisper(TargetActor);
		SpawnOptionalVFX();
		break;
	case ERoomCurseType::SealedCombatRoom_Phase2:
		// TODO Phase 2: spawn smoke blockers from exit markers only after manual door/exit sizing is validated.
		UE_LOG(LogRoomCurseVolume, Log, TEXT("SealedCombatRoom_Phase2 entered on %s, but Phase 2 sealing is intentionally disabled."), *GetNameSafe(this));
		break;
	default:
		break;
	}
}

void ARoomCurseVolume::RemoveVolumeOnlyEffects()
{
	if (AssignedCurse.CurseType == ERoomCurseType::LightReduction)
	{
		RestoreModifiedLights();
	}
}

void ARoomCurseVolume::ApplyLightReduction()
{
	if (bLightReductionApplied)
	{
		return;
	}

	TArray<ULightComponent*> LocalLights;
	CollectLocalLights(LocalLights);
	if (LocalLights.Num() == 0)
	{
		if (!bWarnedNoLights)
		{
			bWarnedNoLights = true;
			UE_LOG(LogRoomCurseVolume, Warning, TEXT("LightReduction curse %s found no lights inside %s."),
				*ResolveDisplayCurseID().ToString(),
				*GetNameSafe(this));
		}
		return;
	}

	const float LightMultiplier = FMath::Max(AssignedCurse.LightMultiplier, 0.0f);
	for (ULightComponent* LightComponent : LocalLights)
	{
		if (!IsValid(LightComponent))
		{
			UE_LOG(LogRoomCurseVolume, Warning, TEXT("LightReduction found an invalid light component in %s."), *GetNameSafe(this));
			continue;
		}

		const TWeakObjectPtr<ULightComponent> LightKey(LightComponent);
		if (!ModifiedLightIntensities.Contains(LightKey))
		{
			ModifiedLightIntensities.Add(LightKey, LightComponent->Intensity);
		}

		LightComponent->SetIntensity(LightComponent->Intensity * LightMultiplier);
	}

	bLightReductionApplied = true;
}

void ARoomCurseVolume::CollectLocalLights(TArray<ULightComponent*>& OutLights) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FBox Bounds = GetCurseBounds();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		TArray<ULightComponent*> LightComponents;
		Actor->GetComponents(LightComponents);
		for (ULightComponent* LightComponent : LightComponents)
		{
			if (IsValid(LightComponent) && Bounds.IsInsideOrOn(LightComponent->GetComponentLocation()))
			{
				OutLights.Add(LightComponent);
			}
		}
	}
}

void ARoomCurseVolume::PlayWhisper(AActor* TargetActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (AssignedCurse.OptionalSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, AssignedCurse.OptionalSound, GetActorLocation());
	}

	if (UProjectActivityFeedSubsystem* ActivityFeed = World->GetSubsystem<UProjectActivityFeedSubsystem>())
	{
		const FText Message = AssignedCurse.Description.ToString().IsEmpty()
			? FText::FromString(TEXT("A whisper crawls through the room."))
			: AssignedCurse.Description;
		ActivityFeed->AddSystemEntry(Message);
	}
}

void ARoomCurseVolume::SpawnOptionalVFX() const
{
	UWorld* World = GetWorld();
	if (!World || !AssignedCurse.OptionalVFXActor)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = const_cast<ARoomCurseVolume*>(this);
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<AActor>(AssignedCurse.OptionalVFXActor, GetActorTransform(), SpawnParameters);
}

void ARoomCurseVolume::NotifyListenersEntered() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Listener = *It;
		if (IsValid(Listener) && Listener->GetClass()->ImplementsInterface(UDungeonCurseListenerInterface::StaticClass()))
		{
			IDungeonCurseListenerInterface::Execute_OnRoomCurseEntered(Listener, ResolveDisplayCurseID(), AssignedCurse.DisplayName);
		}
	}
}

void ARoomCurseVolume::NotifyListenersExited() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Listener = *It;
		if (IsValid(Listener) && Listener->GetClass()->ImplementsInterface(UDungeonCurseListenerInterface::StaticClass()))
		{
			IDungeonCurseListenerInterface::Execute_OnRoomCurseExited(Listener, ResolveDisplayCurseID());
		}
	}
}

void ARoomCurseVolume::NotifyListenersDiscovered() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Listener = *It;
		if (IsValid(Listener) && Listener->GetClass()->ImplementsInterface(UDungeonCurseListenerInterface::StaticClass()))
		{
			IDungeonCurseListenerInterface::Execute_OnRoomCurseDiscovered(Listener, ResolveDisplayCurseID());
		}
	}
}

void ARoomCurseVolume::DrawVolumeDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugBox(World, GetActorLocation(), VolumeExtent, GetActorQuat(), DebugColor, false, 0.0f, 0, 3.0f);

	const FString Label = AssignedCurse.DisplayName.ToString().IsEmpty()
		? ResolveDisplayCurseID().ToString()
		: AssignedCurse.DisplayName.ToString();
	DrawDebugString(World, GetActorLocation() + FVector(0.0f, 0.0f, VolumeExtent.Z + 80.0f), Label, nullptr, DebugColor, 0.0f, true);
}

FBox ARoomCurseVolume::GetCurseBounds() const
{
	return BoxComponent ? BoxComponent->Bounds.GetBox() : FBox::BuildAABB(GetActorLocation(), VolumeExtent);
}

FName ARoomCurseVolume::ResolveDisplayCurseID() const
{
	if (!AssignedCurse.CurseID.IsNone())
	{
		return AssignedCurse.CurseID;
	}

	return FName(*StaticEnum<ERoomCurseType>()->GetNameStringByValue(static_cast<int64>(AssignedCurse.CurseType)));
}

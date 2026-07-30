#include "DungeonCurse/DungeonCurseManager.h"

#include "DrawDebugHelpers.h"
#include "DungeonCurse/RoomCurseVolume.h"
#include "DungeonCurse/RoomGameplayMarker.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDungeonCurseManager, Log, All);

namespace DungeonCurseManagerPrivate
{
	static FRoomCurseDefinition MakeCurse(
		const ERoomCurseType Type,
		const TCHAR* ID,
		const TCHAR* Name,
		const TCHAR* Description,
		const float Weight)
	{
		FRoomCurseDefinition Definition;
		Definition.CurseType = Type;
		Definition.CurseID = FName(ID);
		Definition.DisplayName = FText::FromString(Name);
		Definition.Description = FText::FromString(Description);
		Definition.Weight = Weight;
		Definition.bRemoveOnExit = true;
		return Definition;
	}

	static FName ResolveCurseID(const FRoomCurseDefinition& Definition)
	{
		if (!Definition.CurseID.IsNone())
		{
			return Definition.CurseID;
		}

		return FName(*StaticEnum<ERoomCurseType>()->GetNameStringByValue(static_cast<int64>(Definition.CurseType)));
	}

	static FString BuildActorSearchText(const AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}

		FString SearchText = Actor->GetName() + TEXT(" ") + Actor->GetClass()->GetName();
		for (const FName Tag : Actor->Tags)
		{
			SearchText += TEXT(" ");
			SearchText += Tag.ToString();
		}
		return SearchText;
	}
}

ADungeonCurseManager::ADungeonCurseManager()
{
	PrimaryActorTick.bCanEverTick = false;

	BaseCurseChance = 0.20f;
	CombatRoomCurseChance = 0.25f;
	TreasureRoomCurseChance = 0.10f;
	RestRoomCurseChance = 0.05f;
	BossRoomCurseChance = 1.0f;
	CorridorCurseChance = 0.0f;
	MaxCursedRoomsPerFloor = 5;
	MinCursedRoomsPerFloor = 1;
	bAllowEntranceRoomCurse = false;
	bAllowExitRoomCurse = false;
	RandomSeed = 1337;
	bUseDeterministicSeed = false;
	DebugMode = true;
	bDebugDraw = false;
	GeneratedActorTag = TEXT("PCG Generated Actor");
	MinimumRoomExtent = FVector(100.0f, 100.0f, 50.0f);
	bAutoInitialize = true;
	InitializationDelay = 2.0f;
	ReadinessPollInterval = 0.25f;
	MaxReadinessPollAttempts = 40;
	bEnableMaxLevelEnemyCurse = true;
	MaxEnemyLevelForCurrentFloor = 0;
	EnemyOverrideRetryCount = 5;
	EnemyOverrideRetryDelay = 0.20f;
	bEnableSealedCombatRooms = false;
	ExitMarkerTag = TEXT("RoomExitMarker");

	MarkerTags = {
		TEXT("DungeonRoomMarker"),
		TEXT("CalystoRoom"),
		TEXT("DungeonRoom"),
		TEXT("Room"),
		TEXT("PCGRoom"),
		TEXT("RoomMarker")
	};

	FRoomCurseDefinition LightReduction = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::LightReduction,
		TEXT("LightReduction"),
		TEXT("Dimmed Room"),
		TEXT("The room light falls by half."),
		1.0f);
	LightReduction.LightMultiplier = 0.5f;

	FRoomCurseDefinition Madness = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::MadnessPerSecond,
		TEXT("MadnessPerSecond"),
		TEXT("Madness Leak"),
		TEXT("Madness rises while you remain inside."),
		1.0f);
	Madness.MadnessPerSecond = 0.1f;

	FRoomCurseDefinition CurseBuildup = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::CurseBuildupPerSecond,
		TEXT("CurseBuildupPerSecond"),
		TEXT("Arcane Pressure"),
		TEXT("Curse builds while you remain inside."),
		1.0f);
	CurseBuildup.CurseBuildupPerSecond = 1.0f;
	CurseBuildup.CurseSourceKind = EProjectCurseSourceKind::Room;

	FRoomCurseDefinition Hunger = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::HungerDrainMultiplier,
		TEXT("HungerDrainMultiplier"),
		TEXT("Hungry Air"),
		TEXT("Hunger drains faster in this room."),
		1.0f);
	Hunger.HungerDrainMultiplier = 1.2f;

	FRoomCurseDefinition Thirst = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::ThirstDrainMultiplier,
		TEXT("ThirstDrainMultiplier"),
		TEXT("Dry Curse"),
		TEXT("Thirst drains faster in this room."),
		1.0f);
	Thirst.ThirstDrainMultiplier = 1.2f;

	FRoomCurseDefinition MaxLevelEnemies = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::MaxLevelEnemies,
		TEXT("MaxLevelEnemies"),
		TEXT("Cruel Spawn"),
		TEXT("Enemies inside use the maximum allowed level for this floor."),
		0.75f);
	MaxLevelEnemies.bForceMaxLevelEnemies = true;

	FRoomCurseDefinition TynaWhisper = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::TynaWhisper,
		TEXT("TynaWhisper"),
		TEXT("Tyna Whisper"),
		TEXT("A whisper crawls through the room."),
		0.5f);

	FRoomCurseDefinition SealedPhase2 = DungeonCurseManagerPrivate::MakeCurse(
		ERoomCurseType::SealedCombatRoom_Phase2,
		TEXT("SealedCombatRoom_Phase2"),
		TEXT("Sealed Combat Room"),
		TEXT("Reserved for Phase 2 smoke-door sealing."),
		0.0f);
	SealedPhase2.bCanRepeatInSameFloor = false;

	CurseDefinitions = {
		LightReduction,
		Madness,
		CurseBuildup,
		Hunger,
		Thirst,
		MaxLevelEnemies,
		TynaWhisper,
		SealedPhase2
	};
}

void ADungeonCurseManager::BeginPlay()
{
	Super::BeginPlay();
	RegisterSpawnHandler();

	if (bAutoInitialize)
	{
		ScheduleAutoInitialize();
	}
}

void ADungeonCurseManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterSpawnHandler();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoInitializeTimerHandle);
	}

	ClearCurseSystem();
	Super::EndPlay(EndPlayReason);
}

void ADungeonCurseManager::InitializeCurseSystem()
{
	ClearCurseSystem();

	int32 MarkerCount = 0;
	int32 TaggedActorCount = 0;
	int32 PCGActorCount = 0;
	ValidRooms = DiscoverRooms(MarkerCount, TaggedActorCount, PCGActorCount);

	UE_LOG(LogDungeonCurseManager, Log, TEXT("Dungeon Curse discovery: markers=%d tagged_candidates=%d pcg_candidates=%d valid_rooms=%d."),
		MarkerCount,
		TaggedActorCount,
		PCGActorCount,
		ValidRooms.Num());

	if (MarkerCount == 0)
	{
		UE_LOG(LogDungeonCurseManager, Warning, TEXT("No ARoomGameplayMarker actors found. Using tag/PCG fallback if available; manual markers remain the stable Calysto PCG path."));
	}

	if (ValidRooms.Num() == 0)
	{
		UE_LOG(LogDungeonCurseManager, Warning, TEXT("Dungeon Curse System found no valid rooms. Place ARoomGameplayMarker in Calysto modules to enable stable detection."));
		return;
	}

	FRandomStream RandomStream(ResolveRandomSeed());
	GenerateCurseAssignments(RandomStream);

	if (bDebugDraw)
	{
		DrawDebugCursedRooms();
	}

	PrintCurseSummary();
}

void ADungeonCurseManager::ClearCurseSystem()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoInitializeTimerHandle);
	}

	for (ARoomCurseVolume* Volume : SpawnedCurseVolumes)
	{
		if (IsValid(Volume))
		{
			Volume->RestoreModifiedLights();
			Volume->Destroy();
		}
	}

	SpawnedCurseVolumes.Reset();
	CursedRooms.Reset();
	ValidRooms.Reset();
}

void ADungeonCurseManager::RegenerateCurses()
{
	InitializeCurseSystem();
}

void ADungeonCurseManager::PrintCurseSummary() const
{
	UE_LOG(LogDungeonCurseManager, Log, TEXT("Dungeon Curse summary: valid_rooms=%d cursed_rooms=%d active_volumes=%d."),
		ValidRooms.Num(),
		CursedRooms.Num(),
		SpawnedCurseVolumes.Num());

	for (const FDetectedDungeonCurseRoom& Room : CursedRooms)
	{
		UE_LOG(LogDungeonCurseManager, Log, TEXT("Cursed room %s type=%s curse=%s source=%s center=%s extent=%s."),
			*Room.RoomID.ToString(),
			*StaticEnum<EGeneratedRoomType>()->GetNameStringByValue(static_cast<int64>(Room.RoomType)),
			*Room.AssignedCurseID.ToString(),
			*GetNameSafe(Room.SourceActor),
			*Room.Center.ToCompactString(),
			*Room.Extent.ToCompactString());
	}
}

void ADungeonCurseManager::DrawDebugCursedRooms() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const FDetectedDungeonCurseRoom& Room : ValidRooms)
	{
		DrawDebugBox(World, Room.Center, Room.Extent, Room.Rotation.Quaternion(), FColor::Green, false, 10.0f, 0, 2.0f);
	}

	for (const FDetectedDungeonCurseRoom& Room : CursedRooms)
	{
		DrawDebugBox(World, Room.Center, Room.Extent, Room.Rotation.Quaternion(), FColor(180, 40, 255), false, 10.0f, 0, 4.0f);
		DrawDebugString(World, Room.Center + FVector(0.0f, 0.0f, Room.Extent.Z + 80.0f), Room.AssignedCurseID.ToString(), nullptr, FColor(180, 40, 255), 10.0f, true);
	}
}

TArray<ARoomCurseVolume*> ADungeonCurseManager::GetActiveCursedRooms() const
{
	TArray<ARoomCurseVolume*> Result;
	for (ARoomCurseVolume* Volume : SpawnedCurseVolumes)
	{
		if (IsValid(Volume))
		{
			Result.Add(Volume);
		}
	}
	return Result;
}

bool ADungeonCurseManager::SelectWeightedCurseDefinition(
	const TArray<FRoomCurseDefinition>& Definitions,
	const FDetectedDungeonCurseRoom& Room,
	const bool bInEnableMaxLevelEnemyCurse,
	const bool bInEnableSealedCombatRooms,
	FRandomStream& RandomStream,
	TSet<FName>& UsedNonRepeatableCurseIDs,
	FRoomCurseDefinition& OutCurse)
{
	TArray<const FRoomCurseDefinition*> Candidates;
	float TotalWeight = 0.0f;

	for (const FRoomCurseDefinition& Definition : Definitions)
	{
		if (Definition.Weight <= 0.0f || Definition.CurseType == ERoomCurseType::None)
		{
			continue;
		}

		if (!Room.IsCurseTypeAllowed(Definition.CurseType, bInEnableMaxLevelEnemyCurse, bInEnableSealedCombatRooms))
		{
			continue;
		}

		const FName CurseID = DungeonCurseManagerPrivate::ResolveCurseID(Definition);
		if (!Definition.bCanRepeatInSameFloor && UsedNonRepeatableCurseIDs.Contains(CurseID))
		{
			continue;
		}

		Candidates.Add(&Definition);
		TotalWeight += Definition.Weight;
	}

	if (Candidates.Num() == 0 || TotalWeight <= 0.0f)
	{
		return false;
	}

	const float Roll = RandomStream.FRandRange(0.0f, TotalWeight);
	float RunningWeight = 0.0f;
	for (const FRoomCurseDefinition* Candidate : Candidates)
	{
		RunningWeight += Candidate->Weight;
		if (Roll <= RunningWeight)
		{
			OutCurse = *Candidate;
			if (!OutCurse.bCanRepeatInSameFloor)
			{
				UsedNonRepeatableCurseIDs.Add(DungeonCurseManagerPrivate::ResolveCurseID(OutCurse));
			}
			return true;
		}
	}

	OutCurse = *Candidates.Last();
	if (!OutCurse.bCanRepeatInSameFloor)
	{
		UsedNonRepeatableCurseIDs.Add(DungeonCurseManagerPrivate::ResolveCurseID(OutCurse));
	}
	return true;
}

void ADungeonCurseManager::ScheduleAutoInitialize()
{
	CurrentReadinessAttempt = 0;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AutoInitializeTimerHandle,
			this,
			&ThisClass::TryInitializeWhenReady,
			FMath::Max(InitializationDelay, 0.0f),
			false);
	}
}

void ADungeonCurseManager::TryInitializeWhenReady()
{
	if (IsProceduralRuntimeReady())
	{
		InitializeCurseSystem();
		return;
	}

	++CurrentReadinessAttempt;
	if (CurrentReadinessAttempt >= FMath::Max(MaxReadinessPollAttempts, 0))
	{
		UE_LOG(LogDungeonCurseManager, Warning, TEXT("Dungeon runtime readiness did not complete after %d attempts. Initializing Dungeon Curse System with current world state."),
			CurrentReadinessAttempt);
		InitializeCurseSystem();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AutoInitializeTimerHandle,
			this,
			&ThisClass::TryInitializeWhenReady,
			FMath::Max(ReadinessPollInterval, 0.01f),
			false);
	}
}

bool ADungeonCurseManager::IsProceduralRuntimeReady() const
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return true;
	}

	UEFProceduralRuntimeSubsystem* ProceduralRuntime = GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>();
	if (!ProceduralRuntime)
	{
		return true;
	}

	return ProceduralRuntime->IsDungeonRuntimeReady(GetWorld());
}

int32 ADungeonCurseManager::ResolveRandomSeed() const
{
	return bUseDeterministicSeed ? RandomSeed : FMath::Rand();
}

void ADungeonCurseManager::GenerateCurseAssignments(FRandomStream& RandomStream)
{
	const int32 MaxCurses = FMath::Max(MaxCursedRoomsPerFloor, 0);
	const int32 DesiredMinCurses = FMath::Min(FMath::Max(MinCursedRoomsPerFloor, 0), MaxCurses);
	if (MaxCurses <= 0)
	{
		UE_LOG(LogDungeonCurseManager, Log, TEXT("MaxCursedRoomsPerFloor is 0. No curses will be assigned."));
		return;
	}

	TSet<int32> SelectedRoomIndices;
	TSet<FName> UsedNonRepeatableCurseIDs;

	for (int32 RoomIndex = 0; RoomIndex < ValidRooms.Num() && CursedRooms.Num() < MaxCurses; ++RoomIndex)
	{
		const FDetectedDungeonCurseRoom& Room = ValidRooms[RoomIndex];
		const float Chance = GetCurseChanceForRoom(Room);
		if (Chance <= 0.0f || RandomStream.FRand() > Chance)
		{
			continue;
		}

		FRoomCurseDefinition Curse;
		if (SelectWeightedCurseDefinition(CurseDefinitions, Room, bEnableMaxLevelEnemyCurse, bEnableSealedCombatRooms, RandomStream, UsedNonRepeatableCurseIDs, Curse))
		{
			SelectedRoomIndices.Add(RoomIndex);
			SpawnCurseVolumeForRoom(Room, Curse);
		}
	}

	TArray<int32> ForceCandidateIndices;
	for (int32 RoomIndex = 0; RoomIndex < ValidRooms.Num(); ++RoomIndex)
	{
		if (!SelectedRoomIndices.Contains(RoomIndex) && GetCurseChanceForRoom(ValidRooms[RoomIndex]) > 0.0f)
		{
			ForceCandidateIndices.Add(RoomIndex);
		}
	}

	for (int32 Index = ForceCandidateIndices.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = RandomStream.RandRange(0, Index);
		ForceCandidateIndices.Swap(Index, SwapIndex);
	}

	for (const int32 RoomIndex : ForceCandidateIndices)
	{
		if (CursedRooms.Num() >= DesiredMinCurses || CursedRooms.Num() >= MaxCurses)
		{
			break;
		}

		FRoomCurseDefinition Curse;
		if (SelectWeightedCurseDefinition(CurseDefinitions, ValidRooms[RoomIndex], bEnableMaxLevelEnemyCurse, bEnableSealedCombatRooms, RandomStream, UsedNonRepeatableCurseIDs, Curse))
		{
			SpawnCurseVolumeForRoom(ValidRooms[RoomIndex], Curse);
		}
	}
}

void ADungeonCurseManager::SpawnCurseVolumeForRoom(const FDetectedDungeonCurseRoom& Room, const FRoomCurseDefinition& Curse)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UClass* SpawnClass = RoomCurseVolumeClass ? RoomCurseVolumeClass.Get() : ARoomCurseVolume::StaticClass();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARoomCurseVolume* Volume = World->SpawnActor<ARoomCurseVolume>(SpawnClass, Room.Center, Room.Rotation, SpawnParameters);
	if (!Volume)
	{
		UE_LOG(LogDungeonCurseManager, Warning, TEXT("Failed to spawn RoomCurseVolume for room %s."), *Room.RoomID.ToString());
		return;
	}

	Volume->InitializeFromDetectedRoom(Room, Curse, this);
	SpawnedCurseVolumes.Add(Volume);

	FDetectedDungeonCurseRoom CursedRoom = Room;
	CursedRoom.AssignedCurseType = Curse.CurseType;
	CursedRoom.AssignedCurseID = DungeonCurseManagerPrivate::ResolveCurseID(Curse);
	CursedRooms.Add(CursedRoom);

	UE_LOG(LogDungeonCurseManager, Log, TEXT("Assigned curse %s to room %s."),
		*CursedRoom.AssignedCurseID.ToString(),
		*CursedRoom.RoomID.ToString());
}

float ADungeonCurseManager::GetCurseChanceForRoom(const FDetectedDungeonCurseRoom& Room) const
{
	if (!Room.bAllowCurses || !IsRoomAllowedByType(Room))
	{
		return 0.0f;
	}

	if (Room.CurseChanceOverride >= 0.0f)
	{
		return FMath::Clamp(Room.CurseChanceOverride, 0.0f, 1.0f);
	}

	switch (Room.RoomType)
	{
	case EGeneratedRoomType::Combat:
		return CombatRoomCurseChance;
	case EGeneratedRoomType::Treasure:
		return TreasureRoomCurseChance;
	case EGeneratedRoomType::Rest:
		return RestRoomCurseChance;
	case EGeneratedRoomType::Boss:
		return BossRoomCurseChance;
	case EGeneratedRoomType::Corridor:
		return CorridorCurseChance;
	default:
		return BaseCurseChance;
	}
}

bool ADungeonCurseManager::IsRoomAllowedByType(const FDetectedDungeonCurseRoom& Room) const
{
	if (Room.RoomType == EGeneratedRoomType::Entrance && !bAllowEntranceRoomCurse)
	{
		return false;
	}

	if (Room.RoomType == EGeneratedRoomType::Exit && !bAllowExitRoomCurse)
	{
		return false;
	}

	return true;
}

TArray<FDetectedDungeonCurseRoom> ADungeonCurseManager::DiscoverRooms(int32& OutMarkerCount, int32& OutTaggedActorCount, int32& OutPCGActorCount) const
{
	TArray<FDetectedDungeonCurseRoom> MarkerRooms;
	TArray<FDetectedDungeonCurseRoom> TaggedActorRooms;
	TArray<FDetectedDungeonCurseRoom> PCGActorRooms;

	ScanMarkerRooms(MarkerRooms);
	ScanTaggedActorRooms(TaggedActorRooms);
	ScanPCGActorRooms(PCGActorRooms);

	OutMarkerCount = MarkerRooms.Num();
	OutTaggedActorCount = TaggedActorRooms.Num();
	OutPCGActorCount = PCGActorRooms.Num();

	TArray<FDetectedDungeonCurseRoom> SelectedRooms;
	if (MarkerRooms.Num() > 0)
	{
		SelectedRooms = MoveTemp(MarkerRooms);
	}
	else if (TaggedActorRooms.Num() > 0)
	{
		SelectedRooms = MoveTemp(TaggedActorRooms);
	}
	else
	{
		SelectedRooms = MoveTemp(PCGActorRooms);
	}

	SortRooms(SelectedRooms);
	return SelectedRooms;
}

void ADungeonCurseManager::ScanMarkerRooms(TArray<FDetectedDungeonCurseRoom>& OutRooms) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ARoomGameplayMarker> It(World); It; ++It)
	{
		FDetectedDungeonCurseRoom Room;
		if (BuildRoomFromMarker(*It, Room))
		{
			OutRooms.Add(Room);
		}
	}
}

void ADungeonCurseManager::ScanTaggedActorRooms(TArray<FDetectedDungeonCurseRoom>& OutRooms) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsCandidateActor(Actor) || !HasAnyMarkerTag(Actor))
		{
			continue;
		}

		FDetectedDungeonCurseRoom Room;
		if (BuildRoomFromActor(Actor, 0, Room))
		{
			OutRooms.Add(Room);
		}
	}
}

void ADungeonCurseManager::ScanPCGActorRooms(TArray<FDetectedDungeonCurseRoom>& OutRooms) const
{
	UWorld* World = GetWorld();
	if (!World || GeneratedActorTag.IsNone())
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsCandidateActor(Actor) || !Actor->ActorHasTag(GeneratedActorTag))
		{
			continue;
		}

		FDetectedDungeonCurseRoom Room;
		if (BuildRoomFromActor(Actor, -10, Room))
		{
			OutRooms.Add(Room);
		}
	}
}

bool ADungeonCurseManager::BuildRoomFromMarker(ARoomGameplayMarker* Marker, FDetectedDungeonCurseRoom& OutRoom) const
{
	if (!IsValid(Marker) || !HasValidCandidateExtent(Marker->BoxExtent))
	{
		return false;
	}

	OutRoom.RoomID = FName(*Marker->GetName());
	OutRoom.RoomType = Marker->RoomType;
	OutRoom.Center = Marker->GetActorLocation();
	OutRoom.Extent = Marker->BoxExtent;
	OutRoom.Rotation = Marker->GetActorRotation();
	OutRoom.SourceActor = Marker;
	OutRoom.SourceMarker = Marker;
	OutRoom.CurseChanceOverride = Marker->CurseChanceOverride;
	OutRoom.Priority = Marker->Priority;
	OutRoom.bAllowCurses = Marker->bAllowCurses;
	OutRoom.bAllowEnemyLevelCurse = Marker->bAllowEnemyLevelCurse;
	OutRoom.bAllowLightCurse = Marker->bAllowLightCurse;
	OutRoom.bAllowInnerStateCurse = Marker->bAllowInnerStateCurse;
	return true;
}

bool ADungeonCurseManager::BuildRoomFromActor(AActor* Actor, const int32 Priority, FDetectedDungeonCurseRoom& OutRoom) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	FBox Bounds = Actor->GetComponentsBoundingBox(true);
	if (!Bounds.IsValid)
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Actor->GetActorBounds(false, Origin, Extent);
		Bounds = FBox::BuildAABB(Origin, Extent);
	}

	if (!Bounds.IsValid || !HasValidCandidateExtent(Bounds.GetExtent()))
	{
		return false;
	}

	OutRoom.RoomID = FName(*Actor->GetName());
	OutRoom.RoomType = InferRoomTypeFromActor(Actor);
	OutRoom.Center = Bounds.GetCenter();
	OutRoom.Extent = Bounds.GetExtent();
	OutRoom.Rotation = Actor->GetActorRotation();
	OutRoom.SourceActor = Actor;
	OutRoom.SourceMarker = nullptr;
	OutRoom.CurseChanceOverride = -1.0f;
	OutRoom.Priority = Priority;
	return true;
}

bool ADungeonCurseManager::IsCandidateActor(AActor* Actor) const
{
	if (!IsValid(Actor) || Actor == this || Actor->IsA<ARoomGameplayMarker>() || Actor->IsA<ARoomCurseVolume>() || Actor->IsA<APawn>())
	{
		return false;
	}

	if (Actor->IsTemplate() || Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	return true;
}

bool ADungeonCurseManager::HasAnyMarkerTag(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	for (const FName Tag : MarkerTags)
	{
		if (!Tag.IsNone() && Actor->ActorHasTag(Tag))
		{
			return true;
		}
	}

	return false;
}

EGeneratedRoomType ADungeonCurseManager::InferRoomTypeFromActor(const AActor* Actor) const
{
	const FString SearchText = DungeonCurseManagerPrivate::BuildActorSearchText(Actor);
	if (SearchText.Contains(TEXT("Boss"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Boss;
	}
	if (SearchText.Contains(TEXT("Treasure"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Treasure;
	}
	if (SearchText.Contains(TEXT("Rest"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Rest;
	}
	if (SearchText.Contains(TEXT("Trap"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Trap;
	}
	if (SearchText.Contains(TEXT("Shrine"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Shrine;
	}
	if (SearchText.Contains(TEXT("Corridor"), ESearchCase::IgnoreCase) || SearchText.Contains(TEXT("Hall"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Corridor;
	}
	if (SearchText.Contains(TEXT("Entrance"), ESearchCase::IgnoreCase) || SearchText.Contains(TEXT("Start"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Entrance;
	}
	if (SearchText.Contains(TEXT("Exit"), ESearchCase::IgnoreCase) || SearchText.Contains(TEXT("End"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Exit;
	}
	if (SearchText.Contains(TEXT("Special"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Special;
	}
	if (SearchText.Contains(TEXT("Combat"), ESearchCase::IgnoreCase))
	{
		return EGeneratedRoomType::Combat;
	}
	return EGeneratedRoomType::Unknown;
}

bool ADungeonCurseManager::HasValidCandidateExtent(const FVector& Extent) const
{
	return Extent.X >= MinimumRoomExtent.X
		&& Extent.Y >= MinimumRoomExtent.Y
		&& Extent.Z >= MinimumRoomExtent.Z;
}

void ADungeonCurseManager::SortRooms(TArray<FDetectedDungeonCurseRoom>& Rooms) const
{
	Rooms.Sort([](const FDetectedDungeonCurseRoom& Left, const FDetectedDungeonCurseRoom& Right)
	{
		if (Left.Priority != Right.Priority)
		{
			return Left.Priority > Right.Priority;
		}

		return Left.RoomID.LexicalLess(Right.RoomID);
	});
}

void ADungeonCurseManager::RegisterSpawnHandler()
{
	UWorld* World = GetWorld();
	if (!World || ActorSpawnedHandle.IsValid())
	{
		return;
	}

	ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
}

void ADungeonCurseManager::UnregisterSpawnHandler()
{
	UWorld* World = GetWorld();
	if (World && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	ActorSpawnedHandle.Reset();
}

void ADungeonCurseManager::HandleActorSpawned(AActor* SpawnedActor)
{
	if (!IsValid(SpawnedActor) || SpawnedCurseVolumes.Num() == 0)
	{
		return;
	}

	RetryApplySpawnedEnemyCurse(SpawnedActor, 0);
}

void ADungeonCurseManager::RetryApplySpawnedEnemyCurse(TWeakObjectPtr<AActor> SpawnedActorPtr, const int32 AttemptIndex)
{
	AActor* SpawnedActor = SpawnedActorPtr.Get();
	if (!IsValid(SpawnedActor))
	{
		return;
	}

	bool bAnyContainingVolume = false;
	bool bApplied = false;
	for (ARoomCurseVolume* Volume : SpawnedCurseVolumes)
	{
		if (!IsValid(Volume) || !Volume->WantsEnemyLevelProcessing() || !Volume->ContainsActor(SpawnedActor))
		{
			continue;
		}

		bAnyContainingVolume = true;
		bApplied |= Volume->ApplyEnemyLevelCurseToActor(SpawnedActor);
	}

	if (bAnyContainingVolume && !bApplied && AttemptIndex < EnemyOverrideRetryCount)
	{
		if (UWorld* World = GetWorld())
		{
			FTimerHandle RetryHandle;
			World->GetTimerManager().SetTimer(
				RetryHandle,
				FTimerDelegate::CreateUObject(this, &ThisClass::RetryApplySpawnedEnemyCurse, SpawnedActorPtr, AttemptIndex + 1),
				FMath::Max(EnemyOverrideRetryDelay, 0.01f),
				false);
		}
	}
}

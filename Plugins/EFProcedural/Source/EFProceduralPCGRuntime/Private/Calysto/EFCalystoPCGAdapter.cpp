#include "Calysto/EFCalystoPCGAdapter.h"

#include "Calysto/EFCalystoFloorDoor.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoPopulationAnchor.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPCGAdapter, Log, All);

namespace EFCalystoPCGAdapterPrivate
{
	struct FActorSchema
	{
		FObjectProperty* Dungeon = nullptr;
		FObjectProperty* Spawners = nullptr;
		FObjectProperty* RoomThemeList = nullptr;
		FStructProperty* DungeonSize = nullptr;
		FNumericProperty* SpawnerDensity = nullptr;
		FNumericProperty* SidePathChance = nullptr;
		FNumericProperty* WallLightHeight = nullptr;
		FIntProperty* WallLightTileDistance = nullptr;
	};

	struct FSpawnerSchema
	{
		FArrayProperty* Entries = nullptr;
		FStructProperty* EntryStruct = nullptr;
		FClassProperty* SpawnerClass = nullptr;
		FIntProperty* Weight = nullptr;
	};

	struct FThemeSchema
	{
		FArrayProperty* Entries = nullptr;
		FStructProperty* EntryStruct = nullptr;
		FObjectProperty* RoomType = nullptr;
		FDoubleProperty* Weight = nullptr;
	};

	static FString Canonicalize(const FString& Name)
	{
		FString Result;
		Result.Reserve(Name.Len());
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				Result.AppendChar(FChar::ToLower(Character));
			}
		}
		return Result;
	}

	static FProperty* FindAllowlistedProperty(
		const UStruct* Owner,
		const TCHAR* CanonicalName,
		FString& OutError)
	{
		if (!Owner)
		{
			OutError = FString::Printf(TEXT("Cannot resolve allowlisted property '%s' on a null schema."), CanonicalName);
			return nullptr;
		}

		const FString ExpectedName = Canonicalize(CanonicalName);
		FProperty* Match = nullptr;
		for (TFieldIterator<FProperty> PropertyIt(Owner); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property || Canonicalize(Property->GetAuthoredName()) != ExpectedName)
			{
				continue;
			}

			if (Match)
			{
				OutError = FString::Printf(
					TEXT("Schema %s has multiple properties matching canonical allowlist name '%s'."),
					*Owner->GetPathName(),
					CanonicalName);
				return nullptr;
			}
			Match = Property;
		}

		if (!Match)
		{
			OutError = FString::Printf(
				TEXT("Schema %s is missing canonical allowlist property '%s'."),
				*Owner->GetPathName(),
				CanonicalName);
			return nullptr;
		}

		if (Match->HasAnyPropertyFlags(CPF_EditorOnly))
		{
			OutError = FString::Printf(
				TEXT("Allowlisted property %s.%s unexpectedly became editor-only."),
				*Owner->GetPathName(),
				*Match->GetName());
			return nullptr;
		}

		return Match;
	}

	template <typename PropertyType>
	static PropertyType* FindTypedAllowlistedProperty(
		const UStruct* Owner,
		const TCHAR* CanonicalName,
		const TCHAR* ExpectedType,
		FString& OutError)
	{
		FProperty* Property = FindAllowlistedProperty(Owner, CanonicalName, OutError);
		if (!Property)
		{
			return nullptr;
		}

		PropertyType* TypedProperty = CastField<PropertyType>(Property);
		if (!TypedProperty)
		{
			OutError = FString::Printf(
				TEXT("Allowlisted property %s.%s has type %s; expected %s."),
				*Owner->GetPathName(),
				*Property->GetName(),
				*Property->GetClass()->GetName(),
				ExpectedType);
		}
		return TypedProperty;
	}

	static FObjectProperty* FindPlainObjectProperty(
		const UStruct* Owner,
		const TCHAR* CanonicalName,
		FString& OutError)
	{
		FObjectProperty* Property = FindTypedAllowlistedProperty<FObjectProperty>(
			Owner,
			CanonicalName,
			TEXT("hard UObject property"),
			OutError);
		if (Property && CastField<FClassProperty>(Property))
		{
			OutError = FString::Printf(
				TEXT("Allowlisted property %s.%s unexpectedly stores a class rather than an object."),
				*Owner->GetPathName(),
				*Property->GetName());
			return nullptr;
		}
		return Property;
	}

	static FNumericProperty* FindFloatingProperty(
		const UStruct* Owner,
		const TCHAR* CanonicalName,
		FString& OutError)
	{
		FNumericProperty* Property = FindTypedAllowlistedProperty<FNumericProperty>(
			Owner,
			CanonicalName,
			TEXT("floating-point property"),
			OutError);
		if (Property && !Property->IsFloatingPoint())
		{
			OutError = FString::Printf(
				TEXT("Allowlisted property %s.%s is numeric but not floating-point."),
				*Owner->GetPathName(),
				*Property->GetName());
			return nullptr;
		}
		return Property;
	}

	static bool ResolveActorSchema(AActor* DungeonActor, FActorSchema& OutSchema, FString& OutError)
	{
		const UClass* ActorClass = DungeonActor ? DungeonActor->GetClass() : nullptr;
		if (!ActorClass)
		{
			OutError = TEXT("Dungeon actor has no valid class.");
			return false;
		}

		OutSchema.Dungeon = FindPlainObjectProperty(ActorClass, TEXT("Dungeon"), OutError);
		OutSchema.Spawners = FindPlainObjectProperty(ActorClass, TEXT("Spawners"), OutError);
		OutSchema.RoomThemeList = FindPlainObjectProperty(ActorClass, TEXT("RoomThemeList"), OutError);
		OutSchema.DungeonSize = FindTypedAllowlistedProperty<FStructProperty>(
			ActorClass,
			TEXT("DungeonSize"),
			TEXT("FIntVector struct property"),
			OutError);
		OutSchema.SpawnerDensity = FindFloatingProperty(ActorClass, TEXT("Spawner Density"), OutError);
		OutSchema.SidePathChance = FindFloatingProperty(ActorClass, TEXT("Side Path Chance"), OutError);
		OutSchema.WallLightHeight = FindFloatingProperty(ActorClass, TEXT("Wall Light Height"), OutError);
		OutSchema.WallLightTileDistance = FindTypedAllowlistedProperty<FIntProperty>(
			ActorClass, TEXT("Wall Light Tile Distance"), TEXT("int32 property"), OutError);

		if (!OutSchema.Dungeon
			|| !OutSchema.Spawners
			|| !OutSchema.RoomThemeList
			|| !OutSchema.DungeonSize
			|| !OutSchema.SpawnerDensity
			|| !OutSchema.SidePathChance
			|| !OutSchema.WallLightHeight
			|| !OutSchema.WallLightTileDistance)
		{
			return false;
		}

		if (OutSchema.DungeonSize->Struct != TBaseStructure<FIntVector>::Get())
		{
			OutError = FString::Printf(
				TEXT("Allowlisted DungeonSize uses %s; expected FIntVector."),
				*GetNameSafe(OutSchema.DungeonSize->Struct));
			return false;
		}

		return true;
	}

	static bool ResolveGetPiecesShapeFunction(
		AActor* DungeonActor,
		UFunction*& OutFunction,
		FString& OutError)
	{
		OutFunction = nullptr;
		if (!IsValid(DungeonActor))
		{
			OutError = TEXT("Cannot resolve GetPiecesShape on an invalid dungeon actor.");
			return false;
		}

		static const FName FunctionName(TEXT("GetPiecesShape"));
		UFunction* Function = DungeonActor->FindFunction(FunctionName);
		if (!IsValid(Function)
			|| !Function->GetName().Equals(FunctionName.ToString(), ESearchCase::CaseSensitive))
		{
			OutError = FString::Printf(
				TEXT("Dungeon class %s does not expose the exact GetPiecesShape function."),
				*DungeonActor->GetClass()->GetPathName());
			return false;
		}

		if (Function->NumParms != 0 || Function->ParmsSize != 0)
		{
			OutError = FString::Printf(
				TEXT("GetPiecesShape signature drifted (NumParms=%d ParmsSize=%d); zero parameters are required."),
				Function->NumParms,
				Function->ParmsSize);
			return false;
		}

		for (TFieldIterator<FProperty> PropertyIt(Function); PropertyIt; ++PropertyIt)
		{
			if ((*PropertyIt)->HasAnyPropertyFlags(CPF_Parm))
			{
				OutError = FString::Printf(
					TEXT("GetPiecesShape unexpectedly exposes parameter property %s."),
					*(*PropertyIt)->GetName());
				return false;
			}
		}

		OutFunction = Function;
		return true;
	}

	static bool ResolveDungeonMeshSchema(UObject* DungeonMesh, FClassProperty*& OutEndBlueprint, FString& OutError)
	{
		OutEndBlueprint = FindTypedAllowlistedProperty<FClassProperty>(
			DungeonMesh ? DungeonMesh->GetClass() : nullptr,
			TEXT("EndBlueprint"),
			TEXT("Actor class property"),
			OutError);
		if (!OutEndBlueprint)
		{
			return false;
		}

		if (!OutEndBlueprint->MetaClass || !OutEndBlueprint->MetaClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(
				TEXT("Allowlisted EndBlueprint meta-class %s is not an Actor class."),
				*GetNameSafe(OutEndBlueprint->MetaClass));
			return false;
		}
		return true;
	}

	static bool ResolveSpawnerSchema(UObject* Spawners, FSpawnerSchema& OutSchema, FString& OutError)
	{
		OutSchema.Entries = FindTypedAllowlistedProperty<FArrayProperty>(
			Spawners ? Spawners->GetClass() : nullptr,
			TEXT("Spawner"),
			TEXT("array of ST_Spawner"),
			OutError);
		if (!OutSchema.Entries)
		{
			return false;
		}

		OutSchema.EntryStruct = CastField<FStructProperty>(OutSchema.Entries->Inner);
		if (!OutSchema.EntryStruct || !OutSchema.EntryStruct->Struct)
		{
			OutError = FString::Printf(
				TEXT("Allowlisted Spawner array on %s is not an array of structs."),
				*GetNameSafe(Spawners));
			return false;
		}

		OutSchema.SpawnerClass = FindTypedAllowlistedProperty<FClassProperty>(
			OutSchema.EntryStruct->Struct,
			TEXT("Spawner"),
			TEXT("Actor class property"),
			OutError);
		OutSchema.Weight = FindTypedAllowlistedProperty<FIntProperty>(
			OutSchema.EntryStruct->Struct,
			TEXT("Weight"),
			TEXT("int32 property"),
			OutError);
		if (!OutSchema.SpawnerClass || !OutSchema.Weight)
		{
			return false;
		}

		if (!OutSchema.SpawnerClass->MetaClass
			|| !OutSchema.SpawnerClass->MetaClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(
				TEXT("ST_Spawner.Spawner meta-class %s is not an Actor class."),
				*GetNameSafe(OutSchema.SpawnerClass->MetaClass));
			return false;
		}
		return true;
	}

	static bool ResolveThemeSchema(UObject* ThemeList, FThemeSchema& OutSchema, FString& OutError)
	{
		OutSchema.Entries = FindTypedAllowlistedProperty<FArrayProperty>(
			ThemeList ? ThemeList->GetClass() : nullptr,
			TEXT("RoomTheme"),
			TEXT("array of ST_RoomTheme"),
			OutError);
		if (!OutSchema.Entries)
		{
			return false;
		}

		OutSchema.EntryStruct = CastField<FStructProperty>(OutSchema.Entries->Inner);
		if (!OutSchema.EntryStruct || !OutSchema.EntryStruct->Struct)
		{
			OutError = FString::Printf(
				TEXT("Allowlisted RoomTheme array on %s is not an array of structs."),
				*GetNameSafe(ThemeList));
			return false;
		}

		OutSchema.RoomType = FindPlainObjectProperty(
			OutSchema.EntryStruct->Struct,
			TEXT("RoomType"),
			OutError);
		OutSchema.Weight = FindTypedAllowlistedProperty<FDoubleProperty>(
			OutSchema.EntryStruct->Struct,
			TEXT("Weight"),
			TEXT("double property"),
			OutError);
		if (!OutSchema.RoomType || !OutSchema.Weight)
		{
			return false;
		}

		return true;
	}

	static UObject* DuplicateTransiently(UObject* Source, AActor* DungeonActor, const TCHAR* Suffix)
	{
		if (!IsValid(Source) || !IsValid(DungeonActor))
		{
			return nullptr;
		}

		const FName BaseName(*FString::Printf(TEXT("EFCalysto_%s_%s"), *Source->GetName(), Suffix));
		const FName CloneName = MakeUniqueObjectName(DungeonActor, Source->GetClass(), BaseName);
		UObject* Clone = StaticDuplicateObject(Source, DungeonActor, CloneName);
		if (Clone)
		{
			Clone->ClearFlags(RF_Public | RF_Standalone);
			Clone->SetFlags(RF_Transient);
		}
		return Clone;
	}

	static bool ApplyPopulationAnchorSpawner(
		UObject* SpawnersClone,
		const FSpawnerSchema& Schema,
		int32& OutUpdatedEntries,
		FString& OutError)
	{
		OutUpdatedEntries = 0;
		FScriptArrayHelper Entries(Schema.Entries, Schema.Entries->ContainerPtrToValuePtr<void>(SpawnersClone));
		Entries.Resize(0);
		UClass* AnchorClass = AEFCalystoPopulationAnchor::StaticClass();
		if (!IsValid(AnchorClass)
			|| AnchorClass->HasAnyClassFlags(CLASS_Abstract)
			|| !AnchorClass->IsChildOf(Schema.SpawnerClass->MetaClass))
		{
			OutError = FString::Printf(
				TEXT("Native population anchor class %s is incompatible with ST_Spawner.Spawner."),
				*GetPathNameSafe(AnchorClass));
			return false;
		}

		const int32 EntryIndex = Entries.AddValue();
		void* Entry = Entries.GetRawPtr(EntryIndex);
		Schema.SpawnerClass->SetObjectPropertyValue_InContainer(Entry, AnchorClass);
		Schema.Weight->SetPropertyValue_InContainer(Entry, 1);
		OutUpdatedEntries = 1;
		return true;
	}

	static double ReadFloating(const FNumericProperty* Property, const void* Container)
	{
		return Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Container));
	}

	static void WriteFloating(const FNumericProperty* Property, void* Container, double Value)
	{
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Container), Value);
	}

	static bool ApplySelectedThemeV4(
		UObject* ThemeClone,
		const FThemeSchema& Schema,
		const EEFCalystoThemeV4 Theme,
		UObject* SelectedRoomType,
		int32& OutUpdatedEntries,
		FString& OutError)
	{
		OutUpdatedEntries = 0;
		FScriptArrayHelper Entries(Schema.Entries, Schema.Entries->ContainerPtrToValuePtr<void>(ThemeClone));
		if (Entries.Num() <= 0)
		{
			OutError = TEXT("The transient Calysto V4 theme clone has no ST_RoomTheme entries.");
			return false;
		}

		if (Theme == EEFCalystoThemeV4::Default)
		{
			if (IsValid(SelectedRoomType))
			{
				OutError = TEXT("The neutral Default V4 theme must not force a Calysto room type.");
				return false;
			}
			for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
			{
				void* Entry = Entries.GetRawPtr(EntryIndex);
				UObject* ExistingRoomType = Schema.RoomType->GetObjectPropertyValue_InContainer(Entry);
				const double ExistingWeight = Schema.Weight->GetFloatingPointPropertyValue(
					Schema.Weight->ContainerPtrToValuePtr<void>(Entry));
				if (!IsValid(ExistingRoomType) || !FMath::IsFinite(ExistingWeight) || ExistingWeight <= 0.0)
				{
					OutError = FString::Printf(
						TEXT("Default V4 theme entry %d has invalid room type or weight."),
						EntryIndex);
					return false;
				}
			}
			return true;
		}

		if (!IsValid(SelectedRoomType))
		{
			OutError = TEXT("Forge/Shrine V4 theme selected an unloaded Calysto room type.");
			return false;
		}
		int32 MatchCount = 0;
		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			void* Entry = Entries.GetRawPtr(EntryIndex);
			UObject* ExistingRoomType = Schema.RoomType->GetObjectPropertyValue_InContainer(Entry);
			if (!IsValid(ExistingRoomType))
			{
				OutError = FString::Printf(TEXT("V4 theme entry %d has no valid RoomType."), EntryIndex);
				return false;
			}
			const bool bSelected = ExistingRoomType == SelectedRoomType;
			Schema.Weight->SetPropertyValue_InContainer(Entry, bSelected ? 5.0 : 1.0);
			MatchCount += bSelected ? 1 : 0;
			++OutUpdatedEntries;
		}
		if (MatchCount != 1)
		{
			OutError = FString::Printf(
				TEXT("Selected V4 Calysto room type %s matched %d vendor theme entries; exactly one is required."),
				*GetPathNameSafe(SelectedRoomType),
				MatchCount);
			return false;
		}
		return true;
	}
}

FEFCalystoPCGAdapterResult FEFCalystoPCGAdapter::TryApply(AActor* DungeonActor)
{
	using namespace EFCalystoPCGAdapterPrivate;

	FEFCalystoPCGAdapterResult Result;
	auto Fail = [&Result](FString&& Reason)
	{
		Result.FailureReason = MoveTemp(Reason);
		return Result;
	};

	if (!IsValid(DungeonActor) || !DungeonActor->GetWorld() || !DungeonActor->GetWorld()->IsGameWorld())
	{
		return Fail(TEXT("Dungeon actor is invalid or is not in a game world."));
	}

	UGameInstance* GameInstance = DungeonActor->GetWorld()->GetGameInstance();
	UEFCalystoDungeonSubsystem* DungeonSubsystem = GameInstance
		? GameInstance->GetSubsystem<UEFCalystoDungeonSubsystem>()
		: nullptr;
	const UEFCalystoDungeonHarnessSettings* Settings = UEFCalystoDungeonHarnessSettings::Get();
	if (!DungeonSubsystem || !Settings)
	{
		return Fail(TEXT("Calysto dungeon subsystem or harness settings are unavailable."));
	}

	const FEFCalystoResolvedFloorIntentV4 Plan = DungeonSubsystem->GetResolvedFloorIntent();
	if (!Plan.bIsValid
		|| Plan.GeneratorVersion != 4
		|| Plan.RunSeed <= 0
		|| Plan.FloorNumber < 1
		|| Plan.GenerationSerial < 1
		|| Plan.PolicyHash.IsEmpty()
		|| Plan.EcologyHash.IsEmpty()
		|| Plan.CompanionSnapshotHash.IsEmpty()
		|| Plan.IntentHash.IsEmpty())
	{
		return Fail(FString::Printf(
			TEXT("Frozen V4 floor intent is invalid (valid=%d run=%lld floor=%lld serial=%lld policy=%s ecology=%s companion=%s intent=%s); legacy fallback is forbidden."),
			Plan.bIsValid ? 1 : 0,
			Plan.RunSeed,
			Plan.FloorNumber,
			Plan.GenerationSerial,
			*Plan.PolicyHash,
			*Plan.EcologyHash,
			*Plan.CompanionSnapshotHash,
			*Plan.IntentHash));
	}

	FActorSchema ActorSchema;
	FString Error;
	if (!ResolveActorSchema(DungeonActor, ActorSchema, Error))
	{
		return Fail(MoveTemp(Error));
	}
	UFunction* GetPiecesShapeFunction = nullptr;
	if (!ResolveGetPiecesShapeFunction(DungeonActor, GetPiecesShapeFunction, Error))
	{
		return Fail(MoveTemp(Error));
	}

	UObject* OriginalDungeon = ActorSchema.Dungeon->GetObjectPropertyValue_InContainer(DungeonActor);
	UObject* OriginalSpawners = ActorSchema.Spawners->GetObjectPropertyValue_InContainer(DungeonActor);
	UObject* OriginalThemeList = ActorSchema.RoomThemeList->GetObjectPropertyValue_InContainer(DungeonActor);
	if (!IsValid(OriginalDungeon) || !IsValid(OriginalSpawners) || !IsValid(OriginalThemeList))
	{
		return Fail(TEXT("One or more allowlisted Calysto source DataAssets are null on the dungeon actor."));
	}

	UObject* ConfiguredDungeonMesh = Settings->DungeonMeshDataAsset.Get();
	if (!IsValid(ConfiguredDungeonMesh) || ConfiguredDungeonMesh != OriginalDungeon)
	{
		return Fail(FString::Printf(
			TEXT("Actor Dungeon source %s does not match preloaded harness source %s."),
			*GetPathNameSafe(OriginalDungeon),
			*GetPathNameSafe(ConfiguredDungeonMesh)));
	}
	UObject* ConfiguredSpawners = Settings->SpawnerDataAsset.Get();
	if (!IsValid(ConfiguredSpawners) || ConfiguredSpawners != OriginalSpawners)
	{
		return Fail(FString::Printf(
			TEXT("Actor Spawners source %s does not match preloaded harness source %s."),
			*GetPathNameSafe(OriginalSpawners),
			*GetPathNameSafe(ConfiguredSpawners)));
	}
	UObject* ConfiguredThemeList = Settings->RoomThemeDataAsset.Get();
	if (!IsValid(ConfiguredThemeList) || ConfiguredThemeList != OriginalThemeList)
	{
		return Fail(FString::Printf(
			TEXT("Actor RoomThemeList source %s does not match preloaded harness source %s."),
			*GetPathNameSafe(OriginalThemeList),
			*GetPathNameSafe(ConfiguredThemeList)));
	}

	UClass* FloorDoorClass = Settings->DungeonFloorDoorClass.Get();
	if (!IsValid(FloorDoorClass) || !FloorDoorClass->IsChildOf(AEFCalystoFloorDoor::StaticClass()))
	{
		return Fail(FString::Printf(
			TEXT("Preloaded DungeonFloorDoorClass is missing or is not an AEFCalystoFloorDoor subclass: %s."),
			*Settings->DungeonFloorDoorClass.ToSoftObjectPath().ToString()));
	}
	UClass* ConfiguredAnchorClass = Settings->PopulationAnchorClass.Get();
	if (ConfiguredAnchorClass != AEFCalystoPopulationAnchor::StaticClass())
	{
		return Fail(FString::Printf(
			TEXT("PopulationAnchorClass must resolve to the native V4 anchor %s, got %s."),
			*AEFCalystoPopulationAnchor::StaticClass()->GetPathName(),
			*GetPathNameSafe(ConfiguredAnchorClass)));
	}

	FClassProperty* EndBlueprintProperty = nullptr;
	FSpawnerSchema SpawnerSchema;
	FThemeSchema ThemeSchema;
	if (!ResolveDungeonMeshSchema(OriginalDungeon, EndBlueprintProperty, Error)
		|| !ResolveSpawnerSchema(OriginalSpawners, SpawnerSchema, Error)
		|| !ResolveThemeSchema(OriginalThemeList, ThemeSchema, Error))
	{
		return Fail(MoveTemp(Error));
	}
	if (!FloorDoorClass->IsChildOf(EndBlueprintProperty->MetaClass))
	{
		return Fail(FString::Printf(
			TEXT("DungeonFloorDoorClass %s is incompatible with EndBlueprint meta-class %s."),
			*GetPathNameSafe(FloorDoorClass),
			*GetPathNameSafe(EndBlueprintProperty->MetaClass)));
	}

	const FIntVector BaselineDungeonSize = *ActorSchema.DungeonSize->ContainerPtrToValuePtr<FIntVector>(DungeonActor);
	const double BaselineSpawnerDensity = ReadFloating(ActorSchema.SpawnerDensity, DungeonActor);
	const double BaselineSidePathChance = ReadFloating(ActorSchema.SidePathChance, DungeonActor);
	const double BaselineWallLightHeight = ReadFloating(ActorSchema.WallLightHeight, DungeonActor);
	const int32 BaselineWallLightTileDistance = ActorSchema.WallLightTileDistance->GetPropertyValue_InContainer(DungeonActor);
	if (!FMath::IsFinite(BaselineSpawnerDensity) || !FMath::IsFinite(BaselineSidePathChance)
		|| !FMath::IsFinite(BaselineWallLightHeight) || BaselineWallLightTileDistance < 1)
	{
		return Fail(FString::Printf(TEXT("Dungeon actor contains invalid native diagnostic values (size=%s density=%.6f sidePath=%.6f lightHeight=%.3f lightInterval=%d)."),
			*BaselineDungeonSize.ToString(),
			BaselineSpawnerDensity,
			BaselineSidePathChance,
			BaselineWallLightHeight,
			BaselineWallLightTileDistance));
	}
	UE_LOG(LogEFCalystoPCGAdapter, Log, TEXT("Diagnostic BP_MassiveDungeon baseline size=%s density=%.6f sidePath=%.6f nativeLightHeight=%.3f nativeLightInterval=%d; authoritative V4 intent will be applied to the transient runtime actor."),
		*BaselineDungeonSize.ToString(), BaselineSpawnerDensity, BaselineSidePathChance, BaselineWallLightHeight, BaselineWallLightTileDistance);

	constexpr int32 MinDungeonEdge = 18;
	constexpr int32 MaxDungeonEdge = 30;
	if (Plan.DungeonSize.X < MinDungeonEdge
		|| Plan.DungeonSize.X > MaxDungeonEdge
		|| Plan.DungeonSize.Y < MinDungeonEdge
		|| Plan.DungeonSize.Y > MaxDungeonEdge
		|| Plan.DungeonSize.Z != 1)
	{
		return Fail(FString::Printf(
			TEXT("Frozen V4 layout size %s violates authoritative 18-30x18-30x1 limits (diagnostic baseline %s)."),
			*Plan.DungeonSize.ToString(),
			*BaselineDungeonSize.ToString()));
	}
	if (!FMath::IsFinite(Plan.CandidateAnchorDensity)
		|| Plan.CandidateAnchorDensity < 0.20f
		|| Plan.CandidateAnchorDensity > 0.50f
		|| !FMath::IsFinite(Plan.SidePathChance)
		|| Plan.SidePathChance < 0.30f
		|| Plan.SidePathChance > 0.70f)
	{
		return Fail(TEXT("Frozen V4 candidate-anchor density or side-path chance violates immutable limits."));
	}
	if (!FMath::IsFinite(Plan.NativeWallLightHeight)
		|| Plan.NativeWallLightHeight < 0.0f
		|| Plan.NativeWallLightHeight > 1000.0f
		|| Plan.NativeWallLightTileDistance < 1
		|| Plan.NativeWallLightTileDistance > 100)
	{
		return Fail(TEXT("Frozen V4 native lighting values violate the Calysto-safe height or tile-interval range."));
	}
	const FIntVector AppliedDungeonSize = Plan.DungeonSize;
	// FloorIntent owns the Shape RNG domain. Consuming that frozen value here keeps
	// Calysto geometry independent from unrelated catalog/population fields that also
	// participate in IntentHash, and makes runtime PCG match replay telemetry exactly.
	const int32 DeterministicPCGSeed = Plan.PCGSeed;
	if (DeterministicPCGSeed <= 0)
	{
		return Fail(TEXT("Failed to derive the deterministic V4 PCG seed."));
	}

	UObject* DungeonClone = DuplicateTransiently(OriginalDungeon, DungeonActor, TEXT("Dungeon"));
	UObject* SpawnersClone = DuplicateTransiently(OriginalSpawners, DungeonActor, TEXT("Spawners"));
	UObject* ThemeClone = DuplicateTransiently(OriginalThemeList, DungeonActor, TEXT("Themes"));
	if (!DungeonClone || !SpawnersClone || !ThemeClone)
	{
		return Fail(TEXT("Failed to create all three transient Calysto DataAsset clones."));
	}

	EndBlueprintProperty->SetObjectPropertyValue_InContainer(DungeonClone, FloorDoorClass);
	if (!ApplyPopulationAnchorSpawner(
			SpawnersClone,
			SpawnerSchema,
			Result.UpdatedAnchorEntries,
			Error)
		|| !ApplySelectedThemeV4(
			ThemeClone,
			ThemeSchema,
			Plan.Theme,
			Plan.CalystoRoomType.Get(),
			Result.UpdatedThemeEntries,
			Error))
	{
		return Fail(MoveTemp(Error));
	}

	// Commit only after every reflected schema, source row, clone, and scalar has passed.
	ActorSchema.Dungeon->SetObjectPropertyValue_InContainer(DungeonActor, DungeonClone);
	ActorSchema.Spawners->SetObjectPropertyValue_InContainer(DungeonActor, SpawnersClone);
	ActorSchema.RoomThemeList->SetObjectPropertyValue_InContainer(DungeonActor, ThemeClone);
	*ActorSchema.DungeonSize->ContainerPtrToValuePtr<FIntVector>(DungeonActor) = AppliedDungeonSize;
	WriteFloating(ActorSchema.SpawnerDensity, DungeonActor, Plan.CandidateAnchorDensity);
	WriteFloating(ActorSchema.SidePathChance, DungeonActor, Plan.SidePathChance);
	// These are Calysto's own BP_MassiveDungeon variables.  The actor is a
	// transient runtime instance, so this alters neither the vendor Blueprint
	// nor its CDO.  Calysto remains the only system that places torch actors.
	WriteFloating(ActorSchema.WallLightHeight, DungeonActor, Plan.NativeWallLightHeight);
	ActorSchema.WallLightTileDistance->SetPropertyValue_InContainer(
		DungeonActor, Plan.NativeWallLightTileDistance);

	// Calysto remains the owner of its derived piece/shape state. Invoke its exact native
	// boundary after the transient inputs are committed and before PCG delegates/seed/generation.
	DungeonActor->ProcessEvent(GetPiecesShapeFunction, nullptr);
	Result.bGetPiecesShapeInvoked = true;

	Result.PCGSeed = DeterministicPCGSeed;
	Result.bApplied = true;
	UE_LOG(
		LogEFCalystoPCGAdapter,
		Log,
		TEXT("PASS V4 actor=%s run=%lld floor=%lld serial=%lld seed=%d style=%d theme=%d policyHash=%s ecologyHash=%s companionHash=%s intentHash=%s size=%s anchorDensity=%.3f sidePath=%.3f nativeLightHeight=%.3f nativeLightInterval=%d nativeLightBlend=%.3f anchorEntries=%d themeEntries=%d."),
		*DungeonActor->GetName(),
		Plan.RunSeed,
		Plan.FloorNumber,
		Plan.GenerationSerial,
		Result.PCGSeed,
		static_cast<int32>(Plan.Style),
		static_cast<int32>(Plan.Theme),
		*Plan.PolicyHash,
		*Plan.EcologyHash,
		*Plan.CompanionSnapshotHash,
		*Plan.IntentHash,
		*AppliedDungeonSize.ToString(),
		Plan.CandidateAnchorDensity,
		Plan.SidePathChance,
		Plan.NativeWallLightHeight,
		Plan.NativeWallLightTileDistance,
		Plan.LightingBlend,
		Result.UpdatedAnchorEntries,
		Result.UpdatedThemeEntries);
	return Result;
}

#include "Calysto/EFCalystoPCGAdapter.h"

#include "Calysto/EFCalystoFloorDoor.h"
#include "Calysto/EFCalystoDungeonHarnessSettings.h"
#include "Calysto/EFCalystoDungeonRuntimeV6.h"
#include "Calysto/EFCalystoDungeonSubsystem.h"
#include "Calysto/EFCalystoPopulationAnchor.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogEFCalystoPCGAdapter, Log, All);

namespace EFCalystoPCGAdapterPrivate
{
	struct FActorSchema
	{
		FObjectProperty* Dungeon = nullptr;
		FObjectProperty* Spawners = nullptr;
		FObjectProperty* RoomThemeList = nullptr;
		FObjectProperty* DungeonMaterial = nullptr;
		FBoolProperty* OverrideFloorMaterial = nullptr;
		FBoolProperty* OverrideWallMaterial = nullptr;
		FBoolProperty* OverrideRoofMaterial = nullptr;
		FStructProperty* DungeonSize = nullptr;
		FNumericProperty* SpawnerDensity = nullptr;
		FNumericProperty* SidePathChance = nullptr;
		FNumericProperty* WallLightHeight = nullptr;
		FIntProperty* WallLightTileDistance = nullptr;
	};

	struct FMaterialSlotSchema
	{
		FStructProperty* Slot = nullptr;
		FObjectProperty* Material = nullptr;
	};

	struct FDungeonMaterialSchema
	{
		FMaterialSlotSchema Floor;
		FMaterialSlotSchema Wall;
		FMaterialSlotSchema Roof;
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
		FBoolProperty* OverrideFloorMaterial = nullptr;
		FObjectProperty* FloorMaterial = nullptr;
		FBoolProperty* OverrideWallMaterial = nullptr;
		FObjectProperty* WallMaterial = nullptr;
		FBoolProperty* OverrideRoofMaterial = nullptr;
		FObjectProperty* RoofMaterial = nullptr;
	};

	/** Reflected, runtime-safe view of Calysto's PDA_RoomMeshes/ST_ObjectDungeon contract. */
	struct FRoomArchitectureSchema
	{
		FArrayProperty* WallBottom = nullptr;
		FArrayProperty* WallMiddle = nullptr;
		FArrayProperty* WallTop = nullptr;
		FArrayProperty* Floor = nullptr;
		FArrayProperty* CornerBottom = nullptr;
		FArrayProperty* CornerMiddle = nullptr;
		FArrayProperty* CornerTop = nullptr;
		FArrayProperty* Roof = nullptr;
		FStructProperty* EntryStruct = nullptr;
		FProperty* Type = nullptr;
		FObjectProperty* Mesh = nullptr;
		FClassProperty* Blueprint = nullptr;
		FObjectProperty* LevelInstance = nullptr;
		FIntProperty* Weight = nullptr;
		FProperty* RotationType = nullptr;
		FStructProperty* TransformMinimum = nullptr;
		FStructProperty* TransformMaximum = nullptr;
		FStructProperty* RotationMinimum = nullptr;
		FStructProperty* RotationMaximum = nullptr;
		FBoolProperty* UniformScale = nullptr;
		FStructProperty* ScaleMinimum = nullptr;
		FStructProperty* ScaleMaximum = nullptr;
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

	static bool ValidateMaterialInstanceProperty(
		const FObjectProperty* Property,
		const TCHAR* ContractName,
		FString& OutError)
	{
		if (!Property || !Property->PropertyClass
			|| !Property->PropertyClass->IsChildOf(UMaterialInstance::StaticClass()))
		{
			OutError = FString::Printf(
				TEXT("Allowlisted %s must be a hard UMaterialInstance property; found %s."),
				ContractName,
				Property && Property->PropertyClass
					? *Property->PropertyClass->GetPathName()
					: TEXT("<null>"));
			return false;
		}
		return true;
	}

	static bool ResolveDungeonMaterialSlotSchema(
		UObject* DungeonMaterial,
		const TCHAR* SlotName,
		FMaterialSlotSchema& OutSchema,
		FString& OutError)
	{
		OutSchema.Slot = FindTypedAllowlistedProperty<FStructProperty>(
			DungeonMaterial ? DungeonMaterial->GetClass() : nullptr,
			SlotName,
			TEXT("ST_DungeonMaterial struct property"),
			OutError);
		if (!OutSchema.Slot || !OutSchema.Slot->Struct)
		{
			return false;
		}
		OutSchema.Material = FindPlainObjectProperty(
			OutSchema.Slot->Struct,
			TEXT("Material"),
			OutError);
		return OutSchema.Material
			&& ValidateMaterialInstanceProperty(
				OutSchema.Material,
				SlotName,
				OutError);
	}

	static bool ResolveDungeonMaterialSchema(
		UObject* DungeonMaterial,
		FDungeonMaterialSchema& OutSchema,
		FString& OutError)
	{
		return ResolveDungeonMaterialSlotSchema(
			DungeonMaterial, TEXT("Floor"), OutSchema.Floor, OutError)
			&& ResolveDungeonMaterialSlotSchema(
				DungeonMaterial, TEXT("Wall"), OutSchema.Wall, OutError)
			&& ResolveDungeonMaterialSlotSchema(
				DungeonMaterial, TEXT("Roof"), OutSchema.Roof, OutError);
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
		OutSchema.DungeonMaterial = FindPlainObjectProperty(ActorClass, TEXT("DungeonMaterial"), OutError);
		OutSchema.OverrideFloorMaterial = FindTypedAllowlistedProperty<FBoolProperty>(
			ActorClass, TEXT("OverrideFloorMaterial"), TEXT("bool property"), OutError);
		OutSchema.OverrideWallMaterial = FindTypedAllowlistedProperty<FBoolProperty>(
			ActorClass, TEXT("OverrideWallMaterial"), TEXT("bool property"), OutError);
		OutSchema.OverrideRoofMaterial = FindTypedAllowlistedProperty<FBoolProperty>(
			ActorClass, TEXT("OverrideRoofMaterial"), TEXT("bool property"), OutError);
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
			|| !OutSchema.DungeonMaterial
			|| !OutSchema.OverrideFloorMaterial
			|| !OutSchema.OverrideWallMaterial
			|| !OutSchema.OverrideRoofMaterial
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
		OutSchema.OverrideFloorMaterial = FindTypedAllowlistedProperty<FBoolProperty>(
			OutSchema.EntryStruct->Struct,
			TEXT("ThemeOverrideFloorMaterial"),
			TEXT("bool property"),
			OutError);
		OutSchema.FloorMaterial = FindPlainObjectProperty(
			OutSchema.EntryStruct->Struct,
			TEXT("FloorMaterial"),
			OutError);
		OutSchema.OverrideWallMaterial = FindTypedAllowlistedProperty<FBoolProperty>(
			OutSchema.EntryStruct->Struct,
			TEXT("ThemeOverrideWallMaterial"),
			TEXT("bool property"),
			OutError);
		OutSchema.WallMaterial = FindPlainObjectProperty(
			OutSchema.EntryStruct->Struct,
			TEXT("WallMaterial"),
			OutError);
		OutSchema.OverrideRoofMaterial = FindTypedAllowlistedProperty<FBoolProperty>(
			OutSchema.EntryStruct->Struct,
			TEXT("ThemeOverrideRoofMaterial"),
			TEXT("bool property"),
			OutError);
		OutSchema.RoofMaterial = FindPlainObjectProperty(
			OutSchema.EntryStruct->Struct,
			TEXT("RoofMaterial"),
			OutError);
		if (!OutSchema.RoomType || !OutSchema.Weight
			|| !OutSchema.OverrideFloorMaterial || !OutSchema.FloorMaterial
			|| !OutSchema.OverrideWallMaterial || !OutSchema.WallMaterial
			|| !OutSchema.OverrideRoofMaterial || !OutSchema.RoofMaterial)
		{
			return false;
		}
		if (!ValidateMaterialInstanceProperty(OutSchema.FloorMaterial, TEXT("ST_RoomTheme.FloorMaterial"), OutError)
			|| !ValidateMaterialInstanceProperty(OutSchema.WallMaterial, TEXT("ST_RoomTheme.WallMaterial"), OutError)
			|| !ValidateMaterialInstanceProperty(OutSchema.RoofMaterial, TEXT("ST_RoomTheme.RoofMaterial"), OutError))
		{
			return false;
		}

		return true;
	}

	static UEnum* ResolveEnumDefinition(const FProperty* Property)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum;
		}
		return nullptr;
	}

	static bool ResolveEnumValue(
		const FProperty* Property,
		const TArray<FString>& CanonicalCandidates,
		int64& OutValue,
		FString& OutError)
	{
		OutValue = INDEX_NONE;
		UEnum* Enum = ResolveEnumDefinition(Property);
		if (!Enum)
		{
			OutError = FString::Printf(
				TEXT("Allowlisted property %s is not backed by an enum."),
				Property ? *Property->GetPathName() : TEXT("<null>"));
			return false;
		}

		for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums(); ++EnumIndex)
		{
			const int64 CandidateValue = Enum->GetValueByIndex(EnumIndex);
			if (CandidateValue == INDEX_NONE)
			{
				continue;
			}
			const FString InternalName = Canonicalize(Enum->GetNameStringByIndex(EnumIndex));
			const FString DisplayName = Canonicalize(Enum->GetDisplayNameTextByIndex(EnumIndex).ToString());
			for (const FString& Expected : CanonicalCandidates)
			{
				if (InternalName == Expected || DisplayName == Expected)
				{
					OutValue = CandidateValue;
					return true;
				}
			}
		}

		OutError = FString::Printf(
			TEXT("Enum %s has no allowlisted value matching '%s'."),
			*Enum->GetPathName(),
			*FString::Join(CanonicalCandidates, TEXT("/")));
		return false;
	}

	static bool ResolveArchitectureTypeValue(
		const FProperty* Property,
		const EEFCalystoArchitectureObjectTypeV6 Type,
		int64& OutValue,
		FString& OutError)
	{
		switch (Type)
		{
		case EEFCalystoArchitectureObjectTypeV6::StaticMesh:
			return ResolveEnumValue(Property, { TEXT("staticmesh") }, OutValue, OutError);
		case EEFCalystoArchitectureObjectTypeV6::ActorBlueprint:
			return ResolveEnumValue(Property, { TEXT("blueprint"), TEXT("actorblueprint") }, OutValue, OutError);
		case EEFCalystoArchitectureObjectTypeV6::LevelInstance:
			return ResolveEnumValue(Property, { TEXT("levelinstance") }, OutValue, OutError);
		default:
			OutError = FString::Printf(TEXT("Unsupported V6 Architecture Object Type value %d."), static_cast<int32>(Type));
			return false;
		}
	}

	static bool ResolveArchitectureRotationValue(
		const FProperty* Property,
		const EEFCalystoArchitectureRotationV6 Rotation,
		int64& OutValue,
		FString& OutError)
	{
		switch (Rotation)
		{
		case EEFCalystoArchitectureRotationV6::None:
			return ResolveEnumValue(Property, { TEXT("none") }, OutValue, OutError);
		case EEFCalystoArchitectureRotationV6::Degrees45:
			return ResolveEnumValue(Property, { TEXT("45"), TEXT("degrees45"), TEXT("45degrees") }, OutValue, OutError);
		case EEFCalystoArchitectureRotationV6::Degrees90:
			return ResolveEnumValue(Property, { TEXT("90"), TEXT("degrees90"), TEXT("90degrees") }, OutValue, OutError);
		case EEFCalystoArchitectureRotationV6::Full360:
			return ResolveEnumValue(Property, { TEXT("360"), TEXT("full360"), TEXT("full360degrees") }, OutValue, OutError);
		default:
			OutError = FString::Printf(TEXT("Unsupported V6 Architecture Rotation value %d."), static_cast<int32>(Rotation));
			return false;
		}
	}

	static bool WriteEnumValue(FProperty* Property, void* Container, const int64 Value, FString& OutError)
	{
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
			void* ValueAddress = EnumProperty->ContainerPtrToValuePtr<void>(Container);
			if (!UnderlyingProperty || !ValueAddress)
			{
				OutError = FString::Printf(TEXT("Enum property %s has no writable underlying value."), *EnumProperty->GetPathName());
				return false;
			}
			UnderlyingProperty->SetIntPropertyValue(ValueAddress, Value);
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (!ByteProperty->Enum || Value < 0 || Value > MAX_uint8)
			{
				OutError = FString::Printf(TEXT("Byte enum property %s cannot store value %lld."), *ByteProperty->GetPathName(), Value);
				return false;
			}
			ByteProperty->SetPropertyValue_InContainer(Container, static_cast<uint8>(Value));
			return true;
		}
		OutError = FString::Printf(
			TEXT("Property %s is not a supported enum representation."),
			Property ? *Property->GetPathName() : TEXT("<null>"));
		return false;
	}

	static bool ResolveRoomArchitectureSchema(
		UClass* RoomTypeClass,
		FRoomArchitectureSchema& OutSchema,
		FString& OutError)
	{
		OutSchema = FRoomArchitectureSchema();
		if (!IsValid(RoomTypeClass)
			|| RoomTypeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			OutError = FString::Printf(
				TEXT("Calysto Room Type property class %s is invalid or cannot be instantiated."),
				*GetPathNameSafe(RoomTypeClass));
			return false;
		}

		const auto ResolveArray = [&OutSchema, RoomTypeClass, &OutError](
			const TCHAR* Name,
			FArrayProperty*& OutArray)
		{
			OutArray = FindTypedAllowlistedProperty<FArrayProperty>(
				RoomTypeClass,
				Name,
				TEXT("array of ST_ObjectDungeon"),
				OutError);
			FStructProperty* Inner = OutArray ? CastField<FStructProperty>(OutArray->Inner) : nullptr;
			if (!Inner || !Inner->Struct)
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Allowlisted room architecture array %s.%s is not an array of structs."),
						*RoomTypeClass->GetPathName(), Name);
				}
				return false;
			}
			if (OutSchema.EntryStruct && OutSchema.EntryStruct->Struct != Inner->Struct)
			{
				OutError = FString::Printf(
					TEXT("Room architecture array %s.%s uses %s; expected shared entry struct %s."),
					*RoomTypeClass->GetPathName(), Name, *Inner->Struct->GetPathName(),
					*OutSchema.EntryStruct->Struct->GetPathName());
				return false;
			}
			if (!OutSchema.EntryStruct)
			{
				OutSchema.EntryStruct = Inner;
			}
			return true;
		};

		if (!ResolveArray(TEXT("WallBottom"), OutSchema.WallBottom)
			|| !ResolveArray(TEXT("WallMiddle"), OutSchema.WallMiddle)
			|| !ResolveArray(TEXT("WallTop"), OutSchema.WallTop)
			|| !ResolveArray(TEXT("Floor"), OutSchema.Floor)
			|| !ResolveArray(TEXT("CornerBottom"), OutSchema.CornerBottom)
			|| !ResolveArray(TEXT("CornerMiddle"), OutSchema.CornerMiddle)
			|| !ResolveArray(TEXT("CornerTop"), OutSchema.CornerTop)
			|| !ResolveArray(TEXT("Roof"), OutSchema.Roof)
			|| !OutSchema.EntryStruct || !OutSchema.EntryStruct->Struct)
		{
			return false;
		}

		UScriptStruct* EntryStruct = OutSchema.EntryStruct->Struct;
		OutSchema.Type = FindAllowlistedProperty(EntryStruct, TEXT("Type"), OutError);
		OutSchema.Mesh = FindPlainObjectProperty(EntryStruct, TEXT("Mesh"), OutError);
		OutSchema.Blueprint = FindTypedAllowlistedProperty<FClassProperty>(
			EntryStruct, TEXT("Blueprint"), TEXT("Actor class property"), OutError);
		OutSchema.LevelInstance = FindPlainObjectProperty(EntryStruct, TEXT("LevelInstance"), OutError);
		OutSchema.Weight = FindTypedAllowlistedProperty<FIntProperty>(
			EntryStruct, TEXT("Weight"), TEXT("int32 property"), OutError);
		OutSchema.RotationType = FindAllowlistedProperty(EntryStruct, TEXT("Rotation Type"), OutError);
		OutSchema.TransformMinimum = FindTypedAllowlistedProperty<FStructProperty>(
			EntryStruct, TEXT("Transform Min"), TEXT("FVector struct property"), OutError);
		OutSchema.TransformMaximum = FindTypedAllowlistedProperty<FStructProperty>(
			EntryStruct, TEXT("Transform Max"), TEXT("FVector struct property"), OutError);
		OutSchema.RotationMinimum = FindTypedAllowlistedProperty<FStructProperty>(
			EntryStruct, TEXT("Rotation Min"), TEXT("FRotator struct property"), OutError);
		OutSchema.RotationMaximum = FindTypedAllowlistedProperty<FStructProperty>(
			EntryStruct, TEXT("Rotation Max"), TEXT("FRotator struct property"), OutError);
		OutSchema.UniformScale = FindTypedAllowlistedProperty<FBoolProperty>(
			EntryStruct, TEXT("Uniform Scale"), TEXT("bool property"), OutError);
		OutSchema.ScaleMinimum = FindTypedAllowlistedProperty<FStructProperty>(
			EntryStruct, TEXT("Scale Min"), TEXT("FVector struct property"), OutError);
		OutSchema.ScaleMaximum = FindTypedAllowlistedProperty<FStructProperty>(
			EntryStruct, TEXT("Scale Max"), TEXT("FVector struct property"), OutError);
		if (!OutSchema.Type || !OutSchema.Mesh || !OutSchema.Blueprint || !OutSchema.LevelInstance
			|| !OutSchema.Weight || !OutSchema.RotationType || !OutSchema.TransformMinimum
			|| !OutSchema.TransformMaximum || !OutSchema.RotationMinimum || !OutSchema.RotationMaximum
			|| !OutSchema.UniformScale || !OutSchema.ScaleMinimum || !OutSchema.ScaleMaximum)
		{
			return false;
		}

		if (!ResolveEnumDefinition(OutSchema.Type) || !ResolveEnumDefinition(OutSchema.RotationType))
		{
			OutError = TEXT("Calysto ST_ObjectDungeon Type or Rotation Type is no longer an enum property.");
			return false;
		}
		if (!OutSchema.Mesh->PropertyClass
			|| !OutSchema.Mesh->PropertyClass->IsChildOf(UStaticMesh::StaticClass()))
		{
			OutError = TEXT("Calysto ST_ObjectDungeon.Mesh is no longer a hard UStaticMesh property.");
			return false;
		}
		if (!OutSchema.Blueprint->MetaClass
			|| !OutSchema.Blueprint->MetaClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = TEXT("Calysto ST_ObjectDungeon.Blueprint is no longer an Actor class property.");
			return false;
		}
		if (!OutSchema.LevelInstance->PropertyClass)
		{
			OutError = TEXT("Calysto ST_ObjectDungeon.LevelInstance has no valid hard object class.");
			return false;
		}

		const auto ValidateStructType = [&OutError](
			const FStructProperty* Property,
			const UScriptStruct* Expected,
			const TCHAR* Label)
		{
			if (!Property || Property->Struct != Expected)
			{
				OutError = FString::Printf(
					TEXT("Calysto ST_ObjectDungeon.%s uses %s; expected %s."),
					Label,
					*GetPathNameSafe(Property ? Property->Struct : nullptr),
					*GetPathNameSafe(Expected));
				return false;
			}
			return true;
		};
		if (!ValidateStructType(OutSchema.TransformMinimum, TBaseStructure<FVector>::Get(), TEXT("Transform Min"))
			|| !ValidateStructType(OutSchema.TransformMaximum, TBaseStructure<FVector>::Get(), TEXT("Transform Max"))
			|| !ValidateStructType(OutSchema.RotationMinimum, TBaseStructure<FRotator>::Get(), TEXT("Rotation Min"))
			|| !ValidateStructType(OutSchema.RotationMaximum, TBaseStructure<FRotator>::Get(), TEXT("Rotation Max"))
			|| !ValidateStructType(OutSchema.ScaleMinimum, TBaseStructure<FVector>::Get(), TEXT("Scale Min"))
			|| !ValidateStructType(OutSchema.ScaleMaximum, TBaseStructure<FVector>::Get(), TEXT("Scale Max")))
		{
			return false;
		}

		// Validate the complete open V6 enum mapping once at the schema boundary.
		int64 Ignored = 0;
		return ResolveArchitectureTypeValue(OutSchema.Type, EEFCalystoArchitectureObjectTypeV6::StaticMesh, Ignored, OutError)
			&& ResolveArchitectureTypeValue(OutSchema.Type, EEFCalystoArchitectureObjectTypeV6::ActorBlueprint, Ignored, OutError)
			&& ResolveArchitectureTypeValue(OutSchema.Type, EEFCalystoArchitectureObjectTypeV6::LevelInstance, Ignored, OutError)
			&& ResolveArchitectureRotationValue(OutSchema.RotationType, EEFCalystoArchitectureRotationV6::None, Ignored, OutError)
			&& ResolveArchitectureRotationValue(OutSchema.RotationType, EEFCalystoArchitectureRotationV6::Degrees45, Ignored, OutError)
			&& ResolveArchitectureRotationValue(OutSchema.RotationType, EEFCalystoArchitectureRotationV6::Degrees90, Ignored, OutError)
			&& ResolveArchitectureRotationValue(OutSchema.RotationType, EEFCalystoArchitectureRotationV6::Full360, Ignored, OutError);
	}

	static bool WriteArchitectureEntry(
		void* Entry,
		const FRoomArchitectureSchema& Schema,
		const FEFCalystoArchitectureObjectV6& Source,
		const FString& Label,
		FString& OutError)
	{
		if (!Entry || Source.SelectionWeight <= 0
			|| Source.Variation.LocationMinimum.ContainsNaN()
			|| Source.Variation.LocationMaximum.ContainsNaN()
			|| Source.Variation.RotationMinimum.ContainsNaN()
			|| Source.Variation.RotationMaximum.ContainsNaN()
			|| Source.Variation.ScaleMinimum.ContainsNaN()
			|| Source.Variation.ScaleMaximum.ContainsNaN())
		{
			OutError = FString::Printf(
				TEXT("%s has an invalid weight or non-finite transform range."),
				*Label);
			return false;
		}

		Schema.Mesh->SetObjectPropertyValue_InContainer(Entry, nullptr);
		Schema.Blueprint->SetObjectPropertyValue_InContainer(Entry, nullptr);
		Schema.LevelInstance->SetObjectPropertyValue_InContainer(Entry, nullptr);
		switch (Source.Type)
		{
		case EEFCalystoArchitectureObjectTypeV6::StaticMesh:
		{
			UStaticMesh* Mesh = Source.Mesh.Get();
			if (!Source.Mesh.IsNull() && (!IsValid(Mesh) || !Mesh->IsA(Schema.Mesh->PropertyClass)))
			{
				OutError = FString::Printf(
					TEXT("%s Static Mesh %s is not resident or violates the Calysto property class."),
					*Label, *Source.Mesh.ToSoftObjectPath().ToString());
				return false;
			}
			Schema.Mesh->SetObjectPropertyValue_InContainer(Entry, Mesh);
			break;
		}
		case EEFCalystoArchitectureObjectTypeV6::ActorBlueprint:
		{
			UClass* ActorClass = Source.ActorClass.Get();
			if (!Source.ActorClass.IsNull() && (!IsValid(ActorClass) || ActorClass->HasAnyClassFlags(CLASS_Abstract)
				|| !ActorClass->IsChildOf(Schema.Blueprint->MetaClass)))
			{
				OutError = FString::Printf(
					TEXT("%s Actor Blueprint %s is not resident or is incompatible with Calysto."),
					*Label, *Source.ActorClass.ToSoftObjectPath().ToString());
				return false;
			}
			Schema.Blueprint->SetObjectPropertyValue_InContainer(Entry, ActorClass);
			break;
		}
		case EEFCalystoArchitectureObjectTypeV6::LevelInstance:
		{
			UObject* LevelInstance = Source.LevelInstance.Get();
			if (!Source.LevelInstance.IsNull() && (!IsValid(LevelInstance) || !LevelInstance->IsA(Schema.LevelInstance->PropertyClass)))
			{
				OutError = FString::Printf(
					TEXT("%s Level Instance %s is not resident or violates the Calysto property class."),
					*Label, *Source.LevelInstance.ToSoftObjectPath().ToString());
				return false;
			}
			Schema.LevelInstance->SetObjectPropertyValue_InContainer(Entry, LevelInstance);
			break;
		}
		default:
			OutError = FString::Printf(TEXT("%s uses unsupported object type %d."), *Label, static_cast<int32>(Source.Type));
			return false;
		}

		int64 TypeValue = 0;
		int64 RotationValue = 0;
		if (!ResolveArchitectureTypeValue(Schema.Type, Source.Type, TypeValue, OutError)
			|| !ResolveArchitectureRotationValue(Schema.RotationType, Source.Rotation, RotationValue, OutError)
			|| !WriteEnumValue(Schema.Type, Entry, TypeValue, OutError)
			|| !WriteEnumValue(Schema.RotationType, Entry, RotationValue, OutError))
		{
			return false;
		}

		Schema.Weight->SetPropertyValue_InContainer(Entry, Source.SelectionWeight);
		*Schema.TransformMinimum->ContainerPtrToValuePtr<FVector>(Entry) = Source.Variation.LocationMinimum;
		*Schema.TransformMaximum->ContainerPtrToValuePtr<FVector>(Entry) = Source.Variation.LocationMaximum;
		*Schema.RotationMinimum->ContainerPtrToValuePtr<FRotator>(Entry) = Source.Variation.RotationMinimum;
		*Schema.RotationMaximum->ContainerPtrToValuePtr<FRotator>(Entry) = Source.Variation.RotationMaximum;
		Schema.UniformScale->SetPropertyValue_InContainer(Entry, Source.Variation.bUniformScale);
		*Schema.ScaleMinimum->ContainerPtrToValuePtr<FVector>(Entry) = Source.Variation.ScaleMinimum;
		*Schema.ScaleMaximum->ContainerPtrToValuePtr<FVector>(Entry) = Source.Variation.ScaleMaximum;
		return true;
	}

	static bool PopulateArchitectureArray(
		UObject* RoomType,
		FArrayProperty* ArrayProperty,
		const FRoomArchitectureSchema& Schema,
		const TArray<FEFCalystoArchitectureObjectV6>& Source,
		const FString& Label,
		FString& OutError)
	{
		if (!IsValid(RoomType) || !ArrayProperty)
		{
			OutError = FString::Printf(TEXT("%s has no valid transient target array."), *Label);
			return false;
		}
		FScriptArrayHelper Entries(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(RoomType));
		Entries.Resize(Source.Num());
		for (int32 Index = 0; Index < Source.Num(); ++Index)
		{
			if (!WriteArchitectureEntry(
				Entries.GetRawPtr(Index),
				Schema,
				Source[Index],
				FString::Printf(TEXT("%s[%d]"), *Label, Index),
				OutError))
			{
				return false;
			}
		}
		return true;
	}

	static bool PopulateRoomArchitecture(
		UObject* RoomType,
		const FRoomArchitectureSchema& Schema,
		const FEFCalystoRoomArchitectureV6& Architecture,
		const FName ThemeId,
		FString& OutError)
	{
		const FString Prefix = FString::Printf(TEXT("V6 Theme '%s' Architecture"), *ThemeId.ToString());
		return PopulateArchitectureArray(RoomType, Schema.WallBottom, Schema, Architecture.WallBottom, Prefix + TEXT(".Wall Bottom"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.WallMiddle, Schema, Architecture.WallMiddle, Prefix + TEXT(".Wall Middle"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.WallTop, Schema, Architecture.WallTop, Prefix + TEXT(".Wall Top"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.Floor, Schema, Architecture.Floor, Prefix + TEXT(".Floor"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.CornerBottom, Schema, Architecture.CornerBottom, Prefix + TEXT(".Corner Bottom"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.CornerMiddle, Schema, Architecture.CornerMiddle, Prefix + TEXT(".Corner Middle"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.CornerTop, Schema, Architecture.CornerTop, Prefix + TEXT(".Corner Top"), OutError)
			&& PopulateArchitectureArray(RoomType, Schema.Roof, Schema, Architecture.Roof, Prefix + TEXT(".Roof"), OutError);
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

	static bool ResolveResidentMaterial(
		const TSoftObjectPtr<UMaterialInstance>& MaterialReference,
		const FString& ContractName,
		UMaterialInstance*& OutMaterial,
		FString& OutError)
	{
		OutMaterial = Cast<UMaterialInstance>(MaterialReference.Get());
		if (!IsValid(OutMaterial))
		{
			OutError = FString::Printf(
				TEXT("%s material %s is not a resident UMaterialInstance. V6 Floor Visual preload must complete before PCG generation."),
				*ContractName,
				*MaterialReference.ToSoftObjectPath().ToString());
			return false;
		}
		return true;
	}

	// All writes below target a transient clone and reject changes in the vendor schema.
	static bool WriteNativeObject(const UStruct* Schema, void* Data, const TCHAR* Name,
		UObject* Value, const bool bRequired, FString& Error)
	{
		FObjectPropertyBase* Property = FindTypedAllowlistedProperty<FObjectPropertyBase>(
			Schema, Name, TEXT("object/class property"), Error);
		if (!Property || (bRequired && !IsValid(Value))
			|| (Value && !Value->IsA(Property->PropertyClass)))
		{
			if (Error.IsEmpty()) Error = FString::Printf(TEXT("Inline Architecture %s is not resident or has an incompatible class."), Name);
			return false;
		}
		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			UClass* Class = Cast<UClass>(Value);
			if (Value && (!Class || !Class->IsChildOf(ClassProperty->MetaClass)))
			{
				Error = FString::Printf(TEXT("Inline Architecture %s violates the native Actor class contract."), Name);
				return false;
			}
		}
		Property->SetObjectPropertyValue_InContainer(Data, Value);
		return true;
	}

	template<typename T>
	static bool WriteNativeStruct(const UStruct* Schema, void* Data, const TCHAR* Name, const T& Value, FString& Error)
	{
		FStructProperty* Property = FindTypedAllowlistedProperty<FStructProperty>(Schema, Name, TEXT("struct property"), Error);
		if (!Property || Property->Struct != TBaseStructure<T>::Get())
		{
			if (Error.IsEmpty()) Error = FString::Printf(TEXT("Inline Architecture %s has changed struct type."), Name);
			return false;
		}
		*Property->ContainerPtrToValuePtr<T>(Data) = Value;
		return true;
	}

	static bool WriteNativeBool(const UStruct* Schema, void* Data, const TCHAR* Name, bool Value, FString& Error)
	{
		FBoolProperty* Property = FindTypedAllowlistedProperty<FBoolProperty>(Schema, Name, TEXT("bool property"), Error);
		if (!Property) return false;
		Property->SetPropertyValue_InContainer(Data, Value);
		return true;
	}

	static bool WriteNativeWeight(const UStruct* Schema, void* Data, double Value, FString& Error)
	{
		FNumericProperty* Property = FindTypedAllowlistedProperty<FNumericProperty>(Schema, TEXT("Weight"), TEXT("numeric property"), Error);
		if (!Property || !FMath::IsFinite(Value) || Value < 0.0) return false;
		void* Address = Property->ContainerPtrToValuePtr<void>(Data);
		if (Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Address, Value);
		else Property->SetIntPropertyValue(Address, FMath::RoundToInt64(Value));
		return true;
	}

	static bool WriteFixedTransform(const UStruct* Schema, void* Data,
		const FEFCalystoArchitectureTransformV6& Transform, const FString& Suffix, FString& Error)
	{
		return WriteNativeBool(Schema, Data, *(TEXT("Object Uniform Scale") + Suffix), Transform.bUniformScale, Error)
			&& WriteNativeStruct(Schema, Data, *(TEXT("Object Transform") + Suffix), Transform.LocationOffset, Error)
			&& WriteNativeStruct(Schema, Data, *(TEXT("Object Rotation") + Suffix), Transform.RotationOffset, Error)
			&& WriteNativeStruct(Schema, Data, *(TEXT("Object Scale") + Suffix), Transform.Scale, Error);
	}

	static bool WriteVariation(const UStruct* Schema, void* Data,
		const FEFCalystoArchitectureVariationV6& Variation, FString& Error)
	{
		return WriteNativeStruct(Schema, Data, TEXT("Transform Min"), Variation.LocationMinimum, Error)
			&& WriteNativeStruct(Schema, Data, TEXT("Transform Max"), Variation.LocationMaximum, Error)
			&& WriteNativeStruct(Schema, Data, TEXT("Rotation Min"), Variation.RotationMinimum, Error)
			&& WriteNativeStruct(Schema, Data, TEXT("Rotation Max"), Variation.RotationMaximum, Error)
			&& WriteNativeBool(Schema, Data, TEXT("Uniform Scale"), Variation.bUniformScale, Error)
			&& WriteNativeStruct(Schema, Data, TEXT("Scale Min"), Variation.ScaleMinimum, Error)
			&& WriteNativeStruct(Schema, Data, TEXT("Scale Max"), Variation.ScaleMaximum, Error);
	}

	template<typename EntryType, typename Writer>
	static bool WriteStyleArray(UObject* Clone, const TCHAR* Name, const TArray<EntryType>& Source, Writer&& Write, FString& Error)
	{
		FArrayProperty* Property = FindTypedAllowlistedProperty<FArrayProperty>(Clone->GetClass(), Name, TEXT("struct array"), Error);
		FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
		if (!Inner || !Inner->Struct)
		{
			if (Error.IsEmpty()) Error = FString::Printf(TEXT("Style Architecture %s is not a struct array."), Name);
			return false;
		}
		FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Clone));
		Array.Resize(0);
		for (const EntryType& Entry : Source)
		{
			void* Data = Array.GetRawPtr(Array.AddValue());
			if (!Write(Inner->Struct, Data, Entry)) return false;
		}
		return true;
	}

	static bool ApplyStyleArchitectureV6(UObject* Clone, const FEFCalystoStyleArchitectureV6& SourceArchitecture,
		UClass* RoomTypeClass, FString& Error)
	{
		FEFCalystoStyleArchitectureV6 Architecture = SourceArchitecture;
		Architecture.WallLights.Reset();
		for (const FEFCalystoArchitectureLightV6& Light : SourceArchitecture.WallLights)
		{
			if (Light.PlacementZone == EEFCalystoPlacementZoneV6::WallMiddle)
			{
				Architecture.WallLights.Add(Light);
				continue;
			}
			FEFCalystoArchitectureObjectV6 Object;
			Object.Type = EEFCalystoArchitectureObjectTypeV6::ActorBlueprint;
			Object.ActorClass = Light.ActorClass;
			Object.SelectionWeight = Light.SelectionWeight;
			Object.Variation = Light.Variation;
			Object.Variation.LocationMinimum.Y -= Light.PositionJitterCm;
			Object.Variation.LocationMaximum.Y += Light.PositionJitterCm;
			Object.Variation.LocationMinimum.Z -= Light.PositionJitterCm;
			Object.Variation.LocationMaximum.Z += Light.PositionJitterCm;
			if (Light.PlacementZone == EEFCalystoPlacementZoneV6::WallBottom) Architecture.WallBottomObjects.Add(Object);
			else if (Light.PlacementZone == EEFCalystoPlacementZoneV6::WallTop) Architecture.WallTopObjects.Add(Object);
			else { Error = TEXT("Style lights require a Wall Bottom, Wall Middle or Wall Top placement zone."); return false; }
		}
		const auto MeshWriter = [&Error](const UStruct* Schema, void* Data, const FEFCalystoArchitectureMeshV6& Entry)
		{
			return WriteNativeObject(Schema, Data, TEXT("Mesh"), Entry.Mesh.Get(), !Entry.Mesh.IsNull(), Error)
				&& WriteNativeWeight(Schema, Data, Entry.SelectionWeight, Error)
				&& WriteFixedTransform(Schema, Data, Entry.Transform, TEXT(""), Error);
		};
		const auto ActorWriter = [&Error](const UStruct* Schema, void* Data, const FEFCalystoArchitectureActorV6& Entry)
		{
			return WriteNativeObject(Schema, Data, TEXT("Mesh"), Entry.ActorClass.Get(), !Entry.ActorClass.IsNull(), Error)
				&& WriteNativeWeight(Schema, Data, Entry.SelectionWeight, Error)
				&& WriteFixedTransform(Schema, Data, Entry.Transform, TEXT(""), Error);
		};
		const auto DoorWriter = [&Error](const UStruct* Schema, void* Data, const FEFCalystoArchitectureDoorwayV6& Entry)
		{
			return WriteNativeObject(Schema, Data, TEXT("Mesh Wall"), Entry.WallMesh.Get(), !Entry.WallMesh.IsNull(), Error)
				&& WriteNativeObject(Schema, Data, TEXT("Mesh Frame"), Entry.FrameMesh.Get(), !Entry.FrameMesh.IsNull(), Error)
				&& WriteNativeObject(Schema, Data, TEXT("Door Blueprint"), Entry.DoorClass.Get(), !Entry.DoorClass.IsNull(), Error)
				&& WriteNativeWeight(Schema, Data, Entry.SelectionWeight, Error)
				&& WriteFixedTransform(Schema, Data, Entry.WallTransform, TEXT(""), Error)
				&& WriteFixedTransform(Schema, Data, Entry.FrameTransform, TEXT(" Frame"), Error)
				&& WriteFixedTransform(Schema, Data, Entry.DoorTransform, TEXT(" Door"), Error);
		};
		const auto LightWriter = [&Error](const UStruct* Schema, void* Data, const FEFCalystoArchitectureLightV6& Entry)
		{
			FEFCalystoArchitectureVariationV6 Variation = Entry.Variation;
			// Native light transforms are in the wall's local frame. Keep depth fixed.
			Variation.LocationMinimum.Y -= Entry.PositionJitterCm;
			Variation.LocationMaximum.Y += Entry.PositionJitterCm;
			Variation.LocationMinimum.Z -= Entry.PositionJitterCm;
			Variation.LocationMaximum.Z += Entry.PositionJitterCm;
			return WriteNativeObject(Schema, Data, TEXT("Blueprint"), Entry.ActorClass.Get(), !Entry.ActorClass.IsNull(), Error)
				&& WriteNativeWeight(Schema, Data, Entry.SelectionWeight, Error)
				&& WriteVariation(Schema, Data, Variation, Error);
		};
		FRoomArchitectureSchema ObjectSchema;
		if (!ResolveRoomArchitectureSchema(RoomTypeClass, ObjectSchema, Error)) return false;
		const auto ObjectWriter = [&Error, &ObjectSchema](const UStruct* Schema, void* Data, const FEFCalystoArchitectureObjectV6& Entry)
		{
			if (Schema != ObjectSchema.EntryStruct->Struct)
			{
				Error = TEXT("Style decoration entries no longer share ST_ObjectDungeon with Room Architecture.");
				return false;
			}
			return WriteArchitectureEntry(Data, ObjectSchema, Entry, TEXT("Style Architecture"), Error);
		};
		return WriteStyleArray(Clone, TEXT("Floor"), Architecture.Floor, MeshWriter, Error)
			&& WriteStyleArray(Clone, TEXT("Wall"), Architecture.Wall, MeshWriter, Error)
			&& WriteStyleArray(Clone, TEXT("Roof"), Architecture.Roof, MeshWriter, Error)
			&& WriteStyleArray(Clone, TEXT("DoorFrame"), Architecture.DoorFrame, MeshWriter, Error)
			&& WriteStyleArray(Clone, TEXT("RampTop"), Architecture.RampTop, MeshWriter, Error)
			&& WriteStyleArray(Clone, TEXT("RampBottom"), Architecture.RampBottom, MeshWriter, Error)
			&& WriteStyleArray(Clone, TEXT("Door"), Architecture.Door, ActorWriter, Error)
			&& WriteStyleArray(Clone, TEXT("WallDoor"), Architecture.WallDoor, DoorWriter, Error)
			&& WriteStyleArray(Clone, TEXT("WallLightObject"), Architecture.WallLights, LightWriter, Error)
			&& WriteStyleArray(Clone, TEXT("WallBottomObject"), Architecture.WallBottomObjects, ObjectWriter, Error)
			&& WriteStyleArray(Clone, TEXT("WallMiddleObject"), Architecture.WallMiddleObjects, ObjectWriter, Error)
			&& WriteStyleArray(Clone, TEXT("WallTopObject"), Architecture.WallTopObjects, ObjectWriter, Error)
			&& WriteStyleArray(Clone, TEXT("RoofObject"), Architecture.RoofObjects, ObjectWriter, Error)
			&& WriteNativeObject(Clone->GetClass(), Clone, TEXT("StartBlueprint"), Architecture.StartBlueprint.Get(), !Architecture.StartBlueprint.IsNull(), Error)
			&& WriteNativeObject(Clone->GetClass(), Clone, TEXT("EndBlueprint"), Architecture.EndBlueprint.Get(), !Architecture.EndBlueprint.IsNull(), Error);
	}

	static bool ApplyDungeonMaterialPlanV6(
		UObject* DungeonMaterialClone,
		const FDungeonMaterialSchema& Schema,
		const FEFCalystoSurfaceMaterialSetV6& StyleMaterials,
		int32& OutUpdatedSlots,
		FString& OutError)
	{
		OutUpdatedSlots = 0;
		UMaterialInstance* FloorMaterial = nullptr;
		UMaterialInstance* WallMaterial = nullptr;
		UMaterialInstance* RoofMaterial = nullptr;
		if (!ResolveResidentMaterial(StyleMaterials.FloorMaterial, TEXT("V6 Style Floor"), FloorMaterial, OutError)
			|| !ResolveResidentMaterial(StyleMaterials.WallMaterial, TEXT("V6 Style Wall"), WallMaterial, OutError)
			|| !ResolveResidentMaterial(StyleMaterials.RoofMaterial, TEXT("V6 Style Roof"), RoofMaterial, OutError))
		{
			return false;
		}

		const auto WriteSlot = [DungeonMaterialClone](
			const FMaterialSlotSchema& Slot,
			UMaterialInstance* Material)
		{
			void* SlotValue = Slot.Slot->ContainerPtrToValuePtr<void>(DungeonMaterialClone);
			Slot.Material->SetObjectPropertyValue_InContainer(SlotValue, Material);
		};
		WriteSlot(Schema.Floor, FloorMaterial);
		WriteSlot(Schema.Wall, WallMaterial);
		WriteSlot(Schema.Roof, RoofMaterial);
		OutUpdatedSlots = 3;
		return true;
	}

	static bool ApplyReachableThemeCompatibilityV6(
		AActor* DungeonActor,
		UObject* ThemeClone,
		const FThemeSchema& Schema,
		const FEFCalystoResolvedFloorPlanV6& FloorPlan,
		TMap<FName, TObjectPtr<UObject>>& OutThemeRoomTypes,
		TArray<TStrongObjectPtr<UObject>>& OutStrongReferences,
		int32& OutUpdatedEntries,
		int32& OutUpdatedMaterialEntries,
		FString& OutError)
	{
		OutThemeRoomTypes.Reset();
		OutStrongReferences.Reset();
		OutUpdatedEntries = 0;
		OutUpdatedMaterialEntries = 0;
		if (!IsValid(DungeonActor) || !Schema.RoomType || !IsValid(Schema.RoomType->PropertyClass))
		{
			OutError = TEXT("The V6 Room Theme adapter cannot synthesize room architecture without a valid runtime actor and RoomType property class.");
			return false;
		}

		FRoomArchitectureSchema ArchitectureSchema;
		if (!ResolveRoomArchitectureSchema(Schema.RoomType->PropertyClass, ArchitectureSchema, OutError))
		{
			return false;
		}
		FScriptArrayHelper Entries(Schema.Entries, Schema.Entries->ContainerPtrToValuePtr<void>(ThemeClone));
		if (Entries.Num() <= 0)
		{
			OutError = TEXT("The transient Calysto V6 Room Theme compatibility clone has no ST_RoomTheme entries.");
			return false;
		}

		FStructOnScope VendorTemplate(Schema.EntryStruct->Struct);
		if (!VendorTemplate.IsValid())
		{
			OutError = TEXT("The vendor ST_RoomTheme schema could not create a transient template record.");
			return false;
		}
		Schema.EntryStruct->Struct->CopyScriptStruct(
			VendorTemplate.GetStructMemory(), Entries.GetRawPtr(0));

		TArray<const FEFCalystoResolvedThemeProfileV6*> OrderedThemes;
		OrderedThemes.Reserve(FloorPlan.Themes.Num());
		TSet<FName> ThemeIds;
		for (const FEFCalystoResolvedThemeProfileV6& Theme : FloorPlan.Themes)
		{
			if (Theme.ThemeId.IsNone() || !FMath::IsFinite(Theme.SelectionWeight)
				|| Theme.SelectionWeight <= 0.0f || Theme.ArchitectureHash.Len() != 64)
			{
				OutError = FString::Printf(
					TEXT("Reachable V6 Theme '%s' has an invalid weight or inline Architecture Hash."),
					*Theme.ThemeId.ToString());
				return false;
			}
			if (ThemeIds.Contains(Theme.ThemeId))
			{
				OutError = FString::Printf(
					TEXT("Frozen V6 floor plan contains duplicate Theme ID '%s'."),
					*Theme.ThemeId.ToString());
				return false;
			}
			ThemeIds.Add(Theme.ThemeId);
			OrderedThemes.Add(&Theme);
		}
		OrderedThemes.Sort([](
			const FEFCalystoResolvedThemeProfileV6& Left,
			const FEFCalystoResolvedThemeProfileV6& Right)
		{
			return Left.ThemeId.ToString().ToLower() < Right.ThemeId.ToString().ToLower();
		});

		const auto ApplyThemeSlot = [&OutError](
			FBoolProperty* OverrideProperty,
			FObjectProperty* MaterialProperty,
			void* Entry,
			const EEFCalystoMaterialResolutionModeV6 Mode,
			const TSoftObjectPtr<UMaterialInstance>& MaterialReference,
			const FString& ContractName)
		{
			const bool bOverride = Mode == EEFCalystoMaterialResolutionModeV6::Override;
			OverrideProperty->SetPropertyValue_InContainer(Entry, bOverride);
			if (!bOverride)
			{
				MaterialProperty->SetObjectPropertyValue_InContainer(Entry, nullptr);
				return true;
			}
			UMaterialInstance* Material = nullptr;
			if (!ResolveResidentMaterial(MaterialReference, ContractName, Material, OutError))
			{
				return false;
			}
			MaterialProperty->SetObjectPropertyValue_InContainer(Entry, Material);
			return true;
		};

		// The vendor asset supplies only the reflected schema and one initialized
		// template. V6 rebuilds the transient array from the complete frozen Theme
		// set, so an open FName policy is not capped by the vendor's authored rows.
		Entries.Resize(0);
		for (const FEFCalystoResolvedThemeProfileV6* ThemePtr : OrderedThemes)
		{
			if (!ThemePtr)
			{
				OutError = TEXT("The canonical reachable Theme order contains a null profile.");
				return false;
			}
			const FEFCalystoResolvedThemeProfileV6& Theme = *ThemePtr;
			FString SafeThemeName = Theme.ThemeId.ToString();
			for (TCHAR& Character : SafeThemeName)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
				{
					Character = TEXT('_');
				}
			}
			const FName BaseName(*FString::Printf(TEXT("EFCalystoV6_%s_RoomArchitecture"), *SafeThemeName));
			const FName ObjectName = MakeUniqueObjectName(DungeonActor, Schema.RoomType->PropertyClass, BaseName);
			UObject* RoomType = NewObject<UObject>(
				DungeonActor,
				Schema.RoomType->PropertyClass,
				ObjectName,
				RF_Transient);
			if (!IsValid(RoomType) || RoomType->GetOuter() != DungeonActor
				|| !RoomType->HasAnyFlags(RF_Transient)
				|| !PopulateRoomArchitecture(RoomType, ArchitectureSchema, Theme.Architecture, Theme.ThemeId, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("V6 failed to synthesize transient Calysto Room Type for Theme '%s'."),
						*Theme.ThemeId.ToString());
				}
				return false;
			}
			OutThemeRoomTypes.Add(Theme.ThemeId, RoomType);
			OutStrongReferences.Emplace(RoomType);

			const int32 EntryIndex = Entries.AddValue();
			void* Entry = Entries.GetRawPtr(EntryIndex);
			Schema.EntryStruct->Struct->CopyScriptStruct(
				Entry, VendorTemplate.GetStructMemory());
			Schema.RoomType->SetObjectPropertyValue_InContainer(Entry, RoomType);
			const FString ThemeLabel = FString::Printf(TEXT("V6 Room Theme %s"), *Theme.ThemeId.ToString());
			if (!ApplyThemeSlot(Schema.OverrideFloorMaterial, Schema.FloorMaterial, Entry,
					Theme.FloorMaterialMode, Theme.EffectiveMaterials.FloorMaterial, ThemeLabel + TEXT(" Floor"))
				|| !ApplyThemeSlot(Schema.OverrideWallMaterial, Schema.WallMaterial, Entry,
					Theme.WallMaterialMode, Theme.EffectiveMaterials.WallMaterial, ThemeLabel + TEXT(" Wall"))
				|| !ApplyThemeSlot(Schema.OverrideRoofMaterial, Schema.RoofMaterial, Entry,
					Theme.RoofMaterialMode, Theme.EffectiveMaterials.RoofMaterial, ThemeLabel + TEXT(" Roof")))
			{
				return false;
			}
			Schema.Weight->SetPropertyValue_InContainer(Entry, static_cast<double>(Theme.SelectionWeight));
			++OutUpdatedEntries;
			++OutUpdatedMaterialEntries;
		}
		if (Entries.Num() != FloorPlan.Themes.Num())
		{
			OutError = TEXT("The transient ST_RoomTheme array did not materialize every frozen reachable Theme.");
			return false;
		}
		if (OutThemeRoomTypes.Num() != FloorPlan.Themes.Num()
			|| OutStrongReferences.Num() != FloorPlan.Themes.Num())
		{
			OutError = TEXT("The transient Room Type retention set does not cover every reachable V6 Theme.");
			return false;
		}
		return true;
	}

	/**
	 * Read the committed transient objects back through the same narrow reflected
	 * contract used for application.  This is deliberately native rather than
	 * Python/editor-property based: Calysto keeps the relevant BP variables
	 * private, while the runtime contract must be certified in packaged builds
	 * too.  A failure here happens before GetPiecesShape/PCG starts.
	 */
	static bool VerifyCommittedVisualMaterialPlanV6(
		AActor* DungeonActor,
		UObject* DungeonMaterialClone,
		UObject* ThemeClone,
		const FActorSchema& ActorSchema,
		const FDungeonMaterialSchema& DungeonMaterialSchema,
		const FThemeSchema& ThemeSchema,
		const FEFCalystoResolvedFloorPlanV6& FloorPlan,
		FString& OutProof,
		const TMap<FName, TObjectPtr<UObject>>& ThemeRoomTypes,
		FString& OutError)
	{
		OutProof.Reset();
		OutError.Reset();
		if (!IsValid(DungeonActor) || !IsValid(DungeonMaterialClone) || !IsValid(ThemeClone))
		{
			OutError = TEXT("V6 visual-material verification received an invalid runtime object.");
			return false;
		}
		if (ActorSchema.DungeonMaterial->GetObjectPropertyValue_InContainer(DungeonActor) != DungeonMaterialClone
			|| ActorSchema.RoomThemeList->GetObjectPropertyValue_InContainer(DungeonActor) != ThemeClone
			|| DungeonMaterialClone->GetOuter() != DungeonActor
			|| ThemeClone->GetOuter() != DungeonActor
			|| !DungeonMaterialClone->HasAnyFlags(RF_Transient)
			|| !ThemeClone->HasAnyFlags(RF_Transient))
		{
			OutError = TEXT("V6 visual-material clones were not committed as transient properties of the runtime dungeon actor.");
			return false;
		}
		if (!ActorSchema.OverrideFloorMaterial->GetPropertyValue_InContainer(DungeonActor)
			|| !ActorSchema.OverrideWallMaterial->GetPropertyValue_InContainer(DungeonActor)
			|| !ActorSchema.OverrideRoofMaterial->GetPropertyValue_InContainer(DungeonActor))
		{
			OutError = TEXT("V6 Style Dungeon Material priority flags are not all enabled on the runtime dungeon actor.");
			return false;
		}

		const auto VerifyDungeonSlot = [&OutError, DungeonMaterialClone](
			const FMaterialSlotSchema& Slot,
			const TSoftObjectPtr<UMaterialInstance>& ExpectedReference,
			const TCHAR* Label)
		{
			UMaterialInstance* ExpectedMaterial = nullptr;
			if (!ResolveResidentMaterial(ExpectedReference, FString::Printf(TEXT("V6 verification %s"), Label), ExpectedMaterial, OutError))
			{
				return false;
			}
			void* SlotValue = Slot.Slot->ContainerPtrToValuePtr<void>(DungeonMaterialClone);
			UObject* ActualMaterial = Slot.Material->GetObjectPropertyValue_InContainer(SlotValue);
			if (ActualMaterial != ExpectedMaterial)
			{
				OutError = FString::Printf(
					TEXT("V6 %s material mismatch after commit: expected %s, actual %s."),
					Label,
					*GetPathNameSafe(ExpectedMaterial),
					*GetPathNameSafe(ActualMaterial));
				return false;
			}
			return true;
		};
		if (!VerifyDungeonSlot(DungeonMaterialSchema.Floor, FloorPlan.StyleMaterials.FloorMaterial, TEXT("Dungeon Floor"))
			|| !VerifyDungeonSlot(DungeonMaterialSchema.Wall, FloorPlan.StyleMaterials.WallMaterial, TEXT("Dungeon Wall"))
			|| !VerifyDungeonSlot(DungeonMaterialSchema.Roof, FloorPlan.StyleMaterials.RoofMaterial, TEXT("Dungeon Roof")))
		{
			return false;
		}

		TMap<UObject*, const FEFCalystoResolvedThemeProfileV6*> ThemesByRoomType;
		for (const FEFCalystoResolvedThemeProfileV6& Theme : FloorPlan.Themes)
		{
			UObject* RoomType = ThemeRoomTypes.FindRef(Theme.ThemeId);
			if (!IsValid(RoomType) || ThemesByRoomType.Contains(RoomType))
			{
				OutError = TEXT("V6 visual-material verification requires one resident Room Type per reachable Theme.");
				return false;
			}
			ThemesByRoomType.Add(RoomType, &Theme);
		}

		const auto VerifyThemeSlot = [&OutError](
			FBoolProperty* OverrideProperty,
			FObjectProperty* MaterialProperty,
			void* Entry,
			const bool bExpectedOverride,
			const TSoftObjectPtr<UMaterialInstance>& ExpectedReference,
			const FString& Label)
		{
			const bool bActualOverride = OverrideProperty->GetPropertyValue_InContainer(Entry);
			if (bActualOverride != bExpectedOverride)
			{
				OutError = FString::Printf(TEXT("V6 %s priority flag mismatch after commit."), *Label);
				return false;
			}
			UObject* ActualMaterial = MaterialProperty->GetObjectPropertyValue_InContainer(Entry);
			if (!bExpectedOverride)
			{
				if (ActualMaterial != nullptr)
				{
					OutError = FString::Printf(
						TEXT("V6 %s Inherit slot retained material %s; nullptr is required."),
						*Label,
						*GetPathNameSafe(ActualMaterial));
					return false;
				}
				return true;
			}
			UMaterialInstance* ExpectedMaterial = nullptr;
			if (!ResolveResidentMaterial(ExpectedReference, TEXT("V6 Room Theme verification"), ExpectedMaterial, OutError))
			{
				return false;
			}
			if (ActualMaterial != ExpectedMaterial)
			{
				OutError = FString::Printf(
					TEXT("V6 %s material mismatch after commit: expected %s, actual %s."),
					*Label,
					*GetPathNameSafe(ExpectedMaterial),
					*GetPathNameSafe(ActualMaterial));
				return false;
			}
			return true;
		};

		FScriptArrayHelper Entries(ThemeSchema.Entries, ThemeSchema.Entries->ContainerPtrToValuePtr<void>(ThemeClone));
		TSet<UObject*> VerifiedRoomTypes;
		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			void* Entry = Entries.GetRawPtr(EntryIndex);
			UObject* RoomType = ThemeSchema.RoomType->GetObjectPropertyValue_InContainer(Entry);
			const FEFCalystoResolvedThemeProfileV6* const* PlannedTheme = ThemesByRoomType.Find(RoomType);
			if (!RoomType || !PlannedTheme || !*PlannedTheme || VerifiedRoomTypes.Contains(RoomType))
			{
				OutError = TEXT("V6 visual-material verification could not match one unique reachable Room Theme clone entry.");
				return false;
			}
			const FEFCalystoResolvedThemeProfileV6& Theme = **PlannedTheme;
			const FString ThemeLabel = Theme.ThemeId.ToString();
			if (!VerifyThemeSlot(ThemeSchema.OverrideFloorMaterial, ThemeSchema.FloorMaterial, Entry,
					Theme.FloorMaterialMode == EEFCalystoMaterialResolutionModeV6::Override, Theme.EffectiveMaterials.FloorMaterial, ThemeLabel + TEXT(" Floor"))
				|| !VerifyThemeSlot(ThemeSchema.OverrideWallMaterial, ThemeSchema.WallMaterial, Entry,
					Theme.WallMaterialMode == EEFCalystoMaterialResolutionModeV6::Override, Theme.EffectiveMaterials.WallMaterial, ThemeLabel + TEXT(" Wall"))
				|| !VerifyThemeSlot(ThemeSchema.OverrideRoofMaterial, ThemeSchema.RoofMaterial, Entry,
					Theme.RoofMaterialMode == EEFCalystoMaterialResolutionModeV6::Override, Theme.EffectiveMaterials.RoofMaterial, ThemeLabel + TEXT(" Roof")))
			{
				return false;
			}
			VerifiedRoomTypes.Add(RoomType);
		}
		if (VerifiedRoomTypes.Num() != ThemesByRoomType.Num())
		{
			OutError = TEXT("V6 visual-material verification did not read every reachable Room Theme clone entry.");
			return false;
		}
		OutProof = FString::Printf(
			TEXT("visualInputPlanProof=PASS visualActorPriority=111 visualDungeonMaterialClone=%s visualRoomThemeListClone=%s styleId=%s styleMaterialHash=%s reachableThemes=%d"),
			*GetPathNameSafe(DungeonMaterialClone),
			*GetPathNameSafe(ThemeClone),
			*FloorPlan.StyleId.ToString(),
			*FloorPlan.StyleMaterialHash,
			FloorPlan.Themes.Num());
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

	const FEFCalystoResolvedFloorIntentV6 Plan = DungeonSubsystem->GetResolvedFloorIntentV6();
	if (!Plan.bIsValid
		|| Plan.SchemaVersion != EFCalystoDungeonRuntimeSchemaV6::SchemaVersion
		|| Plan.GeneratorVersion != EFCalystoDungeonRuntimeSchemaV6::GeneratorVersion
		|| Plan.GenerationContext.RunSeed <= 0
		|| Plan.GenerationContext.FloorNumber < 1
		|| Plan.GenerationContext.GenerationSerial < 0
		|| Plan.GenerationContext.PolicyHash.IsEmpty()
		|| Plan.EcologyHash.IsEmpty()
		|| Plan.CompanionRoster.SnapshotHash.IsEmpty()
		|| Plan.FloorPlan.FloorPlanHash.IsEmpty()
		|| Plan.StyleId.IsNone()
		|| Plan.StyleId != Plan.FloorPlan.StyleId
		|| Plan.IntentHash.IsEmpty())
	{
		return Fail(FString::Printf(
			TEXT("Frozen V6 floor intent is invalid (valid=%d run=%lld floor=%lld serial=%lld style=%s policy=%s ecology=%s companion=%s plan=%s intent=%s); fallback is forbidden."),
			Plan.bIsValid ? 1 : 0,
			Plan.GenerationContext.RunSeed,
			Plan.GenerationContext.FloorNumber,
			Plan.GenerationContext.GenerationSerial,
			*Plan.StyleId.ToString(),
			*Plan.GenerationContext.PolicyHash,
			*Plan.EcologyHash,
			*Plan.CompanionRoster.SnapshotHash,
			*Plan.FloorPlan.FloorPlanHash,
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
	UObject* OriginalDungeonMaterial = ActorSchema.DungeonMaterial->GetObjectPropertyValue_InContainer(DungeonActor);
	if (!IsValid(OriginalDungeon) || !IsValid(OriginalSpawners) || !IsValid(OriginalThemeList) || !IsValid(OriginalDungeonMaterial))
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
	UObject* ConfiguredDungeonMaterial = Settings->DungeonMaterialDataAsset.Get();
	if (!IsValid(ConfiguredDungeonMaterial) || ConfiguredDungeonMaterial != OriginalDungeonMaterial)
	{
		return Fail(FString::Printf(
			TEXT("Actor DungeonMaterial source %s does not match preloaded harness source %s."),
			*GetPathNameSafe(OriginalDungeonMaterial),
			*GetPathNameSafe(ConfiguredDungeonMaterial)));
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
			TEXT("PopulationAnchorClass must resolve to the project-owned V6 anchor %s, got %s."),
			*AEFCalystoPopulationAnchor::StaticClass()->GetPathName(),
			*GetPathNameSafe(ConfiguredAnchorClass)));
	}

	FClassProperty* EndBlueprintProperty = nullptr;
	FSpawnerSchema SpawnerSchema;
	FThemeSchema ThemeSchema;
	FDungeonMaterialSchema DungeonMaterialSchema;
	if (!ResolveDungeonMeshSchema(OriginalDungeon, EndBlueprintProperty, Error)
		|| !ResolveSpawnerSchema(OriginalSpawners, SpawnerSchema, Error)
		|| !ResolveThemeSchema(OriginalThemeList, ThemeSchema, Error)
		|| !ResolveDungeonMaterialSchema(OriginalDungeonMaterial, DungeonMaterialSchema, Error))
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
	UE_LOG(LogEFCalystoPCGAdapter, Log, TEXT("Diagnostic BP_MassiveDungeon baseline size=%s density=%.6f sidePath=%.6f nativeLightHeight=%.3f nativeLightInterval=%d; authoritative V6 intent will be applied to the transient runtime actor."),
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
			TEXT("Frozen V6 layout size %s violates authoritative 18-30x18-30x1 limits (diagnostic baseline %s)."),
			*Plan.DungeonSize.ToString(),
			*BaselineDungeonSize.ToString()));
	}
	if (!FMath::IsFinite(Plan.CandidateDensity)
		|| Plan.CandidateDensity < 0.20f
		|| Plan.CandidateDensity > 0.50f
		|| !FMath::IsFinite(Plan.SidePathChance)
		|| Plan.SidePathChance < 0.30f
		|| Plan.SidePathChance > 0.70f)
	{
		return Fail(TEXT("Frozen V6 candidate density or side-path chance violates immutable limits."));
	}
	if (!FMath::IsFinite(Plan.Lighting.WallLightHeightCm)
		|| Plan.Lighting.WallLightHeightCm < 0.0f
		|| Plan.Lighting.WallLightHeightCm > 1000.0f
		|| Plan.Lighting.WallLightTileDistance < 1
		|| Plan.Lighting.WallLightTileDistance > 100)
	{
		return Fail(TEXT("Frozen V6 lighting values violate the Calysto-safe height or tile-interval range."));
	}
	const FIntVector AppliedDungeonSize = Plan.DungeonSize;
	// FloorIntent owns the Shape RNG domain. Consuming that frozen value here keeps
	// Calysto geometry independent from unrelated catalog/population fields that also
	// participate in IntentHash, and makes runtime PCG match replay telemetry exactly.
	const int32 DeterministicPCGSeed = Plan.PCGSeed;
	if (DeterministicPCGSeed <= 0)
	{
		return Fail(TEXT("Frozen V6 intent has an invalid deterministic PCG seed."));
	}

	UObject* DungeonClone = DuplicateTransiently(OriginalDungeon, DungeonActor, TEXT("Dungeon"));
	UObject* SpawnersClone = DuplicateTransiently(OriginalSpawners, DungeonActor, TEXT("Spawners"));
	UObject* ThemeClone = DuplicateTransiently(OriginalThemeList, DungeonActor, TEXT("Themes"));
	UObject* DungeonMaterialClone = DuplicateTransiently(OriginalDungeonMaterial, DungeonActor, TEXT("DungeonMaterial"));
	if (!DungeonClone || !SpawnersClone || !ThemeClone || !DungeonMaterialClone)
	{
		return Fail(TEXT("Failed to create all four transient Calysto DataAsset clones."));
	}

	if (!ApplyStyleArchitectureV6(DungeonClone, Plan.FloorPlan.StyleArchitecture, ThemeSchema.RoomType->PropertyClass, Error))
	{
		return Fail(MoveTemp(Error));
	}
	// The progression door remains project-owned regardless of the decorative end marker.
	EndBlueprintProperty->SetObjectPropertyValue_InContainer(DungeonClone, FloorDoorClass);
	if (!ApplyDungeonMaterialPlanV6(
			DungeonMaterialClone,
			DungeonMaterialSchema,
			Plan.FloorPlan.StyleMaterials,
			Result.UpdatedDungeonMaterialSlots,
			Error)
		|| !ApplyPopulationAnchorSpawner(
			SpawnersClone,
			SpawnerSchema,
			Result.UpdatedAnchorEntries,
			Error)
		|| !ApplyReachableThemeCompatibilityV6(
			DungeonActor,
			ThemeClone,
			ThemeSchema,
			Plan.FloorPlan,
			Result.ThemeRoomTypes,
			Result.RuntimeStrongReferences,
			Result.UpdatedThemeEntries,
			Result.UpdatedThemeMaterialEntries,
			Error))
	{
		return Fail(MoveTemp(Error));
	}

	// Commit only after every reflected schema, source row, clone, and scalar has passed.
	ActorSchema.Dungeon->SetObjectPropertyValue_InContainer(DungeonActor, DungeonClone);
	ActorSchema.Spawners->SetObjectPropertyValue_InContainer(DungeonActor, SpawnersClone);
	ActorSchema.RoomThemeList->SetObjectPropertyValue_InContainer(DungeonActor, ThemeClone);
	ActorSchema.DungeonMaterial->SetObjectPropertyValue_InContainer(DungeonActor, DungeonMaterialClone);
	ActorSchema.OverrideFloorMaterial->SetPropertyValue_InContainer(DungeonActor, true);
	ActorSchema.OverrideWallMaterial->SetPropertyValue_InContainer(DungeonActor, true);
	ActorSchema.OverrideRoofMaterial->SetPropertyValue_InContainer(DungeonActor, true);
	*ActorSchema.DungeonSize->ContainerPtrToValuePtr<FIntVector>(DungeonActor) = AppliedDungeonSize;
	WriteFloating(ActorSchema.SpawnerDensity, DungeonActor, Plan.CandidateDensity);
	WriteFloating(ActorSchema.SidePathChance, DungeonActor, Plan.SidePathChance);
	// These are Calysto's own BP_MassiveDungeon variables.  The actor is a
	// transient runtime instance, so this alters neither the vendor Blueprint
	// nor its CDO.  Calysto remains the only system that places torch actors.
	WriteFloating(ActorSchema.WallLightHeight, DungeonActor, Plan.Lighting.WallLightHeightCm);
	ActorSchema.WallLightTileDistance->SetPropertyValue_InContainer(
		DungeonActor, Plan.Lighting.WallLightTileDistance);

	// Calysto remains the owner of its derived piece/shape state. Invoke its exact native
	// boundary after the transient inputs are committed and before PCG delegates/seed/generation.
	DungeonActor->ProcessEvent(GetPiecesShapeFunction, nullptr);
	Result.bGetPiecesShapeInvoked = true;
	FString VisualMaterialProof;
	if (!VerifyCommittedVisualMaterialPlanV6(
			DungeonActor,
			DungeonMaterialClone,
			ThemeClone,
			ActorSchema,
			DungeonMaterialSchema,
			ThemeSchema,
			Plan.FloorPlan,
			VisualMaterialProof,
			Result.ThemeRoomTypes,
			Error))
	{
		return Fail(MoveTemp(Error));
	}

	Result.PCGSeed = DeterministicPCGSeed;
	Result.bApplied = true;
	UE_LOG(
		LogEFCalystoPCGAdapter,
		Log,
		TEXT("PASS V6 actor=%s run=%lld floor=%lld serial=%lld seed=%d style=%s policyHash=%s ecologyHash=%s companionHash=%s intentHash=%s floorPlanHash=%s styleMaterialHash=%s size=%s candidateDensity=%.3f sidePath=%.3f wallLightHeight=%.3f wallLightInterval=%d lightingDraw=%.3f anchorEntries=%d reachableThemeEntries=%d dungeonMaterialSlots=%d themeMaterialEntries=%d."),
		*DungeonActor->GetName(),
		Plan.GenerationContext.RunSeed,
		Plan.GenerationContext.FloorNumber,
		Plan.GenerationContext.GenerationSerial,
		Result.PCGSeed,
		*Plan.StyleId.ToString(),
		*Plan.GenerationContext.PolicyHash,
		*Plan.EcologyHash,
		*Plan.CompanionRoster.SnapshotHash,
		*Plan.IntentHash,
		*Plan.FloorPlan.FloorPlanHash,
		*Plan.FloorPlan.StyleMaterialHash,
		*AppliedDungeonSize.ToString(),
		Plan.CandidateDensity,
		Plan.SidePathChance,
		Plan.Lighting.WallLightHeightCm,
		Plan.Lighting.WallLightTileDistance,
		Plan.Lighting.IntensityDraw,
		Result.UpdatedAnchorEntries,
		Result.UpdatedThemeEntries,
		Result.UpdatedDungeonMaterialSlots,
		Result.UpdatedThemeMaterialEntries);
	UE_LOG(
		LogEFCalystoPCGAdapter,
		Log,
		TEXT("PASS V6_VISUAL_INPUT_PLAN_APPLIED actor=%s floorPlanHash=%s %s."),
		*DungeonActor->GetName(),
		*Plan.FloorPlan.FloorPlanHash,
		*VisualMaterialProof);
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoInlineArchitectureBridgeV6Test,
	"NoShellForWinter.CalystoDungeon.V6.PCG.InlineArchitectureBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEFCalystoInlineArchitectureBridgeV6Test::RunTest(const FString& Parameters)
{
	using namespace EFCalystoPCGAdapterPrivate;
	// Editor-only, read-only fixture loading. The runtime bridge itself performs no loads.
	UObject* SourceDungeon = LoadObject<UObject>(nullptr, TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.DA_DungeonMesh"));
	UObject* SourceThemes = LoadObject<UObject>(nullptr, TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.DA_RoomTheme"));
	UObject* SourceMaterials = LoadObject<UObject>(nullptr, TEXT("/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial.DA_DungeonMaterial"));
	if (!TestNotNull(TEXT("Native architecture schema fixture"), SourceDungeon)
		|| !TestNotNull(TEXT("Native theme schema fixture"), SourceThemes)
		|| !TestNotNull(TEXT("Native material schema fixture"), SourceMaterials)) return false;
	const bool DungeonDirty = SourceDungeon->GetOutermost()->IsDirty();
	const bool ThemesDirty = SourceThemes->GetOutermost()->IsDirty();
	UEFCalystoDungeonDirectorPolicyV6Asset* Policy = NewObject<UEFCalystoDungeonDirectorPolicyV6Asset>();
	Policy->InitializeV6Defaults();
	FEFCalystoResolvedFloorPlanV6 Plan;
	FString Error;
	if (!Policy->BuildResolvedFloorPlanForStyle(717, TEXT("Standard"), Plan, Error))
	{ AddError(Error); return false; }
	for (const FSoftObjectPath& Path : Plan.ReachableVisualPreloadPaths)
		if (!TestNotNull(*FString::Printf(TEXT("Inline visual dependency %s exists"), *Path.ToString()), Path.TryLoad())) return false;
	FThemeSchema ThemeSchema;
	FDungeonMaterialSchema MaterialSchema;
	if (!ResolveThemeSchema(SourceThemes, ThemeSchema, Error)
		|| !ResolveDungeonMaterialSchema(SourceMaterials, MaterialSchema, Error))
	{ AddError(Error); return false; }
	UObject* DungeonClone = DuplicateObject<UObject>(SourceDungeon, GetTransientPackage());
	DungeonClone->SetFlags(RF_Transient);
	if (!ApplyStyleArchitectureV6(DungeonClone, Plan.StyleArchitecture, ThemeSchema.RoomType->PropertyClass, Error))
	{ AddError(Error); return false; }
	FRoomArchitectureSchema RoomSchema;
	if (!ResolveRoomArchitectureSchema(ThemeSchema.RoomType->PropertyClass, RoomSchema, Error))
	{ AddError(Error); return false; }
	for (const FEFCalystoResolvedThemeProfileV6& Theme : Plan.Themes)
	{
		UObject* Room = NewObject<UObject>(GetTransientPackage(), ThemeSchema.RoomType->PropertyClass, NAME_None, RF_Transient);
		if (!PopulateRoomArchitecture(Room, RoomSchema, Theme.Architecture, Theme.ThemeId, Error))
		{ AddError(Error); return false; }
		FScriptArrayHelper Floors(RoomSchema.Floor, RoomSchema.Floor->ContainerPtrToValuePtr<void>(Room));
		TestEqual(TEXT("Inline room floor entry count survives native reflection"), Floors.Num(), Theme.Architecture.Floor.Num());
	}
	UObject* MaterialClone = DuplicateObject<UObject>(SourceMaterials, GetTransientPackage());
	int32 Slots = 0;
	if (!ApplyDungeonMaterialPlanV6(MaterialClone, MaterialSchema, Plan.StyleMaterials, Slots, Error))
	{ AddError(Error); return false; }
	TestEqual(TEXT("Three independent Style material slots"), Slots, 3);
	TestEqual(TEXT("Vendor dungeon package remains unchanged"), SourceDungeon->GetOutermost()->IsDirty(), DungeonDirty);
	TestEqual(TEXT("Vendor theme package remains unchanged"), SourceThemes->GetOutermost()->IsDirty(), ThemesDirty);
	return true;
}
#endif

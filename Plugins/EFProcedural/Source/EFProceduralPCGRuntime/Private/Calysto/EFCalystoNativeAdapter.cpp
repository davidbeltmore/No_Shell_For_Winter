#include "Calysto/EFCalystoNativeAdapter.h"
#include "Calysto/EFCalystoPCGCookedCompatibility.h"
#include "Calysto/EFCalystoPopulationAnchor.h"
#include "Calysto/EFCalystoLightingResolver.h"
#include "Calysto/EFCalystoFloorLightingComponent.h"
#include "Engine/World.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Data/PCGBasePointData.h"
#include "Elements/PCGReroute.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGAddTag.h"
#include "Elements/PCGCreateAttribute.h"
#include "Elements/PCGLoopElement.h"
#include "Elements/PCGGetActorProperty.h"
#include "Elements/PCGAttributeGetFromPointIndexElement.h"
#include "Elements/PCGTransformPoints.h"
#include "Elements/PCGSpawnActor.h"
#include "Elements/Metadata/PCGMetadataPartition.h"
#include "Elements/Metadata/PCGMetadataMathsOpElement.h"
#include "Elements/Metadata/PCGMetadataMakeVector.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "Metadata/PCGMetadata.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "PCGComponent.h"
#include "PCGCommon.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGNode.h"
#include "PCGManagedResource.h"
#include "PCGPin.h"
#include "PCGPoint.h"
#include "PCGSubgraph.h"
#include "Misc/SecureHash.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Data/PCGPointData.h"
#include "Engine/Light.h"
#include "Engine/PointLight.h"
#include "Misc/AutomationTest.h"
#endif

namespace EFCalystoNativeAdapterPrivate
{
	static UPointLightComponent* ResolveNativeTorchTemplate(UBlueprintGeneratedClass* Class)
	{
		if (!Class || !Class->SimpleConstructionScript) return nullptr;
		const auto& Nodes = Class->SimpleConstructionScript->GetAllNodes();
		if (Nodes.Num() > 64) return nullptr;
		UPointLightComponent* Result = nullptr;
		for (const USCS_Node* Node : Nodes)
		{
			if (!Node || Node->GetVariableName() != TEXT("PointLight")) continue;
			if (Result) return nullptr;
			Result = Cast<UPointLightComponent>(Node->GetActualComponentTemplate(Class));
			if (!Result) return nullptr;
		}
		return Result;
	}

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
		const FEFCalystoArchitectureTransform& Transform, const FString& Suffix, FString& Error)
	{
		return WriteNativeBool(Schema, Data, *(TEXT("Object Uniform Scale") + Suffix), Transform.bUniformScale, Error)
			&& WriteNativeStruct(Schema, Data, *(TEXT("Object Transform") + Suffix), Transform.LocationOffset, Error)
			&& WriteNativeStruct(Schema, Data, *(TEXT("Object Rotation") + Suffix), Transform.RotationOffset, Error)
			&& WriteNativeStruct(Schema, Data, *(TEXT("Object Scale") + Suffix), Transform.Scale, Error);
	}

	static bool WriteVariation(const UStruct* Schema, void* Data,
		const FEFCalystoArchitectureVariation& Variation, FString& Error)
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


	static const TMap<FName, FName>& ExpectedSurfaceMaterialConsumers()
	{
		// The vendor graph owns three independently transformed consumers for each
		// surface. Keep every one explicit: the historical defect connected the
		// roof group to FloorMaterial and was visually masked when all slots shared
		// one material.
		static const TMap<FName, FName> Consumers =
		{
			{ TEXT("StaticMeshSpawner_1"), TEXT("FloorMaterial") },
			{ TEXT("StaticMeshSpawner_2"), TEXT("FloorMaterial") },
			{ TEXT("StaticMeshSpawner_3"), TEXT("FloorMaterial") },
			{ TEXT("StaticMeshSpawner_0"), TEXT("WallMaterial") },
			{ TEXT("StaticMeshSpawner_4"), TEXT("WallMaterial") },
			{ TEXT("StaticMeshSpawner_5"), TEXT("WallMaterial") },
			{ TEXT("StaticMeshSpawner_9"), TEXT("RoofMaterial") },
			{ TEXT("StaticMeshSpawner_48"), TEXT("RoofMaterial") },
			{ TEXT("StaticMeshSpawner_58"), TEXT("RoofMaterial") }
		};
		return Consumers;
	}

	static bool ReadAttributeMaterialOverrideContract(
		const UPCGMeshSelectorBase* Selector,
		bool& bOutEnabled,
		TArray<FName>& OutAttributes,
		FString& OutError)
	{
		bOutEnabled = false;
		OutAttributes.Reset();
		if (!IsValid(Selector))
		{
			return true;
		}

		const FBoolProperty* EnabledProperty = FindFProperty<FBoolProperty>(
			Selector->GetClass(),
			TEXT("bUseAttributeMaterialOverrides"));
		if (!EnabledProperty)
		{
			return true;
		}
		bOutEnabled = EnabledProperty->GetPropertyValue_InContainer(Selector);
		if (!bOutEnabled)
		{
			return true;
		}

		const FArrayProperty* AttributesProperty = FindFProperty<FArrayProperty>(
			Selector->GetClass(),
			TEXT("MaterialOverrideAttributes"));
		const FNameProperty* AttributeNameProperty = AttributesProperty
			? CastField<FNameProperty>(AttributesProperty->Inner)
			: nullptr;
		if (!AttributesProperty || !AttributeNameProperty)
		{
			OutError = FString::Printf(
				TEXT("Calysto surface selector %s enables attribute material overrides without a TArray<FName> contract."),
				*Selector->GetClass()->GetPathName());
			return false;
		}

		FScriptArrayHelper_InContainer Attributes(AttributesProperty, Selector);
		OutAttributes.Reserve(Attributes.Num());
		for (int32 Index = 0; Index < Attributes.Num(); ++Index)
		{
			OutAttributes.Add(AttributeNameProperty->GetPropertyValue(Attributes.GetRawPtr(Index)));
		}
		return true;
	}

	static bool ValidateSurfaceMaterialConsumerGraph(
		const UPCGGraph* Graph,
		FString& OutError)
	{
		if (!IsValid(Graph))
		{
			OutError = TEXT("Cannot validate surface material consumers on an invalid PCG graph.");
			return false;
		}

		const TMap<FName, FName>& ExpectedConsumers = ExpectedSurfaceMaterialConsumers();
		TSet<FName> SeenConsumers;
		TMap<FName, int32> SurfaceCounts;

		for (const UPCGNode* Node : Graph->GetNodes())
		{
			const UPCGStaticMeshSpawnerSettings* Spawner = Node
				? Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings())
				: nullptr;
			if (!Spawner)
			{
				continue;
			}

			bool bUsesAttributeMaterialOverrides = false;
			TArray<FName> MaterialAttributes;
			if (!ReadAttributeMaterialOverrideContract(
				Spawner->MeshSelectorParameters,
				bUsesAttributeMaterialOverrides,
				MaterialAttributes,
				OutError))
			{
				return false;
			}

			const FName* ExpectedAttribute = ExpectedConsumers.Find(Node->GetFName());
			if (!ExpectedAttribute)
			{
				if (bUsesAttributeMaterialOverrides)
				{
					OutError = FString::Printf(
						TEXT("Calysto SetDungeonMesh exposes unexpected attribute-material consumer %s."),
						*Node->GetName());
					return false;
				}
				continue;
			}

			if (SeenConsumers.Contains(Node->GetFName()))
			{
				OutError = FString::Printf(
					TEXT("Calysto SetDungeonMesh duplicates required surface consumer %s."),
					*Node->GetName());
				return false;
			}
			SeenConsumers.Add(Node->GetFName());

			if (Spawner->MeshSelectorType != UPCGMeshSelectorByAttribute::StaticClass()
				|| !Cast<UPCGMeshSelectorByAttribute>(Spawner->MeshSelectorParameters)
				|| !bUsesAttributeMaterialOverrides
				|| MaterialAttributes.Num() != 1
				|| MaterialAttributes[0] != *ExpectedAttribute)
			{
				const FString ActualAttributes = MaterialAttributes.IsEmpty()
					? TEXT("<none>")
					: FString::JoinBy(MaterialAttributes, TEXT(","), [](const FName Value)
					{
						return Value.ToString();
					});
				OutError = FString::Printf(
					TEXT("Calysto surface consumer %s expected '%s' but receives '%s' (selector=%s enabled=%s)."),
					*Node->GetName(),
					*ExpectedAttribute->ToString(),
					*ActualAttributes,
					*GetPathNameSafe(Spawner->MeshSelectorParameters),
					bUsesAttributeMaterialOverrides ? TEXT("true") : TEXT("false"));
				return false;
			}
			SurfaceCounts.FindOrAdd(*ExpectedAttribute)++;
		}

		if (SeenConsumers.Num() != ExpectedConsumers.Num()
			|| SurfaceCounts.FindRef(TEXT("FloorMaterial")) != 3
			|| SurfaceCounts.FindRef(TEXT("WallMaterial")) != 3
			|| SurfaceCounts.FindRef(TEXT("RoofMaterial")) != 3)
		{
			OutError = FString::Printf(
				TEXT("Calysto SetDungeonMesh surface contract is incomplete: consumers=%d/%d floor=%d wall=%d roof=%d; each surface requires exactly three."),
				SeenConsumers.Num(),
				ExpectedConsumers.Num(),
				SurfaceCounts.FindRef(TEXT("FloorMaterial")),
				SurfaceCounts.FindRef(TEXT("WallMaterial")),
				SurfaceCounts.FindRef(TEXT("RoofMaterial")));
			return false;
		}
		return true;
	}


	static constexpr TCHAR MasterPath[] = TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.PCG_MassiveDungeonMaster");
	static constexpr TCHAR ShapePath[] = TEXT("/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.PCG_MassiveDungeonShape");
	// Loop_1 emits both ordinary final walls and audited NoSocket upper pieces.
	// Its diagnostic name must not imply that every emitted point is an upper extension.
	static const FName WallFinalPin(TEXT("EF Native Final Wall"));
	static const FName WallDoorFinalPin(TEXT("EF Native Final Wall-Door"));
	static const FName StyleOwnedWallAttribute(TEXT("EF_StyleOwnedWall"));
	// These are read-only, transient diagnostics for the exact native wall and
	// door branches. They let a rejected attempt identify the first native node
	// that loses room/material authority without changing construction or
	// accepting a spatial substitute for that authority.
	static const FName WallNormalInputPin(TEXT("EF Native Wall Normal Input"));
	static const FName WallUpperInputPin(TEXT("EF Native Wall Upper Input"));
	static const FName WallMatchedPin(TEXT("EF Native Wall Matched"));
	static const FName WallPreLoopPin(TEXT("EF Native Wall PreLoop"));
	static const FName WallAllRoomsInputPin(TEXT("EF Native Wall All Rooms Input"));
	static const FName WallConstructionInputPin(TEXT("EF Native Wall Construction Input"));
	static const FName WallThemeOutputPin(TEXT("EF Native Wall Theme Output"));
	static const FName WallTextureOutputPin(TEXT("EF Native Wall Texture Output"));
	static const FName WallDoorThemeOutputPin(TEXT("EF Native Wall-Door Theme Output"));
	static const FName WallDoorTextureOutputPin(TEXT("EF Native Wall-Door Texture Output"));
	static const FName WallDoorConstructionInputPin(TEXT("EF Native Wall-Door Construction Input"));
	static const FName WallLightRequestsPin(TEXT("EF Native Wall Light Requests"));
	static const FName WallLightActorsPin(TEXT("EF Native Wall Light Actors"));
	static constexpr TCHAR NativeTorchPath[] = TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch.BP_WallTorch_C");
	static bool CheckWallLightBlueprint(const UPCGMetadata* Metadata, PCGMetadataEntryKey Key,
		bool bAuthoredLights, const FSoftObjectPath& ExpectedClass, FString& Error)
	{
		FSoftObjectPath Path;
		if (Metadata && Metadata->HasAttribute(TEXT("Blueprint")))
		{
			if (const auto* Classes = Metadata->GetConstTypedAttribute<FSoftClassPath>(TEXT("Blueprint")))
				Path = Classes->GetValueFromItemKey(Key);
			else if (const auto* Objects = Metadata->GetConstTypedAttribute<FSoftObjectPath>(TEXT("Blueprint")))
				Path = Objects->GetValueFromItemKey(Key);
			else if (const auto* Strings = Metadata->GetConstTypedAttribute<FString>(TEXT("Blueprint")))
				Path = FSoftObjectPath(Strings->GetValueFromItemKey(Key));
			else { Error = TEXT("Native wall-light Blueprint metadata has an unsupported type."); return false; }
		}
		if (bAuthoredLights ? (ExpectedClass.IsNull() || Path != ExpectedClass) : !Path.IsNull())
		{ Error = TEXT("Native wall-light selection differs from the eligible authored catalog."); return false; }
		return true;
	}
	static bool ReadWallLightActorReferences(const UPCGMetadata* Metadata, bool bActorsExpected,
		const FPCGMetadataAttribute<FSoftObjectPath>*& References, FString& Error)
	{
		References = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(PCGPointDataConstants::ActorReferenceAttribute) : nullptr;
		if (!References && Metadata && Metadata->HasAttribute(PCGPointDataConstants::ActorReferenceAttribute))
		{ Error = TEXT("Native wall-light ActorReference metadata must have the UE 5.8 FSoftObjectPath type."); return false; }
		if (bActorsExpected && !References)
		{ Error = TEXT("Native wall-light output lacks UE 5.8 owned ActorReference metadata."); return false; }
		return true;
	}
	static bool ValidateManagedWallLightClass(const UClass* Class, bool bAuthoredLights, bool& bNativeTorch,
		FString& Error, const FString& NativeClassPath = NativeTorchPath)
	{
		bNativeTorch = false;
		// Inspect resident ancestry, independently of the selected catalog/cache. No loads.
		for (const UClass* Ancestor = Class; Ancestor; Ancestor = Ancestor->GetSuperClass())
			if (Ancestor->GetPathName() == NativeClassPath) { bNativeTorch = true; break; }
		if (bNativeTorch && !bAuthoredLights)
		{ Error = TEXT("An explicitly empty wall-light catalog retains an unexpected managed native torch."); return false; }
		return true;
	}
	static const TCHAR* Zones[] = { TEXT("Floor"), TEXT("Wall Bottom"), TEXT("Wall Middle"), TEXT("Wall Top"),
		TEXT("Corner Bottom"), TEXT("Corner Middle"), TEXT("Corner Top"), TEXT("Roof") };

	static UPCGNode* Node(UPCGGraph* Graph, const FName Name, FString& Error)
	{
		UPCGNode* Found = nullptr;
		if (Graph) for (UPCGNode* N : Graph->GetNodes()) if (N && N->GetFName() == Name)
		{
			if (Found) { Error = FString::Printf(TEXT("Native graph duplicates %s."), *Name.ToString()); return nullptr; }
			Found = N;
		}
		if (!Found) Error = FString::Printf(TEXT("Native graph is missing %s."), *Name.ToString());
		return Found;
	}

	static bool Edge(const UPCGNode* Producer, FName Out, const UPCGNode* Consumer, FName In)
	{
		const UPCGPin* A = Producer ? Producer->GetOutputPin(Out) : nullptr;
		const UPCGPin* B = Consumer ? Consumer->GetInputPin(In) : nullptr;
		if (!A || !B) return false;
		for (const UPCGEdge* E : A->Edges) if (E && E->IsValid() && E->InputPin == A && E->OutputPin == B) return true;
		return false;
	}
	static bool EdgeToNode(const UPCGNode* Producer, FName Out, const UPCGNode* Consumer)
	{
		const UPCGPin* P = Producer ? Producer->GetOutputPin(Out) : nullptr;
		if (P && Consumer) for (const UPCGEdge* E : P->Edges)
			if (E && E->IsValid() && E->OutputPin && E->OutputPin->Node == Consumer) return true;
		return false;
	}

	static UPCGGraph* CloneGraph(UPCGGraph* Source, UObject* Outer, FString& Error)
	{
		UPCGGraph* Clone = Source ? DuplicateObject<UPCGGraph>(Source, Outer,
			MakeUniqueObjectName(Outer, Source->GetClass(), *FString::Printf(TEXT("EFCalystoNative_%s"), *Source->GetName()))) : nullptr;
		if (!Clone) { Error = TEXT("Could not duplicate the exact native graph."); return nullptr; }
		Clone->ClearFlags(RF_Public | RF_Standalone); Clone->SetFlags(RF_Transient);
		FObjectProperty* Cache = FindFProperty<FObjectProperty>(UPCGGraph::StaticClass(), TEXT("CookedCompilationData"));
		if (!Cache || !Cache->PropertyClass || Cache->PropertyClass->GetFName() != TEXT("PCGGraphCompilationData"))
		{ Error = TEXT("UE 5.8 cooked graph cache capability changed."); return nullptr; }
		Cache->SetObjectPropertyValue_InContainer(Clone, nullptr);
		return Clone;
	}

	static bool AddOutput(UPCGGraph* Graph, UPCGNode* Producer, FName From, FName To, FString& Error)
	{
		UPCGNode* Output = Graph ? Graph->GetOutputNode() : nullptr;
		auto* Settings = Output ? Cast<UPCGGraphInputOutputSettings>(Output->GetSettings()) : nullptr;
		if (!Settings || !Producer || !Producer->GetOutputPin(From) || Output->GetInputPin(To))
		{ Error = TEXT("Native graph output contract is missing or already occupied."); return false; }
		const FPCGPinProperties& AddedPin = Settings->AddPin(FPCGPinProperties(To, EPCGDataType::Point));
		if (AddedPin.Label != To) { Error = TEXT("Native graph output pin was renamed unexpectedly."); return false; }
		Output->UpdateAfterSettingsChangeDuringCreation();
		Graph->AddEdge(Producer, From, Output, To);
		if (!Edge(Producer, From, Output, To)) { Error = TEXT("Could not route a native graph output."); return false; }
		return true;
	}

	static FString DescribeWallRoomIdentityStream(const FPCGDataCollection& Output, const FName Pin)
	{
		int32 TaggedCount = 0, PointDataCount = 0, PointCount = 0;
		int32 MissingOwner = 0, WrongOwnerType = 0, ValidOwner = 0, InvalidOwner = 0;
		int32 SoftMaterial = 0, StringMaterial = 0, MissingMaterial = 0, WrongMaterialType = 0, NullMaterial = 0;
		for (const FPCGTaggedData& Tagged : Output.GetInputsByPin(Pin))
		{
			++TaggedCount;
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Points || Points->GetNumPoints() < 0 || Points->GetNumPoints() > 65536 - PointCount)
			{
				++WrongOwnerType;
				continue;
			}
			++PointDataCount;
			PointCount += Points->GetNumPoints();
			const UPCGMetadata* Metadata = Points->ConstMetadata();
			const FPCGMetadataAttribute<int64>* Ids = Metadata ? Metadata->GetConstTypedAttribute<int64>(TEXT("EF_RoomId")) : nullptr;
			const FPCGMetadataAttribute<FSoftObjectPath>* SoftMaterials = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial")) : nullptr;
			const FPCGMetadataAttribute<FString>* StringMaterials = Metadata ? Metadata->GetConstTypedAttribute<FString>(TEXT("WallMaterial")) : nullptr;
			const bool bHasOwner = Metadata && Metadata->HasAttribute(TEXT("EF_RoomId"));
			const bool bHasMaterial = Metadata && Metadata->HasAttribute(TEXT("WallMaterial"));
			if (!Ids) (bHasOwner ? WrongOwnerType : MissingOwner) += Points->GetNumPoints();
			const FConstPCGPointValueRanges Ranges(Points);
			for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
			{
				const FPCGPoint Point = Ranges.GetPoint(Index);
				if (Ids)
				{
					if (Ids->GetValueFromItemKey(Point.MetadataEntry) > 0) ++ValidOwner;
					else ++InvalidOwner;
				}
				if (SoftMaterials)
				{
					const FSoftObjectPath Material = SoftMaterials->GetValueFromItemKey(Point.MetadataEntry);
					if (Material.IsNull() || !Material.IsValid()) ++NullMaterial;
					else ++SoftMaterial;
				}
				else if (StringMaterials)
				{
					const FSoftObjectPath Material(StringMaterials->GetValueFromItemKey(Point.MetadataEntry));
					if (Material.IsNull() || !Material.IsValid()) ++NullMaterial;
					else ++StringMaterial;
				}
				else if (bHasMaterial) ++WrongMaterialType;
				else ++MissingMaterial;
			}
		}
		return FString::Printf(TEXT("%s={tagged=%d data=%d points=%d owner_valid=%d owner_invalid=%d owner_missing=%d owner_wrong=%d material_soft=%d material_string=%d material_null=%d material_missing=%d material_wrong=%d}"),
			*Pin.ToString(), TaggedCount, PointDataCount, PointCount, ValidOwner, InvalidOwner, MissingOwner, WrongOwnerType,
			SoftMaterial, StringMaterial, NullMaterial, MissingMaterial, WrongMaterialType);
	}

	static FString DescribeWallRoomIdentityStreams(const FPCGDataCollection& Output)
	{
		return FString::Printf(TEXT("%s %s %s %s %s %s %s %s %s %s %s %s %s"),
			*DescribeWallRoomIdentityStream(Output, WallAllRoomsInputPin),
			*DescribeWallRoomIdentityStream(Output, WallThemeOutputPin),
			*DescribeWallRoomIdentityStream(Output, WallDoorThemeOutputPin),
			*DescribeWallRoomIdentityStream(Output, WallTextureOutputPin),
			*DescribeWallRoomIdentityStream(Output, WallDoorTextureOutputPin),
			*DescribeWallRoomIdentityStream(Output, WallConstructionInputPin),
			*DescribeWallRoomIdentityStream(Output, WallDoorConstructionInputPin),
			*DescribeWallRoomIdentityStream(Output, WallNormalInputPin),
			*DescribeWallRoomIdentityStream(Output, WallUpperInputPin),
			*DescribeWallRoomIdentityStream(Output, WallMatchedPin),
			*DescribeWallRoomIdentityStream(Output, WallPreLoopPin),
			*DescribeWallRoomIdentityStream(Output, WallFinalPin),
			*DescribeWallRoomIdentityStream(Output, WallDoorFinalPin));
	}

	/**
	 * PCG_SetRoomTheme intersects each structural piece stream with exactly one
	 * RoomOverride. UE's native metadata merge is intentionally generic and
	 * can preserve a source wall ID from before the intersection. Patch only the
	 * retained transient child loop: its active RoomOverride and post-intersection
	 * point stream are the same loop iteration, so replacing that source ID needs
	 * no proximity or array-order inference.
	 */
	static bool PatchExactRoomOwnerInsideThemeLoop(UPCGGraph* Master, UPCGNode* ThemeMaterials, FString& Error)
	{
		static constexpr TCHAR ThemePath[] = TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetRoomTheme.PCG_SetRoomTheme");
		static constexpr TCHAR ExtractPath[] = TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_ExtractRoomThemeMaterial.PCG_ExtractRoomThemeMaterial");
		auto* ThemeSettings = ThemeMaterials ? Cast<UPCGSubgraphSettings>(ThemeMaterials->GetSettings()) : nullptr;
		UPCGGraph* ThemeSource = ThemeSettings ? ThemeSettings->GetSubgraph() : nullptr;
		if (!ThemeSettings || !ThemeSource || ThemeSource->GetPathName() != ThemePath)
		{
			Error = TEXT("Native PCG_SetRoomTheme capability changed.");
			return false;
		}
		UPCGGraph* ThemeClone = CloneGraph(ThemeSource, Master, Error);
		if (!ThemeClone) return false;
		UPCGNode* Loop = Node(ThemeClone, TEXT("Loop_2"), Error);
		UPCGNode* RoomOverridePartition = Node(ThemeClone, TEXT("AttributePartition_68"), Error);
		auto* LoopSettings = Loop ? Cast<UPCGLoopSettings>(Loop->GetSettings()) : nullptr;
		auto* RoomOverridePartitionSettings = RoomOverridePartition
			? Cast<UPCGMetadataPartitionSettings>(RoomOverridePartition->GetSettings()) : nullptr;
		UPCGGraph* ExtractSource = LoopSettings ? LoopSettings->GetSubgraph() : nullptr;
		if (!LoopSettings || !RoomOverridePartitionSettings || !ExtractSource || ExtractSource->GetPathName() != ExtractPath
			|| !Edge(RoomOverridePartition, PCGPinConstants::DefaultOutputLabel, Loop, TEXT("RoomOverride")))
		{
			Error = TEXT("Native PCG_SetRoomTheme Loop_2 no longer receives RoomOverride partitions for PCG_ExtractRoomThemeMaterial.");
			return false;
		}
		// The vendor graph leaves this selector at @Last.  That groups arbitrary
		// tagged data and occasionally gives the exact loop bridge zero or multiple
		// owner points. EF_RoomId is authored once per room context, so partitioning
		// explicitly by it preserves the native loop while making owner cardinality
		// deterministic without an array-order or spatial lookup.
		RoomOverridePartitionSettings->PartitionAttributeSelectors.SetNum(1);
		RoomOverridePartitionSettings->PartitionAttributeSelectors[0].SetAttributeName(TEXT("EF_RoomId"));
		RoomOverridePartitionSettings->PartitionAttributeNames.Reset();
		RoomOverridePartitionSettings->bAssignIndexPartition = false;
		RoomOverridePartitionSettings->bDoNotPartition = true;
		RoomOverridePartition->UpdateAfterSettingsChangeDuringCreation();
		UPCGGraph* ExtractClone = CloneGraph(ExtractSource, ThemeClone, Error);
		if (!ExtractClone) return false;
		UPCGNode* RoomOverride = Node(ExtractClone, TEXT("NamedRerouteUsage_16"), Error);
		UPCGNode* MaterialReader = Node(ExtractClone, TEXT("GetAttributeFromPointIndex_6"), Error);
		UPCGNode* ToPoint = Node(ExtractClone, TEXT("ToPoint_4"), Error);
		UPCGNode* Branch = Node(ExtractClone, TEXT("Branch_3"), Error);
		UPCGNode* NormalWall = Node(ExtractClone, TEXT("AddAttribute_2"), Error);
		UPCGNode* OverrideWall = Node(ExtractClone, TEXT("AddTags_2"), Error);
		UPCGNode* Output = ExtractClone->GetOutputNode();
		const auto* MaterialSettings = MaterialReader ? Cast<UPCGAttributeGetFromPointIndexSettings>(MaterialReader->GetSettings()) : nullptr;
		if (!RoomOverride || !MaterialSettings || !ToPoint || !Branch || !NormalWall || !OverrideWall || !Output
			|| MaterialSettings->InputSource.GetName() != TEXT("WallMaterial")
			|| MaterialSettings->OutputAttributeName.GetName() != TEXT("WallMaterial")
			|| MaterialSettings->Index != 0 || !MaterialSettings->bForceOutputAttributeToBeInElementsDomain
			|| !Edge(RoomOverride, TEXT("Out"), MaterialReader, PCGPinConstants::DefaultInputLabel)
			|| !Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, Branch, PCGPinConstants::DefaultInputLabel)
			|| !Edge(NormalWall, PCGPinConstants::DefaultOutputLabel, Output, TEXT("Wall"))
			|| !Edge(OverrideWall, PCGPinConstants::DefaultOutputLabel, Output, TEXT("Wall")))
		{
			Error = TEXT("Native RoomOverride-to-final-Wall ownership route changed.");
			return false;
		}
		UPCGPin* BranchInput = Branch->GetInputPin(PCGPinConstants::DefaultInputLabel);
		UPCGPin* ToPointOutput = ToPoint->GetOutputPin(PCGPinConstants::DefaultOutputLabel);
		if (!BranchInput || !ToPointOutput || BranchInput->Edges.Num() != 1 || !BranchInput->BreakEdgeTo(ToPointOutput))
		{
			Error = TEXT("Could not replace the exact post-intersection RoomOverride owner edge.");
			return false;
		}
		auto* OwnerSettings = NewObject<UPCGCalystoRoomOverrideOwnerSettings>(ExtractClone,
			MakeUniqueObjectName(ExtractClone, UPCGCalystoRoomOverrideOwnerSettings::StaticClass(), TEXT("EFCalystoRoomOverrideOwner")), RF_Transient);
		UPCGNode* Owner = OwnerSettings ? ExtractClone->AddNode(OwnerSettings) : nullptr;
		if (!Owner || !Owner->GetInputPin(FEFCalystoRoomOverrideOwnerPins::RoomOverride)
			|| !Owner->GetInputPin(FEFCalystoRoomOverrideOwnerPins::Pieces)
			|| !Owner->GetOutputPin(PCGPinConstants::DefaultOutputLabel))
		{
			Error = TEXT("Could not create the transient RoomOverride owner bridge.");
			return false;
		}
		ExtractClone->AddEdge(RoomOverride, TEXT("Out"), Owner, FEFCalystoRoomOverrideOwnerPins::RoomOverride);
		ExtractClone->AddEdge(ToPoint, PCGPinConstants::DefaultOutputLabel, Owner, FEFCalystoRoomOverrideOwnerPins::Pieces);
		ExtractClone->AddEdge(Owner, PCGPinConstants::DefaultOutputLabel, Branch, PCGPinConstants::DefaultInputLabel);
		if (!Edge(RoomOverride, TEXT("Out"), Owner, FEFCalystoRoomOverrideOwnerPins::RoomOverride)
			|| !Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, Owner, FEFCalystoRoomOverrideOwnerPins::Pieces)
			|| !Edge(Owner, PCGPinConstants::DefaultOutputLabel, Branch, PCGPinConstants::DefaultInputLabel))
		{
			Error = TEXT("The transient RoomOverride owner bridge is incomplete.");
			return false;
		}
		LoopSettings->SetSubgraph(ExtractClone);
		Loop->UpdateAfterSettingsChangeDuringCreation();
		if (LoopSettings->GetSubgraph() != ExtractClone || !ExtractClone->HasAnyFlags(RF_Transient))
		{
			Error = TEXT("The transient RoomOverride child graph was not retained by Loop_2.");
			return false;
		}
		ThemeSettings->SetSubgraph(ThemeClone);
		ThemeMaterials->UpdateAfterSettingsChangeDuringCreation();
		if (ThemeSettings->GetSubgraph() != ThemeClone || !ThemeClone->HasAnyFlags(RF_Transient))
		{
			Error = TEXT("The transient PCG_SetRoomTheme clone was not retained by the Master graph.");
			return false;
		}
		return true;
	}

	static bool ValidateExactRoomOwnerInsideThemeLoop(const UPCGNode* ThemeMaterials, FString& Error)
	{
		const auto* ThemeSettings = ThemeMaterials ? Cast<UPCGSubgraphSettings>(ThemeMaterials->GetSettings()) : nullptr;
		UPCGGraph* ThemeGraph = ThemeSettings ? ThemeSettings->GetSubgraph() : nullptr;
		if (!ThemeGraph || !ThemeGraph->HasAnyFlags(RF_Transient) || ThemeGraph->GetOuter() != ThemeMaterials->GetOuter())
		{
			Error = TEXT("PCG_SetRoomTheme must be retained as a transient Master child.");
			return false;
		}
		UPCGNode* Loop = Node(ThemeGraph, TEXT("Loop_2"), Error);
		UPCGNode* RoomOverridePartition = Node(ThemeGraph, TEXT("AttributePartition_68"), Error);
		const auto* LoopSettings = Loop ? Cast<UPCGLoopSettings>(Loop->GetSettings()) : nullptr;
		const auto* RoomOverridePartitionSettings = RoomOverridePartition
			? Cast<UPCGMetadataPartitionSettings>(RoomOverridePartition->GetSettings()) : nullptr;
		UPCGGraph* ExtractGraph = LoopSettings ? LoopSettings->GetSubgraph() : nullptr;
		if (!LoopSettings || !ExtractGraph || !ExtractGraph->HasAnyFlags(RF_Transient)
			|| ExtractGraph->GetOuter() != ThemeGraph)
		{
			Error = TEXT("PCG_ExtractRoomThemeMaterial must be retained as a transient Loop_2 child.");
			return false;
		}
		UPCGNode* RoomOverride = Node(ExtractGraph, TEXT("NamedRerouteUsage_16"), Error);
		UPCGNode* MaterialReader = Node(ExtractGraph, TEXT("GetAttributeFromPointIndex_6"), Error);
		UPCGNode* ToPoint = Node(ExtractGraph, TEXT("ToPoint_4"), Error);
		UPCGNode* Branch = Node(ExtractGraph, TEXT("Branch_3"), Error);
		UPCGNode* NormalWall = Node(ExtractGraph, TEXT("AddAttribute_2"), Error);
		UPCGNode* OverrideWall = Node(ExtractGraph, TEXT("AddTags_2"), Error);
		UPCGNode* Output = ExtractGraph ? ExtractGraph->GetOutputNode() : nullptr;
		const auto* MaterialSettings = MaterialReader ? Cast<UPCGAttributeGetFromPointIndexSettings>(MaterialReader->GetSettings()) : nullptr;
		UPCGNode* Owner = nullptr;
		for (UPCGNode* Candidate : ExtractGraph->GetNodes())
		{
			if (Candidate && Cast<UPCGCalystoRoomOverrideOwnerSettings>(Candidate->GetSettings()))
			{
				if (Owner) { Error = TEXT("PCG_ExtractRoomThemeMaterial contains duplicate RoomOverride owner bridges."); return false; }
				Owner = Candidate;
			}
		}
		if (!RoomOverride || !MaterialSettings || !ToPoint || !Branch || !Owner || !NormalWall || !OverrideWall || !Output
			|| !RoomOverridePartitionSettings || RoomOverridePartitionSettings->PartitionAttributeSelectors.Num() != 1
			|| !RoomOverridePartitionSettings->PartitionAttributeSelectors[0].IsBasicAttribute()
			|| RoomOverridePartitionSettings->PartitionAttributeSelectors[0].GetAttributeName() != TEXT("EF_RoomId")
			|| !RoomOverridePartitionSettings->PartitionAttributeNames.IsEmpty()
			|| RoomOverridePartitionSettings->bAssignIndexPartition
			|| !RoomOverridePartitionSettings->bDoNotPartition
			|| !Edge(RoomOverridePartition, PCGPinConstants::DefaultOutputLabel, Loop, TEXT("RoomOverride"))
			|| MaterialSettings->InputSource.GetName() != TEXT("WallMaterial")
			|| MaterialSettings->OutputAttributeName.GetName() != TEXT("WallMaterial")
			|| MaterialSettings->Index != 0 || !MaterialSettings->bForceOutputAttributeToBeInElementsDomain
			|| !Edge(RoomOverride, TEXT("Out"), MaterialReader, PCGPinConstants::DefaultInputLabel)
			|| !Edge(RoomOverride, TEXT("Out"), Owner, FEFCalystoRoomOverrideOwnerPins::RoomOverride)
			|| !Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, Owner, FEFCalystoRoomOverrideOwnerPins::Pieces)
			|| !Edge(Owner, PCGPinConstants::DefaultOutputLabel, Branch, PCGPinConstants::DefaultInputLabel)
			|| Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, Branch, PCGPinConstants::DefaultInputLabel)
			|| !Edge(NormalWall, PCGPinConstants::DefaultOutputLabel, Output, TEXT("Wall"))
			|| !Edge(OverrideWall, PCGPinConstants::DefaultOutputLabel, Output, TEXT("Wall"))
			|| !Owner->GetInputPin(FEFCalystoRoomOverrideOwnerPins::RoomOverride)
			|| !Owner->GetInputPin(FEFCalystoRoomOverrideOwnerPins::Pieces)
			|| Owner->GetInputPin(FEFCalystoRoomOverrideOwnerPins::RoomOverride)->Edges.Num() != 1
			|| Owner->GetInputPin(FEFCalystoRoomOverrideOwnerPins::Pieces)->Edges.Num() != 1
			|| Branch->GetInputPin(PCGPinConstants::DefaultInputLabel)->Edges.Num() != 1)
		{
			Error = TEXT("PCG_ExtractRoomThemeMaterial no longer carries exact RoomOverride ownership to both Wall branches.");
			return false;
		}
		return true;
	}

	/**
	 * The native Difference branch is not a RoomOverride loop: it is exactly the
	 * raw Wall/Wall-Door geometry left after themed rooms are removed.  It has no
	 * Theme RoomOverride. It may retain a canonical unthemed EF_RoomId from the
	 * all-rooms input; preserve it exactly when present and stamp the frozen
	 * selected Style material. A zero ID is explicitly marked Style-owned. This
	 * is not a spatial owner lookup or a vendor-material fallback.
	 */
	static bool PatchExactStyleWallFallback(UPCGNode* ThemeMaterials, const FSoftObjectPath& StyleWallMaterial, FString& Error)
	{
		auto* ThemeSettings = ThemeMaterials ? Cast<UPCGSubgraphSettings>(ThemeMaterials->GetSettings()) : nullptr;
		UPCGGraph* ThemeGraph = ThemeSettings ? ThemeSettings->GetSubgraph() : nullptr;
		if (!ThemeSettings || !ThemeGraph || !ThemeGraph->HasAnyFlags(RF_Transient) || !StyleWallMaterial.IsValid())
		{
			Error = TEXT("The transient PCG_SetRoomTheme graph lacks a selected Style wall material.");
			return false;
		}
		UPCGNode* Difference = Node(ThemeGraph, TEXT("Difference_14"), Error);
		UPCGNode* ToPoint = Node(ThemeGraph, TEXT("ToPoint_18"), Error);
		UPCGNode* AddAttribute = Node(ThemeGraph, TEXT("AddAttribute_4"), Error);
		if (!Difference || !ToPoint || !AddAttribute
			|| !Edge(Difference, PCGPinConstants::DefaultOutputLabel, ToPoint, PCGPinConstants::DefaultInputLabel)
			|| !Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, AddAttribute, PCGPinConstants::DefaultInputLabel))
		{
			Error = TEXT("Native PCG_SetRoomTheme default-wall Difference route changed.");
			return false;
		}
		UPCGPin* FallbackInput = AddAttribute->GetInputPin(PCGPinConstants::DefaultInputLabel);
		UPCGPin* ToPointOutput = ToPoint->GetOutputPin(PCGPinConstants::DefaultOutputLabel);
		if (!FallbackInput || !ToPointOutput || FallbackInput->Edges.Num() != 1 || !FallbackInput->BreakEdgeTo(ToPointOutput))
		{
			Error = TEXT("Could not replace the exact default-wall Difference edge.");
			return false;
		}
		auto* FallbackSettings = NewObject<UPCGCalystoStyleWallFallbackSettings>(ThemeGraph,
			MakeUniqueObjectName(ThemeGraph, UPCGCalystoStyleWallFallbackSettings::StaticClass(), TEXT("EFCalystoStyleWallFallback")), RF_Transient);
		if (!FallbackSettings)
		{
			Error = TEXT("Could not create the transient Style wall fallback bridge.");
			return false;
		}
		FallbackSettings->StyleWallMaterial = StyleWallMaterial;
		UPCGNode* Fallback = ThemeGraph->AddNode(FallbackSettings);
		if (!Fallback || !Fallback->GetInputPin(PCGPinConstants::DefaultInputLabel)
			|| !Fallback->GetOutputPin(PCGPinConstants::DefaultOutputLabel))
		{
			Error = TEXT("The transient Style wall fallback bridge has an invalid pin contract.");
			return false;
		}
		ThemeGraph->AddEdge(ToPoint, PCGPinConstants::DefaultOutputLabel, Fallback, PCGPinConstants::DefaultInputLabel);
		ThemeGraph->AddEdge(Fallback, PCGPinConstants::DefaultOutputLabel, AddAttribute, PCGPinConstants::DefaultInputLabel);
		if (!Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, Fallback, PCGPinConstants::DefaultInputLabel)
			|| !Edge(Fallback, PCGPinConstants::DefaultOutputLabel, AddAttribute, PCGPinConstants::DefaultInputLabel)
			|| Fallback->GetInputPin(PCGPinConstants::DefaultInputLabel)->Edges.Num() != 1
			|| AddAttribute->GetInputPin(PCGPinConstants::DefaultInputLabel)->Edges.Num() != 1)
		{
			Error = TEXT("The transient Style wall fallback bridge is incomplete.");
			return false;
		}
		ThemeMaterials->UpdateAfterSettingsChangeDuringCreation();
		return true;
	}

	static bool ValidateExactStyleWallFallback(const UPCGNode* ThemeMaterials, FString& Error)
	{
		const auto* ThemeSettings = ThemeMaterials ? Cast<UPCGSubgraphSettings>(ThemeMaterials->GetSettings()) : nullptr;
		UPCGGraph* ThemeGraph = ThemeSettings ? ThemeSettings->GetSubgraph() : nullptr;
		if (!ThemeGraph || !ThemeGraph->HasAnyFlags(RF_Transient))
		{
			Error = TEXT("PCG_SetRoomTheme must retain the transient Style wall fallback graph.");
			return false;
		}
		UPCGNode* Difference = Node(ThemeGraph, TEXT("Difference_14"), Error);
		UPCGNode* ToPoint = Node(ThemeGraph, TEXT("ToPoint_18"), Error);
		UPCGNode* AddAttribute = Node(ThemeGraph, TEXT("AddAttribute_4"), Error);
		UPCGNode* Fallback = nullptr;
		for (UPCGNode* Candidate : ThemeGraph->GetNodes())
		{
			if (Candidate && Cast<UPCGCalystoStyleWallFallbackSettings>(Candidate->GetSettings()))
			{
				if (Fallback)
				{
					Error = TEXT("PCG_SetRoomTheme contains duplicate Style wall fallback bridges.");
					return false;
				}
				Fallback = Candidate;
			}
		}
		const auto* Settings = Fallback ? Cast<UPCGCalystoStyleWallFallbackSettings>(Fallback->GetSettings()) : nullptr;
		if (!Difference || !ToPoint || !AddAttribute || !Fallback || !Settings || !Settings->StyleWallMaterial.IsValid()
			|| !Edge(Difference, PCGPinConstants::DefaultOutputLabel, ToPoint, PCGPinConstants::DefaultInputLabel)
			|| !Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, Fallback, PCGPinConstants::DefaultInputLabel)
			|| !Edge(Fallback, PCGPinConstants::DefaultOutputLabel, AddAttribute, PCGPinConstants::DefaultInputLabel)
			|| Edge(ToPoint, PCGPinConstants::DefaultOutputLabel, AddAttribute, PCGPinConstants::DefaultInputLabel)
			|| Fallback->GetInputPin(PCGPinConstants::DefaultInputLabel)->Edges.Num() != 1
			|| AddAttribute->GetInputPin(PCGPinConstants::DefaultInputLabel)->Edges.Num() != 1)
		{
			Error = TEXT("PCG_SetRoomTheme no longer carries exact Style authority through its default-wall Difference branch.");
			return false;
		}
		return true;
	}

	/**
	 * PCG_SetDungeonMesh intentionally removes WallMaterial through its final
	 * DeleteAttributes chain. Carry a typed copy per exact Wall - Door point
	 * before that vendor subgraph, then restore it only when the final native
	 * point retains the carrier. This does not alter structural selection,
	 * transforms, cardinality, or any vendor asset.
	 */
	static bool PatchExactFinalDoorMaterialProvenance(UPCGGraph* Master, UPCGNode* TextureOverride,
		UPCGNode* Construction, FString& Error)
	{
		UPCGNode* Output = Master ? Master->GetOutputNode() : nullptr;
		UPCGPin* FinalOutputInput = Output ? Output->GetInputPin(WallDoorFinalPin) : nullptr;
		UPCGPin* NativeFinalOutput = Construction ? Construction->GetOutputPin(TEXT("All Doors")) : nullptr;
		UPCGPin* TextureDoorOutput = TextureOverride ? TextureOverride->GetOutputPin(TEXT("Wall - Door")) : nullptr;
		UPCGPin* ConstructionDoorInput = Construction ? Construction->GetInputPin(TEXT("Wall - Door")) : nullptr;
		if (!Master || !TextureOverride || !Construction || !Output || !FinalOutputInput || !NativeFinalOutput
			|| !TextureDoorOutput || !ConstructionDoorInput
			|| !Edge(TextureOverride, TEXT("Wall - Door"), Construction, TEXT("Wall - Door"))
			|| !Edge(Construction, TEXT("All Doors"), Output, WallDoorFinalPin)
			|| ConstructionDoorInput->Edges.Num() != 1 || FinalOutputInput->Edges.Num() != 1)
		{
			Error = TEXT("Native final Wall - Door provenance route changed before the transient bridge could be installed.");
			return false;
		}
		if (!ConstructionDoorInput->BreakEdgeTo(TextureDoorOutput)
			|| !FinalOutputInput->BreakEdgeTo(NativeFinalOutput))
		{
			Error = TEXT("Could not replace the exact native Wall - Door or All Doors provenance edge.");
			return false;
		}
		auto* CarrierSettings = NewObject<UPCGCalystoDoorMaterialCarrierSettings>(Master,
			MakeUniqueObjectName(Master, UPCGCalystoDoorMaterialCarrierSettings::StaticClass(), TEXT("EFCalystoDoorMaterialCarrier")), RF_Transient);
		UPCGNode* Carrier = CarrierSettings ? Master->AddNode(CarrierSettings) : nullptr;
		if (!Carrier || !Carrier->GetInputPin(PCGPinConstants::DefaultInputLabel)
			|| !Carrier->GetOutputPin(PCGPinConstants::DefaultOutputLabel))
		{
			Error = TEXT("The transient Wall - Door material carrier has an invalid pin contract.");
			return false;
		}
		auto* Settings = NewObject<UPCGCalystoDoorMaterialProvenanceSettings>(Master,
			MakeUniqueObjectName(Master, UPCGCalystoDoorMaterialProvenanceSettings::StaticClass(), TEXT("EFCalystoDoorMaterialProvenance")), RF_Transient);
		UPCGNode* Provenance = Settings ? Master->AddNode(Settings) : nullptr;
		if (!Provenance || !Provenance->GetInputPin(FEFCalystoDoorMaterialProvenancePins::DoorSource)
			|| !Provenance->GetInputPin(FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces)
			|| !Provenance->GetOutputPin(PCGPinConstants::DefaultOutputLabel))
		{
			Error = TEXT("The transient final Wall - Door provenance bridge has an invalid pin contract.");
			return false;
		}
		Master->AddEdge(TextureOverride, TEXT("Wall - Door"), Carrier, PCGPinConstants::DefaultInputLabel);
		Master->AddEdge(Carrier, PCGPinConstants::DefaultOutputLabel, Construction, TEXT("Wall - Door"));
		Master->AddEdge(Carrier, PCGPinConstants::DefaultOutputLabel, Provenance, FEFCalystoDoorMaterialProvenancePins::DoorSource);
		Master->AddEdge(Construction, TEXT("All Doors"), Provenance, FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces);
		Master->AddEdge(Provenance, PCGPinConstants::DefaultOutputLabel, Output, WallDoorFinalPin);
		if (!Edge(TextureOverride, TEXT("Wall - Door"), Carrier, PCGPinConstants::DefaultInputLabel)
			|| !Edge(Carrier, PCGPinConstants::DefaultOutputLabel, Construction, TEXT("Wall - Door"))
			|| !Edge(Carrier, PCGPinConstants::DefaultOutputLabel, Provenance, FEFCalystoDoorMaterialProvenancePins::DoorSource)
			|| !Edge(Construction, TEXT("All Doors"), Provenance, FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces)
			|| !Edge(Provenance, PCGPinConstants::DefaultOutputLabel, Output, WallDoorFinalPin)
			|| Carrier->GetInputPin(PCGPinConstants::DefaultInputLabel)->Edges.Num() != 1
			|| ConstructionDoorInput->Edges.Num() != 1
			|| Provenance->GetInputPin(FEFCalystoDoorMaterialProvenancePins::DoorSource)->Edges.Num() != 1
			|| Provenance->GetInputPin(FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces)->Edges.Num() != 1
			|| FinalOutputInput->Edges.Num() != 1)
		{
			Error = TEXT("The transient final Wall - Door provenance bridge is incomplete.");
			return false;
		}
		return true;
	}

	static bool ValidateExactFinalDoorMaterialProvenance(const UPCGGraph* Master, const UPCGNode* TextureOverride,
		const UPCGNode* Construction, FString& Error)
	{
		const UPCGNode* Output = Master ? Master->GetOutputNode() : nullptr;
		const UPCGNode* Carrier = nullptr;
		const UPCGNode* Provenance = nullptr;
		if (Master) for (const UPCGNode* Candidate : Master->GetNodes())
		{
			if (Candidate && Cast<UPCGCalystoDoorMaterialCarrierSettings>(Candidate->GetSettings()))
			{
				if (Carrier)
				{
					Error = TEXT("Director graph contains duplicate Wall - Door material carriers.");
					return false;
				}
				Carrier = Candidate;
			}
			if (Candidate && Cast<UPCGCalystoDoorMaterialProvenanceSettings>(Candidate->GetSettings()))
			{
				if (Provenance)
				{
					Error = TEXT("Director graph contains duplicate final Wall - Door provenance bridges.");
					return false;
				}
				Provenance = Candidate;
			}
		}
		const UPCGPin* FinalOutputInput = Output ? Output->GetInputPin(WallDoorFinalPin) : nullptr;
		const UPCGPin* CarrierInput = Carrier ? Carrier->GetInputPin(PCGPinConstants::DefaultInputLabel) : nullptr;
		const UPCGPin* ConstructionDoorInput = Construction ? Construction->GetInputPin(TEXT("Wall - Door")) : nullptr;
		const UPCGPin* DoorSourceInput = Provenance ? Provenance->GetInputPin(FEFCalystoDoorMaterialProvenancePins::DoorSource) : nullptr;
		const UPCGPin* FinalDoorPiecesInput = Provenance ? Provenance->GetInputPin(FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces) : nullptr;
		if (!TextureOverride || !Construction || !Output || !Carrier || !Provenance || !FinalOutputInput
			|| !CarrierInput || !ConstructionDoorInput || !DoorSourceInput || !FinalDoorPiecesInput
			|| !Edge(TextureOverride, TEXT("Wall - Door"), Carrier, PCGPinConstants::DefaultInputLabel)
			|| !Edge(Carrier, PCGPinConstants::DefaultOutputLabel, Construction, TEXT("Wall - Door"))
			|| !Edge(Carrier, PCGPinConstants::DefaultOutputLabel, Provenance, FEFCalystoDoorMaterialProvenancePins::DoorSource)
			|| !Edge(Construction, TEXT("All Doors"), Provenance, FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces)
			|| !Edge(Provenance, PCGPinConstants::DefaultOutputLabel, Output, WallDoorFinalPin)
			|| Edge(TextureOverride, TEXT("Wall - Door"), Construction, TEXT("Wall - Door"))
			|| Edge(TextureOverride, TEXT("Wall - Door"), Provenance, FEFCalystoDoorMaterialProvenancePins::DoorSource)
			|| Edge(Construction, TEXT("All Doors"), Output, WallDoorFinalPin)
			|| CarrierInput->Edges.Num() != 1
			|| ConstructionDoorInput->Edges.Num() != 1
			|| DoorSourceInput->Edges.Num() != 1
			|| FinalDoorPiecesInput->Edges.Num() != 1
			|| FinalOutputInput->Edges.Num() != 1)
		{
			Error = TEXT("Director graph no longer preserves the exact final Wall - Door material provenance route.");
			return false;
		}
		return true;
	}

	static UPCGGraph* BuildGraph(UPCGGraph* Source, UObject* Owner, const FEFCalystoNativeRoomConfig& Config, FString& Error)
	{
		if (!Source || Source->GetPathName() != MasterPath)
		{ Error = TEXT("Director requires the exact native Calysto Master graph as its source."); return nullptr; }
		const auto Cooked = FEFCalystoPCGCookedCompatibility::TryBuild(Source, Owner);
		if (!Cooked.bApplied) { Error = Cooked.FailureReason; return nullptr; }
		UPCGGraph* Master = Cooked.RuntimeGraph;
		if (!Master || !Master->HasAnyFlags(RF_Transient)) Master = CloneGraph(Source, Owner, Error);
		if (!Master) return nullptr;
		UPCGNode* ShapeCall = Node(Master, TEXT("Subgraph_0"), Error);
		auto* ShapeSettings = ShapeCall ? Cast<UPCGSubgraphSettings>(ShapeCall->GetSettings()) : nullptr;
		UPCGGraph* ShapeSource = ShapeSettings ? ShapeSettings->GetSubgraph() : nullptr;
		if (!ShapeSource || ShapeSource->GetPathName() != ShapePath)
		{ Error = TEXT("Native Master.Subgraph_0 must call the exact Calysto Shape graph."); return nullptr; }
		UPCGGraph* Shape = CloneGraph(ShapeSource, Master, Error);
		if (!Shape) return nullptr;
		UPCGNode* NativeTheme = Node(Shape, TEXT("MatchAndSetAttributes_26"), Error);
		UPCGNode* Merge = Node(Shape, TEXT("MergePoints_48"), Error);
		UPCGNode* All = Node(Shape, TEXT("NamedRerouteDeclaration_34"), Error);
		if (!NativeTheme || !Merge || !All || !Edge(NativeTheme, TEXT("Out"), Merge, TEXT("In"))
			|| !Edge(Merge, TEXT("Out"), All, TEXT("In")) || Merge->GetInputPin(TEXT("In"))->Edges.Num() != 1)
		{ Error = TEXT("Native Shape room construction merge contract changed."); return nullptr; }
		auto* RoomSettings = NewObject<UPCGCalystoDirectorRoomSettings>(Shape, NAME_None, RF_Transient);
		RoomSettings->Config = Config;
		UPCGNode* Rooms = Shape->AddNode(RoomSettings);
		const TPair<FName, FName> Inputs[] = {
			{TEXT("NamedRerouteDeclaration_2"), FEFCalystoNativeRoomPins::MainRooms},
			{TEXT("NamedRerouteDeclaration_32"), FEFCalystoNativeRoomPins::SideRooms},
			{TEXT("NamedRerouteDeclaration_38"), FEFCalystoNativeRoomPins::StartRooms},
			{TEXT("NamedRerouteDeclaration_39"), FEFCalystoNativeRoomPins::EndRooms}};
		for (const auto& Input : Inputs)
		{
			UPCGNode* From = Node(Shape, Input.Key, Error);
			if (!From || !Cast<UPCGNamedRerouteDeclarationSettings>(From->GetSettings()) || !From->GetOutputPin(TEXT("Out")))
			{ Error = TEXT("Native room declaration capability changed."); return nullptr; }
			Shape->AddEdge(From, TEXT("Out"), Rooms, Input.Value);
			if (!Edge(From, TEXT("Out"), Rooms, Input.Value)) { Error = TEXT("Cannot connect native room inputs."); return nullptr; }
		}
		if (!Merge->GetInputPin(TEXT("In"))->BreakEdgeTo(NativeTheme->GetOutputPin(TEXT("Out"))))
		{ Error = TEXT("Could not replace the exact native room assignment edge."); return nullptr; }
		Shape->AddEdge(Rooms, FEFCalystoNativeRoomPins::NativeRooms, Merge, TEXT("In"));
		Shape->RemoveNode(NativeTheme);
		if (!AddOutput(Shape, Rooms, FEFCalystoNativeRoomPins::RoomContexts, FEFCalystoNativeRoomPins::RoomContexts, Error)
			|| !AddOutput(Shape, All, TEXT("Out"), WallAllRoomsInputPin, Error)) return nullptr;
		ShapeSettings->SetSubgraph(Shape);
		ShapeCall->UpdateAfterSettingsChangeDuringCreation();
		if (!AddOutput(Master, ShapeCall, FEFCalystoNativeRoomPins::RoomContexts, FEFCalystoNativeRoomPins::RoomContexts, Error)
			|| !AddOutput(Master, ShapeCall, WallAllRoomsInputPin, WallAllRoomsInputPin, Error)) return nullptr;
		UPCGNode* Placements = Node(Master, TEXT("Subgraph_3"), Error);
		const auto* PlacementSettings = Placements ? Cast<UPCGSubgraphSettings>(Placements->GetSettings()) : nullptr;
		if (!PlacementSettings || !PlacementSettings->GetSubgraph()
			|| PlacementSettings->GetSubgraph()->GetPathName() != TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_GenerateRoomPoints.PCG_GenerateRoomPoints"))
		{ Error = TEXT("Native placement graph capability changed."); return nullptr; }
		for (const TCHAR* Zone : Zones)
			if (!AddOutput(Master, Placements, Zone, *FString::Printf(TEXT("EF Native %s"), Zone), Error)) return nullptr;
		// Retain native light opportunity selection/transforms and expose both requested points
		// and actual PCG actor references, so failed native spawning cannot disappear silently.
		UPCGNode* Decoration = Node(Master, TEXT("Subgraph_5"), Error);
		auto* DecorationSettings = Decoration ? Cast<UPCGSubgraphSettings>(Decoration->GetSettings()) : nullptr;
		UPCGGraph* DecorationSource = DecorationSettings ? DecorationSettings->GetSubgraph() : nullptr;
		if (!DecorationSource || DecorationSource->GetPathName() != TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_RoomMesh.PCG_RoomMesh")
			|| !Edge(Placements, TEXT("Wall Light"), Decoration, TEXT("Wall Light")))
		{ Error = TEXT("The exact native wall-light construction branch changed."); return nullptr; }
		UPCGGraph* DecorationClone = CloneGraph(DecorationSource, Owner, Error);
		if (!DecorationClone) return nullptr;
		UPCGNode* LightPoints = Node(DecorationClone, TEXT("ToPoint_80"), Error);
		UPCGNode* LightSpawn = Node(DecorationClone, TEXT("SpawnActor_77"), Error);
		auto* LightSpawnSettings = LightSpawn ? Cast<UPCGSpawnActorSettings>(LightSpawn->GetSettings()) : nullptr;
		if (!LightSpawnSettings || LightSpawnSettings->Option != EPCGSpawnActorOption::NoMerging
			|| !LightSpawnSettings->bSpawnByAttribute || LightSpawnSettings->SpawnAttribute != TEXT("Blueprint")
			|| !LightSpawnSettings->PostSpawnFunctionNames.IsEmpty() || !Edge(LightPoints, TEXT("Out"), LightSpawn, TEXT("In")))
		{ Error = TEXT("Native wall lights require one tracked actor per selected Blueprint point."); return nullptr; }
		LightSpawnSettings->GenerationTrigger = EPCGSpawnActorGenerationTrigger::DoNotGenerate;
		if (!AddOutput(DecorationClone, LightPoints, TEXT("Out"), WallLightRequestsPin, Error)
			|| !AddOutput(DecorationClone, LightSpawn, TEXT("Out"), WallLightActorsPin, Error)) return nullptr;
		DecorationSettings->SetSubgraph(DecorationClone); Decoration->UpdateAfterSettingsChangeDuringCreation();
		if (!AddOutput(Master, Decoration, WallLightRequestsPin, WallLightRequestsPin, Error)
			|| !AddOutput(Master, Decoration, WallLightActorsPin, WallLightActorsPin, Error)) return nullptr;
		// Add one read-only diagnostic branch after final native mesh transforms.
		// The existing construction/material outputs and spawn connections are unchanged.
		UPCGNode* Construction = Node(Master, TEXT("Subgraph_43"), Error);
		UPCGNode* ThemeMaterials = Node(Master, TEXT("Subgraph_2"), Error);
		UPCGNode* TextureOverride = Node(Master, TEXT("Subgraph_1"), Error);
		auto* ConstructionSettings = Construction ? Cast<UPCGSubgraphSettings>(Construction->GetSettings()) : nullptr;
		if (!ThemeMaterials || !TextureOverride || !ThemeMaterials->GetOutputPin(TEXT("Wall")) || !ThemeMaterials->GetOutputPin(TEXT("Wall - Door"))
			|| !TextureOverride->GetOutputPin(TEXT("Wall")) || !TextureOverride->GetOutputPin(TEXT("Wall - Door")))
		{
			if (Error.IsEmpty()) Error = TEXT("Native wall theme/texture diagnostic capability changed.");
			return nullptr;
		}
		if (!PatchExactRoomOwnerInsideThemeLoop(Master, ThemeMaterials, Error)
			|| !PatchExactStyleWallFallback(ThemeMaterials, Config.WallMaterial, Error)) return nullptr;
		if (!AddOutput(Master, ThemeMaterials, TEXT("Wall"), WallThemeOutputPin, Error)
			|| !AddOutput(Master, ThemeMaterials, TEXT("Wall - Door"), WallDoorThemeOutputPin, Error)
			|| !AddOutput(Master, TextureOverride, TEXT("Wall"), WallTextureOutputPin, Error)
			|| !AddOutput(Master, TextureOverride, TEXT("Wall - Door"), WallDoorTextureOutputPin, Error)) return nullptr;
		UPCGGraph* MeshSource = ConstructionSettings ? ConstructionSettings->GetSubgraph() : nullptr;
		UPCGGraph* MeshClone = CloneGraph(MeshSource, Master, Error);
		if (!MeshClone) return nullptr;
		UPCGNode* ConstructionInput = MeshClone->GetInputNode();
		UPCGNode* NormalWalls = Node(MeshClone, TEXT("Reroute_0"), Error);
		UPCGNode* UpperWalls = Node(MeshClone, TEXT("AddTags_0"), Error);
		UPCGNode* MatchedWalls = Node(MeshClone, TEXT("MatchAndSetAttributes_3"), Error);
		UPCGNode* PreLoopWalls = Node(MeshClone, TEXT("AttributePartition_3"), Error);
		UPCGNode* FinalWalls = Node(MeshClone, TEXT("Loop_1"), Error);
		if (!ConstructionInput || !ConstructionInput->GetOutputPin(TEXT("Wall")) || !ConstructionInput->GetOutputPin(TEXT("Wall - Door"))
			|| !NormalWalls || !UpperWalls || !MatchedWalls || !PreLoopWalls || !FinalWalls) return nullptr;
		const TPair<UPCGNode*, FName> WallDiagnosticOutputs[] = {
			{ConstructionInput, WallConstructionInputPin}, {NormalWalls, WallNormalInputPin}, {UpperWalls, WallUpperInputPin},
			{MatchedWalls, WallMatchedPin}, {PreLoopWalls, WallPreLoopPin}, {FinalWalls, WallFinalPin}};
		for (const TPair<UPCGNode*, FName>& Diagnostic : WallDiagnosticOutputs)
			if (!AddOutput(MeshClone, Diagnostic.Key,
				Diagnostic.Key == ConstructionInput ? TEXT("Wall") : TEXT("Out"), Diagnostic.Value, Error)) return nullptr;
		if (!AddOutput(MeshClone, ConstructionInput, TEXT("Wall - Door"), WallDoorConstructionInputPin, Error)) return nullptr;
		ConstructionSettings->SetSubgraph(MeshClone);
		Construction->UpdateAfterSettingsChangeDuringCreation();
		for (const TPair<UPCGNode*, FName>& Diagnostic : WallDiagnosticOutputs)
			if (!AddOutput(Master, Construction, Diagnostic.Value, Diagnostic.Value, Error)) return nullptr;
		if (!AddOutput(Master, Construction, WallDoorConstructionInputPin, WallDoorConstructionInputPin, Error)) return nullptr;
		// All Doors is the exact native final output for the Wall - Door input.
		// PCG_SetDungeonMesh removes WallMaterial after it has completed native
		// construction, so restore it through a strict per-piece carrier before this
		// diagnostic becomes the authoritative final structural stream.
		if (!AddOutput(Master, Construction, TEXT("All Doors"), WallDoorFinalPin, Error)
			|| !PatchExactFinalDoorMaterialProvenance(Master, TextureOverride, Construction, Error)) return nullptr;
		if (!FEFCalystoNativeAdapter::ValidateGraph(Master, Error)) return nullptr;
		return Master;
	}

	static bool EmptyArray(UObject* Object, const TCHAR* Name, FString& Error)
	{
		FArrayProperty* P = FindTypedAllowlistedProperty<FArrayProperty>(Object->GetClass(), Name, TEXT("array"), Error);
		if (!P) return false;
		FScriptArrayHelper(P, P->ContainerPtrToValuePtr<void>(Object)).EmptyValues();
		return true;
	}

	static bool FixedEntry(const FEFCalystoArchitectureEntry& E, bool bActor, int64 Floor)
	{
		const auto& V = E.Variation;
		return FEFCalystoDirectorProbability::IsEligible(E.Selection, Floor, {})
			&& E.Payload == (bActor ? EEFCalystoArchitecturePayload::Actor : EEFCalystoArchitecturePayload::Mesh)
			&& E.Rotation == EEFCalystoArchitectureRotation::None
			&& V.LocationMinimum.IsZero() && V.LocationMaximum.IsZero() && V.RotationMinimum.IsZero() && V.RotationMaximum.IsZero()
			&& V.ScaleMinimum.Equals(FVector::OneVector) && V.ScaleMaximum.Equals(FVector::OneVector);
	}

	static bool ApplyArchitecture(UObject* Dungeon, const FEFCalystoStyle& Style, int64 Floor, FString& Error)
	{
		const auto MeshWriter = [&Error, Floor](const UStruct* S, void* Data, const FEFCalystoArchitectureEntry& E)
		{
			if (!FixedEntry(E, E.Payload == EEFCalystoArchitecturePayload::Actor, Floor))
			{ Error = TEXT("Native traversal candidate requires eligible fixed structural entries."); return false; }
			UObject* Payload = E.Payload == EEFCalystoArchitecturePayload::Actor ? static_cast<UObject*>(E.ActorClass.Get()) : E.Mesh.Get();
			return WriteNativeObject(S, Data, TEXT("Mesh"), Payload, true, Error)
				&& WriteNativeWeight(S, Data, 1.0, Error) && WriteFixedTransform(S, Data, E.Transform, TEXT(""), Error);
		};
		const auto DoorWriter = [&Error](const UStruct* S, void* Data, const FEFCalystoDoorway& E)
		{
			return WriteNativeObject(S, Data, TEXT("Mesh Wall"), E.WallMesh.Get(), true, Error)
				&& WriteNativeObject(S, Data, TEXT("Mesh Frame"), E.FrameMesh.Get(), true, Error)
				&& WriteNativeObject(S, Data, TEXT("Door Blueprint"), E.DoorClass.Get(), true, Error)
				&& WriteNativeWeight(S, Data, 1.0, Error)
				&& WriteFixedTransform(S, Data, E.WallTransform, TEXT(""), Error)
				&& WriteFixedTransform(S, Data, E.FrameTransform, TEXT(" Frame"), Error)
				&& WriteFixedTransform(S, Data, E.DoorTransform, TEXT(" Door"), Error);
		};
		const auto& A = Style.Architecture;
		if (!WriteStyleArray(Dungeon, TEXT("Floor"), A.Floor, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("Wall"), A.Wall, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("Roof"), A.Roof, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("DoorFrame"), A.DoorFrames, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("Door"), A.Doors, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("RampTop"), A.RampTop, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("RampBottom"), A.RampBottom, MeshWriter, Error)
			|| !WriteStyleArray(Dungeon, TEXT("WallDoor"), A.Doorways, DoorWriter, Error)) return false;
		TArray<FEFCalystoArchitectureEntry> Lights;
		for (const auto& E : Style.Lighting.WallLights)
			if (FEFCalystoDirectorProbability::IsEligible(E.Selection, Floor, {})) Lights.Add(E);
		const auto LightWriter = [&Error, &Style](const UStruct* Schema, void* Data, const FEFCalystoArchitectureEntry& E)
		{
			FEFCalystoArchitectureVariation V = E.Variation;
			V.LocationMinimum += E.Transform.LocationOffset; V.LocationMaximum += E.Transform.LocationOffset;
			V.RotationMinimum += E.Transform.RotationOffset; V.RotationMaximum += E.Transform.RotationOffset;
			V.ScaleMinimum *= E.Transform.Scale; V.ScaleMaximum *= E.Transform.Scale;
			V.LocationMinimum.Y -= Style.Lighting.Placement.PositionVariationCm;
			V.LocationMaximum.Y += Style.Lighting.Placement.PositionVariationCm;
			V.LocationMinimum.Z -= Style.Lighting.Placement.PositionVariationCm;
			V.LocationMaximum.Z += Style.Lighting.Placement.PositionVariationCm;
			return WriteNativeObject(Schema, Data, TEXT("Blueprint"), E.ActorClass.Get(), true, Error)
				&& WriteNativeWeight(Schema, Data, 1.0, Error) && WriteVariation(Schema, Data, V, Error);
		};
		if (!WriteStyleArray(Dungeon, TEXT("WallLightObject"), Lights, LightWriter, Error)) return false;
		for (const TCHAR* Name : { TEXT("WallBottomObject"), TEXT("WallMiddleObject"), TEXT("WallTopObject"), TEXT("RoofObject") })
			if (!EmptyArray(Dungeon, Name, Error)) return false;
		return true;
	}
}

struct FEFCalystoNativeAdapter::FImpl
{
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UPCGComponent> Component;
	TStrongObjectPtr<UPCGGraph> Graph;
	TStrongObjectPtr<UPCGGraph> OriginalGraph;
	TWeakObjectPtr<UClass> StartClass, EndClass;
	TSet<UStaticMesh*> FloorMeshes, WallMeshes, RoofMeshes;
	TArray<TPair<FObjectProperty*, TStrongObjectPtr<UObject>>> OriginalData;
	TArray<TStrongObjectPtr<UObject>> Schemas;
	FEFCalystoNativeRoomConfig Rooms;
	int32 GenerateRequests = 0;
	bool bCleanupRequested = false;
	FEFCalystoResolvedLighting Lighting;
	FGuid LightEntryId;
	TWeakObjectPtr<UClass> TorchClass;
	TStrongObjectPtr<UEFCalystoFloorLightingComponent> LightingController;
};

FEFCalystoNativeAdapter::FEFCalystoNativeAdapter() : Impl(MakeUnique<FImpl>()) {}
FEFCalystoNativeAdapter::~FEFCalystoNativeAdapter() = default;
int32 FEFCalystoNativeAdapter::GetGenerateRequestCount() const { return Impl->GenerateRequests; }

bool FEFCalystoNativeAdapter::ValidateZeroHeightWallExtensionContract(const UPCGGraph* MeshGraph,
	const double WallHeight, const double InitialWallHeight, FString& Error)
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset();
	if (!FMath::IsFinite(WallHeight) || !FMath::IsFinite(InitialWallHeight)
		|| WallHeight <= 0.0 || InitialWallHeight <= 0.0)
	{ Error = TEXT("Native wall extension heights must be finite and positive on the prepared runtime actor."); return false; }
	if (WallHeight != InitialWallHeight) return false;
	UPCGGraph* Graph = const_cast<UPCGGraph*>(MeshGraph);
	const auto Find = [Graph, &Error](const TCHAR* Name) { return Node(Graph, Name, Error); };
	UPCGNode* Height = Find(TEXT("GetActorProperty_9"));
	UPCGNode* Initial = Find(TEXT("GetActorProperty_7"));
	UPCGNode* OffsetHeight = Find(TEXT("GetActorProperty_5"));
	UPCGNode* Divide = Find(TEXT("AttributeMathsOp_11"));
	UPCGNode* Subtract = Find(TEXT("AttributeMathsOp_13"));
	UPCGNode* OneXY = Find(TEXT("CreateAttribute_1"));
	UPCGNode* OneZ = Find(TEXT("CreateAttribute_14"));
	UPCGNode* ScaleVector = Find(TEXT("MakeVectorAttribute_15"));
	UPCGNode* OffsetVector = Find(TEXT("MakeVectorAttribute_6"));
	UPCGNode* Declaration = Find(TEXT("NamedRerouteDeclaration_12"));
	UPCGNode* Usage = Find(TEXT("NamedRerouteUsage_16"));
	UPCGNode* Offset = Find(TEXT("TransformPoints_1"));
	UPCGNode* WallPoints = Find(TEXT("ToPoint_33"));
	UPCGNode* Scale = Find(TEXT("TransformPoints_8"));
	UPCGNode* Tags = Find(TEXT("AddTags_0"));
	if (!Error.IsEmpty()) return false;
	const auto ReadProperty = [](const UPCGNode* N, const FName Expected)
	{
		const auto* S = N ? Cast<UPCGGetActorPropertySettings>(N->GetSettings()) : nullptr;
		return S && S->bEnabled && !S->bSelectComponent && S->PropertyName == Expected
			&& S->ActorSelector.ActorFilter == EPCGActorFilter::Original && !S->ActorSelector.bIncludeChildren;
	};
	const auto ConstantOne = [](const UPCGNode* N)
	{
		const auto* S = N ? Cast<UPCGCreateAttributeSetSettings>(N->GetSettings()) : nullptr;
		return S && S->bEnabled && S->AttributeTypes.Type == EPCGMetadataTypes::Double && S->AttributeTypes.DoubleValue == 1.0;
	};
	const auto Math = [](const UPCGNode* N, const EPCGMetadataMathsOperation Expected)
	{
		const auto* S = N ? Cast<UPCGMetadataMathsSettings>(N->GetSettings()) : nullptr;
		return S && S->bEnabled && S->Operation == Expected && S->InputSource1.ToString() == TEXT("@Last")
			&& S->InputSource2.ToString() == TEXT("@Last") && S->OutputTarget.ToString() == TEXT("@Source");
	};
	const auto Vector = [](const UPCGNode* N)
	{
		const auto* S = N ? Cast<UPCGMetadataMakeVectorSettings>(N->GetSettings()) : nullptr;
		return S && S->bEnabled && S->OutputType == EPCGMetadataTypes::Vector
			&& S->MakeVector3Op == EPCGMetadataMakeVector3::ThreeValues
			&& S->InputSource1.ToString() == TEXT("@Last") && S->InputSource2.ToString() == TEXT("@Last")
			&& S->InputSource3.ToString() == TEXT("@Last") && S->OutputTarget.ToString() == TEXT("@Source");
	};
	const auto* OffsetSettings = Cast<UPCGTransformPointsSettings>(Offset->GetSettings());
	const auto* ScaleSettings = Cast<UPCGTransformPointsSettings>(Scale->GetSettings());
	const auto* TagSettings = Cast<UPCGAddTagSettings>(Tags->GetSettings());
	const auto* UsageSettings = Cast<UPCGNamedRerouteUsageSettings>(Usage->GetSettings());
	if (!ReadProperty(Height, TEXT("Wall Height")) || !ReadProperty(Initial, TEXT("Initial Wall Height"))
		|| !ReadProperty(OffsetHeight, TEXT("Initial Wall Height")) || !ConstantOne(OneXY) || !ConstantOne(OneZ)
		|| !Math(Divide, EPCGMetadataMathsOperation::Divide) || !Math(Subtract, EPCGMetadataMathsOperation::Subtract)
		|| !Vector(ScaleVector) || !Vector(OffsetVector)
		|| !Cast<UPCGNamedRerouteDeclarationSettings>(Declaration->GetSettings())
		|| !UsageSettings || UsageSettings->Declaration.Get() != Cast<UPCGNamedRerouteDeclarationSettings>(Declaration->GetSettings())
		|| !OffsetSettings || !OffsetSettings->bEnabled || OffsetSettings->bAbsoluteOffset || OffsetSettings->bAbsoluteScale
		|| OffsetSettings->bApplyToAttribute || !OffsetSettings->ScaleMin.Equals(FVector::OneVector)
		|| !OffsetSettings->ScaleMax.Equals(FVector::OneVector)
		|| !ScaleSettings || !ScaleSettings->bEnabled || !ScaleSettings->bAbsoluteScale || ScaleSettings->bApplyToAttribute
		|| ScaleSettings->bAbsoluteOffset
		|| !ScaleSettings->OffsetMin.IsZero() || !ScaleSettings->OffsetMax.IsZero()
		|| !TagSettings || !TagSettings->bEnabled || TagSettings->TagsToAdd != TEXT("NoSocket"))
	{ Error = TEXT("Native upper-wall extension settings no longer implement the audited height ratio minus one."); return false; }
	struct FExpectedEdge { const UPCGNode* From; const TCHAR* ToPin; const UPCGNode* To; FName FromPin = TEXT("Out"); };
	const FExpectedEdge Required[] = {
		{Height, TEXT("InA"), Divide}, {Initial, TEXT("InB"), Divide},
		{Divide, TEXT("InA"), Subtract}, {OneZ, TEXT("InB"), Subtract},
		{OneXY, TEXT("X"), ScaleVector}, {OneXY, TEXT("Y"), ScaleVector}, {Subtract, TEXT("Z"), ScaleVector},
		{ScaleVector, TEXT("In"), Declaration}, {Declaration, TEXT("In"), Usage, PCGNamedRerouteConstants::InvisiblePinLabel},
		{Usage, TEXT("ScaleMin"), Scale}, {Usage, TEXT("ScaleMax"), Scale},
		{OffsetHeight, TEXT("Z"), OffsetVector}, {OffsetVector, TEXT("OffsetMin"), Offset},
		{OffsetVector, TEXT("OffsetMax"), Offset}, {WallPoints, TEXT("In"), Offset},
		{Offset, TEXT("In"), Scale}, {Scale, TEXT("In"), Tags}
	};
	for (const auto& E : Required)
	{
		const UPCGPin* Pin = E.To->GetInputPin(E.ToPin);
		if (!Pin || Pin->Edges.Num() != 1 || !Edge(E.From, E.FromPin, E.To, E.ToPin))
		{
			Error = FString::Printf(TEXT("Native upper-wall extension requires %s.%s -> %s.%s with one incoming edge (observed %d)."),
				*E.From->GetName(), *E.FromPin.ToString(), *E.To->GetName(), E.ToPin, Pin ? Pin->Edges.Num() : 0);
			return false;
		}
	}
	// Parameter override inputs may not change the checked constants/selectors/settings.
	for (const UPCGNode* N : {Height, Initial, OffsetHeight, Divide, Subtract, OneXY, OneZ, ScaleVector, OffsetVector, Declaration, Usage, Offset, Scale})
		for (const UPCGPin* Pin : N->GetInputPins())
		{
			if (!Pin || Pin->Edges.IsEmpty()) continue;
			bool bExpected = false;
			for (const auto& E : Required) if (E.To == N && Pin->Properties.Label == E.ToPin) bExpected = true;
			if (!bExpected) { Error = TEXT("Native upper-wall extension has an unsupported parameter override."); return false; }
		}
	UPCGNode* Match = Find(TEXT("MatchAndSetAttributes_3"));
	UPCGNode* Partition = Find(TEXT("AttributePartition_3"));
	UPCGNode* Loop = Find(TEXT("Loop_1"));
	UPCGNode* Materials = Find(TEXT("AttributeFilter_24"));
	if (!Error.IsEmpty() || !Edge(Tags, TEXT("Out"), Match, TEXT("In"))
		|| !Edge(Match, TEXT("Out"), Partition, TEXT("In")) || !Edge(Partition, TEXT("Out"), Loop, TEXT("In"))
		|| !Edge(Loop, TEXT("Out"), Materials, TEXT("In")))
	{ Error = TEXT("Native upper-wall extensions no longer reach the audited wall material branch."); return false; }
	for (const TCHAR* Name : {TEXT("StaticMeshSpawner_0"), TEXT("StaticMeshSpawner_4"), TEXT("StaticMeshSpawner_5")})
	{
		UPCGNode* SpawnerNode = Find(Name);
		const auto* Spawner = SpawnerNode ? Cast<UPCGStaticMeshSpawnerSettings>(SpawnerNode->GetSettings()) : nullptr;
		const auto* Selector = Spawner ? Cast<UPCGMeshSelectorByAttribute>(Spawner->MeshSelectorParameters) : nullptr;
		if (!Selector || Selector->AttributeName != TEXT("Mesh"))
		{ Error = TEXT("Native wall spawners no longer consume the recorded Mesh attribute."); return false; }
	}
	return true;
}

bool FEFCalystoNativeAdapter::CanContainZeroHeightWallExtensions(FString& Error) const
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset();
	const AActor* Actor = Impl->Actor.Get();
	const UPCGComponent* Component = Impl->Component.Get();
	if (!IsInGameThread() || !IsValid(Actor) || Actor->HasAnyFlags(RF_ClassDefaultObject)
		|| !Actor->GetWorld() || !Actor->GetWorld()->IsGameWorld() || Impl->bCleanupRequested
		|| !Component || Component->GetGraph() != Impl->Graph.Get())
	{ Error = TEXT("Wall extension classification requires the exact prepared runtime actor and graph."); return false; }
	const FNumericProperty* Height = FindFloatingProperty(Actor->GetClass(), TEXT("Wall Height"), Error);
	const FNumericProperty* Initial = FindFloatingProperty(Actor->GetClass(), TEXT("Initial Wall Height"), Error);
	if (!Height || !Initial) return false;
	const UPCGNode* Construction = Node(Impl->Graph.Get(), TEXT("Subgraph_43"), Error);
	const auto* Call = Construction ? Cast<UPCGSubgraphSettings>(Construction->GetSettings()) : nullptr;
	if (!Call || !Call->GetSubgraph()) { Error = TEXT("Native wall construction graph is unavailable."); return false; }
	return ValidateZeroHeightWallExtensionContract(Call->GetSubgraph(), ReadFloating(Height, Actor), ReadFloating(Initial, Actor), Error);
}

bool FEFCalystoNativeAdapter::ReadFinalWallSurfaces(const FPCGDataCollection& Output,
	const bool bZeroHeightCapabilityVerified, TArray<FEFCalystoNativeWallSurface>& Surfaces,
	TArray<FEFCalystoNativeWallExtension>& ZeroHeightExtensions, FString& Error)
{
	Surfaces.Reset(); ZeroHeightExtensions.Reset(); Error.Reset(); int32 Visited = 0, TaggedIndex = 0;
	const auto Reject = [&Surfaces, &ZeroHeightExtensions]()
	{
		Surfaces.Reset();
		ZeroHeightExtensions.Reset();
		return false;
	};
	const FName FinalWallPins[] = {
		EFCalystoNativeAdapterPrivate::WallFinalPin,
		EFCalystoNativeAdapterPrivate::WallDoorFinalPin};
	for (const FName SourcePin : FinalWallPins)
	{
		const bool bMayContainZeroHeight = SourcePin == EFCalystoNativeAdapterPrivate::WallFinalPin;
		for (const FPCGTaggedData& Tagged : Output.GetInputsByPin(SourcePin))
		{
		const int32 CurrentTaggedIndex = TaggedIndex++;
		const auto* Points = Cast<UPCGBasePointData>(Tagged.Data);
		if (!Points || Points->GetNumPoints() > 65536 - Visited)
		{ Error = TEXT("Native final-wall provenance exceeded its bounded point contract."); return Reject(); }
		Visited += Points->GetNumPoints();
		const UPCGMetadata* Metadata = Points->ConstMetadata();
		const auto* SoftMesh = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(TEXT("Mesh")) : nullptr;
		const auto* StringMesh = Metadata ? Metadata->GetConstTypedAttribute<FString>(TEXT("Mesh")) : nullptr;
		const auto* RoomId = Metadata ? Metadata->GetConstTypedAttribute<int64>(TEXT("EF_RoomId")) : nullptr;
		const auto* SoftMaterial = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial")) : nullptr;
		const auto* StringMaterial = Metadata ? Metadata->GetConstTypedAttribute<FString>(TEXT("WallMaterial")) : nullptr;
		const auto* StyleOwned = Metadata ? Metadata->GetConstTypedAttribute<bool>(EFCalystoNativeAdapterPrivate::StyleOwnedWallAttribute) : nullptr;
		const bool bInvalidStyleAuthorityType = Metadata && Metadata->HasAttribute(EFCalystoNativeAdapterPrivate::StyleOwnedWallAttribute) && !StyleOwned;
		if ((!SoftMesh && !StringMesh) || !RoomId || (!SoftMaterial && !StringMaterial) || bInvalidStyleAuthorityType)
		{
			TArray<FString> Tags;
			for (const FString& Tag : Tagged.Tags) Tags.Add(Tag);
			Tags.Sort();
			const FString MeshState = SoftMesh ? TEXT("SoftObjectPath") : StringMesh ? TEXT("String")
				: !Metadata ? TEXT("NoMetadata") : Metadata->HasAttribute(TEXT("Mesh")) ? TEXT("WrongType") : TEXT("Missing");
			const FString RoomState = RoomId ? TEXT("int64")
				: !Metadata ? TEXT("NoMetadata") : Metadata->HasAttribute(TEXT("EF_RoomId")) ? TEXT("WrongType") : TEXT("Missing");
			const FString MaterialState = SoftMaterial ? TEXT("SoftObjectPath") : StringMaterial ? TEXT("String")
				: !Metadata ? TEXT("NoMetadata") : Metadata->HasAttribute(TEXT("WallMaterial")) ? TEXT("WrongType") : TEXT("Missing");
			const FString StyleState = StyleOwned ? TEXT("bool")
				: !Metadata ? TEXT("NoMetadata") : Metadata->HasAttribute(EFCalystoNativeAdapterPrivate::StyleOwnedWallAttribute) ? TEXT("WrongType") : TEXT("Absent");
			Error = FString::Printf(TEXT("Native final-wall provenance has an invalid typed metadata contract: source=%s tagged=%d points=%d Mesh=%s EF_RoomId=%s WallMaterial=%s EF_StyleOwnedWall=%s tags=%s. %s"),
				*SourcePin.ToString(), CurrentTaggedIndex, Points->GetNumPoints(), *MeshState, *RoomState, *MaterialState, *StyleState,
				*FString::Join(Tags, TEXT(",")), *EFCalystoNativeAdapterPrivate::DescribeWallRoomIdentityStreams(Output));
			return Reject();
		}
		const FConstPCGPointValueRanges Ranges(Points);
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			const FPCGPoint Point = Ranges.GetPoint(Index);
			if (Point.Transform.ContainsNaN())
			{ Error = TEXT("Native final-wall provenance contains a nonfinite transform."); return Reject(); }
			const auto Key = Point.MetadataEntry;
			FSoftObjectPath Mesh;
			if (SoftMesh) Mesh = SoftMesh->GetValueFromItemKey(Key);
			else if (StringMesh) Mesh = FSoftObjectPath(StringMesh->GetValueFromItemKey(Key));
			FSoftObjectPath Material;
			if (SoftMaterial) Material = SoftMaterial->GetValueFromItemKey(Key);
			else if (StringMaterial) Material = FSoftObjectPath(StringMaterial->GetValueFromItemKey(Key));
			const int64 OwningRoomId = RoomId->GetValueFromItemKey(Key);
			const bool bStyleOwned = StyleOwned && StyleOwned->GetValueFromItemKey(Key);
			if (Mesh.IsNull() || !Mesh.IsValid() || !Cast<UStaticMesh>(Mesh.ResolveObject()))
			{ Error = TEXT("Native final-wall provenance lacks its actual resident mesh."); return Reject(); }
			if ((bStyleOwned && OwningRoomId != 0) || (!bStyleOwned && OwningRoomId <= 0))
			{
				TArray<FString> Tags;
				for (const FString& Tag : Tagged.Tags) Tags.Add(Tag);
				Tags.Sort();
				Error = FString::Printf(TEXT("Native final-wall provenance has an invalid emitted authority: source=%s tagged=%d point=%d entry=%lld style_owned=%d room=%lld tags=%s mesh=%s material=%s transform=%s. %s"),
					*SourcePin.ToString(), CurrentTaggedIndex, Index, static_cast<int64>(Key), bStyleOwned, OwningRoomId, *FString::Join(Tags, TEXT(",")), *Mesh.ToString(), *Material.ToString(),
					*Point.Transform.ToHumanReadableString(), *EFCalystoNativeAdapterPrivate::DescribeWallRoomIdentityStreams(Output));
				return Reject();
			}
			if (Material.IsNull() || !Material.IsValid() || !Cast<UMaterialInterface>(Material.ResolveObject()))
			{ Error = TEXT("Native final-wall provenance lacks its actual resident material."); return Reject(); }
			const FVector Scale = Point.Transform.GetScale3D();
			const bool bZeroHeight = Scale.Z == 0.0;
			if (bZeroHeight && (!bMayContainZeroHeight || !bZeroHeightCapabilityVerified || !Tagged.Tags.Contains(TEXT("NoSocket")) || Scale.X == 0.0 || Scale.Y == 0.0))
			{ Error = TEXT("A zero-height native wall lacks the audited upper-extension provenance."); return Reject(); }
		Surfaces.Add({Mesh, Point.Transform, OwningRoomId, Material, bZeroHeight, bStyleOwned,
			SourcePin == EFCalystoNativeAdapterPrivate::WallDoorFinalPin});
		if (bZeroHeight) ZeroHeightExtensions.Add({Mesh, Point.Transform});
		}
	}
	}
	// Preserve cardinality; identical transforms are separate native instance requests.
	return true;
}

bool FEFCalystoNativeAdapter::ValidateCapabilities(const FEFCalystoCompiledDirector& Compiled, const FGuid& StyleId, FString& Error)
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset(); const FEFCalystoStyle* S = Compiled.FindStyle(StyleId);
	if (!Compiled.IsValid() || !S) { Error = TEXT("Native preflight requires an immutable compiled Style."); return false; }
	const auto Reject = [&Error](const TCHAR* Field) { Error = FString::Printf(TEXT("%s is not supported by the native traversal candidate yet; use an explicit parity fixture."), Field); return false; };
	if (Compiled.GetAdvanced().Adaptation.bEnabled) return Reject(TEXT("Advanced.Adaptation"));
	const auto ValidateOptionalArchitecture=[&](const TArray<FEFCalystoSurfaceDecoration>& Rules,const TCHAR* Field)
	{
		for (const auto& Rule:Rules) for (const auto& Entry:Rule.Alternatives)
		{
			if (!Entry.Selection.bEnabled || Entry.Selection.Weight<=0) continue;
			if (Entry.Selection.CooldownFloors!=0 || Entry.Payload==EEFCalystoArchitecturePayload::Actor)
				return Reject(*FString::Printf(TEXT("%s tracked actor/cooldown integration"),Field));
		}
		return true;
	};
	if (!ValidateOptionalArchitecture(S->Architecture.Decoration,TEXT("Style.Architecture.Decoration"))) return false;
	if (S->Architecture.ProgressionDoorMesh.IsNull()) { Error = TEXT("Style.Architecture.ProgressionDoorMesh requires a supported door mesh."); return false; }
	int32 EnabledLights = 0;
	for (const auto& E : S->Lighting.WallLights)
	{
		if (!E.Selection.bEnabled || E.Selection.Weight <= 0) continue;
		++EnabledLights;
		if (E.Payload != EEFCalystoArchitecturePayload::Actor || E.ActorClass.ToSoftObjectPath().ToString() != NativeTorchPath
			|| E.Selection.CooldownFloors != 0 || E.Rotation != EEFCalystoArchitectureRotation::None
			|| E.Transform.bUniformScale != E.Variation.bUniformScale)
			return Reject(TEXT("Style.Lighting.WallLights native torch payload/rotation/cooldown"));
	}
	if (EnabledLights > 1) return Reject(TEXT("Style.Lighting.WallLights multiple alternatives"));
	if (S->Lighting.Placement.Zone != EEFCalystoPlacementZone::WallMiddle)
		return Reject(TEXT("Style.Lighting.Placement.Zone"));
	if (S->Layout.MinimumRoomSize != 4 || S->Layout.MaximumRoomSize != 8) return Reject(TEXT("Style.Layout.RoomSize"));
	const TPair<const TCHAR*, const TArray<FEFCalystoArchitectureEntry>*> Arrays[] = {
		{TEXT("Floor"), &S->Architecture.Floor}, {TEXT("Wall"), &S->Architecture.Wall}, {TEXT("Roof"), &S->Architecture.Roof},
		{TEXT("DoorFrames"), &S->Architecture.DoorFrames}, {TEXT("Doors"), &S->Architecture.Doors},
		{TEXT("RampTop"), &S->Architecture.RampTop}, {TEXT("RampBottom"), &S->Architecture.RampBottom}};
	for (const auto& A : Arrays)
	{
		if (A.Value->Num() > 1) return Reject(*FString::Printf(TEXT("Style.Architecture.%s alternatives"), A.Key));
		for (const auto& E : *A.Value)
			if (!FixedEntry(E, FString(A.Key) == TEXT("Doors"), E.Selection.FirstEligibleFloor) || E.Selection.CooldownFloors != 0)
				return Reject(*FString::Printf(TEXT("Style.Architecture.%s payload/variation/selection"), A.Key));
	}
	if (S->Architecture.Doorways.Num() > 1) return Reject(TEXT("Style.Architecture.Doorways alternatives"));
	for (const auto& T : Compiled.GetThemes())
	{
		if (!ValidateOptionalArchitecture(T.Architecture,TEXT("RoomTheme.Architecture"))) return false;
		if (T.Selection.CooldownFloors != 0) return Reject(TEXT("RoomTheme.Selection.CooldownFloors"));
	}
	return true;
}

TSharedPtr<FEFCalystoNativeAdapter> FEFCalystoNativeAdapter::Prepare(AActor* Actor,
	TSharedRef<const FEFCalystoCompiledDirector> Compiled, const FEFCalystoRandomKey& Key,
	int32 TopologySeed, UClass* ProgressionDoorClass, FString& Error)
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset();
	if (!IsInGameThread() || !IsValid(Actor) || !Actor->GetWorld() || !Actor->GetWorld()->IsGameWorld()
		|| !ProgressionDoorClass || !ProgressionDoorClass->IsChildOf(AActor::StaticClass()) || TopologySeed <= 0)
	{ Error = TEXT("Native preparation requires a live game actor, explicit progression class and positive topology seed."); return nullptr; }
	if (!ValidateCapabilities(*Compiled, Key.StyleId, Error)) return nullptr;
	UPCGComponent* RuntimeComponent = FindIdleRuntimeComponent(Actor, Error);
	if (!RuntimeComponent) return nullptr;
	const auto* Style = Compiled->FindStyle(Key.StyleId);
	FEFCalystoResolvedLighting ResolvedLighting;
	if (!FEFCalystoLightingResolver::Resolve(Style->Lighting, Key, ResolvedLighting, Error)) return nullptr;
	FActorSchema AS; UFunction* GetPieces = nullptr;
	if (!ResolveActorSchema(Actor, AS, Error) || !ResolveGetPiecesShapeFunction(Actor, GetPieces, Error)) return nullptr;
	UObject* OriginalDungeon = AS.Dungeon->GetObjectPropertyValue_InContainer(Actor);
	UObject* OriginalMaterials = AS.DungeonMaterial->GetObjectPropertyValue_InContainer(Actor);
	UObject* OriginalThemes = AS.RoomThemeList->GetObjectPropertyValue_InContainer(Actor);
	UObject* OriginalSpawners = AS.Spawners->GetObjectPropertyValue_InContainer(Actor);
	if (!OriginalDungeon || !OriginalMaterials || !OriginalThemes || !OriginalSpawners)
	{ Error = TEXT("Native actor is missing a required source data object."); return nullptr; }
	FDungeonMaterialSchema MS; FThemeSchema TS; FSpawnerSchema SS;
	if (!ResolveDungeonMaterialSchema(OriginalMaterials, MS, Error) || !ResolveThemeSchema(OriginalThemes, TS, Error) || !ResolveSpawnerSchema(OriginalSpawners, SS, Error)) return nullptr;
	TSharedPtr<FEFCalystoNativeAdapter> Adapter = MakeShareable(new FEFCalystoNativeAdapter());
	auto& I = *Adapter->Impl; I.Actor = Actor; I.Component = RuntimeComponent;
	I.OriginalGraph.Reset(RuntimeComponent->GetGraph()); I.EndClass = ProgressionDoorClass;
	FClassProperty* StartProperty = FindTypedAllowlistedProperty<FClassProperty>(OriginalDungeon->GetClass(), TEXT("StartBlueprint"), TEXT("class"), Error);
	I.StartClass = StartProperty ? Cast<UClass>(StartProperty->GetObjectPropertyValue_InContainer(OriginalDungeon)) : nullptr;
	if (!I.StartClass.IsValid()) { Error = TEXT("Native Dungeon.StartBlueprint class is unavailable."); return nullptr; }
	for (auto* P : {AS.Dungeon, AS.DungeonMaterial, AS.RoomThemeList, AS.Spawners}) I.OriginalData.Emplace(P, TStrongObjectPtr<UObject>(P->GetObjectPropertyValue_InContainer(Actor)));
	for (const auto& E : Style->Architecture.Floor) I.FloorMeshes.Add(E.Mesh.Get());
	for (const auto& E : Style->Architecture.Wall) I.WallMeshes.Add(E.Mesh.Get());
	for (const auto& E : Style->Architecture.Roof) I.RoofMeshes.Add(E.Mesh.Get());
	UObject* Dungeon = DuplicateTransiently(OriginalDungeon, Actor, TEXT("NativeDungeon"));
	UObject* Materials = DuplicateTransiently(OriginalMaterials, Actor, TEXT("NativeMaterials"));
	UObject* Themes = DuplicateTransiently(OriginalThemes, Actor, TEXT("NativeThemes"));
	UObject* Spawners = DuplicateTransiently(OriginalSpawners, Actor, TEXT("NativeSpawners"));
	if (!Dungeon || !Materials || !Themes || !Spawners) { Error = TEXT("Native transient schema cloning failed."); return nullptr; }
	for (UObject* O : {Dungeon, Materials, Themes, Spawners}) I.Schemas.Emplace(O);
	if (!ApplyArchitecture(Dungeon, *Style, Key.FloorNumber, Error)
		|| !WriteNativeObject(Dungeon->GetClass(), Dungeon, TEXT("EndBlueprint"), ProgressionDoorClass, true, Error)) return nullptr;
	I.Lighting = ResolvedLighting;
	for (const auto& E : Style->Lighting.WallLights) if (FEFCalystoDirectorProbability::IsEligible(E.Selection, Key.FloorNumber, {}))
	{
		auto* Class = Cast<UBlueprintGeneratedClass>(E.ActorClass.Get());
		// Native wall-torch PointLight is an SCS component. ComponentTemplates is
		// a different BPGC array and does not contain this actual template.
		auto* Template = ResolveNativeTorchTemplate(Class);
		if (!Class || Class->GetPathName() != NativeTorchPath || !Template || Template->Mobility != EComponentMobility::Movable
			|| !FMath::IsFinite(Template->Intensity) || Template->Intensity < 0 || Template->IntensityUnits != ELightUnits::Unitless)
		{ Error = TEXT("The resident native wall torch no longer has its supported movable point-light template."); return nullptr; }
		I.TorchClass = Class; I.LightEntryId = E.Selection.Id;
	}
	// Preserve the native spawner algorithm with the existing inert, PCG-owned opportunity marker.
	int32 MarkerEntries = 0;
	if (!ApplyPopulationAnchorSpawner(Spawners, SS, MarkerEntries, Error)) return nullptr;
	// RoomType data owns optional native decoration records.  The Director must
	// not inherit the first vendor Theme's Mesh/Actor/LevelInstance entries: all
	// optional decoration is reserved and realized from the frozen V7 manifest.
	// Use the resident RoomType class default as the neutral, empty schema and
	// duplicate it only for this attempt.
	UClass* RoomSchemaClass = TS.RoomType ? TS.RoomType->PropertyClass : nullptr;
	UObject* NeutralRoomTemplate = RoomSchemaClass ? RoomSchemaClass->GetDefaultObject() : nullptr;
	if (!RoomSchemaClass || !NeutralRoomTemplate || !NeutralRoomTemplate->IsA(RoomSchemaClass))
	{
		Error = TEXT("Native RoomTheme RoomType class has no compatible neutral default schema.");
		return nullptr;
	}
	UObject* NeutralRoomSchema = DuplicateTransiently(NeutralRoomTemplate, Actor, TEXT("NativeNeutralRoomSchema"));
	if (!NeutralRoomSchema || NeutralRoomSchema->GetOuter() != Actor || !NeutralRoomSchema->HasAnyFlags(RF_Transient))
	{
		Error = TEXT("Native neutral RoomType schema could not be retained transiently.");
		return nullptr;
	}
	// The class default supplies only the native schema shape.  Optional native
	// decoration is owned by the V7 reservation/materialization transaction, so
	// every advertised native decoration array must begin empty for this attempt.
	for (const TCHAR* Name : { TEXT("Floor"), TEXT("WallBottom"), TEXT("WallMiddle"), TEXT("WallTop"),
		TEXT("CornerBottom"), TEXT("CornerMiddle"), TEXT("CornerTop"), TEXT("Roof") })
	{
		if (!EmptyArray(NeutralRoomSchema, Name, Error)) return nullptr;
	}
	FScriptArrayHelper_InContainer(TS.Entries, Themes).EmptyValues();
	const TPair<FMaterialSlotSchema, UMaterialInterface*> MaterialSlots[] = {
		{MS.Floor, Style->Materials.Floor.Get()}, {MS.Wall, Style->Materials.Wall.Get()}, {MS.Roof, Style->Materials.Roof.Get()}};
	for (const auto& Slot : MaterialSlots)
	{
		if (!Slot.Value || !Slot.Value->IsA(Slot.Key.Material->PropertyClass))
		{ Error = TEXT("Style material must be resident and compatible with the native material-instance slot."); return nullptr; }
		Slot.Key.Material->SetObjectPropertyValue_InContainer(Slot.Key.Slot->ContainerPtrToValuePtr<void>(Materials), Slot.Value);
	}
	auto& RC = I.Rooms; RC.RunSeed = Key.RunSeed; RC.FloorNumber = Key.FloorNumber; RC.RerollIndex = Key.RerollIndex;
	RC.AttemptIndex = Key.AttemptIndex; RC.TopologySeed = TopologySeed; RC.StyleId = Key.StyleId; RC.DungeonTransform = Actor->GetActorTransform();
	RC.QuantizationCm = Compiled->GetAdvanced().RoomIdentityQuantizationCm; RC.MaximumRooms = Compiled->GetAdvanced().MaximumRoomRecords;
	RC.AdditionalThemeChancePercent = Compiled->GetDungeon().AdditionalRoomThemeChancePercent;
	RC.FloorMaterial = Style->Materials.Floor.ToSoftObjectPath(); RC.WallMaterial = Style->Materials.Wall.ToSoftObjectPath(); RC.RoofMaterial = Style->Materials.Roof.ToSoftObjectPath();
	RC.EmptyRoomSchema = NeutralRoomSchema;
	I.Schemas.Emplace(NeutralRoomSchema);
	for (const auto& Theme : Compiled->GetThemes())
	{
		if (!FEFCalystoDirectorProbability::IsEligible(Theme.Selection, Key.FloorNumber, {})) continue;
		FEFCalystoSurfaceMaterials Effective;
		if (!Compiled->ResolveMaterials(Key.StyleId, Theme.Selection.Id, Effective, Error)) return nullptr;
		for (UMaterialInterface* M : {Effective.Floor.Get(), Effective.Wall.Get(), Effective.Roof.Get()})
			if (!M || !M->IsA(UMaterialInstance::StaticClass())) { Error = TEXT("Theme effective material is not a resident native material instance."); return nullptr; }
		FEFCalystoNativeTheme T; T.Id = Theme.Selection.Id; T.Weight = Theme.Selection.Weight;
		T.RoomSchema = DuplicateTransiently(RC.EmptyRoomSchema.Get(), Actor, TEXT("NativeRoomSchema"));
		if (!T.RoomSchema || T.RoomSchema->GetOuter() != Actor || !T.RoomSchema->HasAnyFlags(RF_Transient))
		{ Error = TEXT("Native Theme schema could not be retained transiently."); return nullptr; }
		I.Schemas.Emplace(T.RoomSchema.Get());
		T.FloorMaterial = Effective.Floor.ToSoftObjectPath(); T.WallMaterial = Effective.Wall.ToSoftObjectPath(); T.RoofMaterial = Effective.Roof.ToSoftObjectPath();
		T.bOverrideFloor = Theme.Materials.Floor.Mode == EEFCalystoMaterialMode::Override;
		T.bOverrideWall = Theme.Materials.Wall.Mode == EEFCalystoMaterialMode::Override;
		T.bOverrideRoof = Theme.Materials.Roof.Mode == EEFCalystoMaterialMode::Override;
		RC.Themes.Add(T);
		FScriptArrayHelper Rows(TS.Entries, TS.Entries->ContainerPtrToValuePtr<void>(Themes));
		void* Row = Rows.GetRawPtr(Rows.AddValue());
		if (!Row)
		{
			Error = TEXT("Native RoomTheme could not allocate an initialized transient entry.");
			return nullptr;
		}
		TS.RoomType->SetObjectPropertyValue_InContainer(Row, T.RoomSchema.Get());
		TS.Weight->SetPropertyValue_InContainer(Row, T.Weight);
		TS.OverrideFloorMaterial->SetPropertyValue_InContainer(Row, T.bOverrideFloor);
		TS.OverrideWallMaterial->SetPropertyValue_InContainer(Row, T.bOverrideWall);
		TS.OverrideRoofMaterial->SetPropertyValue_InContainer(Row, T.bOverrideRoof);
		TS.FloorMaterial->SetObjectPropertyValue_InContainer(Row, Effective.Floor.Get());
		TS.WallMaterial->SetObjectPropertyValue_InContainer(Row, Effective.Wall.Get());
		TS.RoofMaterial->SetObjectPropertyValue_InContainer(Row, Effective.Roof.Get());
	}
	if (!RC.Validate(Error)) return nullptr;
	double Size = 0, Density = 0, Side = 0;
	if (!FEFCalystoDirectorProbability::SampleFloat(Style->Layout.DungeonSize, FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Topology, {}, 1), Size, Error)
		|| !FEFCalystoDirectorProbability::SampleFloat(Style->Layout.CandidateDensity, FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Topology, {}, 2), Density, Error)
		|| !FEFCalystoDirectorProbability::SampleFloat(Style->Layout.SidePathPercent, FEFCalystoDirectorProbability::Unit(Key, EEFCalystoRandomDomain::Topology, {}, 3), Side, Error)) return nullptr;
	UPCGGraph* Graph = BuildGraph(RuntimeComponent->GetGraph(), Actor, RC, Error);
	if (!Graph) return nullptr;
	I.Graph.Reset(Graph);
	// Commit the transient actor view only after configuration, references and graph capabilities pass.
	AS.Dungeon->SetObjectPropertyValue_InContainer(Actor, Dungeon); AS.DungeonMaterial->SetObjectPropertyValue_InContainer(Actor, Materials);
	AS.RoomThemeList->SetObjectPropertyValue_InContainer(Actor, Themes); AS.Spawners->SetObjectPropertyValue_InContainer(Actor, Spawners);
	AS.OverrideFloorMaterial->SetPropertyValue_InContainer(Actor, true); AS.OverrideWallMaterial->SetPropertyValue_InContainer(Actor, true); AS.OverrideRoofMaterial->SetPropertyValue_InContainer(Actor, true);
	*AS.DungeonSize->ContainerPtrToValuePtr<FIntVector>(Actor) = FIntVector(FMath::RoundToInt(Size), FMath::RoundToInt(Size), 1);
	WriteFloating(AS.SpawnerDensity, Actor, Density); WriteFloating(AS.SidePathChance, Actor, Side / 100.0);
	WriteFloating(AS.WallLightHeight, Actor, I.Lighting.WallLightHeightCm);
	AS.WallLightTileDistance->SetPropertyValue_InContainer(Actor, I.Lighting.WallLightTileDistance);
	Actor->ProcessEvent(GetPieces, nullptr);
	RuntimeComponent->Seed = TopologySeed; RuntimeComponent->SetGraphLocal(Graph);
	if (RuntimeComponent->GetGraph() != Graph) { Error = TEXT("Native runtime graph installation failed."); return nullptr; }
	return Adapter;
}

bool FEFCalystoNativeAdapter::ValidateGraph(const UPCGGraph* Graph, FString& Error)
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset();
	if (!Graph || !Graph->HasAnyFlags(RF_Transient)) { Error = TEXT("Director graph must be transient."); return false; }
	UPCGNode* ShapeCall = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_0"), Error);
	const auto* Call = ShapeCall ? Cast<UPCGSubgraphSettings>(ShapeCall->GetSettings()) : nullptr;
	UPCGGraph* Shape = Call ? Call->GetSubgraph() : nullptr;
	if (!Shape || !Shape->HasAnyFlags(RF_Transient)) { Error = TEXT("Director Shape must be the retained transient clone."); return false; }
	UPCGNode* Rooms = nullptr;
	for (UPCGNode* N : Shape->GetNodes())
	{
		if (N && Cast<UPCGCalystoDirectorRoomSettings>(N->GetSettings()))
		{
			if (Rooms) { Error = TEXT("Director Shape contains duplicate room assignment nodes."); return false; }
			Rooms = N;
		}
		if (N && N->GetFName() == TEXT("MatchAndSetAttributes_26")) { Error = TEXT("Native random Theme assignment remains active."); return false; }
	}
	UPCGNode* Merge = Node(Shape, TEXT("MergePoints_48"), Error);
	UPCGNode* All = Node(Shape, TEXT("NamedRerouteDeclaration_34"), Error);
	if (!Rooms || !Merge || !All || !Edge(Rooms, FEFCalystoNativeRoomPins::NativeRooms, Merge, TEXT("In"))
		|| Merge->GetInputPin(TEXT("In"))->Edges.Num() != 1 || !Edge(Merge, TEXT("Out"), All, TEXT("In"))
		|| !Edge(Rooms, FEFCalystoNativeRoomPins::RoomContexts, Shape->GetOutputNode(), FEFCalystoNativeRoomPins::RoomContexts)
		|| !Edge(ShapeCall, FEFCalystoNativeRoomPins::RoomContexts, Graph->GetOutputNode(), FEFCalystoNativeRoomPins::RoomContexts)
		|| !Edge(All, TEXT("Out"), Shape->GetOutputNode(), WallAllRoomsInputPin)
		|| !Edge(ShapeCall, WallAllRoomsInputPin, Graph->GetOutputNode(), WallAllRoomsInputPin))
	{ Error = TEXT("All native rooms must reach structural construction and the root Room Contexts output."); return false; }
	for (FName P : {FEFCalystoNativeRoomPins::MainRooms, FEFCalystoNativeRoomPins::SideRooms, FEFCalystoNativeRoomPins::StartRooms, FEFCalystoNativeRoomPins::EndRooms})
		if (!Rooms->GetInputPin(P) || Rooms->GetInputPin(P)->Edges.Num() != 1) { Error = TEXT("Director native room input is missing or ambiguous."); return false; }
	UPCGNode* Materials = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_43"), Error);
	const auto* MaterialCall = Materials ? Cast<UPCGSubgraphSettings>(Materials->GetSettings()) : nullptr;
	if (!MaterialCall || !MaterialCall->GetSubgraph() || !ValidateSurfaceMaterialConsumerGraph(MaterialCall->GetSubgraph(), Error)) return false;
	UPCGGraph* MeshGraph = MaterialCall->GetSubgraph();
	UPCGNode* ConstructionInput = MeshGraph ? MeshGraph->GetInputNode() : nullptr;
	UPCGNode* NormalWalls = Node(MeshGraph, TEXT("Reroute_0"), Error);
	UPCGNode* UpperWalls = Node(MeshGraph, TEXT("AddTags_0"), Error);
	UPCGNode* MatchedWalls = Node(MeshGraph, TEXT("MatchAndSetAttributes_3"), Error);
	UPCGNode* PreLoopWalls = Node(MeshGraph, TEXT("AttributePartition_3"), Error);
	UPCGNode* FinalWalls = Node(MeshGraph, TEXT("Loop_1"), Error);
	const TPair<UPCGNode*, FName> WallDiagnosticOutputs[] = {
		{ConstructionInput, WallConstructionInputPin}, {NormalWalls, WallNormalInputPin}, {UpperWalls, WallUpperInputPin},
		{MatchedWalls, WallMatchedPin}, {PreLoopWalls, WallPreLoopPin}, {FinalWalls, WallFinalPin}};
	bool bDiagnosticEdges = true;
	for (const TPair<UPCGNode*, FName>& Diagnostic : WallDiagnosticOutputs)
		bDiagnosticEdges &= Diagnostic.Key && Edge(Diagnostic.Key,
			Diagnostic.Key == ConstructionInput ? TEXT("Wall") : TEXT("Out"), MeshGraph->GetOutputNode(), Diagnostic.Value)
			&& Edge(Materials, Diagnostic.Value, Graph->GetOutputNode(), Diagnostic.Value);
	const bool bDoorDiagnosticEdges = ConstructionInput
		&& Edge(ConstructionInput, TEXT("Wall - Door"), MeshGraph->GetOutputNode(), WallDoorConstructionInputPin)
		&& Edge(Materials, WallDoorConstructionInputPin, Graph->GetOutputNode(), WallDoorConstructionInputPin);
	if (!MeshGraph->HasAnyFlags(RF_Transient) || !ValidateZeroHeightWallExtensionContract(MeshGraph, 1.0, 1.0, Error)
		|| !bDiagnosticEdges || !bDoorDiagnosticEdges)
	{ if (Error.IsEmpty()) Error = TEXT("Native wall provenance diagnostic outputs are missing from the retained transient graphs."); return false; }
	UPCGNode* ThemeMaterials = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_2"), Error);
	UPCGNode* TextureOverride = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_1"), Error);
	UPCGNode* Markers = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_4"), Error);
	if (!Edge(ShapeCall, TEXT("All Rooms"), ThemeMaterials, TEXT("All Rooms"))
		|| !TextureOverride || !Edge(ShapeCall, TEXT("Wall"), ThemeMaterials, TEXT("Wall"))
		|| !Edge(ShapeCall, TEXT("Wall - Door"), ThemeMaterials, TEXT("Wall - Door"))
		|| !Edge(ThemeMaterials, TEXT("Wall"), TextureOverride, TEXT("Wall"))
		|| !Edge(ThemeMaterials, TEXT("Wall - Door"), TextureOverride, TEXT("Wall - Door"))
		|| !Edge(ThemeMaterials, TEXT("Wall"), Graph->GetOutputNode(), WallThemeOutputPin)
		|| !Edge(ThemeMaterials, TEXT("Wall - Door"), Graph->GetOutputNode(), WallDoorThemeOutputPin)
		|| !Edge(TextureOverride, TEXT("Wall"), Graph->GetOutputNode(), WallTextureOutputPin)
		|| !Edge(TextureOverride, TEXT("Wall - Door"), Graph->GetOutputNode(), WallDoorTextureOutputPin)
		|| !ValidateExactFinalDoorMaterialProvenance(Graph, TextureOverride, Materials, Error)
		|| !ValidateExactRoomOwnerInsideThemeLoop(ThemeMaterials, Error)
		|| !ValidateExactStyleWallFallback(ThemeMaterials, Error)
		|| !EdgeToNode(ShapeCall, TEXT("Start Room"), Markers)
		|| !EdgeToNode(ShapeCall, TEXT("End Room"), Markers))
	{ if (Error.IsEmpty()) Error = TEXT("Native material/progression branches no longer preserve exact structural wall ownership."); return false; }
	const auto* Settings = CastChecked<UPCGCalystoDirectorRoomSettings>(Rooms->GetSettings());
	if (!Settings->Config.Validate(Error)) return false;
	UPCGNode* Placement = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_3"), Error);
	for (const TCHAR* Z : Zones)
		if (!Edge(Placement, Z, Graph->GetOutputNode(), *FString::Printf(TEXT("EF Native %s"), Z)))
		{ Error = TEXT("Director lost a native surface opportunity output."); return false; }
	UPCGNode* Decoration = Node(const_cast<UPCGGraph*>(Graph), TEXT("Subgraph_5"), Error);
	const auto* DecorationCall = Decoration ? Cast<UPCGSubgraphSettings>(Decoration->GetSettings()) : nullptr;
	UPCGGraph* DecorationGraph = DecorationCall ? DecorationCall->GetSubgraph() : nullptr;
	UPCGNode* LightPoints = DecorationGraph ? Node(DecorationGraph, TEXT("ToPoint_80"), Error) : nullptr;
	UPCGNode* LightSpawn = DecorationGraph ? Node(DecorationGraph, TEXT("SpawnActor_77"), Error) : nullptr;
	const auto* SpawnSettings = LightSpawn ? Cast<UPCGSpawnActorSettings>(LightSpawn->GetSettings()) : nullptr;
	if (!DecorationGraph || !DecorationGraph->HasAnyFlags(RF_Transient) || !SpawnSettings
		|| SpawnSettings->Option != EPCGSpawnActorOption::NoMerging || !SpawnSettings->bSpawnByAttribute
		|| SpawnSettings->SpawnAttribute != TEXT("Blueprint") || SpawnSettings->GenerationTrigger != EPCGSpawnActorGenerationTrigger::DoNotGenerate
		|| !SpawnSettings->PostSpawnFunctionNames.IsEmpty()
		|| !Edge(Placement, TEXT("Wall Light"), Decoration, TEXT("Wall Light"))
		|| !Edge(LightPoints, TEXT("Out"), LightSpawn, TEXT("In")) || LightSpawn->GetInputPin(TEXT("In"))->Edges.Num() != 1
		|| !Edge(LightPoints, TEXT("Out"), DecorationGraph->GetOutputNode(), WallLightRequestsPin)
		|| !Edge(LightSpawn, TEXT("Out"), DecorationGraph->GetOutputNode(), WallLightActorsPin)
		|| !Edge(Decoration, WallLightRequestsPin, Graph->GetOutputNode(), WallLightRequestsPin)
		|| !Edge(Decoration, WallLightActorsPin, Graph->GetOutputNode(), WallLightActorsPin))
	{ Error = TEXT("Native wall-light request/realization output contract is incomplete."); return false; }
	return true;
}

bool FEFCalystoNativeAdapter::ObserveLighting(FEFCalystoNativeResult& Result, FString& Error) const
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset();
	UPCGComponent* PCG = Impl->Component.Get(); AActor* Dungeon = Impl->Actor.Get();
	if (!PCG || !Dungeon || Impl->bCleanupRequested)
	{ Error = TEXT("Native lighting observation lost attempt ownership."); return false; }
	const auto& Output = PCG->GetGeneratedGraphOutput();
	const bool bAuthoredLights = Impl->LightEntryId.IsValid();
	const FSoftObjectPath ExpectedTorchClass(Impl->TorchClass.Get());
	const auto CheckBlueprint = [&](const UPCGBasePointData* Data, const PCGMetadataEntryKey Key)
	{
		return CheckWallLightBlueprint(Data->ConstMetadata(), Key, bAuthoredLights, ExpectedTorchClass, Error);
	};
	TArray<FTransform> Requested;
	int32 ObservedOpportunities = 0;
	for (const FPCGTaggedData& T : Output.GetInputsByPin(WallLightRequestsPin))
	{
		const auto* P = Cast<UPCGBasePointData>(T.Data);
		if (!P || P->GetNumPoints() > 256 - ObservedOpportunities)
		{ Error = TEXT("Native wall-light request capacity exceeds 256 tracked actors."); return false; }
		ObservedOpportunities += P->GetNumPoints();
		const FConstPCGPointValueRanges Ranges(P);
		for (int32 I = 0; I < P->GetNumPoints(); ++I)
		{
			const FPCGPoint Point = Ranges.GetPoint(I);
			if (!CheckBlueprint(P, Point.MetadataEntry)) return false;
			// An explicitly empty catalog leaves native surface opportunities with
			// default transforms and no Blueprint. They are not selected requests.
			// Still inspect the spawn output below: any realized actor is an error.
			if (!bAuthoredLights) continue;
			const FTransform Tm = Point.Transform;
			if (Tm.ContainsNaN() || !Tm.GetRotation().IsNormalized() || Tm.GetScale3D().GetMin() <= 0)
			{
				Error = FString::Printf(TEXT("Native wall-light request contains an invalid transform: point=%d/%d location=%s rotation=%s scale=%s normalized=%s."),
					I, P->GetNumPoints(), *Tm.GetLocation().ToString(), *Tm.GetRotation().ToString(), *Tm.GetScale3D().ToString(),
					Tm.GetRotation().IsNormalized() ? TEXT("true") : TEXT("false"));
				return false;
			}
			Requested.Add(Tm);
		}
	}
	Result.RequestedWallLightCount = Requested.Num();
	TSet<AActor*> Managed;
	bool bManagedLightsValid = true;
	PCG->ForEachManagedResource([&](UPCGManagedResource* Resource)
	{
		if (!bManagedLightsValid) return;
		if (auto* Actors = Cast<UPCGManagedActors>(Resource)) for (const auto& Ref : Actors->GetConstGeneratedActors())
			if (AActor* A = Ref.Get(); IsValid(A))
			{
				bool bNativeTorch = false;
				if (!ValidateManagedWallLightClass(A->GetClass(), bAuthoredLights, bNativeTorch, Error))
				{ bManagedLightsValid = false; return; }
				if (bNativeTorch) Managed.Add(A);
			}
	});
	if (!bManagedLightsValid) return false;
	TSet<AActor*> Seen;
	TArray<FEFCalystoNativeLightBinding> Bindings;
	TArray<bool> Matched; Matched.Init(false, Requested.Num());
	int32 Processed = 0, ObservedOutputPoints = 0;
	for (const FPCGTaggedData& T : Output.GetInputsByPin(WallLightActorsPin))
	{
		const auto* P = Cast<UPCGBasePointData>(T.Data);
		if (!P || P->GetNumPoints() > 256 - ObservedOutputPoints)
		{ Error = TEXT("Native wall-light realization capacity is invalid."); return false; }
		ObservedOutputPoints += P->GetNumPoints();
		const UPCGMetadata* Metadata = P->ConstMetadata();
		const FPCGMetadataAttribute<FSoftObjectPath>* References = nullptr;
		if (!ReadWallLightActorReferences(Metadata, bAuthoredLights && P->GetNumPoints() > 0, References, Error)) return false;
		const FConstPCGPointValueRanges Ranges(P);
		for (int32 I = 0; I < P->GetNumPoints(); ++I)
		{
			const FPCGPoint Point = Ranges.GetPoint(I);
			if (!CheckBlueprint(P, Point.MetadataEntry)) return false;
			if (!bAuthoredLights)
			{
				if (References && !References->GetValueFromItemKey(Point.MetadataEntry).IsNull())
				{ Error = TEXT("An explicitly empty wall-light catalog realized an unexpected actor."); return false; }
				continue;
			}
			++Processed;
			AActor* Torch = Cast<AActor>(References->GetValueFromItemKey(Point.MetadataEntry).ResolveObject());
			if (!IsValid(Torch) || Torch->GetClass() != Impl->TorchClass.Get() || !Managed.Contains(Torch)
				|| Seen.Contains(Torch) || Torch->GetWorld() != Dungeon->GetWorld()
				|| !Torch->GetActorTransform().Equals(Point.Transform, .01))
			{ Error = TEXT("A selected native wall-light actor is missing, duplicated, foreign or spatially substituted."); return false; }
			int32 Match = INDEX_NONE;
			for (int32 R = 0; R < Requested.Num(); ++R) if (!Matched[R] && Requested[R].Equals(Point.Transform, .01)) { Match = R; break; }
			if (Match == INDEX_NONE) { Error = TEXT("Native wall light has no corresponding frozen spawn request."); return false; }
			Matched[Match] = true; Seen.Add(Torch);
			TArray<UPointLightComponent*> Lights; Torch->GetComponents(Lights);
			TArray<UPCGComponent*> NestedPCG; Torch->GetComponents(NestedPCG);
			FObjectProperty* LightProperty = FindFProperty<FObjectProperty>(Torch->GetClass(), TEXT("PointLight"));
			FObjectProperty* EffectProperty = FindFProperty<FObjectProperty>(Torch->GetClass(), TEXT("NS_LowPolyTorch"));
			UObject* Effect = EffectProperty ? EffectProperty->GetObjectPropertyValue_InContainer(Torch) : nullptr;
			FObjectProperty* EffectAsset = Effect ? FindFProperty<FObjectProperty>(Effect->GetClass(), TEXT("Asset")) : nullptr;
			UObject* EffectSystem = EffectAsset ? EffectAsset->GetObjectPropertyValue_InContainer(Effect) : nullptr;
			if (Lights.Num() != 1 || !NestedPCG.IsEmpty() || !LightProperty
				|| LightProperty->GetObjectPropertyValue_InContainer(Torch) != Lights[0] || !EffectSystem
				|| EffectSystem->GetPathName() != TEXT("/Game/Calysto/Dungeon/Particle/FXS_LowPolyTorch.FXS_LowPolyTorch"))
			{ Error = TEXT("Native wall torch no longer matches its point-light and native particle-effect contract."); return false; }
			const FVector PLocal = Impl->Rooms.DungeonTransform.InverseTransformPosition(Point.Transform.GetLocation());
			const FQuat Q = Point.Transform.GetRotation();
			const FString Identity = FString::Printf(TEXT("%s|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f,%.6f"),
				*Impl->LightEntryId.ToString(EGuidFormats::Digits), PLocal.X, PLocal.Y, PLocal.Z, Q.X, Q.Y, Q.Z, Q.W);
			FGuid Id; FGuid::ParseExact(FMD5::HashAnsiString(*Identity), EGuidFormats::Digits, Id);
			Bindings.Add({Id, Torch, Lights[0]}); Result.NativeWallLights.Add(Torch);
		}
	}
	if (Processed != Requested.Num() || Seen.Num() != Managed.Num())
	{ Error = TEXT("Every native wall-light request must realize exactly one owned actor."); return false; }
	if (!Impl->LightingController.IsValid())
	{
		UEFCalystoFloorLightingComponent* Controller = NewObject<UEFCalystoFloorLightingComponent>(Dungeon, NAME_None, RF_Transient);
		Dungeon->AddInstanceComponent(Controller); Controller->RegisterComponent();
		Impl->LightingController.Reset(Controller);
		if (!Controller->Initialize(Impl->Rooms.RandomKey(), Impl->Lighting, Bindings, Error)) return false;
	}
	if (Impl->LightingController->GetBoundLightCount() != Bindings.Num() || !VerifyLighting(Error)) return false;
	Result.bLightingVerified = true; return true;
}

bool FEFCalystoNativeAdapter::VerifyLighting(FString& Error) const
{
	using namespace EFCalystoNativeAdapterPrivate;
	Error.Reset(); AActor* A = Impl->Actor.Get();
	if (!A || Impl->bCleanupRequested || !Impl->LightingController.IsValid())
	{ Error = TEXT("Native lighting verification requires its current attempt owner."); return false; }
	FActorSchema Schema;
	if (!ResolveActorSchema(A, Schema, Error)) return false;
	if (!FMath::IsNearlyEqual(ReadFloating(Schema.WallLightHeight, A), Impl->Lighting.WallLightHeightCm, .001)
		|| Schema.WallLightTileDistance->GetPropertyValue_InContainer(A) != Impl->Lighting.WallLightTileDistance)
	{ Error = TEXT("Native light height or tile spacing differs from the frozen floor selection."); return false; }
	return Impl->LightingController->Verify(Error);
}

FPCGTaskId FEFCalystoNativeAdapter::GenerateOnce(FString& Error)
{
	Error.Reset(); UPCGComponent* C = Impl->Component.Get();
	if (!IsInGameThread() || Impl->GenerateRequests != 0 || Impl->bCleanupRequested || !C
		|| C->IsGenerating() || C->IsCleaningUp() || C->bGenerated || C->GetGraph() != Impl->Graph.Get())
	{ Error = TEXT("Native generation request must be unique and use its exact prepared graph."); return InvalidPCGTaskId; }
	if (FindIdleRuntimeComponent(Impl->Actor.Get(), Error) != C)
	{
		if (Error.IsEmpty()) Error = TEXT("Native runtime component identity changed after preparation.");
		return InvalidPCGTaskId;
	}
	++Impl->GenerateRequests;
	const FPCGTaskId Task = C->GenerateLocalGetTaskId(true);
	if (Task == InvalidPCGTaskId) Error = TEXT("Native generation scheduling failed; the attempt remains consumed.");
	return Task;
}

EEFCalystoNativeStatus FEFCalystoNativeAdapter::Observe(FEFCalystoNativeResult& Result, FString& Error) const
{
	EEFCalystoNativeFailureKind IgnoredFailure = EEFCalystoNativeFailureKind::Configuration;
	return Observe(Result, Error, IgnoredFailure);
}

EEFCalystoNativeStatus FEFCalystoNativeAdapter::Observe(FEFCalystoNativeResult& Result, FString& Error,
	EEFCalystoNativeFailureKind& OutFailure) const
{
	Error.Reset(); Result = {}; OutFailure = EEFCalystoNativeFailureKind::Configuration;
	const UPCGComponent* C = Impl->Component.Get(); AActor* A = Impl->Actor.Get();
	if (!IsInGameThread() || !C || !A || Impl->bCleanupRequested || Impl->GenerateRequests != 1)
	{
		OutFailure = (!C || !A) ? EEFCalystoNativeFailureKind::Resource : EEFCalystoNativeFailureKind::Configuration;
		Error = TEXT("Native observation lost its attempt ownership.");
		return EEFCalystoNativeStatus::Failed;
	}
	Result.GenerateRequests = Impl->GenerateRequests;
	if (C->IsGenerating() || !C->bGenerated) return EEFCalystoNativeStatus::Pending;
	EEFCalystoNativeRoomReadFailure RoomFailure = EEFCalystoNativeRoomReadFailure::Configuration;
	if (!FEFCalystoNativeRoomReader::Read(C->GetGeneratedGraphOutput(), Impl->Rooms, Result.Rooms, Error, &RoomFailure))
	{
		OutFailure = RoomFailure == EEFCalystoNativeRoomReadFailure::Spatial
			? EEFCalystoNativeFailureKind::Spatial : EEFCalystoNativeFailureKind::Configuration;
		return EEFCalystoNativeStatus::Failed;
	}
	const bool bZeroHeightCapability = CanContainZeroHeightWallExtensions(Error);
	if (!Error.IsEmpty() || !ReadFinalWallSurfaces(C->GetGeneratedGraphOutput(), bZeroHeightCapability,
		Result.NativeFinalWallSurfaces, Result.NativeZeroHeightWallExtensions, Error)) return EEFCalystoNativeStatus::Failed;
	Result.bGraphCompleted = true;
	if (!ObserveLighting(Result, Error)) return EEFCalystoNativeStatus::Failed;
	TArray<UInstancedStaticMeshComponent*> Meshes;
	Impl->Component->ForEachManagedResource([&](UPCGManagedResource* Resource)
	{
		if (const auto* Actors = Cast<UPCGManagedActors>(Resource)) for (const auto& Ref : Actors->GetConstGeneratedActors())
		{
			AActor* Marker = Ref.Get();
			if (!IsValid(Marker)) continue;
			if (Marker->IsA(Impl->StartClass.Get())) Result.NativeStartMarkers.AddUnique(Marker);
			if (Marker->IsA(Impl->EndClass.Get())) Result.NativeEndMarkers.AddUnique(Marker);
		}
		if (const auto* ISM = Cast<UPCGManagedISMComponent>(Resource)) if (auto* M = ISM->GetComponent()) Meshes.AddUnique(M);
	});
	for (const auto* M : Meshes)
	{
		if (!M || !M->GetStaticMesh() || M->GetInstanceCount() == 0 || !M->IsRegistered()) continue;
		// Exact authored structural references define roles; asset labels never decide behavior.
		if (Impl->FloorMeshes.Contains(M->GetStaticMesh())) Result.FloorInstances += M->GetInstanceCount();
		if (Impl->WallMeshes.Contains(M->GetStaticMesh())) Result.WallInstances += M->GetInstanceCount();
		if (Impl->RoofMeshes.Contains(M->GetStaticMesh())) Result.RoofInstances += M->GetInstanceCount();
		if (M->GetCollisionEnabled() != ECollisionEnabled::NoCollision) Result.StructuralBounds += M->Bounds.GetBox();
	}
	const auto& Data = C->GetGeneratedGraphOutput();
	int32 ProcessedSurfacePoints = 0;
	TMap<int64, int32> SurfaceIdentities;
	for (int32 Z = 0; Z < UE_ARRAY_COUNT(EFCalystoNativeAdapterPrivate::Zones); ++Z)
	{
		const FName Pin(*FString::Printf(TEXT("EF Native %s"), EFCalystoNativeAdapterPrivate::Zones[Z]));
		for (const FPCGTaggedData& T : Data.GetInputsByPin(Pin))
		{
			const auto* P = Cast<UPCGBasePointData>(T.Data);
			if (!P || P->GetNumPoints() > 65536 - ProcessedSurfacePoints) { Error = TEXT("Native surface proposal work exceeded its bound."); return EEFCalystoNativeStatus::Failed; }
			ProcessedSurfacePoints += P->GetNumPoints();
			const FConstPCGPointValueRanges Ranges(P);
			for (int32 N = 0; N < P->GetNumPoints(); ++N)
			{
				const FPCGPoint Point = Ranges.GetPoint(N);
				const FVector Local = Impl->Rooms.DungeonTransform.InverseTransformPosition(Point.Transform.GetLocation());
				const FEFCalystoNativeRoom* Owner = nullptr; bool bAmbiguous = false;
				for (const auto& R : Result.Rooms) if (R.LocalBounds.ExpandBy(2.0).IsInsideOrOn(Local))
				{ if (Owner) bAmbiguous = true; else Owner = &R; }
				// Corridor and ambiguous shared-boundary opportunities are ineligible before any selection.
				if (!Owner || bAmbiguous) continue;
				FEFCalystoNativeSurface S; S.RoomId = Owner->RoomId; S.Zone = EEFCalystoPlacementZone(Z);
				S.Transform = Point.Transform; S.Bounds = Point.GetLocalBounds().TransformBy(Point.Transform);
				S.Normal = Point.Transform.GetRotation().RotateVector(Z == 0 ? FVector::UpVector : Z == 7 ? -FVector::UpVector : FVector::ForwardVector);
				const int64 GeometricIndex = FMath::RoundToInt64(Local.X) * 73856093ll ^ FMath::RoundToInt64(Local.Y) * 19349663ll ^ FMath::RoundToInt64(Local.Z) * 83492791ll ^ int64(Z);
				S.OpportunityId = int64(FEFCalystoDirectorProbability::Hash(Impl->Rooms.RandomKey(), EEFCalystoRandomDomain::Placement,
					FEFCalystoDirectorProbability::RoomIdentity(S.RoomId), GeometricIndex) & 0x7fffffffffffffffull);
				if (const int32* Existing = SurfaceIdentities.Find(S.OpportunityId))
				{
					const auto& Prior = Result.Surfaces[*Existing];
					if (Prior.RoomId != S.RoomId || Prior.Zone != S.Zone || !Prior.Transform.Equals(S.Transform) || Prior.Bounds != S.Bounds)
					{ Error = TEXT("Native surface opportunity identity collision."); return EEFCalystoNativeStatus::Failed; }
					continue;
				}
				SurfaceIdentities.Add(S.OpportunityId, Result.Surfaces.Num());
				Result.Surfaces.Add(MoveTemp(S));
			}
		}
	}
	Result.Surfaces.Sort([](const auto& L, const auto& R) { return L.OpportunityId < R.OpportunityId; });
	return EEFCalystoNativeStatus::Complete;
}

void FEFCalystoNativeAdapter::BeginCleanup()
{
	if (Impl->bCleanupRequested) return;
	Impl->bCleanupRequested = true;
	if (Impl->LightingController.IsValid())
	{
		Impl->LightingController->Release();
		Impl->LightingController->DestroyComponent();
		Impl->LightingController.Reset();
	}
	if (UPCGComponent* C = Impl->Component.Get()) { C->CancelGeneration(); C->CleanupLocal(true); }
}
bool FEFCalystoNativeAdapter::IsCleanupComplete() const
{
	const UPCGComponent* C = Impl->Component.Get();
	if (!Impl->bCleanupRequested || (C && (C->IsGenerating() || C->IsCleaningUp() || C->bGenerated))) return false;
	if (C && C->GetGraph() == Impl->Graph.Get()) Impl->Component->SetGraphLocal(Impl->OriginalGraph.Get());
	if (AActor* A = Impl->Actor.Get()) for (const auto& Entry : Impl->OriginalData)
		Entry.Key->SetObjectPropertyValue_InContainer(A, Entry.Value.Get());
	return true;
}
UPCGComponent* FEFCalystoNativeAdapter::GetComponent() const { return Impl->Component.Get(); }
UPCGGraph* FEFCalystoNativeAdapter::GetGraph() const { return Impl->Graph.Get(); }
const FEFCalystoNativeRoomConfig& FEFCalystoNativeAdapter::GetRoomConfig() const { return Impl->Rooms; }

AActor* FEFCalystoNativeAdapter::GetDungeonActor() const { return Impl->Actor.Get(); }

UPCGComponent* FEFCalystoNativeAdapter::FindIdleRuntimeComponent(AActor* Actor, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !IsValid(Actor) || Actor->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{ Error = TEXT("Native PCG selection requires a live actor instance on the game thread."); return nullptr; }
	TArray<UPCGComponent*> Components;
	Actor->GetComponents(Components);
	Components.Sort([](const UPCGComponent& A, const UPCGComponent& B) { return A.GetPathName() < B.GetPathName(); });
	UPCGComponent* Runtime = nullptr;
	int32 RuntimeCount = 0;
	bool bAllIdleOnDemand = true;
	FString Details;
	for (UPCGComponent* Component : Components)
	{
		const bool bEditorOnly = Component->IsEditorOnly();
		if (!bEditorOnly) { Runtime = Component; ++RuntimeCount; }
		// Native PCG_Editor is retained in PIE, but must never auto-generate or carry
		// generated work into this request. Only the runtime component is invoked.
		bAllIdleOnDemand &= Component->GenerationTrigger == EPCGComponentGenerationTrigger::GenerateOnDemand
			&& !Component->IsGenerating() && !Component->IsCleaningUp() && !Component->bGenerated;
		Details += FString::Printf(TEXT(" [%s editorOnly=%d activated=%d trigger=%s(%d) registered=%d generating=%d cleaning=%d generated=%d graph=%s]"),
			*Component->GetPathName(), bEditorOnly, Component->bActivated,
			*StaticEnum<EPCGComponentGenerationTrigger>()->GetNameStringByValue(int64(Component->GenerationTrigger)),
			int32(Component->GenerationTrigger), Component->IsRegistered(), Component->IsGenerating(),
			Component->IsCleaningUp(), Component->bGenerated, *GetPathNameSafe(Component->GetGraph()));
	}
	if (RuntimeCount != 1 || !bAllIdleOnDemand || !Runtime || !Runtime->bActivated)
	{
		Error = FString::Printf(TEXT("Native preparation requires exactly one active, idle Generate On Demand runtime PCG component and idle on-demand editor-only auxiliaries. actor=%s components=%d runtime=%d editorOnly=%d%s"),
			*Actor->GetPathName(), Components.Num(), RuntimeCount, Components.Num() - RuntimeCount, *Details);
		return nullptr;
	}
	return Runtime;
}

AActor* FEFCalystoNativeAdapter::SpawnGenerator(UWorld* World, FString& Error)
{
	Error.Reset();
	UClass* Class = FindObject<UClass>(nullptr, TEXT("/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"));
	if (!IsInGameThread() || !World || !World->IsGameWorld() || !Class || !Class->IsChildOf(AActor::StaticClass()))
	{ Error = TEXT("Native generator requires a game world and its preloaded exact class."); return nullptr; }
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(Class, FTransform::Identity, Parameters);
	if (!Actor) Error = TEXT("Native Calysto generator actor could not be spawned.");
	return Actor;
}

UPCGGraph* FEFCalystoNativeAdapter::ComposeGraph(UPCGGraph* Source, UObject* Owner, const FEFCalystoNativeRoomConfig& Config, FString& Error)
{
	if (!IsInGameThread() || !IsValid(Owner) || !Config.Validate(Error)) return nullptr;
	return EFCalystoNativeAdapterPrivate::BuildGraph(Source, Owner, Config, Error);
}
TArray<FSoftObjectPath> FEFCalystoNativeAdapter::GetSharedDependencies()
{
	return {
		FSoftObjectPath(EFCalystoNativeAdapterPrivate::MasterPath),
		FSoftObjectPath(EFCalystoNativeAdapterPrivate::ShapePath),
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C")),
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes.PDA_RoomMeshes_C")),
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh")),
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps.PCG_AddRamps")),
		FSoftObjectPath(TEXT("/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple.PCG_ObjectTransformSimple")),
		FSoftObjectPath(TEXT("/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon")),
		FSoftObjectPath(TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe")),
		FSoftObjectPath(TEXT("/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe"))
	};
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeWallLightMetadataTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.WallLightMetadataAndEmptyCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeWallLightMetadataTest::RunTest(const FString&)
{
	using namespace EFCalystoNativeAdapterPrivate;
	{
		auto* FixtureClass = NewObject<UBlueprintGeneratedClass>();
		FixtureClass->SetSuperStruct(AActor::StaticClass());
		FixtureClass->SimpleConstructionScript = NewObject<USimpleConstructionScript>(FixtureClass);
		auto* Node = NewObject<USCS_Node>(FixtureClass->SimpleConstructionScript);
		Node->SetVariableName(TEXT("PointLight"), false);
		Node->ComponentClass = UPointLightComponent::StaticClass();
		auto* Template = NewObject<UPointLightComponent>(FixtureClass, TEXT("PointLight_GEN_VARIABLE"));
		Node->ComponentTemplate = Template;
		FixtureClass->SimpleConstructionScript->AddNode(Node);
		TestNull(TEXT("SCS light does not belong to the unrelated ComponentTemplates array"), FixtureClass->FindComponentTemplateByName(TEXT("PointLight_GEN_VARIABLE")));
		TestEqual(TEXT("Native torch preflight resolves the actual SCS light template"), ResolveNativeTorchTemplate(FixtureClass), Template);
		Node->SetVariableName(TEXT("OtherPointLight"), false);
		TestNull(TEXT("An unrelated light cannot substitute for the required native component"), ResolveNativeTorchTemplate(FixtureClass));
		TestNull(TEXT("A missing resident class fails template resolution"), ResolveNativeTorchTemplate(nullptr));
	}
	const FSoftObjectPath Expected(TEXT("/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch.BP_WallTorch_C"));
	FString Error;
	const auto CheckEncoding = [&]<typename T>(const T& AuthoredValue)
	{
		UPCGPointData* Points = NewObject<UPCGPointData>();
		UPCGMetadata* Metadata = Points->MutableMetadata();
		auto* Blueprint = Metadata->CreateAttribute<T>(TEXT("Blueprint"), T(), false, false);
		if (!TestNotNull(TEXT("Native PCG metadata encoding is constructible"), Blueprint)) return;
		const PCGMetadataEntryKey Key = Metadata->AddEntry();
		TestTrue(TEXT("Empty Blueprint opportunity is valid with an empty catalog"), CheckWallLightBlueprint(Metadata, Key, false, {}, Error));
		const FPCGMetadataAttribute<FSoftObjectPath>* References = nullptr;
		TestTrue(TEXT("Empty Blueprint needs no ActorReference attribute"), ReadWallLightActorReferences(Metadata, false, References, Error));
		TestNull(TEXT("No actor reference is invented for an empty output"), References);
		TestFalse(TEXT("Selected output cannot omit ActorReference"), ReadWallLightActorReferences(Metadata, true, References, Error));
		TestFalse(TEXT("A selected light cannot have an empty Blueprint"), CheckWallLightBlueprint(Metadata, Key, true, Expected, Error));
		Blueprint->SetValue(Key, AuthoredValue);
		TestTrue(TEXT("Native class/object/string encoding resolves the exact authored path"), CheckWallLightBlueprint(Metadata, Key, true, Expected, Error));
		TestFalse(TEXT("An empty catalog cannot hide a nonempty Blueprint"), CheckWallLightBlueprint(Metadata, Key, false, {}, Error));
		TestFalse(TEXT("A selected path cannot replace a missing resident class"), CheckWallLightBlueprint(Metadata, Key, true, {}, Error));
	};
	CheckEncoding(FSoftClassPath(Expected.ToString()));
	CheckEncoding(Expected);
	CheckEncoding(Expected.ToString());
	TestTrue(TEXT("Absent selection metadata is legitimate when no actor is expected"), CheckWallLightBlueprint(nullptr, PCGInvalidEntryKey, false, {}, Error));
	const auto CheckMalformedReference = [&]<typename T>(const T& Value)
	{
		UPCGPointData* Points = NewObject<UPCGPointData>();
		UPCGMetadata* Metadata = Points->MutableMetadata();
		TestNotNull(TEXT("Malformed fixture retains its actual PCG type"), Metadata->CreateAttribute<T>(PCGPointDataConstants::ActorReferenceAttribute, Value, false, false));
		const FPCGMetadataAttribute<FSoftObjectPath>* References = nullptr;
		TestFalse(TEXT("An empty catalog rejects an existing ActorReference of the wrong type"), ReadWallLightActorReferences(Metadata, false, References, Error));
		TestTrue(TEXT("Malformed reference identifies the required actual UE type"), Error.Contains(TEXT("FSoftObjectPath")));
	};
	CheckMalformedReference(FString(TEXT("/Engine/Transient.UnexpectedActor")));
	CheckMalformedReference(FSoftClassPath(Expected.ToString()));
	CheckMalformedReference(int32(1));
	UPCGPointData* Points = NewObject<UPCGPointData>();
	auto* ValidReference = Points->MutableMetadata()->CreateAttribute<FSoftObjectPath>(PCGPointDataConstants::ActorReferenceAttribute, {}, false, false);
	const FPCGMetadataAttribute<FSoftObjectPath>* References = nullptr;
	TestTrue(TEXT("The actual SpawnActor reference type remains supported"), ReadWallLightActorReferences(Points->ConstMetadata(), true, References, Error));
	TestTrue(TEXT("The returned reference is the actual typed metadata attribute"), References && References == ValidReference);
	// Use resident Engine class ancestry as an isolated classifier fixture. Production
	// always supplies the exact vendor path; no vendor class or actor needs loading.
	const FString FixtureClassPath = ALight::StaticClass()->GetPathName();
	bool bNativeTorch = false;
	TestFalse(TEXT("An empty catalog rejects a managed torch absent from point output"), ValidateManagedWallLightClass(ALight::StaticClass(), false, bNativeTorch, Error, FixtureClassPath));
	TestTrue(TEXT("Managed detection does not require a selected TorchClass"), bNativeTorch);
	TestFalse(TEXT("A native torch subclass cannot evade empty-catalog ownership checks"), ValidateManagedWallLightClass(APointLight::StaticClass(), false, bNativeTorch, Error, FixtureClassPath));
	TestTrue(TEXT("Resident superclass identity detects the subclass"), bNativeTorch);
	TestTrue(TEXT("Other native actors are not rejected as torches"), ValidateManagedWallLightClass(AActor::StaticClass(), false, bNativeTorch, Error, FixtureClassPath));
	TestFalse(TEXT("Unrelated managed classes are excluded from light ownership"), bNativeTorch);
	TestTrue(TEXT("Selected native-light ownership remains eligible for exact realization checks"), ValidateManagedWallLightClass(APointLight::StaticClass(), true, bNativeTorch, Error, FixtureClassPath));
	TestTrue(TEXT("A selected native-light subclass remains visible to the later exact-class rejection"), bNativeTorch);
	return true;
}
#endif

#include "Calysto/EFCalystoAssignRoomThemeV6.h"

#include "Calysto/EFCalystoDungeonDirectorPolicyV6.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointData.h"
#include "Materials/MaterialInstance.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "PCGContext.h"
#include "PCGPoint.h"
#include "Utils/PCGLogErrors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EFCalystoAssignRoomThemeV6)

#define LOCTEXT_NAMESPACE "EFCalystoAssignRoomThemeV6"

const FName FEFCalystoRoomThemePinsV6::MainRooms(TEXT("Main Rooms"));
const FName FEFCalystoRoomThemePinsV6::SideRooms(TEXT("Side Rooms"));
const FName FEFCalystoRoomThemePinsV6::StartRooms(TEXT("Start Rooms"));
const FName FEFCalystoRoomThemePinsV6::EndRooms(TEXT("End Rooms"));
const FName FEFCalystoRoomThemePinsV6::CriticalRooms(TEXT("Critical Rooms"));
const FName FEFCalystoRoomThemePinsV6::ProgressionRooms(TEXT("Progression Rooms"));
const FName FEFCalystoRoomThemePinsV6::ThemedRooms(TEXT("Themed Rooms"));
const FName FEFCalystoRoomThemePinsV6::UnthemedRooms(TEXT("Unthemed Rooms"));
const FName FEFCalystoRoomThemePinsV6::RoomContexts(TEXT("Room Contexts"));

// UE 5.8 PCG metadata names may contain only alphanumeric characters, spaces,
// underscores, hyphens, and slashes. Keep this schema HLSL-compatible as well.
const FName FEFCalystoRoomThemeMetadataV6::RoomId(TEXT("EF_RoomId"));
const FName FEFCalystoRoomThemeMetadataV6::ThemeId(TEXT("EF_ThemeId"));
const FName FEFCalystoRoomThemeMetadataV6::StyleId(TEXT("EF_StyleId"));
const FName FEFCalystoRoomThemeMetadataV6::RoomFlags(TEXT("EF_RoomFlags"));
const FName FEFCalystoRoomThemeMetadataV6::TopologyKind(TEXT("EF_TopologyKind"));
const FName FEFCalystoRoomThemeMetadataV6::CollisionOrdinal(TEXT("EF_CollisionOrdinal"));
const FName FEFCalystoRoomThemeMetadataV6::CatalogId(TEXT("EF_CatalogId"));
const FName FEFCalystoRoomThemeMetadataV6::CatalogColor(TEXT("EF_CatalogColor"));
const FName FEFCalystoRoomThemeMetadataV6::EffectiveFloorMaterial(TEXT("EF_EffectiveFloorMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::EffectiveWallMaterial(TEXT("EF_EffectiveWallMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::EffectiveRoofMaterial(TEXT("EF_EffectiveRoofMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::FloorMaterialAuthority(TEXT("EF_FloorMaterialAuthority"));
const FName FEFCalystoRoomThemeMetadataV6::WallMaterialAuthority(TEXT("EF_WallMaterialAuthority"));
const FName FEFCalystoRoomThemeMetadataV6::RoofMaterialAuthority(TEXT("EF_RoofMaterialAuthority"));
const FName FEFCalystoRoomThemeMetadataV6::VendorRoomType(TEXT("RoomType"));
const FName FEFCalystoRoomThemeMetadataV6::VendorOverrideFloorMaterial(TEXT("ThemeOverrideFloorMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::VendorFloorMaterial(TEXT("FloorMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::VendorOverrideWallMaterial(TEXT("ThemeOverrideWallMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::VendorWallMaterial(TEXT("WallMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::VendorOverrideRoofMaterial(TEXT("ThemeOverrideRoofMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::VendorRoofMaterial(TEXT("RoofMaterial"));
const FName FEFCalystoRoomThemeMetadataV6::PresenceDraw(TEXT("EF_PresenceDraw"));
const FName FEFCalystoRoomThemeMetadataV6::ThemeDraw(TEXT("EF_ThemeDraw"));

bool FEFCalystoRoomThemeMetadataV6::ValidateSchema(FString& OutError)
{
	OutError.Reset();
	const FName Names[] = {
		RoomId,
		ThemeId,
		StyleId,
		RoomFlags,
		TopologyKind,
		CollisionOrdinal,
		CatalogId,
		CatalogColor,
		EffectiveFloorMaterial,
		EffectiveWallMaterial,
		EffectiveRoofMaterial,
		FloorMaterialAuthority,
		WallMaterialAuthority,
		RoofMaterialAuthority,
		VendorRoomType,
		VendorOverrideFloorMaterial,
		VendorFloorMaterial,
		VendorOverrideWallMaterial,
		VendorWallMaterial,
		VendorOverrideRoofMaterial,
		VendorRoofMaterial,
		PresenceDraw,
		ThemeDraw
	};
	TSet<FName> UniqueNames;
	for (const FName Name : Names)
	{
		if (Name.IsNone() || !FPCGMetadataAttributeBase::IsValidName(Name))
		{
			OutError = FString::Printf(
				TEXT("PCG metadata attribute name '%s' is invalid in this engine version."),
				*Name.ToString());
			return false;
		}
		if (UniqueNames.Contains(Name))
		{
			OutError = FString::Printf(
				TEXT("PCG metadata attribute name '%s' is duplicated."),
				*Name.ToString());
			return false;
		}
		UniqueNames.Add(Name);
	}
	return true;
}

namespace EFCalystoAssignRoomThemeV6Private
{
	static const FName NoThemeId(TEXT("NoTheme"));

	class FStableHasher
	{
	public:
		FStableHasher()
			: A(1469598103934665603ull)
			, B(1099511628211ull ^ 0x9E3779B97F4A7C15ull)
		{
		}

		void AddByte(const uint8 Value)
		{
			A = (A ^ Value) * 1099511628211ull;
			B = (B ^ static_cast<uint8>(Value + 0x9d)) * 14029467366897019727ull;
		}

		void AddInt64(const int64 Value)
		{
			const uint64 UnsignedValue = static_cast<uint64>(Value);
			for (uint32 Shift = 0; Shift < 64; Shift += 8)
			{
				AddByte(static_cast<uint8>((UnsignedValue >> Shift) & 0xffull));
			}
		}

		void AddString(const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			AddInt64(Utf8.Length());
			for (int32 Index = 0; Index < Utf8.Length(); ++Index)
			{
				AddByte(static_cast<uint8>(Utf8.Get()[Index]));
			}
		}

		uint64 FinishA() const { return Avalanche(A ^ (B >> 1)); }
		uint64 FinishB() const { return Avalanche(B ^ (A << 1)); }

	private:
		static uint64 Avalanche(uint64 Value)
		{
			Value ^= Value >> 30;
			Value *= 0xBF58476D1CE4E5B9ull;
			Value ^= Value >> 27;
			Value *= 0x94D049BB133111EBull;
			Value ^= Value >> 31;
			return Value;
		}

		uint64 A;
		uint64 B;
	};

	static FString HashToString(const FStableHasher& Hasher)
	{
		return FString::Printf(
			TEXT("%016llX%016llX"),
			static_cast<unsigned long long>(Hasher.FinishA()),
			static_cast<unsigned long long>(Hasher.FinishB()));
	}

	static int64 Quantize(const double Value, const double Grid)
	{
		return static_cast<int64>(FMath::RoundToDouble(Value / Grid));
	}

	static FString BuildGeometryKey(const FBox& LocalBounds, const double Grid)
	{
		const FVector Center = LocalBounds.GetCenter();
		const FVector Extent = LocalBounds.GetExtent();
		return FString::Printf(
			TEXT("%lld|%lld|%lld|%lld|%lld|%lld"),
			Quantize(Center.X, Grid), Quantize(Center.Y, Grid), Quantize(Center.Z, Grid),
			Quantize(Extent.X, Grid), Quantize(Extent.Y, Grid), Quantize(Extent.Z, Grid));
	}

	static FString BuildExactGeometryKey(const FBox& LocalBounds)
	{
		const FVector Center = LocalBounds.GetCenter();
		const FVector Extent = LocalBounds.GetExtent();
		return FString::Printf(
			TEXT("%.9g|%.9g|%.9g|%.9g|%.9g|%.9g"),
			Center.X, Center.Y, Center.Z, Extent.X, Extent.Y, Extent.Z);
	}

	static bool IsProtectedFlags(const EEFCalystoPCGRoomFlagsV6 Flags)
	{
		const EEFCalystoPCGRoomFlagsV6 Protected = EEFCalystoPCGRoomFlagsV6::Start
			| EEFCalystoPCGRoomFlagsV6::End
			| EEFCalystoPCGRoomFlagsV6::Critical
			| EEFCalystoPCGRoomFlagsV6::Progression;
		return EnumHasAnyFlags(Flags, Protected);
	}

	static FName TopologyKindForFlags(const EEFCalystoPCGRoomFlagsV6 Flags)
	{
		if (EnumHasAnyFlags(Flags, EEFCalystoPCGRoomFlagsV6::Start)) return TEXT("Start");
		if (EnumHasAnyFlags(Flags, EEFCalystoPCGRoomFlagsV6::End)) return TEXT("End");
		if (EnumHasAnyFlags(Flags, EEFCalystoPCGRoomFlagsV6::Critical)) return TEXT("Critical");
		if (EnumHasAnyFlags(Flags, EEFCalystoPCGRoomFlagsV6::Progression)) return TEXT("Progression");
		if (EnumHasAnyFlags(Flags, EEFCalystoPCGRoomFlagsV6::MainPath)) return TEXT("MainPath");
		return TEXT("Ordinary");
	}

	static int32 SelectAliasIndex(
		const double Uniform,
		const TArray<float>& Probability,
		const TArray<int32>& Alias)
	{
		if (Probability.IsEmpty() || Probability.Num() != Alias.Num())
		{
			return INDEX_NONE;
		}
		const double Scaled = FMath::Clamp(Uniform, 0.0, 1.0 - UE_DOUBLE_SMALL_NUMBER)
			* Probability.Num();
		const int32 Column = FMath::Clamp(
			FMath::FloorToInt(Scaled), 0, Probability.Num() - 1);
		const double Fraction = Scaled - Column;
		const int32 Result = Fraction < static_cast<double>(Probability[Column])
			? Column
			: Alias[Column];
		return Probability.IsValidIndex(Result) ? Result : INDEX_NONE;
	}

	static FString CanonicalSourceKey(const FPCGTaggedData& TaggedData)
	{
		TArray<FString> Tags = TaggedData.Tags.Array();
		Tags.Sort([](const FString& Left, const FString& Right)
		{
			return Left.Compare(Right, ESearchCase::CaseSensitive) < 0;
		});
		return FString::Join(Tags, TEXT("|"));
	}

	static void ResolveMaterial(
		const TSoftObjectPtr<UMaterialInstance>& ThemeMaterial,
		const TSoftObjectPtr<UMaterialInstance>& StyleMaterial,
		FSoftObjectPath& OutPath,
		EEFCalystoMaterialAuthorityV6& OutAuthority)
	{
		if (!ThemeMaterial.IsNull())
		{
			OutPath = ThemeMaterial.ToSoftObjectPath();
			OutAuthority = EEFCalystoMaterialAuthorityV6::RoomTheme;
		}
		else if (!StyleMaterial.IsNull())
		{
			OutPath = StyleMaterial.ToSoftObjectPath();
			OutAuthority = EEFCalystoMaterialAuthorityV6::Style;
		}
		else
		{
			OutPath.Reset();
			OutAuthority = EEFCalystoMaterialAuthorityV6::None;
		}
	}

	struct FRoomRecord
	{
		int32 SourceIndex = INDEX_NONE;
		FPCGPoint Point;
		FString SourceKey;
		FString GeometryKey;
		FString ExactGeometryKey;
		FString RoomId;
		int64 StableRoomId = 0;
		FBox WorldBounds = FBox(EForceInit::ForceInit);
		FBox LocalBounds = FBox(EForceInit::ForceInit);
		FName TopologyKind = TEXT("Ordinary");
		int32 CollisionOrdinal = 0;
		EEFCalystoPCGRoomFlagsV6 Flags = EEFCalystoPCGRoomFlagsV6::None;
		int32 ThemeIndex = INDEX_NONE;
		double PresenceDraw = 0.0;
		double ThemeDraw = 0.0;
	};

	static bool CollectMarkerLocations(
		const FPCGContext* Context,
		const FName Pin,
		const int32 RequiredCount,
		TArray<FVector>& OutLocations,
		FString& OutError)
	{
		OutLocations.Reset();
		for (const FPCGTaggedData& TaggedData : Context->InputData.GetInputsByPin(Pin))
		{
			const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(TaggedData.Data);
			if (!PointData)
			{
				OutError = FString::Printf(TEXT("Pin '%s' contains non-point data."), *Pin.ToString());
				return false;
			}
			const FConstPCGPointValueRanges Ranges(PointData);
			for (int32 Index = 0; Index < PointData->GetNumPoints(); ++Index)
			{
				OutLocations.Add(Ranges.GetPoint(Index).Transform.GetLocation());
			}
		}

		if (RequiredCount >= 0 && OutLocations.Num() != RequiredCount)
		{
			OutError = FString::Printf(
				TEXT("Pin '%s' must contain exactly %d marker point(s); found %d."),
				*Pin.ToString(),
				RequiredCount,
				OutLocations.Num());
			return false;
		}
		return true;
	}

	struct FOutputAttributes
	{
		FPCGMetadataAttribute<FString>* RoomId = nullptr;
		FPCGMetadataAttribute<FName>* ThemeId = nullptr;
		FPCGMetadataAttribute<FName>* StyleId = nullptr;
		FPCGMetadataAttribute<int32>* RoomFlags = nullptr;
		FPCGMetadataAttribute<FName>* TopologyKind = nullptr;
		FPCGMetadataAttribute<int32>* CollisionOrdinal = nullptr;
		FPCGMetadataAttribute<FName>* CatalogId = nullptr;
		FPCGMetadataAttribute<FVector4>* CatalogColor = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* EffectiveFloor = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* EffectiveWall = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* EffectiveRoof = nullptr;
		FPCGMetadataAttribute<int32>* FloorAuthority = nullptr;
		FPCGMetadataAttribute<int32>* WallAuthority = nullptr;
		FPCGMetadataAttribute<int32>* RoofAuthority = nullptr;
		FPCGMetadataAttribute<double>* PresenceDraw = nullptr;
		FPCGMetadataAttribute<double>* ThemeDraw = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* RoomType = nullptr;
		FPCGMetadataAttribute<bool>* OverrideFloor = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* Floor = nullptr;
		FPCGMetadataAttribute<bool>* OverrideWall = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* Wall = nullptr;
		FPCGMetadataAttribute<bool>* OverrideRoof = nullptr;
		FPCGMetadataAttribute<FSoftObjectPath>* Roof = nullptr;

		bool IsComplete(const bool bVendorPayload, const bool bProbabilityDiagnostics) const
		{
			const bool bCoreComplete = RoomId && ThemeId && StyleId && RoomFlags
				&& TopologyKind && CollisionOrdinal && CatalogId && CatalogColor
				&& EffectiveFloor && EffectiveWall && EffectiveRoof
				&& FloorAuthority && WallAuthority && RoofAuthority;
			const bool bDiagnosticsComplete = !bProbabilityDiagnostics
				|| (PresenceDraw && ThemeDraw);
			const bool bVendorComplete = !bVendorPayload
				|| (RoomType && OverrideFloor && Floor && OverrideWall && Wall
					&& OverrideRoof && Roof);
			return bCoreComplete && bDiagnosticsComplete && bVendorComplete;
		}
	};

	static FOutputAttributes CreateAttributes(
		UPCGMetadata* Metadata,
		const bool bVendorPayload,
		const bool bProbabilityDiagnostics)
	{
		FOutputAttributes Result;
		if (!Metadata)
		{
			return Result;
		}
		Result.RoomId = Metadata->FindOrCreateAttribute<FString>(FEFCalystoRoomThemeMetadataV6::RoomId, FString(), false, true, false);
		Result.ThemeId = Metadata->FindOrCreateAttribute<FName>(FEFCalystoRoomThemeMetadataV6::ThemeId, NoThemeId, false, true, false);
		Result.StyleId = Metadata->FindOrCreateAttribute<FName>(FEFCalystoRoomThemeMetadataV6::StyleId, NAME_None, false, true, false);
		Result.RoomFlags = Metadata->FindOrCreateAttribute<int32>(FEFCalystoRoomThemeMetadataV6::RoomFlags, 0, false, true, false);
		Result.TopologyKind = Metadata->FindOrCreateAttribute<FName>(FEFCalystoRoomThemeMetadataV6::TopologyKind, TEXT("Ordinary"), false, true, false);
		Result.CollisionOrdinal = Metadata->FindOrCreateAttribute<int32>(FEFCalystoRoomThemeMetadataV6::CollisionOrdinal, 0, false, true, false);
		Result.CatalogId = Metadata->FindOrCreateAttribute<FName>(FEFCalystoRoomThemeMetadataV6::CatalogId, NAME_None, false, true, false);
		Result.CatalogColor = Metadata->FindOrCreateAttribute<FVector4>(FEFCalystoRoomThemeMetadataV6::CatalogColor, FVector4(0.0), false, true, false);
		Result.EffectiveFloor = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::EffectiveFloorMaterial, FSoftObjectPath(), false, true, false);
		Result.EffectiveWall = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::EffectiveWallMaterial, FSoftObjectPath(), false, true, false);
		Result.EffectiveRoof = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::EffectiveRoofMaterial, FSoftObjectPath(), false, true, false);
		Result.FloorAuthority = Metadata->FindOrCreateAttribute<int32>(FEFCalystoRoomThemeMetadataV6::FloorMaterialAuthority, 0, false, true, false);
		Result.WallAuthority = Metadata->FindOrCreateAttribute<int32>(FEFCalystoRoomThemeMetadataV6::WallMaterialAuthority, 0, false, true, false);
		Result.RoofAuthority = Metadata->FindOrCreateAttribute<int32>(FEFCalystoRoomThemeMetadataV6::RoofMaterialAuthority, 0, false, true, false);
		if (bProbabilityDiagnostics)
		{
			Result.PresenceDraw = Metadata->FindOrCreateAttribute<double>(FEFCalystoRoomThemeMetadataV6::PresenceDraw, 0.0, false, true, false);
			Result.ThemeDraw = Metadata->FindOrCreateAttribute<double>(FEFCalystoRoomThemeMetadataV6::ThemeDraw, 0.0, false, true, false);
		}
		if (bVendorPayload)
		{
			Result.RoomType = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorRoomType, FSoftObjectPath(), false, true, false);
			Result.OverrideFloor = Metadata->FindOrCreateAttribute<bool>(FEFCalystoRoomThemeMetadataV6::VendorOverrideFloorMaterial, false, false, true, false);
			Result.Floor = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorFloorMaterial, FSoftObjectPath(), false, true, false);
			Result.OverrideWall = Metadata->FindOrCreateAttribute<bool>(FEFCalystoRoomThemeMetadataV6::VendorOverrideWallMaterial, false, false, true, false);
			Result.Wall = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorWallMaterial, FSoftObjectPath(), false, true, false);
			Result.OverrideRoof = Metadata->FindOrCreateAttribute<bool>(FEFCalystoRoomThemeMetadataV6::VendorOverrideRoofMaterial, false, false, true, false);
			Result.Roof = Metadata->FindOrCreateAttribute<FSoftObjectPath>(FEFCalystoRoomThemeMetadataV6::VendorRoofMaterial, FSoftObjectPath(), false, true, false);
		}
		return Result;
	}

	static bool SetAttributes(
		const FRoomRecord& Record,
		const FEFCalystoRoomThemeGenerationConfigV6& Config,
		const PCGMetadataEntryKey EntryKey,
		const FOutputAttributes& Attributes,
		const bool bVendorPayload)
	{
		if (!Attributes.IsComplete(bVendorPayload, Config.bEmitProbabilityDiagnostics))
		{
			return false;
		}

		const FEFCalystoPCGThemeProfileV6* Theme = Config.Themes.IsValidIndex(Record.ThemeIndex)
			? &Config.Themes[Record.ThemeIndex]
			: nullptr;
		const FName ThemeId = Theme ? Theme->ThemeId : NoThemeId;
		const FName CatalogId = Theme ? Theme->CatalogId : NAME_None;
		const FVector4 CatalogColor = Theme ? FVector4(Theme->CatalogColor) : FVector4(0.0);

		FSoftObjectPath EffectiveFloor;
		FSoftObjectPath EffectiveWall;
		FSoftObjectPath EffectiveRoof;
		EEFCalystoMaterialAuthorityV6 FloorAuthority = EEFCalystoMaterialAuthorityV6::None;
		EEFCalystoMaterialAuthorityV6 WallAuthority = EEFCalystoMaterialAuthorityV6::None;
		EEFCalystoMaterialAuthorityV6 RoofAuthority = EEFCalystoMaterialAuthorityV6::None;
		ResolveMaterial(Theme ? Theme->FloorMaterial : TSoftObjectPtr<UMaterialInstance>(), Config.Style.FloorMaterial, EffectiveFloor, FloorAuthority);
		ResolveMaterial(Theme ? Theme->WallMaterial : TSoftObjectPtr<UMaterialInstance>(), Config.Style.WallMaterial, EffectiveWall, WallAuthority);
		ResolveMaterial(Theme ? Theme->RoofMaterial : TSoftObjectPtr<UMaterialInstance>(), Config.Style.RoofMaterial, EffectiveRoof, RoofAuthority);

		Attributes.RoomId->SetValue(EntryKey, Record.RoomId);
		Attributes.ThemeId->SetValue(EntryKey, ThemeId);
		Attributes.StyleId->SetValue(EntryKey, Config.Style.StyleId);
		Attributes.RoomFlags->SetValue(EntryKey, static_cast<int32>(Record.Flags));
		Attributes.TopologyKind->SetValue(EntryKey, Record.TopologyKind);
		Attributes.CollisionOrdinal->SetValue(EntryKey, Record.CollisionOrdinal);
		Attributes.CatalogId->SetValue(EntryKey, CatalogId);
		Attributes.CatalogColor->SetValue(EntryKey, CatalogColor);
		Attributes.EffectiveFloor->SetValue(EntryKey, EffectiveFloor);
		Attributes.EffectiveWall->SetValue(EntryKey, EffectiveWall);
		Attributes.EffectiveRoof->SetValue(EntryKey, EffectiveRoof);
		Attributes.FloorAuthority->SetValue(EntryKey, static_cast<int32>(FloorAuthority));
		Attributes.WallAuthority->SetValue(EntryKey, static_cast<int32>(WallAuthority));
		Attributes.RoofAuthority->SetValue(EntryKey, static_cast<int32>(RoofAuthority));
		if (Attributes.PresenceDraw && Attributes.ThemeDraw)
		{
			Attributes.PresenceDraw->SetValue(EntryKey, Record.PresenceDraw);
			Attributes.ThemeDraw->SetValue(EntryKey, Record.ThemeDraw);
		}

		if (bVendorPayload)
		{
			// UE 5.8 rejects hard Object metadata at runtime. The adapter retains
			// this synthesized object; its soft path is only the PCG transport.
			Attributes.RoomType->SetValue(EntryKey, Theme ? FSoftObjectPath(Theme->RoomType.Get()) : FSoftObjectPath());
			Attributes.OverrideFloor->SetValue(EntryKey, Theme && !Theme->FloorMaterial.IsNull());
			Attributes.Floor->SetValue(EntryKey, Theme ? Theme->FloorMaterial.ToSoftObjectPath() : FSoftObjectPath());
			Attributes.OverrideWall->SetValue(EntryKey, Theme && !Theme->WallMaterial.IsNull());
			Attributes.Wall->SetValue(EntryKey, Theme ? Theme->WallMaterial.ToSoftObjectPath() : FSoftObjectPath());
			Attributes.OverrideRoof->SetValue(EntryKey, Theme && !Theme->RoofMaterial.IsNull());
			Attributes.Roof->SetValue(EntryKey, Theme ? Theme->RoofMaterial.ToSoftObjectPath() : FSoftObjectPath());
		}
		return true;
	}

	template <typename PredicateType>
	static bool EmitOutput(
		FPCGContext* Context,
		const TArray<FPCGTaggedData>& Sources,
		const TArray<FRoomRecord>& Records,
		const FEFCalystoRoomThemeGenerationConfigV6& Config,
		const FName OutputPin,
		const TCHAR* OutputTag,
		const bool bVendorPayload,
		PredicateType&& Predicate)
	{
		for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
		{
			const UPCGBasePointData* SourceData = Cast<UPCGBasePointData>(Sources[SourceIndex].Data);
			if (!SourceData)
			{
				continue;
			}

			TArray<const FRoomRecord*> SelectedRecords;
			for (const FRoomRecord& Record : Records)
			{
				if (Record.SourceIndex == SourceIndex && Predicate(Record))
				{
					SelectedRecords.Add(&Record);
				}
			}
			if (SelectedRecords.IsEmpty())
			{
				continue;
			}

			UPCGPointData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			OutputData->InitializeFromData(SourceData);
			UPCGMetadata* Metadata = OutputData->MutableMetadata();
			const FOutputAttributes Attributes = CreateAttributes(Metadata, bVendorPayload, Config.bEmitProbabilityDiagnostics);
			if (!Attributes.IsComplete(bVendorPayload, Config.bEmitProbabilityDiagnostics))
			{
				PCGLog::LogErrorOnGraph(LOCTEXT(
					"MetadataSchemaCreation",
					"Calysto V6 could not create its complete PCG metadata schema; generation was stopped safely."), Context);
				return false;
			}
			TArray<FPCGPoint>& OutputPoints = OutputData->GetMutablePoints();
			OutputPoints.Reserve(SelectedRecords.Num());
			for (const FRoomRecord* Record : SelectedRecords)
			{
				FPCGPoint OutputPoint = Record->Point;
				OutputPoint.MetadataEntry = Metadata->AddEntry(Record->Point.MetadataEntry);
				if (!SetAttributes(*Record, Config, OutputPoint.MetadataEntry, Attributes, bVendorPayload))
				{
					PCGLog::LogErrorOnGraph(LOCTEXT(
						"MetadataSchemaWrite",
						"Calysto V6 rejected an incomplete PCG metadata write instead of producing a partial room context."), Context);
					return false;
				}
				OutputPoints.Add(MoveTemp(OutputPoint));
			}

			FPCGTaggedData& TaggedOutput = Context->OutputData.TaggedData.Add_GetRef(Sources[SourceIndex]);
			TaggedOutput.Data = OutputData;
			TaggedOutput.Pin = OutputPin;
			TaggedOutput.Tags.Add(OutputTag);
		}
		return true;
	}
}

bool FEFCalystoRoomThemeGenerationConfigV6::Validate(FString& OutError) const
{
	OutError.Reset();
	if (!FMath::IsFinite(ThemePresenceChance) || ThemePresenceChance < 0.0 || ThemePresenceChance > 1.0)
	{
		OutError = TEXT("Theme Presence Chance must be finite and within [0, 1].");
		return false;
	}
	if (!FMath::IsFinite(RoomIdentityGridCm) || RoomIdentityGridCm < 0.01)
	{
		OutError = TEXT("Room Identity Grid must be finite and at least 0.01 cm.");
		return false;
	}
	if (!FMath::IsFinite(ProtectionToleranceCm) || ProtectionToleranceCm < 0.0)
	{
		OutError = TEXT("Protection Tolerance must be finite and non-negative.");
		return false;
	}
	if (!FMath::IsFinite(DoorClearanceRadiusCm)
		|| DoorClearanceRadiusCm < 0.0
		|| DoorClearanceRadiusCm > 1000.0)
	{
		OutError = TEXT("Door Clearance Radius must be finite and within [0, 1000] cm.");
		return false;
	}
	if (Style.StyleId.IsNone())
	{
		OutError = TEXT("The resolved dungeon Style must have a non-empty Style ID.");
		return false;
	}
	const auto ValidateMaterialReference = [&OutError](
		const TSoftObjectPtr<UMaterialInstance>& Material,
		const FString& Label,
		const bool bRequired)
	{
		if (Material.IsNull())
		{
			if (bRequired)
			{
				OutError = Label + TEXT(" must reference a Material Instance.");
				return false;
			}
			return true;
		}
		if (!Material.ToSoftObjectPath().IsValid())
		{
			OutError = Label + TEXT(" contains an invalid Material Instance soft path.");
			return false;
		}
		return true;
	};
	if (!ValidateMaterialReference(Style.FloorMaterial, TEXT("The resolved Style Floor Material"), true)
		|| !ValidateMaterialReference(Style.WallMaterial, TEXT("The resolved Style Wall Material"), true)
		|| !ValidateMaterialReference(Style.RoofMaterial, TEXT("The resolved Style Roof Material"), true))
	{
		return false;
	}
	if (DungeonTransform.ContainsNaN() || !DungeonTransform.IsValid())
	{
		OutError = TEXT("Dungeon Transform is invalid.");
		return false;
	}
	if (ThemePresenceChance > 0.0 && Themes.IsEmpty())
	{
		OutError = TEXT("At least one Room Theme is required when Theme Presence Chance is greater than zero.");
		return false;
	}

	TSet<FName> ThemeIds;
	double TotalWeight = 0.0;
	for (int32 Index = 0; Index < Themes.Num(); ++Index)
	{
		const FEFCalystoPCGThemeProfileV6& Theme = Themes[Index];
		if (Theme.ThemeId.IsNone() || Theme.ThemeId == EFCalystoAssignRoomThemeV6Private::NoThemeId)
		{
			OutError = FString::Printf(TEXT("Room Theme index %d has a reserved or empty Theme ID."), Index);
			return false;
		}
		if (ThemeIds.Contains(Theme.ThemeId))
		{
			OutError = FString::Printf(TEXT("Room Theme ID '%s' is duplicated."), *Theme.ThemeId.ToString());
			return false;
		}
		ThemeIds.Add(Theme.ThemeId);
		if (!FMath::IsFinite(Theme.SelectionWeight) || Theme.SelectionWeight <= 0.0)
		{
			OutError = FString::Printf(TEXT("Room Theme '%s' must have a finite positive Selection Weight."), *Theme.ThemeId.ToString());
			return false;
		}
		if (!IsValid(Theme.RoomType))
		{
			OutError = FString::Printf(
				TEXT("Room Theme '%s' must reference its resident transient Calysto Room Type."),
				*Theme.ThemeId.ToString());
			return false;
		}
		const FString ThemeLabel = FString::Printf(TEXT("Room Theme '%s'"), *Theme.ThemeId.ToString());
		if (!ValidateMaterialReference(Theme.FloorMaterial, ThemeLabel + TEXT(" Floor Material override"), false)
			|| !ValidateMaterialReference(Theme.WallMaterial, ThemeLabel + TEXT(" Wall Material override"), false)
			|| !ValidateMaterialReference(Theme.RoofMaterial, ThemeLabel + TEXT(" Roof Material override"), false))
		{
			return false;
		}
		TotalWeight += Theme.SelectionWeight;
	}
	if (!FMath::IsFinite(TotalWeight) || (ThemePresenceChance > 0.0 && TotalWeight <= 0.0))
	{
		OutError = TEXT("Room Theme weights do not produce a finite positive total.");
		return false;
	}
	if (!FloorPlanHash.IsEmpty())
	{
		if (FloorPlanHash.Len() != 64)
		{
			OutError = TEXT("The frozen Floor Plan Hash must be a 64-character SHA-256 value.");
			return false;
		}
		if (ThemeAliasProbability.Num() != Themes.Num()
			|| ThemeAliasIndex.Num() != Themes.Num())
		{
			OutError = TEXT("The frozen V6 Theme alias table must match the resolved Theme count.");
			return false;
		}
		for (int32 Index = 0; Index < Themes.Num(); ++Index)
		{
			if (!FMath::IsFinite(ThemeAliasProbability[Index])
				|| ThemeAliasProbability[Index] < 0.0f
				|| ThemeAliasProbability[Index] > 1.0f
				|| !Themes.IsValidIndex(ThemeAliasIndex[Index]))
			{
				OutError = FString::Printf(TEXT("The frozen V6 Theme alias table is invalid at index %d."), Index);
				return false;
			}
		}
	}
	return true;
}

FString FEFCalystoRoomThemeDeterminismV6::BuildStableRoomId(
	const FPCGPoint& Point,
	const FTransform& DungeonTransform,
	const double IdentityGridCm,
	const int64 GenerationSeed,
	const FString& CanonicalSourceKey)
{
	using namespace EFCalystoAssignRoomThemeV6Private;
	const FBox LocalBounds = Point.GetLocalBounds().TransformBy(Point.Transform).TransformBy(DungeonTransform.Inverse());
	const FVector Center = LocalBounds.GetCenter();
	const FVector Extent = LocalBounds.GetExtent();
	FStableHasher Hasher;
	Hasher.AddString(TEXT("Calysto.Room.V6"));
	Hasher.AddInt64(GenerationSeed);
	Hasher.AddString(CanonicalSourceKey);
	Hasher.AddInt64(Point.Seed);
	Hasher.AddInt64(Quantize(Center.X, IdentityGridCm));
	Hasher.AddInt64(Quantize(Center.Y, IdentityGridCm));
	Hasher.AddInt64(Quantize(Center.Z, IdentityGridCm));
	Hasher.AddInt64(Quantize(Extent.X, IdentityGridCm));
	Hasher.AddInt64(Quantize(Extent.Y, IdentityGridCm));
	Hasher.AddInt64(Quantize(Extent.Z, IdentityGridCm));
	return HashToString(Hasher);
}

double FEFCalystoRoomThemeDeterminismV6::Draw01(
	const int64 GenerationSeed,
	const FString& RoomId,
	const TCHAR* Lane)
{
	using namespace EFCalystoAssignRoomThemeV6Private;
	FStableHasher Hasher;
	Hasher.AddString(TEXT("Calysto.Theme.Draw.V6"));
	Hasher.AddInt64(GenerationSeed);
	Hasher.AddString(RoomId);
	Hasher.AddString(Lane ? FString(Lane) : FString());
	const uint64 Mantissa = Hasher.FinishA() >> 11;
	return static_cast<double>(Mantissa) * (1.0 / 9007199254740992.0);
}

bool FEFCalystoRoomThemeDeterminismV6::IsThemePresent(
	const int64 GenerationSeed,
	const FString& RoomId,
	const double ThemePresenceChance)
{
	return Draw01(GenerationSeed, RoomId, TEXT("ThemePresence")) < FMath::Clamp(ThemePresenceChance, 0.0, 1.0);
}

bool FEFCalystoRoomThemeDeterminismV6::ResolveUpstreamProtectionFlags(
	const int32 UpstreamRoomFlags,
	EEFCalystoPCGRoomFlagsV6& OutProtectionFlags,
	FString& OutError)
{
	OutProtectionFlags = EEFCalystoPCGRoomFlagsV6::None;
	OutError.Reset();
	const int32 AllowedMask =
		static_cast<int32>(EEFCalystoPCGRoomFlagsV6::Critical) |
		static_cast<int32>(EEFCalystoPCGRoomFlagsV6::Progression);
	if ((UpstreamRoomFlags & ~AllowedMask) != 0)
	{
		OutError = TEXT("Upstream EF_RoomFlags may contain only Critical or Progression protection bits.");
		return false;
	}
	OutProtectionFlags = static_cast<EEFCalystoPCGRoomFlagsV6>(
		UpstreamRoomFlags & AllowedMask);
	return true;
}

int32 FEFCalystoRoomThemeDeterminismV6::SelectThemeIndex(
	const int64 GenerationSeed,
	const FString& RoomId,
	const TArray<FEFCalystoPCGThemeProfileV6>& Themes)
{
	TArray<int32> OrderedIndexes;
	OrderedIndexes.Reserve(Themes.Num());
	for (int32 Index = 0; Index < Themes.Num(); ++Index)
	{
		if (!Themes[Index].ThemeId.IsNone() && FMath::IsFinite(Themes[Index].SelectionWeight) && Themes[Index].SelectionWeight > 0.0)
		{
			OrderedIndexes.Add(Index);
		}
	}
	OrderedIndexes.Sort([&Themes](const int32 Left, const int32 Right)
	{
		return Themes[Left].ThemeId.ToString().Compare(Themes[Right].ThemeId.ToString(), ESearchCase::CaseSensitive) < 0;
	});

	double TotalWeight = 0.0;
	for (const int32 Index : OrderedIndexes)
	{
		TotalWeight += Themes[Index].SelectionWeight;
	}
	if (OrderedIndexes.IsEmpty() || !FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0)
	{
		return INDEX_NONE;
	}

	const double Target = Draw01(GenerationSeed, RoomId, TEXT("ThemeType")) * TotalWeight;
	double Cumulative = 0.0;
	for (const int32 Index : OrderedIndexes)
	{
		Cumulative += Themes[Index].SelectionWeight;
		if (Target < Cumulative)
		{
			return Index;
		}
	}
	return OrderedIndexes.Last();
}

int32 FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(
	const FVector& MarkerLocation,
	const TArray<FBox>& RoomBounds,
	const TArray<FString>& RoomIds,
	const double ToleranceCm)
{
	if (RoomBounds.Num() != RoomIds.Num())
	{
		return INDEX_NONE;
	}
	int32 BestIndex = INDEX_NONE;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	bool bAmbiguousExactTie = false;
	for (int32 Index = 0; Index < RoomBounds.Num(); ++Index)
	{
		const FBox Expanded = RoomBounds[Index].ExpandBy(FMath::Max(0.0, ToleranceCm));
		if (!Expanded.IsInsideOrOn(MarkerLocation))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(MarkerLocation, RoomBounds[Index].GetCenter());
		if (DistanceSquared + UE_DOUBLE_SMALL_NUMBER < BestDistanceSquared)
		{
			BestIndex = Index;
			BestDistanceSquared = DistanceSquared;
			bAmbiguousExactTie = false;
		}
		else if (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared, UE_DOUBLE_SMALL_NUMBER)
			&& BestIndex != INDEX_NONE
			&& RoomIds[Index] != RoomIds[BestIndex])
		{
			bAmbiguousExactTie = true;
		}
	}
	return bAmbiguousExactTie ? INDEX_NONE : BestIndex;
}

bool FEFCalystoRoomThemeDeterminismV6::ResolveDoorClearanceRoomIndexes(
	const TArray<FVector>& ConnectorLocations,
	const TArray<FBox>& RoomBounds,
	const TArray<FString>& RoomIds,
	const double ContainmentToleranceCm,
	const double ClearanceRadiusCm,
	TArray<int32>& OutRoomIndexes,
	FString& OutError)
{
	OutRoomIndexes.Reset();
	OutError.Reset();
	if (ConnectorLocations.IsEmpty()
		|| RoomBounds.IsEmpty()
		|| RoomBounds.Num() != RoomIds.Num()
		|| !FMath::IsFinite(ContainmentToleranceCm)
		|| ContainmentToleranceCm < 0.0
		|| !FMath::IsFinite(ClearanceRadiusCm)
		|| ClearanceRadiusCm < 0.0
		|| ClearanceRadiusCm > 1000.0)
	{
		OutError = TEXT("Door-clearance geometry input is incomplete or invalid.");
		return false;
	}
	for (int32 RoomIndex = 0; RoomIndex < RoomBounds.Num(); ++RoomIndex)
	{
		if (!RoomBounds[RoomIndex].IsValid || RoomIds[RoomIndex].IsEmpty())
		{
			OutError = FString::Printf(TEXT("Door-clearance room geometry is invalid at index %d."), RoomIndex);
			return false;
		}
	}

	TSet<int32> ProtectedIndexes;
	const double RadiusSquared = FMath::Square(ClearanceRadiusCm);
	for (int32 ConnectorIndex = 0; ConnectorIndex < ConnectorLocations.Num(); ++ConnectorIndex)
	{
		const FVector& Connector = ConnectorLocations[ConnectorIndex];
		if (Connector.ContainsNaN())
		{
			OutError = FString::Printf(TEXT("Door-clearance connector %d is invalid."), ConnectorIndex);
			return false;
		}
		const int32 AnchorIndex = ResolveProtectedRoomIndex(
			Connector,
			RoomBounds,
			RoomIds,
			ContainmentToleranceCm);
		if (AnchorIndex == INDEX_NONE)
		{
			OutError = FString::Printf(
				TEXT("Door-clearance connector %d does not resolve to one unambiguous anchor room."),
				ConnectorIndex);
			return false;
		}
		ProtectedIndexes.Add(AnchorIndex);
		for (int32 RoomIndex = 0; RoomIndex < RoomBounds.Num(); ++RoomIndex)
		{
			if (RoomBounds[RoomIndex].ComputeSquaredDistanceToPoint(Connector)
				<= RadiusSquared + UE_DOUBLE_SMALL_NUMBER)
			{
				ProtectedIndexes.Add(RoomIndex);
			}
		}
	}

	OutRoomIndexes = ProtectedIndexes.Array();
	OutRoomIndexes.Sort();
	return !OutRoomIndexes.IsEmpty();
}

FString FEFCalystoRoomThemeDeterminismV6::BuildConfigFingerprint(
	const FEFCalystoRoomThemeGenerationConfigV6& Config)
{
	using namespace EFCalystoAssignRoomThemeV6Private;
	FStableHasher Hasher;
	Hasher.AddString(TEXT("Calysto.Theme.Config.V6"));
	Hasher.AddInt64(Config.GenerationSeed);
	Hasher.AddString(Config.FloorPlanHash);
	Hasher.AddInt64(Quantize(Config.ThemePresenceChance, 0.000000001));
	Hasher.AddInt64(Quantize(Config.RoomIdentityGridCm, 0.000001));
	Hasher.AddInt64(Quantize(Config.ProtectionToleranceCm, 0.000001));
	Hasher.AddInt64(Quantize(Config.DoorClearanceRadiusCm, 0.000001));
	const FVector Translation = Config.DungeonTransform.GetTranslation();
	const FQuat Rotation = Config.DungeonTransform.GetRotation();
	const FVector Scale = Config.DungeonTransform.GetScale3D();
	Hasher.AddInt64(Quantize(Translation.X, 0.000001));
	Hasher.AddInt64(Quantize(Translation.Y, 0.000001));
	Hasher.AddInt64(Quantize(Translation.Z, 0.000001));
	Hasher.AddInt64(Quantize(Rotation.X, 0.000000001));
	Hasher.AddInt64(Quantize(Rotation.Y, 0.000000001));
	Hasher.AddInt64(Quantize(Rotation.Z, 0.000000001));
	Hasher.AddInt64(Quantize(Rotation.W, 0.000000001));
	Hasher.AddInt64(Quantize(Scale.X, 0.000001));
	Hasher.AddInt64(Quantize(Scale.Y, 0.000001));
	Hasher.AddInt64(Quantize(Scale.Z, 0.000001));
	Hasher.AddInt64(Config.bEmitProbabilityDiagnostics ? 1 : 0);
	Hasher.AddString(Config.Style.StyleId.ToString());
	Hasher.AddString(Config.Style.FloorMaterial.ToSoftObjectPath().ToString());
	Hasher.AddString(Config.Style.WallMaterial.ToSoftObjectPath().ToString());
	Hasher.AddString(Config.Style.RoofMaterial.ToSoftObjectPath().ToString());

	TArray<const FEFCalystoPCGThemeProfileV6*> OrderedThemes;
	for (const FEFCalystoPCGThemeProfileV6& Theme : Config.Themes)
	{
		OrderedThemes.Add(&Theme);
	}
	OrderedThemes.Sort([](const FEFCalystoPCGThemeProfileV6& Left, const FEFCalystoPCGThemeProfileV6& Right)
	{
		return Left.ThemeId.ToString().Compare(Right.ThemeId.ToString(), ESearchCase::CaseSensitive) < 0;
	});
	for (const FEFCalystoPCGThemeProfileV6* Theme : OrderedThemes)
	{
		Hasher.AddString(Theme->ThemeId.ToString());
		Hasher.AddInt64(Quantize(Theme->SelectionWeight, 0.000000001));
		Hasher.AddString(Theme->ArchitectureHash);
		Hasher.AddString(Theme->FloorMaterial.ToSoftObjectPath().ToString());
		Hasher.AddString(Theme->WallMaterial.ToSoftObjectPath().ToString());
		Hasher.AddString(Theme->RoofMaterial.ToSoftObjectPath().ToString());
		Hasher.AddString(Theme->CatalogId.ToString());
	}
	for (int32 Index = 0; Index < Config.ThemeAliasProbability.Num(); ++Index)
	{
		Hasher.AddInt64(Quantize(Config.ThemeAliasProbability[Index], 0.000000001));
		Hasher.AddInt64(Config.ThemeAliasIndex.IsValidIndex(Index) ? Config.ThemeAliasIndex[Index] : INDEX_NONE);
	}
	return HashToString(Hasher);
}

void UPCGCalystoAssignRoomThemeSettings::SetResolvedConfig(const FEFCalystoRoomThemeGenerationConfigV6& InConfig)
{
	ResolvedConfig = InConfig;
}

#if WITH_EDITOR
FText UPCGCalystoAssignRoomThemeSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Calysto Assign Room Theme V6");
}

FText UPCGCalystoAssignRoomThemeSettings::GetNodeTooltipText() const
{
	return LOCTEXT(
		"NodeTooltip",
		"Combines the exact vendor Main Rooms and Side Rooms streams, assigns Room Themes with independent deterministic presence and type draws, freezes MainPath and connector-clearance flags, preserves upstream Critical/Progression protection, excludes protected rooms, and emits Style -> Room Theme material precedence metadata without loading assets.");
}
#endif

TArray<FPCGPinProperties> UPCGCalystoAssignRoomThemeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& MainRoomsPin = Pins.Emplace_GetRef(FEFCalystoRoomThemePinsV6::MainRooms, EPCGDataType::Point);
	MainRoomsPin.SetRequiredPin();
#if WITH_EDITOR
	MainRoomsPin.Tooltip = LOCTEXT("MainRoomsPin", "The exact vendor Main Rooms set. Membership freezes EF_RoomFlags.MainPath; upstream EF_RoomFlags Critical/Progression bits are preserved.");
#endif
	FPCGPinProperties& SideRoomsPin = Pins.Emplace_GetRef(FEFCalystoRoomThemePinsV6::SideRooms, EPCGDataType::Point);
	SideRoomsPin.SetRequiredPin();
#if WITH_EDITOR
	SideRoomsPin.Tooltip = LOCTEXT("SideRoomsPin", "The exact vendor Side Rooms set. It is combined with and deduplicated against Main Rooms; upstream EF_RoomFlags Critical/Progression bits are preserved.");
#endif
	FPCGPinProperties& StartPin = Pins.Emplace_GetRef(FEFCalystoRoomThemePinsV6::StartRooms, EPCGDataType::Point);
	StartPin.SetRequiredPin();
#if WITH_EDITOR
	StartPin.Tooltip = LOCTEXT("StartPin", "Exactly one marker identifying the Start room.");
#endif
	FPCGPinProperties& EndPin = Pins.Emplace_GetRef(FEFCalystoRoomThemePinsV6::EndRooms, EPCGDataType::Point);
	EndPin.SetRequiredPin();
#if WITH_EDITOR
	EndPin.Tooltip = LOCTEXT("EndPin", "Exactly one marker identifying the End room.");
#endif
	FPCGPinProperties& CriticalPin = Pins.Emplace_GetRef(FEFCalystoRoomThemePinsV6::CriticalRooms, EPCGDataType::Point);
	CriticalPin.SetAdvancedPin();
#if WITH_EDITOR
	CriticalPin.Tooltip = LOCTEXT("CriticalPin", "Optional markers for any additional rooms that must never receive a Room Theme.");
#endif
	FPCGPinProperties& ProgressionPin = Pins.Emplace_GetRef(FEFCalystoRoomThemePinsV6::ProgressionRooms, EPCGDataType::Point);
	ProgressionPin.SetAdvancedPin();
#if WITH_EDITOR
	ProgressionPin.Tooltip = LOCTEXT("ProgressionPin", "Optional markers for progression-gated rooms that must never receive a Room Theme.");
#endif
	return Pins;
}

TArray<FPCGPinProperties> UPCGCalystoAssignRoomThemeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(FEFCalystoRoomThemePinsV6::ThemedRooms, EPCGDataType::Point);
	Pins.Emplace(FEFCalystoRoomThemePinsV6::UnthemedRooms, EPCGDataType::Point);
	Pins.Emplace(FEFCalystoRoomThemePinsV6::RoomContexts, EPCGDataType::Point);
#if WITH_EDITOR
	Pins[0].Tooltip = LOCTEXT("ThemedRoomsPin", "Rooms selected for a Room Theme, with vendor-compatible RoomType and material override attributes.");
	Pins[1].Tooltip = LOCTEXT("UnthemedRoomsPin", "NoTheme and protected rooms. These inherit their selected dungeon Style.");
	Pins[2].Tooltip = LOCTEXT("RoomContextsPin", "All deduplicated rooms with stable identity, flags, selected Theme, catalog, and effective material metadata.");
#endif
	return Pins;
}

FPCGElementPtr UPCGCalystoAssignRoomThemeSettings::CreateElement() const
{
	return MakeShared<FPCGCalystoAssignRoomThemeElement>();
}

bool FPCGCalystoAssignRoomThemeElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace EFCalystoAssignRoomThemeV6Private;
	check(Context);
	const UPCGCalystoAssignRoomThemeSettings* Settings = Context->GetInputSettings<UPCGCalystoAssignRoomThemeSettings>();
	check(Settings);
	const FEFCalystoRoomThemeGenerationConfigV6& Config = Settings->ResolvedConfig;

	FString Error;
	if (!FEFCalystoRoomThemeMetadataV6::ValidateSchema(Error))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(TEXT("Calysto V6 PCG metadata schema is invalid: %s"), *Error)), Context);
		return true;
	}
	if (!Config.Validate(Error))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(TEXT("Calysto V6 Room Theme configuration is invalid: %s"), *Error)), Context);
		return true;
	}

	const TArray<FPCGTaggedData> MainSources = Context->InputData.GetInputsByPin(FEFCalystoRoomThemePinsV6::MainRooms);
	const TArray<FPCGTaggedData> SideSources = Context->InputData.GetInputsByPin(FEFCalystoRoomThemePinsV6::SideRooms);
	if (MainSources.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoMainRooms", "Calysto V6 Room Theme assignment received no authoritative Main Rooms data."), Context);
		return true;
	}
	TArray<FPCGTaggedData> Sources = MainSources;
	Sources.Append(SideSources);
	const int32 MainSourceCount = MainSources.Num();

	TArray<FRoomRecord> Records;
	for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
	{
		const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Sources[SourceIndex].Data);
		if (!PointData)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("InvalidRooms", "Calysto V6 Main Rooms or Side Rooms contains non-point data."), Context);
			return true;
		}
		const bool bMainPathSource = SourceIndex < MainSourceCount;
		const FString SourceKey = FString::Printf(
			TEXT("%s|%s"),
			bMainPathSource ? TEXT("MainRooms") : TEXT("SideRooms"),
			*CanonicalSourceKey(Sources[SourceIndex]));
		const UPCGMetadata* SourceMetadata = PointData->ConstMetadata();
		const bool bHasUpstreamFlagsAttribute = SourceMetadata
			&& SourceMetadata->HasAttribute(FEFCalystoRoomThemeMetadataV6::RoomFlags);
		const FPCGMetadataAttribute<int32>* UpstreamFlagsAttribute = SourceMetadata
			? SourceMetadata->GetConstTypedAttribute<int32>(
				FEFCalystoRoomThemeMetadataV6::RoomFlags)
			: nullptr;
		if (bHasUpstreamFlagsAttribute && !UpstreamFlagsAttribute)
		{
			PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(
				TEXT("Calysto V6 rejected %s because EF_RoomFlags exists with a type other than int32."),
				*SourceKey)), Context);
			return true;
		}
		const FConstPCGPointValueRanges Ranges(PointData);
		for (int32 PointIndex = 0; PointIndex < PointData->GetNumPoints(); ++PointIndex)
		{
			FRoomRecord& Record = Records.Emplace_GetRef();
			Record.SourceIndex = SourceIndex;
			Record.Point = Ranges.GetPoint(PointIndex);
			Record.SourceKey = SourceKey;
			Record.WorldBounds = Record.Point.GetLocalBounds().TransformBy(Record.Point.Transform);
			Record.LocalBounds = Record.WorldBounds.TransformBy(Config.DungeonTransform.Inverse());
			Record.GeometryKey = BuildGeometryKey(Record.LocalBounds, Config.RoomIdentityGridCm);
			Record.ExactGeometryKey = BuildExactGeometryKey(Record.LocalBounds);
			Record.Flags = bMainPathSource
				? EEFCalystoPCGRoomFlagsV6::MainPath
				: EEFCalystoPCGRoomFlagsV6::None;
			if (UpstreamFlagsAttribute)
			{
				EEFCalystoPCGRoomFlagsV6 UpstreamProtectionFlags =
					EEFCalystoPCGRoomFlagsV6::None;
				const int32 UpstreamRoomFlags = UpstreamFlagsAttribute->GetValueFromItemKey(
					Record.Point.MetadataEntry);
				if (!FEFCalystoRoomThemeDeterminismV6::ResolveUpstreamProtectionFlags(
						UpstreamRoomFlags, UpstreamProtectionFlags, Error))
				{
					PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(
						TEXT("Calysto V6 rejected room protection metadata on %s point %d: %s"),
						*SourceKey, PointIndex, *Error)), Context);
					return true;
				}
				Record.Flags |= UpstreamProtectionFlags;
			}
			// The provisional key is used only to resolve protected marker ties. The
			// authoritative Stable Room ID is built after topology flags are known.
			Record.RoomId = Record.ExactGeometryKey;
		}
	}
	if (Records.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("EmptyRooms", "Calysto V6 Rooms contains no points."), Context);
		return true;
	}

	Records.Sort([](const FRoomRecord& Left, const FRoomRecord& Right)
	{
		const int32 GeometryOrder = Left.GeometryKey.Compare(Right.GeometryKey, ESearchCase::CaseSensitive);
		if (GeometryOrder != 0) return GeometryOrder < 0;
		const int32 ExactOrder = Left.ExactGeometryKey.Compare(Right.ExactGeometryKey, ESearchCase::CaseSensitive);
		if (ExactOrder != 0) return ExactOrder < 0;
		const int32 SourceOrder = Left.SourceKey.Compare(Right.SourceKey, ESearchCase::CaseSensitive);
		return SourceOrder != 0 ? SourceOrder < 0 : Left.Point.Seed < Right.Point.Seed;
	});
	for (int32 Index = Records.Num() - 1; Index > 0; --Index)
	{
		if (Records[Index].ExactGeometryKey == Records[Index - 1].ExactGeometryKey)
		{
			// Main Rooms + Side Rooms can contain the same room more than once.
			// Canonical source ordering above makes this deduplication independent
			// from incoming point/index order.
			Records[Index - 1].Flags |= Records[Index].Flags;
			Records.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}

	TArray<FVector> StartMarkers;
	TArray<FVector> EndMarkers;
	TArray<FVector> CriticalMarkers;
	TArray<FVector> ProgressionMarkers;
	if (!CollectMarkerLocations(Context, FEFCalystoRoomThemePinsV6::StartRooms, 1, StartMarkers, Error)
		|| !CollectMarkerLocations(Context, FEFCalystoRoomThemePinsV6::EndRooms, 1, EndMarkers, Error)
		|| !CollectMarkerLocations(Context, FEFCalystoRoomThemePinsV6::CriticalRooms, -1, CriticalMarkers, Error)
		|| !CollectMarkerLocations(Context, FEFCalystoRoomThemePinsV6::ProgressionRooms, -1, ProgressionMarkers, Error))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(TEXT("Calysto V6 protected-room contract failed: %s"), *Error)), Context);
		return true;
	}

	TArray<FBox> RoomBounds;
	TArray<FString> RoomIds;
	RoomBounds.Reserve(Records.Num());
	RoomIds.Reserve(Records.Num());
	for (const FRoomRecord& Record : Records)
	{
		RoomBounds.Add(Record.WorldBounds);
		RoomIds.Add(Record.RoomId);
	}

	const int32 StartIndex = FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(StartMarkers[0], RoomBounds, RoomIds, Config.ProtectionToleranceCm);
	const int32 EndIndex = FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(EndMarkers[0], RoomBounds, RoomIds, Config.ProtectionToleranceCm);
	if (StartIndex == INDEX_NONE || EndIndex == INDEX_NONE || StartIndex == EndIndex)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("StartEndResolution", "Calysto V6 could not resolve one distinct Start room and one distinct End room; assignment failed closed."), Context);
		return true;
	}
	Records[StartIndex].Flags |= EEFCalystoPCGRoomFlagsV6::Start;
	Records[EndIndex].Flags |= EEFCalystoPCGRoomFlagsV6::End;
	for (const FVector& Marker : CriticalMarkers)
	{
		const int32 CriticalIndex = FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(Marker, RoomBounds, RoomIds, Config.ProtectionToleranceCm);
		if (CriticalIndex == INDEX_NONE)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("CriticalResolution", "Calysto V6 could not resolve a critical-room marker; assignment failed closed."), Context);
			return true;
		}
		Records[CriticalIndex].Flags |= EEFCalystoPCGRoomFlagsV6::Critical;
	}
	for (const FVector& Marker : ProgressionMarkers)
	{
		const int32 ProgressionIndex = FEFCalystoRoomThemeDeterminismV6::ResolveProtectedRoomIndex(
			Marker, RoomBounds, RoomIds, Config.ProtectionToleranceCm);
		if (ProgressionIndex == INDEX_NONE)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("ProgressionResolution", "Calysto V6 could not resolve a progression-room marker; assignment failed closed."), Context);
			return true;
		}
		Records[ProgressionIndex].Flags |= EEFCalystoPCGRoomFlagsV6::Progression;
	}
	TArray<FVector> ConnectorMarkers = StartMarkers;
	ConnectorMarkers.Append(EndMarkers);
	TArray<int32> DoorClearanceIndexes;
	if (!FEFCalystoRoomThemeDeterminismV6::ResolveDoorClearanceRoomIndexes(
		ConnectorMarkers,
		RoomBounds,
		RoomIds,
		Config.ProtectionToleranceCm,
		Config.DoorClearanceRadiusCm,
		DoorClearanceIndexes,
		Error))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(
			TEXT("Calysto V6 deterministic door-clearance contract failed: %s"),
			*Error)), Context);
		return true;
	}
	for (const int32 DoorClearanceIndex : DoorClearanceIndexes)
	{
		check(Records.IsValidIndex(DoorClearanceIndex));
		Records[DoorClearanceIndex].Flags |= EEFCalystoPCGRoomFlagsV6::DoorClearance;
	}

	TSet<int64> StableRoomIds;
	TArray<int64> EligibleStableRoomIds;
	EligibleStableRoomIds.Reserve(Records.Num());
	TMap<FString, int32> NextCollisionOrdinals;
	for (FRoomRecord& Record : Records)
	{
		Record.TopologyKind = TopologyKindForFlags(Record.Flags);
		const FString CollisionKey = FString::Printf(
			TEXT("%s|%s|%d"),
			*Record.GeometryKey,
			*Record.TopologyKind.ToString(),
			static_cast<int32>(Record.Flags));
		Record.CollisionOrdinal = NextCollisionOrdinals.FindOrAdd(CollisionKey)++;
		FEFCalystoRoomIdentityInputV6 Identity;
		Identity.LocalCenter = Record.LocalBounds.GetCenter();
		Identity.Extents = Record.LocalBounds.GetExtent();
		Identity.TopologyKind = Record.TopologyKind;
		Identity.CollisionOrdinal = Record.CollisionOrdinal;
		Identity.RoomFlags = static_cast<int32>(Record.Flags);
		Record.StableRoomId = FEFCalystoDungeonDirectorMathV6::BuildStableRoomId(
			Config.GenerationSeed, Identity, static_cast<float>(Config.RoomIdentityGridCm));
		if (Record.StableRoomId <= 0 || StableRoomIds.Contains(Record.StableRoomId))
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("StableRoomIdCollision", "Calysto V6 could not produce one unique authoritative Stable Room ID per deduplicated room."), Context);
			Context->OutputData.TaggedData.Reset();
			return true;
		}
		StableRoomIds.Add(Record.StableRoomId);
		Record.RoomId = FString::Printf(TEXT("%lld"), Record.StableRoomId);
		Record.PresenceDraw = FEFCalystoDungeonDirectorMathV6::ThemePresenceUniform(
			Config.GenerationSeed, Record.StableRoomId, Config.Style.StyleId);
		Record.ThemeDraw = FEFCalystoDungeonDirectorMathV6::ThemeTypeUniform(
			Config.GenerationSeed, Record.StableRoomId, Config.Style.StyleId);
		if (!IsProtectedFlags(Record.Flags))
		{
			EligibleStableRoomIds.Add(Record.StableRoomId);
		}
	}

	const int64 GuaranteedThemeRoomId =
		Config.ThemePresenceChance > 0.0 && !Config.Themes.IsEmpty()
		? FEFCalystoDungeonDirectorMathV6::SelectGuaranteedThemeRoomId(
			Config.GenerationSeed, Config.Style.StyleId, EligibleStableRoomIds)
		: 0;
	for (FRoomRecord& Record : Records)
	{
		if (IsProtectedFlags(Record.Flags)
			|| (Record.StableRoomId != GuaranteedThemeRoomId
				&& Record.PresenceDraw >= Config.ThemePresenceChance))
		{
			continue;
		}
		Record.ThemeIndex = !Config.FloorPlanHash.IsEmpty()
			? SelectAliasIndex(Record.ThemeDraw, Config.ThemeAliasProbability, Config.ThemeAliasIndex)
			: FEFCalystoRoomThemeDeterminismV6::SelectThemeIndex(
				Config.GenerationSeed, Record.RoomId, Config.Themes);
		if (Record.ThemeIndex == INDEX_NONE)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("ThemeSelection", "Calysto V6 could not select a Room Theme from the validated weight set."), Context);
			Context->OutputData.TaggedData.Reset();
			return true;
		}
	}
	if (GuaranteedThemeRoomId > 0
		&& !Records.ContainsByPredicate([GuaranteedThemeRoomId](const FRoomRecord& Record)
		{
			return Record.StableRoomId == GuaranteedThemeRoomId
				&& Record.ThemeIndex != INDEX_NONE;
		}))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT(
			"MinimumThemeGuarantee",
			"Calysto V6 failed to realize its deterministic minimum Room Theme guarantee."), Context);
		Context->OutputData.TaggedData.Reset();
		return true;
	}

	if (!EmitOutput(Context, Sources, Records, Config, FEFCalystoRoomThemePinsV6::ThemedRooms, TEXT("EF.ThemeAssignments.V6"), true,
			[](const FRoomRecord& Record) { return Record.ThemeIndex != INDEX_NONE; })
		|| !EmitOutput(Context, Sources, Records, Config, FEFCalystoRoomThemePinsV6::UnthemedRooms, TEXT("EF.NoTheme.V6"), false,
			[](const FRoomRecord& Record) { return Record.ThemeIndex == INDEX_NONE; })
		|| !EmitOutput(Context, Sources, Records, Config, FEFCalystoRoomThemePinsV6::RoomContexts, TEXT("EF.RoomContexts.V6"), true,
			[](const FRoomRecord&) { return true; }))
	{
		Context->OutputData.TaggedData.Reset();
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

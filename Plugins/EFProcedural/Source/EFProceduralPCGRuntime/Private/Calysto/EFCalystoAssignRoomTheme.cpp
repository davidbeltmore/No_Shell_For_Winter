#include "Calysto/EFCalystoAssignRoomTheme.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGPointData.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGPoint.h"
#include "Utils/PCGLogErrors.h"
#include "UObject/UnrealType.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(EFCalystoAssignRoomTheme)

const FName FEFCalystoNativeRoomPins::MainRooms(TEXT("Main Rooms"));
const FName FEFCalystoNativeRoomPins::SideRooms(TEXT("Side Rooms"));
const FName FEFCalystoNativeRoomPins::StartRooms(TEXT("Start Rooms"));
const FName FEFCalystoNativeRoomPins::EndRooms(TEXT("End Rooms"));
const FName FEFCalystoNativeRoomPins::CriticalRooms(TEXT("Critical Rooms"));
const FName FEFCalystoNativeRoomPins::ProgressionRooms(TEXT("Progression Rooms"));
const FName FEFCalystoNativeRoomPins::NativeRooms(TEXT("Native Rooms"));
const FName FEFCalystoNativeRoomPins::RoomContexts(TEXT("Room Contexts"));
const FName FEFCalystoRoomOverrideOwnerPins::RoomOverride(TEXT("RoomOverride"));
const FName FEFCalystoRoomOverrideOwnerPins::Pieces(TEXT("Pieces"));
const FName FEFCalystoDoorMaterialProvenancePins::DoorSource(TEXT("DoorSource"));
const FName FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces(TEXT("FinalDoorPieces"));

FEFCalystoRandomKey FEFCalystoNativeRoomConfig::RandomKey() const
{
	FEFCalystoRandomKey Key;
	Key.RunSeed = RunSeed; Key.FloorNumber = FloorNumber; Key.RerollIndex = RerollIndex;
	Key.AttemptIndex = AttemptIndex; Key.StyleId = StyleId;
	return Key;
}

bool FEFCalystoNativeRoomConfig::Validate(FString& Error) const
{
	Error.Reset();
	if (!StyleId.IsValid() || FloorNumber < 1 || AttemptIndex < 0 || TopologySeed <= 0
		|| !FMath::IsFinite(QuantizationCm) || QuantizationCm <= 0 || MaximumRooms < 1 || MaximumRooms > 2048
		|| !FMath::IsFinite(AdditionalThemeChancePercent) || AdditionalThemeChancePercent < 0 || AdditionalThemeChancePercent > 100
		|| DungeonTransform.ContainsNaN() || !IsValid(EmptyRoomSchema)
		|| !FloorMaterial.IsValid() || !WallMaterial.IsValid() || !RoofMaterial.IsValid() || Themes.IsEmpty())
	{
		Error = TEXT("Native room configuration has invalid identity, bounds, probability, material or room schema.");
		return false;
	}
	for (const TCHAR* Name : { TEXT("Floor"), TEXT("WallBottom"), TEXT("WallMiddle"), TEXT("WallTop"),
		TEXT("CornerBottom"), TEXT("CornerMiddle"), TEXT("CornerTop"), TEXT("Roof") })
	{
		const FArrayProperty* P = FindFProperty<FArrayProperty>(EmptyRoomSchema->GetClass(), Name);
		if (!P || !CastField<FStructProperty>(P->Inner) || FScriptArrayHelper_InContainer(P, EmptyRoomSchema).Num() != 0)
		{
			Error = FString::Printf(TEXT("Native neutral room schema.%s must be an empty structural-decoration array."), Name);
			return false;
		}
	}
	TSet<FGuid> Ids;
	for (const FEFCalystoNativeTheme& Theme : Themes)
	{
		if (!Theme.Id.IsValid() || Ids.Contains(Theme.Id) || !FMath::IsFinite(Theme.Weight) || Theme.Weight <= 0
			|| !IsValid(Theme.RoomSchema) || Theme.RoomSchema->GetClass() != EmptyRoomSchema->GetClass()
			|| !Theme.FloorMaterial.IsValid() || !Theme.WallMaterial.IsValid() || !Theme.RoofMaterial.IsValid())
		{
			Error = TEXT("Native Theme has duplicate identity, invalid eligible weight, missing schema or effective material.");
			return false;
		}
		Ids.Add(Theme.Id);
	}
	return true;
}

namespace EFCalystoNativeRooms
{
	struct FInputCounts
	{
		int32 Main = 0;
		int32 Side = 0;
		int32 Start = 0;
		int32 End = 0;
		int32 Critical = 0;
		int32 Progression = 0;
	};

	static bool CountPoints(const FPCGDataCollection& Inputs, const FName Pin, const int32 Maximum, int32& OutCount)
	{
		OutCount = 0;
		for (const FPCGTaggedData& Tagged : Inputs.GetInputsByPin(Pin))
		{
			const UPCGBasePointData* Data = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Data) { OutCount = -1; return false; }
			if (Data->GetNumPoints() > Maximum - OutCount) { OutCount = Maximum + 1; return false; }
			OutCount += Data->GetNumPoints();
		}
		return true;
	}

	static FString DescribeInputs(const FPCGDataCollection& Inputs, const FEFCalystoNativeRoomConfig& Config)
	{
		FInputCounts Counts;
		CountPoints(Inputs, FEFCalystoNativeRoomPins::MainRooms, Config.MaximumRooms, Counts.Main);
		CountPoints(Inputs, FEFCalystoNativeRoomPins::SideRooms, Config.MaximumRooms, Counts.Side);
		CountPoints(Inputs, FEFCalystoNativeRoomPins::StartRooms, Config.MaximumRooms, Counts.Start);
		CountPoints(Inputs, FEFCalystoNativeRoomPins::EndRooms, Config.MaximumRooms, Counts.End);
		CountPoints(Inputs, FEFCalystoNativeRoomPins::CriticalRooms, Config.MaximumRooms, Counts.Critical);
		CountPoints(Inputs, FEFCalystoNativeRoomPins::ProgressionRooms, Config.MaximumRooms, Counts.Progression);
		return FString::Printf(TEXT("main=%d side=%d start=%d end=%d critical=%d progression=%d schema=%s"),
			Counts.Main, Counts.Side, Counts.Start, Counts.End, Counts.Critical, Counts.Progression,
			*GetPathNameSafe(Config.EmptyRoomSchema));
	}

	struct FRecord
	{
		FPCGPoint Point;
		int32 Source = 0;
		FBox WorldBounds = FBox(ForceInit);
		FString Geometry;
		FString ExactGeometry;
		FEFCalystoNativeRoom Room;
	};

	static FString GeometryKey(const FBox& Bounds, const double Quantum)
	{
		const FVector C = Bounds.GetCenter(), E = Bounds.GetExtent();
		return FString::Printf(TEXT("%lld,%lld,%lld|%lld,%lld,%lld"),
			FMath::RoundToInt64(C.X / Quantum), FMath::RoundToInt64(C.Y / Quantum), FMath::RoundToInt64(C.Z / Quantum),
			FMath::RoundToInt64(E.X / Quantum), FMath::RoundToInt64(E.Y / Quantum), FMath::RoundToInt64(E.Z / Quantum));
	}

	static int64 StableId(const FString& Geometry, int32 Seed, int32 Ordinal)
	{
		// Explicit UTF-8/FNV encoding, independent of array indexes, UObject names and mutable labels.
		const FTCHARToUTF8 Bytes(*FString::Printf(TEXT("%d|%s|%d"), Seed, *Geometry, Ordinal));
		uint64 Hash = 1469598103934665603ull;
		for (int32 I = 0; I < Bytes.Length(); ++I) { Hash ^= uint8(Bytes.Get()[I]); Hash *= 1099511628211ull; }
		return int64((Hash & 0x7fffffffffffffffull) | 1ull);
	}

	static FString ExactKey(const FBox& B)
	{
		return FString::Printf(TEXT("%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"), B.Min.X, B.Min.Y, B.Min.Z, B.Max.X, B.Max.Y, B.Max.Z);
	}

	static int32 FindRoom(const FVector& Position, const TArray<FRecord>& Records)
	{
		int32 Best = INDEX_NONE;
		double Distance = TNumericLimits<double>::Max();
		bool bTie = false;
		for (int32 I = 0; I < Records.Num(); ++I)
		{
			if (!Records[I].WorldBounds.ExpandBy(2.0).IsInsideOrOn(Position)) continue;
			const double D = FVector::DistSquared(Position, Records[I].WorldBounds.GetCenter());
			if (D + UE_DOUBLE_SMALL_NUMBER < Distance) { Best = I; Distance = D; bTie = false; }
			else if (FMath::IsNearlyEqual(D, Distance, UE_DOUBLE_SMALL_NUMBER)) bTie = true;
		}
		return bTie ? INDEX_NONE : Best;
	}

	template<typename T>
	static bool Attribute(UPCGMetadata* Metadata, const FName Name, const T& Value, PCGMetadataEntryKey Key)
	{
		FPCGMetadataAttribute<T>* A = Metadata->FindOrCreateAttribute<T>(Name, T{}, false, true);
		if (!A) return false;
		A->SetValue(Key, Value);
		return true;
	}

	static bool Emit(FPCGContext* Context, const TArray<FPCGTaggedData>& Sources,
		const TArray<FRecord>& Records, const FEFCalystoNativeRoomConfig& Config)
	{
		for (int32 S = 0; S < Sources.Num(); ++S)
		{
			const UPCGBasePointData* Input = Cast<UPCGBasePointData>(Sources[S].Data);
			UPCGPointData* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			Data->InitializeFromData(Input);
			UPCGMetadata* M = Data->MutableMetadata();
			// Native attributes may already exist on upstream inputs; replace their definitions
			// deliberately so RoomType stays a UE 5.8 SoftObjectPath on every output point.
			for (const FName Name : { FName(TEXT("RoomType")), FName(TEXT("FloorMaterial")), FName(TEXT("WallMaterial")), FName(TEXT("RoofMaterial")),
				FName(TEXT("ThemeOverrideFloorMaterial")), FName(TEXT("ThemeOverrideWallMaterial")), FName(TEXT("ThemeOverrideRoofMaterial")),
				FName(TEXT("EF_RoomId")), FName(TEXT("EF_ThemeId")), FName(TEXT("EF_StyleId")), FName(TEXT("EF_RoomFlags")) }) M->DeleteAttribute(Name);
			for (const FRecord& Record : Records)
			{
				if (Record.Source != S) continue;
				FPCGPoint P = Record.Point;
				P.MetadataEntry = M->AddEntry(P.MetadataEntry);
				const auto& R = Record.Room;
				const FEFCalystoNativeTheme* Theme = Config.Themes.FindByPredicate([&R](const FEFCalystoNativeTheme& T) { return T.Id == R.ThemeId; });
				const auto K = P.MetadataEntry;
				if (!Attribute(M, TEXT("EF_RoomId"), R.RoomId, K)
					|| !Attribute(M, TEXT("EF_ThemeId"), R.ThemeId.ToString(EGuidFormats::Digits), K)
					|| !Attribute(M, TEXT("EF_StyleId"), R.StyleId.ToString(EGuidFormats::Digits), K)
					|| !Attribute(M, TEXT("EF_RoomFlags"), int32(R.Protection), K)
					|| !Attribute(M, TEXT("EF_MainPath"), R.bMainPath, K)
					|| !Attribute(M, TEXT("EF_DoorClearance"), R.bDoorClearance, K)
					|| !Attribute(M, TEXT("EF_GuaranteedTheme"), R.bGuaranteedTheme, K)
					|| !Attribute(M, TEXT("RoomType"), FSoftObjectPath(Theme ? Theme->RoomSchema.Get() : Config.EmptyRoomSchema.Get()), K)
					|| !Attribute(M, TEXT("ThemeOverrideFloorMaterial"), Theme && Theme->bOverrideFloor, K)
					|| !Attribute(M, TEXT("ThemeOverrideWallMaterial"), Theme && Theme->bOverrideWall, K)
					|| !Attribute(M, TEXT("ThemeOverrideRoofMaterial"), Theme && Theme->bOverrideRoof, K)
					|| !Attribute(M, TEXT("FloorMaterial"), R.FloorMaterial, K)
					|| !Attribute(M, TEXT("WallMaterial"), R.WallMaterial, K)
					|| !Attribute(M, TEXT("RoofMaterial"), R.RoofMaterial, K)) return false;
				Data->GetMutablePoints().Add(P);
			}
			if (Data->GetNumPoints() == 0) continue;
			for (const FName Pin : { FEFCalystoNativeRoomPins::NativeRooms, FEFCalystoNativeRoomPins::RoomContexts })
			{
				FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Sources[S]);
				Output.Data = Data; Output.Pin = Pin;
			}
		}
		return true;
	}

	static bool Execute(FPCGContext* Context, const FEFCalystoNativeRoomConfig& Config, FString& Error)
	{
		if (!Config.Validate(Error)) return false;
		TArray<FPCGTaggedData> Sources;
		TArray<FRecord> Records;
		for (const FName Pin : { FEFCalystoNativeRoomPins::MainRooms, FEFCalystoNativeRoomPins::SideRooms })
		{
			for (const FPCGTaggedData& Tagged : Context->InputData.GetInputsByPin(Pin))
			{
				const UPCGBasePointData* Data = Cast<UPCGBasePointData>(Tagged.Data);
				if (!Data || Data->GetNumPoints() > Config.MaximumRooms - Records.Num())
				{ Error = TEXT("Native room input is not bounded point data."); return false; }
				const int32 S = Sources.Add(Tagged);
				const UPCGMetadata* M = Data->ConstMetadata();
				const auto* Flags = M ? M->GetConstTypedAttribute<int32>(TEXT("EF_RoomFlags")) : nullptr;
				if (M && M->HasAttribute(TEXT("EF_RoomFlags")) && !Flags)
				{ Error = TEXT("Upstream EF_RoomFlags must be int32."); return false; }
				const FConstPCGPointValueRanges Ranges(Data);
				for (int32 I = 0; I < Data->GetNumPoints(); ++I)
				{
					FRecord R; R.Source = S; R.Point = Ranges.GetPoint(I);
					if (R.Point.Transform.ContainsNaN()) { Error = TEXT("Native room transform is non-finite."); return false; }
					R.WorldBounds = R.Point.GetLocalBounds().TransformBy(R.Point.Transform);
					R.Room.LocalBounds = R.WorldBounds.TransformBy(Config.DungeonTransform.Inverse());
					R.Room.Transform = R.Point.Transform;
					R.Room.bMainPath = Pin == FEFCalystoNativeRoomPins::MainRooms;
					const int32 Mask = Flags ? Flags->GetValueFromItemKey(R.Point.MetadataEntry) : 0;
					if ((Mask & ~12) != 0) { Error = TEXT("Upstream protection may only mark Critical or Progression rooms."); return false; }
					R.Room.Protection = EEFCalystoProtectedRoom(Mask);
					R.Geometry = GeometryKey(R.Room.LocalBounds, Config.QuantizationCm);
					R.ExactGeometry = ExactKey(R.Room.LocalBounds);
					Records.Add(MoveTemp(R));
				}
			}
		}
		if (Records.IsEmpty())
		{
			Error = FString::Printf(TEXT("NativeTopologyEmpty: Native room inputs contain no rooms (%s)."),
				*DescribeInputs(Context->InputData, Config));
			return false;
		}
		Records.Sort([](const FRecord& A, const FRecord& B)
		{
			if (A.Geometry != B.Geometry) return A.Geometry < B.Geometry;
			return A.ExactGeometry != B.ExactGeometry ? A.ExactGeometry < B.ExactGeometry : A.Room.bMainPath > B.Room.bMainPath;
		});
		for (int32 I = Records.Num() - 1; I > 0; --I)
		{
			if (Records[I].Room.LocalBounds == Records[I-1].Room.LocalBounds)
			{
				Records[I-1].Room.Protection |= Records[I].Room.Protection;
				Records[I-1].Room.bMainPath |= Records[I].Room.bMainPath;
				Records.RemoveAt(I, 1, EAllowShrinking::No);
			}
		}
		int32 Ordinal = 0;
		TSet<int64> Ids;
		for (int32 I = 0; I < Records.Num(); ++I)
		{
			Ordinal = I > 0 && Records[I].Geometry == Records[I-1].Geometry ? Ordinal + 1 : 0;
			Records[I].Room.RoomId = StableId(Records[I].Geometry, Config.TopologySeed, Ordinal);
			if (Ids.Contains(Records[I].Room.RoomId)) { Error = TEXT("Native stable room identity collision."); return false; }
			Ids.Add(Records[I].Room.RoomId);
		}
		const TPair<FName, EEFCalystoProtectedRoom> MarkerPins[] = {
			{FEFCalystoNativeRoomPins::StartRooms, EEFCalystoProtectedRoom::Start},
			{FEFCalystoNativeRoomPins::EndRooms, EEFCalystoProtectedRoom::End},
			{FEFCalystoNativeRoomPins::CriticalRooms, EEFCalystoProtectedRoom::Critical},
			{FEFCalystoNativeRoomPins::ProgressionRooms, EEFCalystoProtectedRoom::Progression}};
		for (const auto& Pair : MarkerPins)
		{
			int32 Count = 0;
			for (const FPCGTaggedData& Tagged : Context->InputData.GetInputsByPin(Pair.Key))
			{
				const UPCGBasePointData* Data = Cast<UPCGBasePointData>(Tagged.Data);
				if (!Data || Data->GetNumPoints() > Config.MaximumRooms - Count) { Error = TEXT("Native protection input is not bounded point data."); return false; }
				const FConstPCGPointValueRanges Ranges(Data);
				for (int32 I = 0; I < Data->GetNumPoints(); ++I)
				{
					++Count;
					const FVector P = Ranges.GetPoint(I).Transform.GetLocation();
					const int32 Room = FindRoom(P, Records);
					if (Room == INDEX_NONE) { Error = TEXT("Native protection marker has no unique owning room."); return false; }
					Records[Room].Room.Protection |= Pair.Value;
					if (Pair.Value == EEFCalystoProtectedRoom::Start || Pair.Value == EEFCalystoProtectedRoom::End)
						for (FRecord& R : Records) R.Room.bDoorClearance |= R.WorldBounds.ComputeSquaredDistanceToPoint(P) <= 200.0 * 200.0;
				}
			}
			if ((Pair.Value == EEFCalystoProtectedRoom::Start || Pair.Value == EEFCalystoProtectedRoom::End) && Count != 1)
			{ Error = TEXT("Native topology requires exactly one Start and one End marker."); return false; }
		}
		TArray<FEFCalystoThemeOpportunity> Opportunities;
		for (const FRecord& R : Records)
		{
			if (EnumHasAllFlags(R.Room.Protection, EEFCalystoProtectedRoom::Start | EEFCalystoProtectedRoom::End))
			{ Error = TEXT("Native Start and End share one room."); return false; }
			FEFCalystoThemeOpportunity O; O.RoomId = R.Room.RoomId; O.Protected = R.Room.Protection;
			for (const FEFCalystoNativeTheme& T : Config.Themes) O.EligibleThemes.Add({T.Id, T.Weight});
			Opportunities.Add(MoveTemp(O));
		}
		TArray<FEFCalystoThemeDecision> Decisions;
		if (!FEFCalystoDirectorProbability::SelectThemedRooms(Config.RandomKey(), Opportunities,
			Config.AdditionalThemeChancePercent, Decisions, Error)) return false;
		for (FRecord& Record : Records)
		{
			auto& R = Record.Room; R.StyleId = Config.StyleId;
			const auto* D = Decisions.FindByPredicate([&R](const FEFCalystoThemeDecision& V) { return V.RoomId == R.RoomId; });
			if (D) { R.ThemeId = D->ThemeId; R.bGuaranteedTheme = D->bGuaranteed; }
			const auto* T = Config.Themes.FindByPredicate([&R](const FEFCalystoNativeTheme& V) { return V.Id == R.ThemeId; });
			R.FloorMaterial = T ? T->FloorMaterial : Config.FloorMaterial;
			R.WallMaterial = T ? T->WallMaterial : Config.WallMaterial;
			R.RoofMaterial = T ? T->RoofMaterial : Config.RoofMaterial;
		}
		if (!Emit(Context, Sources, Records, Config)) { Error = TEXT("Native room metadata could not be written."); return false; }
		return true;
	}
}

class FPCGCalystoDirectorRoomElement final : public IPCGElement
{
	virtual bool SupportsBasePointDataInputs(FPCGContext*) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override
	{
		const auto* Settings = Context ? Context->GetInputSettings<UPCGCalystoDirectorRoomSettings>() : nullptr;
		FString Error;
		if (!Settings || !EFCalystoNativeRooms::Execute(Context, Settings->Config, Error))
		{
			if (Context) { Context->OutputData.TaggedData.Reset(); PCGLog::LogErrorOnGraph(FText::FromString(Error.IsEmpty() ? TEXT("Missing native Director settings.") : Error), Context); }
		}
		return true;
	}
};

namespace EFCalystoRoomOverrideOwner
{
	static const FName CanonicalOwnerId(TEXT("EF_RoomId"));
	static const FName CanonicalWallMaterial(TEXT("WallMaterial"));

	struct FActiveRoomOverride
	{
		int64 OwnerId = 0;
		FSoftObjectPath WallMaterial;
	};

	struct FValidatedPieces
	{
		const FPCGTaggedData* Tagged = nullptr;
		const UPCGBasePointData* Points = nullptr;
	};

	static bool ReadOwner(const FPCGDataCollection& Input, FActiveRoomOverride& OutOwner, FString& Error)
	{
		const TArray<FPCGTaggedData>& Overrides = Input.GetInputsByPin(FEFCalystoRoomOverrideOwnerPins::RoomOverride);
		if (Overrides.Num() != 1)
		{
			Error = FString::Printf(TEXT("Calysto room-owner bridge requires exactly one active RoomOverride, got %d."), Overrides.Num());
			return false;
		}
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Overrides[0].Data);
		if (!Points || Points->GetNumPoints() != 1 || !Points->ConstMetadata())
		{
			Error = TEXT("Calysto room-owner bridge requires one typed RoomOverride point with metadata.");
			return false;
		}
		const FPCGMetadataAttribute<int64>* Ids = Points->ConstMetadata()->GetConstTypedAttribute<int64>(CanonicalOwnerId);
		if (!Ids)
		{
			Error = Points->ConstMetadata()->HasAttribute(CanonicalOwnerId)
				? TEXT("Calysto RoomOverride EF_RoomId must be int64.")
				: TEXT("Calysto RoomOverride lacks EF_RoomId.");
			return false;
		}
		const FPCGMetadataAttribute<FSoftObjectPath>* Materials = Points->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(CanonicalWallMaterial);
		if (!Materials)
		{
			Error = Points->ConstMetadata()->HasAttribute(CanonicalWallMaterial)
				? TEXT("Calysto RoomOverride WallMaterial must be FSoftObjectPath.")
				: TEXT("Calysto RoomOverride lacks WallMaterial.");
			return false;
		}
		const FConstPCGPointValueRanges Ranges(Points);
		const FPCGPoint OwnerPoint = Ranges.GetPoint(0);
		OutOwner.OwnerId = Ids->GetValueFromItemKey(OwnerPoint.MetadataEntry);
		OutOwner.WallMaterial = Materials->GetValueFromItemKey(OwnerPoint.MetadataEntry);
		if (OwnerPoint.Transform.ContainsNaN() || OutOwner.OwnerId <= 0
			|| !OutOwner.WallMaterial.IsValid() || !Cast<UMaterialInterface>(OutOwner.WallMaterial.ResolveObject()))
		{
			Error = TEXT("Calysto RoomOverride has a nonfinite transform, invalid EF_RoomId or non-resident WallMaterial.");
			return false;
		}
		return true;
	}

	static bool CopyOwner(FPCGContext* Context, FString& Error)
	{
		if (!Context) { Error = TEXT("Calysto room-owner bridge has no PCG context."); return false; }
		FActiveRoomOverride Owner;
		if (!ReadOwner(Context->InputData, Owner, Error)) return false;
		const TArray<FPCGTaggedData>& Inputs = Context->InputData.GetInputsByPin(FEFCalystoRoomOverrideOwnerPins::Pieces);
		if (Inputs.IsEmpty()) { Error = TEXT("Calysto room-owner bridge requires final room pieces."); return false; }
		TArray<FValidatedPieces> Validated;
		Validated.Reserve(Inputs.Num());
		int32 TotalPoints = 0;
		for (int32 TaggedIndex = 0; TaggedIndex < Inputs.Num(); ++TaggedIndex)
		{
			const FPCGTaggedData& Tagged = Inputs[TaggedIndex];
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Points || Points->GetNumPoints() > 65536 - TotalPoints)
			{
				Error = FString::Printf(TEXT("Calysto room-owner bridge received unbounded non-point pieces at tagged input %d."), TaggedIndex);
				return false;
			}
			TotalPoints += Points->GetNumPoints();
			const UPCGMetadata* Metadata = Points->ConstMetadata();
			if (Points->GetNumPoints() > 0 && !Metadata)
			{
				Error = FString::Printf(TEXT("Calysto room-owner bridge pieces lack metadata at tagged input %d."), TaggedIndex);
				return false;
			}
			const FPCGMetadataAttribute<int64>* Existing = Metadata ? Metadata->GetConstTypedAttribute<int64>(CanonicalOwnerId) : nullptr;
			if (!Existing && Metadata && Metadata->HasAttribute(CanonicalOwnerId))
			{
				Error = FString::Printf(TEXT("Calysto room-owner bridge pieces have a non-int64 EF_RoomId at tagged input %d."), TaggedIndex);
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Points);
			for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
			{
				const FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const int64 ExistingOwnerId = Existing ? Existing->GetValueFromItemKey(Point.MetadataEntry) : 0;
				// ToPoint_4 is the exact output of this Loop_2 RoomOverride iteration.
				// A positive source EF_RoomId may describe the wall before the native
				// intersection (including a shared boundary); it is not the authority for
				// this emitted themed piece. The active one-point RoomOverride above is.
				// Preserve only the validity check here, then replace the source identity
				// below without a proximity, ordering or heuristic lookup.
				if (Point.Transform.ContainsNaN() || ExistingOwnerId < 0)
				{
					Error = FString::Printf(TEXT("Calysto room-owner bridge found invalid native ownership at tagged input %d point %d."), TaggedIndex, PointIndex);
					return false;
				}
			}
			Validated.Add({&Tagged, Points});
		}

		TArray<FPCGTaggedData> Outputs;
		Outputs.Reserve(Validated.Num());
		for (const FValidatedPieces& Input : Validated)
		{
			UPCGPointData* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			Data->InitializeFromData(Input.Points);
			UPCGMetadata* Metadata = Data->MutableMetadata();
			if (!Metadata) { Error = TEXT("Calysto room-owner bridge cannot allocate output metadata."); return false; }
			Metadata->DeleteAttribute(CanonicalOwnerId);
			Metadata->DeleteAttribute(CanonicalWallMaterial);
			FPCGMetadataAttribute<int64>* OutputIds = Metadata->FindOrCreateAttribute<int64>(CanonicalOwnerId, 0, false, true);
			FPCGMetadataAttribute<FSoftObjectPath>* OutputMaterials = Metadata->FindOrCreateAttribute<FSoftObjectPath>(CanonicalWallMaterial, FSoftObjectPath{}, false, true);
			if (!OutputIds || !OutputMaterials) { Error = TEXT("Calysto room-owner bridge cannot create EF_RoomId and WallMaterial."); return false; }
			const FConstPCGPointValueRanges Ranges(Input.Points);
			for (int32 PointIndex = 0; PointIndex < Input.Points->GetNumPoints(); ++PointIndex)
			{
				FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const PCGMetadataEntryKey SourceKey = Point.MetadataEntry;
				Point.MetadataEntry = Metadata->AddEntry(SourceKey);
				OutputIds->SetValue(Point.MetadataEntry, Owner.OwnerId);
				OutputMaterials->SetValue(Point.MetadataEntry, Owner.WallMaterial);
				Data->GetMutablePoints().Add(Point);
			}
			FPCGTaggedData& Output = Outputs.Emplace_GetRef(*Input.Tagged);
			Output.Data = Data;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
		Context->OutputData.TaggedData.Append(MoveTemp(Outputs));
		return true;
	}
}

class FPCGCalystoRoomOverrideOwnerElement final : public IPCGElement
{
	virtual bool SupportsBasePointDataInputs(FPCGContext*) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override
	{
		const auto* Settings = Context ? Context->GetInputSettings<UPCGCalystoRoomOverrideOwnerSettings>() : nullptr;
		FString Error;
		if (!Settings || !EFCalystoRoomOverrideOwner::CopyOwner(Context, Error))
		{
			if (Context)
			{
				Context->OutputData.TaggedData.Reset();
				PCGLog::LogErrorOnGraph(FText::FromString(Error.IsEmpty() ? TEXT("Missing Calysto room-owner bridge settings.") : Error), Context);
			}
		}
		return true;
	}
};

namespace EFCalystoDoorMaterialMetadata
{
	static const FName CarrierMaterial(TEXT("EF_DoorMaterialCarrier"));
}

namespace EFCalystoDoorMaterialCarrier
{
	static const FName CanonicalWallMaterial(TEXT("WallMaterial"));

	struct FValidatedPieces
	{
		const FPCGTaggedData* Tagged = nullptr;
		const UPCGBasePointData* Points = nullptr;
	};

	static bool Copy(FPCGContext* Context, FString& Error)
	{
		if (!Context)
		{
			Error = TEXT("Calysto door material carrier has no PCG context.");
			return false;
		}
		const TArray<FPCGTaggedData>& Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
		if (Inputs.IsEmpty()) return true;
		TArray<FValidatedPieces> Validated;
		Validated.Reserve(Inputs.Num());
		int32 TotalPoints = 0;
		for (int32 TaggedIndex = 0; TaggedIndex < Inputs.Num(); ++TaggedIndex)
		{
			const FPCGTaggedData& Tagged = Inputs[TaggedIndex];
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Points || Points->GetNumPoints() > 65536 - TotalPoints)
			{
				Error = FString::Printf(TEXT("Calysto door material carrier received unbounded non-point data at tagged input %d."), TaggedIndex);
				return false;
			}
			TotalPoints += Points->GetNumPoints();
			if (Points->GetNumPoints() == 0)
			{
				Validated.Add({&Tagged, Points});
				continue;
			}
			const UPCGMetadata* Metadata = Points->ConstMetadata();
			const FPCGMetadataAttribute<FSoftObjectPath>* Materials = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(CanonicalWallMaterial) : nullptr;
			if (!Metadata || !Materials || Metadata->HasAttribute(EFCalystoDoorMaterialMetadata::CarrierMaterial))
			{
				Error = FString::Printf(TEXT("Calysto door material carrier requires a fresh typed WallMaterial at tagged input %d."), TaggedIndex);
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Points);
			for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
			{
				const FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const FSoftObjectPath Material = Materials->GetValueFromItemKey(Point.MetadataEntry);
				if (Point.Transform.ContainsNaN() || !Material.IsValid() || !Cast<UMaterialInterface>(Material.ResolveObject()))
				{
					Error = FString::Printf(TEXT("Calysto door material carrier has an invalid transform or resident WallMaterial at tagged input %d point %d."), TaggedIndex, PointIndex);
					return false;
				}
			}
			Validated.Add({&Tagged, Points});
		}

		TArray<FPCGTaggedData> Outputs;
		Outputs.Reserve(Validated.Num());
		for (const FValidatedPieces& Input : Validated)
		{
			UPCGPointData* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			Data->InitializeFromData(Input.Points);
			if (Input.Points->GetNumPoints() == 0)
			{
				FPCGTaggedData& Output = Outputs.Emplace_GetRef(*Input.Tagged);
				Output.Data = Data;
				Output.Pin = PCGPinConstants::DefaultOutputLabel;
				continue;
			}
			UPCGMetadata* Metadata = Data->MutableMetadata();
			if (!Metadata)
			{
				Error = TEXT("Calysto door material carrier cannot allocate output metadata.");
				return false;
			}
			FPCGMetadataAttribute<FSoftObjectPath>* Carrier = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
				EFCalystoDoorMaterialMetadata::CarrierMaterial, FSoftObjectPath{}, false, true);
			const UPCGMetadata* InputMetadata = Input.Points->ConstMetadata();
			const FPCGMetadataAttribute<FSoftObjectPath>* InputMaterials = InputMetadata
				? InputMetadata->GetConstTypedAttribute<FSoftObjectPath>(CanonicalWallMaterial) : nullptr;
			if (!Carrier || !InputMaterials)
			{
				Error = TEXT("Calysto door material carrier lost validated WallMaterial before output.");
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Input.Points);
			for (int32 PointIndex = 0; PointIndex < Input.Points->GetNumPoints(); ++PointIndex)
			{
				FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const PCGMetadataEntryKey SourceKey = Point.MetadataEntry;
				const FSoftObjectPath Material = InputMaterials->GetValueFromItemKey(SourceKey);
				Point.MetadataEntry = Metadata->AddEntry(SourceKey);
				Carrier->SetValue(Point.MetadataEntry, Material);
				Data->GetMutablePoints().Add(Point);
			}
			FPCGTaggedData& Output = Outputs.Emplace_GetRef(*Input.Tagged);
			Output.Data = Data;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
		Context->OutputData.TaggedData.Append(MoveTemp(Outputs));
		return true;
	}
}

class FPCGCalystoDoorMaterialCarrierElement final : public IPCGElement
{
	virtual bool SupportsBasePointDataInputs(FPCGContext*) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override
	{
		const auto* Settings = Context ? Context->GetInputSettings<UPCGCalystoDoorMaterialCarrierSettings>() : nullptr;
		FString Error;
		if (!Settings || !EFCalystoDoorMaterialCarrier::Copy(Context, Error))
		{
			if (Context)
			{
				Context->OutputData.TaggedData.Reset();
				PCGLog::LogErrorOnGraph(FText::FromString(Error.IsEmpty() ? TEXT("Missing Calysto door material carrier settings.") : Error), Context);
			}
		}
		return true;
	}
};

namespace EFCalystoDoorMaterialProvenance
{
	static const FName CanonicalOwnerId(TEXT("EF_RoomId"));
	static const FName CanonicalWallMaterial(TEXT("WallMaterial"));
	static const FName StyleOwned(TEXT("EF_StyleOwnedWall"));

	struct FValidatedPieces
	{
		const FPCGTaggedData* Tagged = nullptr;
		const UPCGBasePointData* Points = nullptr;
	};

	static bool ReadSourceMaterials(const TArray<FPCGTaggedData>& Sources,
		TSet<FSoftObjectPath>& ValidMaterials, FString& Error)
	{
		int32 TotalPoints = 0;
		for (int32 TaggedIndex = 0; TaggedIndex < Sources.Num(); ++TaggedIndex)
		{
			const FPCGTaggedData& Tagged = Sources[TaggedIndex];
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Points || Points->GetNumPoints() > 65536 - TotalPoints)
			{
				Error = FString::Printf(TEXT("Calysto door material provenance received unbounded non-point DoorSource data at tagged input %d."), TaggedIndex);
				return false;
			}
			TotalPoints += Points->GetNumPoints();
			if (Points->GetNumPoints() == 0) continue;
			const UPCGMetadata* Metadata = Points->ConstMetadata();
			if (!Metadata)
			{
				Error = FString::Printf(TEXT("Calysto door material provenance DoorSource lacks metadata at tagged input %d."), TaggedIndex);
				return false;
			}
			const FPCGMetadataAttribute<int64>* Owners = Metadata->GetConstTypedAttribute<int64>(CanonicalOwnerId);
			const FPCGMetadataAttribute<FSoftObjectPath>* Materials = Metadata->GetConstTypedAttribute<FSoftObjectPath>(CanonicalWallMaterial);
			const FPCGMetadataAttribute<FSoftObjectPath>* Carriers = Metadata->GetConstTypedAttribute<FSoftObjectPath>(EFCalystoDoorMaterialMetadata::CarrierMaterial);
			const FPCGMetadataAttribute<bool>* SourceStyleOwned = Metadata->GetConstTypedAttribute<bool>(StyleOwned);
			if (!Owners || !Materials || !Carriers || (!SourceStyleOwned && Metadata->HasAttribute(StyleOwned)))
			{
				Error = FString::Printf(TEXT("Calysto door material provenance DoorSource requires typed EF_RoomId, WallMaterial, EF_DoorMaterialCarrier and an absent or bool EF_StyleOwnedWall at tagged input %d (owner=%s material=%s carrier=%s style=%s)."),
					TaggedIndex, Owners ? TEXT("int64") : (Metadata->HasAttribute(CanonicalOwnerId) ? TEXT("wrong") : TEXT("missing")),
					Materials ? TEXT("FSoftObjectPath") : (Metadata->HasAttribute(CanonicalWallMaterial) ? TEXT("wrong") : TEXT("missing")),
					Carriers ? TEXT("FSoftObjectPath") : (Metadata->HasAttribute(EFCalystoDoorMaterialMetadata::CarrierMaterial) ? TEXT("wrong") : TEXT("missing")),
					SourceStyleOwned ? TEXT("bool") : (Metadata->HasAttribute(StyleOwned) ? TEXT("wrong") : TEXT("absent")));
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Points);
			for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
			{
				const FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const int64 RoomId = Owners->GetValueFromItemKey(Point.MetadataEntry);
				const FSoftObjectPath Material = Materials->GetValueFromItemKey(Point.MetadataEntry);
				const FSoftObjectPath Carrier = Carriers->GetValueFromItemKey(Point.MetadataEntry);
				const bool bStyleOwned = SourceStyleOwned && SourceStyleOwned->GetValueFromItemKey(Point.MetadataEntry);
				if (Point.Transform.ContainsNaN() || (bStyleOwned ? RoomId != 0 : RoomId <= 0) || !Material.IsValid() || Material != Carrier
					|| !Cast<UMaterialInterface>(Material.ResolveObject()))
				{
					Error = FString::Printf(TEXT("Calysto door material provenance DoorSource has invalid transform, EF_RoomId or copied resident material at tagged input %d point %d."), TaggedIndex, PointIndex);
					return false;
				}
				ValidMaterials.Add(Carrier);
			}
		}
		return true;
	}

	static bool Restore(FPCGContext* Context, FString& Error)
	{
		if (!Context)
		{
			Error = TEXT("Calysto door material provenance has no PCG context.");
			return false;
		}
		TSet<FSoftObjectPath> ValidMaterials;
		if (!ReadSourceMaterials(Context->InputData.GetInputsByPin(FEFCalystoDoorMaterialProvenancePins::DoorSource), ValidMaterials, Error)) return false;
		const TArray<FPCGTaggedData>& FinalInputs = Context->InputData.GetInputsByPin(FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces);
		if (FinalInputs.IsEmpty()) return true;

		TArray<FValidatedPieces> Validated;
		Validated.Reserve(FinalInputs.Num());
		int32 TotalPoints = 0;
		for (int32 TaggedIndex = 0; TaggedIndex < FinalInputs.Num(); ++TaggedIndex)
		{
			const FPCGTaggedData& Tagged = FinalInputs[TaggedIndex];
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Points || Points->GetNumPoints() > 65536 - TotalPoints)
			{
				Error = FString::Printf(TEXT("Calysto door material provenance received unbounded non-point FinalDoorPieces data at tagged input %d."), TaggedIndex);
				return false;
			}
			TotalPoints += Points->GetNumPoints();
			if (Points->GetNumPoints() > 0 && !Points->ConstMetadata())
			{
				Error = FString::Printf(TEXT("Calysto door material provenance FinalDoorPieces lack metadata at tagged input %d."), TaggedIndex);
				return false;
			}
			const UPCGMetadata* Metadata = Points->ConstMetadata();
			const FPCGMetadataAttribute<int64>* Owners = Metadata ? Metadata->GetConstTypedAttribute<int64>(CanonicalOwnerId) : nullptr;
			const FPCGMetadataAttribute<FSoftObjectPath>* ExistingMaterials = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(CanonicalWallMaterial) : nullptr;
			const FPCGMetadataAttribute<FSoftObjectPath>* Carriers = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(EFCalystoDoorMaterialMetadata::CarrierMaterial) : nullptr;
			const FPCGMetadataAttribute<bool>* FinalStyleOwned = Metadata ? Metadata->GetConstTypedAttribute<bool>(StyleOwned) : nullptr;
			if (!Owners || !Carriers || (!ExistingMaterials && Metadata && Metadata->HasAttribute(CanonicalWallMaterial))
				|| (!FinalStyleOwned && Metadata && Metadata->HasAttribute(StyleOwned)))
			{
				Error = FString::Printf(TEXT("Calysto door material provenance FinalDoorPieces requires typed EF_RoomId, EF_DoorMaterialCarrier and an absent or typed WallMaterial at tagged input %d."), TaggedIndex);
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Points);
			for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
			{
				const FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const int64 RoomId = Owners->GetValueFromItemKey(Point.MetadataEntry);
				const FSoftObjectPath Carrier = Carriers->GetValueFromItemKey(Point.MetadataEntry);
				const bool bStyleOwned = FinalStyleOwned && FinalStyleOwned->GetValueFromItemKey(Point.MetadataEntry);
				if (Point.Transform.ContainsNaN() || (bStyleOwned ? RoomId != 0 : RoomId <= 0) || !Carrier.IsValid()
					|| !Cast<UMaterialInterface>(Carrier.ResolveObject()) || !ValidMaterials.Contains(Carrier))
				{
					Error = FString::Printf(TEXT("Calysto door material provenance FinalDoorPieces has an invalid or unbound EF_RoomId/material carrier at tagged input %d point %d."), TaggedIndex, PointIndex);
					return false;
				}
				if (ExistingMaterials && ExistingMaterials->GetValueFromItemKey(Point.MetadataEntry) != Carrier)
				{
					Error = FString::Printf(TEXT("Calysto door material provenance FinalDoorPieces disagrees with its exact DoorSource material at tagged input %d point %d."), TaggedIndex, PointIndex);
					return false;
				}
			}
			Validated.Add({&Tagged, Points});
		}

		TArray<FPCGTaggedData> Outputs;
		Outputs.Reserve(Validated.Num());
		for (const FValidatedPieces& Input : Validated)
		{
			UPCGPointData* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			Data->InitializeFromData(Input.Points);
			UPCGMetadata* Metadata = Data->MutableMetadata();
			if (!Metadata)
			{
				Error = TEXT("Calysto door material provenance cannot allocate output metadata.");
				return false;
			}
			Metadata->DeleteAttribute(CanonicalOwnerId);
			Metadata->DeleteAttribute(CanonicalWallMaterial);
			Metadata->DeleteAttribute(EFCalystoDoorMaterialMetadata::CarrierMaterial);
			Metadata->DeleteAttribute(StyleOwned);
			FPCGMetadataAttribute<int64>* OutputOwners = Metadata->FindOrCreateAttribute<int64>(CanonicalOwnerId, 0, false, true);
			FPCGMetadataAttribute<FSoftObjectPath>* OutputMaterials = Metadata->FindOrCreateAttribute<FSoftObjectPath>(CanonicalWallMaterial, FSoftObjectPath{}, false, true);
			FPCGMetadataAttribute<bool>* OutputStyleOwned = Metadata->FindOrCreateAttribute<bool>(StyleOwned, false, false, true);
			if (!OutputOwners || !OutputMaterials || !OutputStyleOwned)
			{
				Error = TEXT("Calysto door material provenance cannot create typed EF_RoomId and WallMaterial.");
				return false;
			}
			const UPCGMetadata* InputMetadata = Input.Points->ConstMetadata();
			const FPCGMetadataAttribute<int64>* InputOwners = InputMetadata ? InputMetadata->GetConstTypedAttribute<int64>(CanonicalOwnerId) : nullptr;
			const FPCGMetadataAttribute<FSoftObjectPath>* InputCarriers = InputMetadata
				? InputMetadata->GetConstTypedAttribute<FSoftObjectPath>(EFCalystoDoorMaterialMetadata::CarrierMaterial) : nullptr;
			const FPCGMetadataAttribute<bool>* InputStyleOwned = InputMetadata ? InputMetadata->GetConstTypedAttribute<bool>(StyleOwned) : nullptr;
			if (!InputOwners || !InputCarriers || (!InputStyleOwned && InputMetadata && InputMetadata->HasAttribute(StyleOwned)))
			{
				Error = TEXT("Calysto door material provenance lost validated final ownership/material carrier before output.");
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Input.Points);
			for (int32 PointIndex = 0; PointIndex < Input.Points->GetNumPoints(); ++PointIndex)
			{
				FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const PCGMetadataEntryKey SourceKey = Point.MetadataEntry;
				const int64 RoomId = InputOwners->GetValueFromItemKey(SourceKey);
				const FSoftObjectPath Material = InputCarriers->GetValueFromItemKey(SourceKey);
				const bool bStyleOwned = InputStyleOwned && InputStyleOwned->GetValueFromItemKey(SourceKey);
				if (!ValidMaterials.Contains(Material))
				{
					Error = TEXT("Calysto door material provenance lost an exact validated source material carrier before output.");
					return false;
				}
				Point.MetadataEntry = Metadata->AddEntry(SourceKey);
				OutputOwners->SetValue(Point.MetadataEntry, RoomId);
				OutputMaterials->SetValue(Point.MetadataEntry, Material);
				OutputStyleOwned->SetValue(Point.MetadataEntry, bStyleOwned);
				Data->GetMutablePoints().Add(Point);
			}
			FPCGTaggedData& Output = Outputs.Emplace_GetRef(*Input.Tagged);
			Output.Data = Data;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
		Context->OutputData.TaggedData.Append(MoveTemp(Outputs));
		return true;
	}
}

class FPCGCalystoDoorMaterialProvenanceElement final : public IPCGElement
{
	virtual bool SupportsBasePointDataInputs(FPCGContext*) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override
	{
		const auto* Settings = Context ? Context->GetInputSettings<UPCGCalystoDoorMaterialProvenanceSettings>() : nullptr;
		FString Error;
		if (!Settings || !EFCalystoDoorMaterialProvenance::Restore(Context, Error))
		{
			if (Context)
			{
				Context->OutputData.TaggedData.Reset();
				PCGLog::LogErrorOnGraph(FText::FromString(Error.IsEmpty() ? TEXT("Missing Calysto door material provenance settings.") : Error), Context);
			}
		}
		return true;
	}
};

namespace EFCalystoStyleWallFallback
{
	static const FName CanonicalOwnerId(TEXT("EF_RoomId"));
	static const FName StyleOwned(TEXT("EF_StyleOwnedWall"));
	static const FName WallMaterial(TEXT("WallMaterial"));

	struct FValidatedPieces
	{
		const FPCGTaggedData* Tagged = nullptr;
		const UPCGBasePointData* Points = nullptr;
	};

	static bool CopyStyleMaterial(FPCGContext* Context, const FSoftObjectPath& StyleMaterial, FString& Error)
	{
		if (!Context)
		{
			Error = TEXT("Calysto Style wall fallback has no PCG context.");
			return false;
		}
		if (!StyleMaterial.IsValid() || !Cast<UMaterialInterface>(StyleMaterial.ResolveObject()))
		{
			Error = TEXT("Calysto Style wall fallback requires a resident selected Style wall material.");
			return false;
		}
		const TArray<FPCGTaggedData>& Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
		if (Inputs.IsEmpty())
		{
			Error = TEXT("Calysto Style wall fallback requires the exact default-wall Difference output.");
			return false;
		}

		TArray<FValidatedPieces> Validated;
		Validated.Reserve(Inputs.Num());
		int32 TotalPoints = 0;
		for (int32 TaggedIndex = 0; TaggedIndex < Inputs.Num(); ++TaggedIndex)
		{
			const FPCGTaggedData& Tagged = Inputs[TaggedIndex];
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
			if (!Points || Points->GetNumPoints() > 65536 - TotalPoints)
			{
				Error = FString::Printf(TEXT("Calysto Style wall fallback received unbounded non-point data at tagged input %d."), TaggedIndex);
				return false;
			}
			TotalPoints += Points->GetNumPoints();
			const UPCGMetadata* Metadata = Points->ConstMetadata();
			if (Points->GetNumPoints() > 0 && !Metadata)
			{
				Error = FString::Printf(TEXT("Calysto Style wall fallback pieces lack metadata at tagged input %d."), TaggedIndex);
				return false;
			}
			const FPCGMetadataAttribute<int64>* ExistingOwners = Metadata ? Metadata->GetConstTypedAttribute<int64>(CanonicalOwnerId) : nullptr;
			if (!ExistingOwners && Metadata && Metadata->HasAttribute(CanonicalOwnerId))
			{
				Error = FString::Printf(TEXT("Calysto Style wall fallback EF_RoomId must be int64 at tagged input %d."), TaggedIndex);
				return false;
			}
			const FPCGMetadataAttribute<FSoftObjectPath>* ExistingSoftMaterial = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(WallMaterial) : nullptr;
			const FPCGMetadataAttribute<FString>* ExistingStringMaterial = Metadata ? Metadata->GetConstTypedAttribute<FString>(WallMaterial) : nullptr;
			if (!ExistingSoftMaterial && !ExistingStringMaterial && Metadata && Metadata->HasAttribute(WallMaterial))
			{
				Error = FString::Printf(TEXT("Calysto Style wall fallback WallMaterial has an unsupported type at tagged input %d."), TaggedIndex);
				return false;
			}
			const FPCGMetadataAttribute<bool>* ExistingStyleOwned = Metadata ? Metadata->GetConstTypedAttribute<bool>(StyleOwned) : nullptr;
			if (!ExistingStyleOwned && Metadata && Metadata->HasAttribute(StyleOwned))
			{
				Error = FString::Printf(TEXT("Calysto Style wall fallback EF_StyleOwnedWall must be bool at tagged input %d."), TaggedIndex);
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Points);
			for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
			{
				const FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const PCGMetadataEntryKey Key = Point.MetadataEntry;
				const int64 ExistingOwner = ExistingOwners ? ExistingOwners->GetValueFromItemKey(Key) : 0;
				const bool bAlreadyStyleOwned = ExistingStyleOwned && ExistingStyleOwned->GetValueFromItemKey(Key);
				// Difference_14 is the exact native branch after themed room geometry has
				// been removed. It has no active Theme RoomOverride. A positive EF_RoomId
				// here is stale input metadata, not an ownership claim for the residual
				// surface; output it as explicit Style authority. A prior Style marker
				// proves this bridge was wired twice and must fail before producing output.
				if (Point.Transform.ContainsNaN() || ExistingOwner < 0 || bAlreadyStyleOwned)
				{
					Error = FString::Printf(TEXT("Calysto Style wall fallback received an invalid or already Style-owned native piece at tagged input %d point %d."), TaggedIndex, PointIndex);
					return false;
				}
			}
			Validated.Add({&Tagged, Points});
		}

		TArray<FPCGTaggedData> Outputs;
		Outputs.Reserve(Validated.Num());
		for (const FValidatedPieces& Input : Validated)
		{
			UPCGPointData* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
			Data->InitializeFromData(Input.Points);
			UPCGMetadata* Metadata = Data->MutableMetadata();
			if (!Metadata)
			{
				Error = TEXT("Calysto Style wall fallback cannot allocate output metadata.");
				return false;
			}
			Metadata->DeleteAttribute(CanonicalOwnerId);
			Metadata->DeleteAttribute(WallMaterial);
			Metadata->DeleteAttribute(StyleOwned);
			FPCGMetadataAttribute<int64>* OwnerIds = Metadata->FindOrCreateAttribute<int64>(CanonicalOwnerId, 0, false, true);
			FPCGMetadataAttribute<FSoftObjectPath>* Materials = Metadata->FindOrCreateAttribute<FSoftObjectPath>(WallMaterial, FSoftObjectPath{}, false, true);
			FPCGMetadataAttribute<bool>* Owned = Metadata->FindOrCreateAttribute<bool>(StyleOwned, false, false, true);
			if (!OwnerIds || !Materials || !Owned)
			{
				Error = TEXT("Calysto Style wall fallback cannot create its explicit Style provenance.");
				return false;
			}
			const FConstPCGPointValueRanges Ranges(Input.Points);
			for (int32 PointIndex = 0; PointIndex < Input.Points->GetNumPoints(); ++PointIndex)
			{
				FPCGPoint Point = Ranges.GetPoint(PointIndex);
				const PCGMetadataEntryKey SourceKey = Point.MetadataEntry;
				Point.MetadataEntry = Metadata->AddEntry(SourceKey);
				OwnerIds->SetValue(Point.MetadataEntry, 0);
				Materials->SetValue(Point.MetadataEntry, StyleMaterial);
				Owned->SetValue(Point.MetadataEntry, true);
				Data->GetMutablePoints().Add(Point);
			}
			FPCGTaggedData& Output = Outputs.Emplace_GetRef(*Input.Tagged);
			Output.Data = Data;
			Output.Pin = PCGPinConstants::DefaultOutputLabel;
		}
		Context->OutputData.TaggedData.Append(MoveTemp(Outputs));
		return true;
	}
}

class FPCGCalystoStyleWallFallbackElement final : public IPCGElement
{
	virtual bool SupportsBasePointDataInputs(FPCGContext*) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override
	{
		const auto* Settings = Context ? Context->GetInputSettings<UPCGCalystoStyleWallFallbackSettings>() : nullptr;
		FString Error;
		if (!Settings || !EFCalystoStyleWallFallback::CopyStyleMaterial(Context, Settings->StyleWallMaterial, Error))
		{
			if (Context)
			{
				Context->OutputData.TaggedData.Reset();
				PCGLog::LogErrorOnGraph(FText::FromString(Error.IsEmpty() ? TEXT("Missing Calysto Style wall fallback settings.") : Error), Context);
			}
		}
		return true;
	}
};

#if WITH_EDITOR
FText UPCGCalystoDirectorRoomSettings::GetDefaultNodeTitle() const { return FText::FromString(TEXT("Calysto Director Rooms")); }
FText UPCGCalystoRoomOverrideOwnerSettings::GetDefaultNodeTitle() const { return FText::FromString(TEXT("Calysto Room Override Owner")); }
FText UPCGCalystoDoorMaterialCarrierSettings::GetDefaultNodeTitle() const { return FText::FromString(TEXT("Calysto Door Material Carrier")); }
FText UPCGCalystoDoorMaterialProvenanceSettings::GetDefaultNodeTitle() const { return FText::FromString(TEXT("Calysto Door Material Provenance")); }
FText UPCGCalystoStyleWallFallbackSettings::GetDefaultNodeTitle() const { return FText::FromString(TEXT("Calysto Style Wall Fallback")); }
#endif
TArray<FPCGPinProperties> UPCGCalystoDirectorRoomSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	for (const FName P : { FEFCalystoNativeRoomPins::MainRooms, FEFCalystoNativeRoomPins::SideRooms, FEFCalystoNativeRoomPins::StartRooms,
		FEFCalystoNativeRoomPins::EndRooms, FEFCalystoNativeRoomPins::CriticalRooms, FEFCalystoNativeRoomPins::ProgressionRooms }) Pins.Emplace(P, EPCGDataType::Point);
	return Pins;
}
TArray<FPCGPinProperties> UPCGCalystoDirectorRoomSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(FEFCalystoNativeRoomPins::NativeRooms, EPCGDataType::Point), FPCGPinProperties(FEFCalystoNativeRoomPins::RoomContexts, EPCGDataType::Point)};
}
FPCGElementPtr UPCGCalystoDirectorRoomSettings::CreateElement() const { return MakeShared<FPCGCalystoDirectorRoomElement>(); }

TArray<FPCGPinProperties> UPCGCalystoRoomOverrideOwnerSettings::InputPinProperties() const
{
	FPCGPinProperties Owner(FEFCalystoRoomOverrideOwnerPins::RoomOverride, EPCGDataType::Point);
	FPCGPinProperties Pieces(FEFCalystoRoomOverrideOwnerPins::Pieces, EPCGDataType::Point);
	Owner.SetRequiredPin();
	Pieces.SetRequiredPin();
	return {Owner, Pieces};
}

TArray<FPCGPinProperties> UPCGCalystoRoomOverrideOwnerSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}

FPCGElementPtr UPCGCalystoRoomOverrideOwnerSettings::CreateElement() const
{
	return MakeShared<FPCGCalystoRoomOverrideOwnerElement>();
}

TArray<FPCGPinProperties> UPCGCalystoDoorMaterialCarrierSettings::InputPinProperties() const
{
	FPCGPinProperties Pieces(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
	Pieces.SetRequiredPin();
	return {Pieces};
}

TArray<FPCGPinProperties> UPCGCalystoDoorMaterialCarrierSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}

FPCGElementPtr UPCGCalystoDoorMaterialCarrierSettings::CreateElement() const
{
	return MakeShared<FPCGCalystoDoorMaterialCarrierElement>();
}

TArray<FPCGPinProperties> UPCGCalystoDoorMaterialProvenanceSettings::InputPinProperties() const
{
	FPCGPinProperties DoorSource(FEFCalystoDoorMaterialProvenancePins::DoorSource, EPCGDataType::Point);
	FPCGPinProperties FinalDoorPieces(FEFCalystoDoorMaterialProvenancePins::FinalDoorPieces, EPCGDataType::Point);
	DoorSource.SetRequiredPin();
	FinalDoorPieces.SetRequiredPin();
	return {DoorSource, FinalDoorPieces};
}

TArray<FPCGPinProperties> UPCGCalystoDoorMaterialProvenanceSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}

FPCGElementPtr UPCGCalystoDoorMaterialProvenanceSettings::CreateElement() const
{
	return MakeShared<FPCGCalystoDoorMaterialProvenanceElement>();
}

TArray<FPCGPinProperties> UPCGCalystoStyleWallFallbackSettings::InputPinProperties() const
{
	FPCGPinProperties Pieces(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
	Pieces.SetRequiredPin();
	return {Pieces};
}

TArray<FPCGPinProperties> UPCGCalystoStyleWallFallbackSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}

FPCGElementPtr UPCGCalystoStyleWallFallbackSettings::CreateElement() const
{
	return MakeShared<FPCGCalystoStyleWallFallbackElement>();
}

bool FEFCalystoNativeRoomReader::Read(const FPCGDataCollection& Data, const FEFCalystoNativeRoomConfig& Config,
	TArray<FEFCalystoNativeRoom>& Rooms, FString& Error, EEFCalystoNativeRoomReadFailure* OutFailure)
{
	Rooms.Reset(); Error.Reset();
	if (OutFailure) *OutFailure = EEFCalystoNativeRoomReadFailure::Configuration;
	TSet<int64> Ids; int32 Starts = 0, Ends = 0, Guaranteed = 0;
	for (const FPCGTaggedData& T : Data.GetInputsByPin(FEFCalystoNativeRoomPins::RoomContexts))
	{
		const UPCGBasePointData* P = Cast<UPCGBasePointData>(T.Data);
		const UPCGMetadata* M = P ? P->ConstMetadata() : nullptr;
		if (!M || P->GetNumPoints() > Config.MaximumRooms - Rooms.Num()) { Error = TEXT("Room Contexts is missing bounded metadata."); return false; }
		const auto* Id = M->GetConstTypedAttribute<int64>(TEXT("EF_RoomId"));
		const auto* Theme = M->GetConstTypedAttribute<FString>(TEXT("EF_ThemeId"));
		const auto* Style = M->GetConstTypedAttribute<FString>(TEXT("EF_StyleId"));
		const auto* Flags = M->GetConstTypedAttribute<int32>(TEXT("EF_RoomFlags"));
		const auto* Main = M->GetConstTypedAttribute<bool>(TEXT("EF_MainPath"));
		const auto* Door = M->GetConstTypedAttribute<bool>(TEXT("EF_DoorClearance"));
		const auto* Guarantee = M->GetConstTypedAttribute<bool>(TEXT("EF_GuaranteedTheme"));
		const auto* Floor = M->GetConstTypedAttribute<FSoftObjectPath>(TEXT("FloorMaterial"));
		const auto* Wall = M->GetConstTypedAttribute<FSoftObjectPath>(TEXT("WallMaterial"));
		const auto* Roof = M->GetConstTypedAttribute<FSoftObjectPath>(TEXT("RoofMaterial"));
		const auto* Schema = M->GetConstTypedAttribute<FSoftObjectPath>(TEXT("RoomType"));
		if (!Id || !Theme || !Style || !Flags || !Main || !Door || !Guarantee || !Floor || !Wall || !Roof || !Schema)
		{ Error = TEXT("Room Contexts is missing a required typed field."); return false; }
		const FConstPCGPointValueRanges Ranges(P);
		for (int32 I = 0; I < P->GetNumPoints(); ++I)
		{
			const FPCGPoint Point = Ranges.GetPoint(I); const auto K = Point.MetadataEntry;
			FEFCalystoNativeRoom R; R.RoomId = Id->GetValueFromItemKey(K);
			if (R.RoomId <= 0 || Ids.Contains(R.RoomId) || !FGuid::Parse(Theme->GetValueFromItemKey(K), R.ThemeId)
				|| !FGuid::Parse(Style->GetValueFromItemKey(K), R.StyleId) || R.StyleId != Config.StyleId || (Flags->GetValueFromItemKey(K) & ~15) != 0)
			{ Error = TEXT("Room Contexts has duplicate or invalid stable identity."); return false; }
			Ids.Add(R.RoomId); R.Protection = EEFCalystoProtectedRoom(Flags->GetValueFromItemKey(K));
			R.bMainPath = Main->GetValueFromItemKey(K); R.bDoorClearance = Door->GetValueFromItemKey(K); R.bGuaranteedTheme = Guarantee->GetValueFromItemKey(K);
			R.Transform = Point.Transform; R.LocalBounds = Point.GetLocalBounds().TransformBy(Point.Transform).TransformBy(Config.DungeonTransform.Inverse());
			R.FloorMaterial = Floor->GetValueFromItemKey(K); R.WallMaterial = Wall->GetValueFromItemKey(K); R.RoofMaterial = Roof->GetValueFromItemKey(K);
			const auto* Expected = Config.Themes.FindByPredicate([&R](const FEFCalystoNativeTheme& V) { return V.Id == R.ThemeId; });
			if ((R.ThemeId.IsValid() && (!Expected || R.Protection != EEFCalystoProtectedRoom::None))
				|| R.FloorMaterial != (Expected ? Expected->FloorMaterial : Config.FloorMaterial)
				|| R.WallMaterial != (Expected ? Expected->WallMaterial : Config.WallMaterial)
				|| R.RoofMaterial != (Expected ? Expected->RoofMaterial : Config.RoofMaterial)
				|| Schema->GetValueFromItemKey(K) != FSoftObjectPath(Expected ? Expected->RoomSchema.Get() : Config.EmptyRoomSchema.Get())
				|| (R.bGuaranteedTheme && !R.ThemeId.IsValid()))
			{ Error = TEXT("Room Contexts violates Theme protection, material precedence or native schema ownership."); return false; }
			Starts += EnumHasAnyFlags(R.Protection, EEFCalystoProtectedRoom::Start); Ends += EnumHasAnyFlags(R.Protection, EEFCalystoProtectedRoom::End); Guaranteed += R.bGuaranteedTheme;
			Rooms.Add(MoveTemp(R));
		}
	}
	if (Rooms.IsEmpty() || Starts != 1 || Ends != 1 || Guaranteed != 1)
	{
		if (OutFailure && Rooms.IsEmpty()) *OutFailure = EEFCalystoNativeRoomReadFailure::Spatial;
		Error = FString::Printf(TEXT("Room Contexts requires all rooms, unique Start/End and exactly one guaranteed Theme: rooms=%d starts=%d ends=%d guaranteed=%d."),
			Rooms.Num(), Starts, Ends, Guaranteed);
		return false;
	}
	Rooms.Sort([&Config](const auto& A, const auto& B)
	{
		const FString KA = EFCalystoNativeRooms::GeometryKey(A.LocalBounds, Config.QuantizationCm);
		const FString KB = EFCalystoNativeRooms::GeometryKey(B.LocalBounds, Config.QuantizationCm);
		return KA != KB ? KA < KB : EFCalystoNativeRooms::ExactKey(A.LocalBounds) < EFCalystoNativeRooms::ExactKey(B.LocalBounds);
	});
	TArray<FEFCalystoThemeOpportunity> Opportunities;
	int32 Ordinal = 0;
	for (int32 I = 0; I < Rooms.Num(); ++I)
	{
		const auto& R = Rooms[I];
		const FString Geometry = EFCalystoNativeRooms::GeometryKey(R.LocalBounds, Config.QuantizationCm);
		Ordinal = I > 0 && Geometry == EFCalystoNativeRooms::GeometryKey(Rooms[I-1].LocalBounds, Config.QuantizationCm) ? Ordinal + 1 : 0;
		if (R.Transform.ContainsNaN() || !R.LocalBounds.IsValid
			|| R.RoomId != EFCalystoNativeRooms::StableId(Geometry, Config.TopologySeed, Ordinal)
			|| EnumHasAllFlags(R.Protection, EEFCalystoProtectedRoom::Start | EEFCalystoProtectedRoom::End))
		{ Error = TEXT("Room Contexts geometry identity or progression ownership does not match the attempt."); return false; }
		FEFCalystoThemeOpportunity O; O.RoomId = R.RoomId; O.Protected = R.Protection;
		for (const auto& T : Config.Themes) O.EligibleThemes.Add({T.Id, T.Weight});
		Opportunities.Add(MoveTemp(O));
	}
	TArray<FEFCalystoThemeDecision> ExpectedDecisions;
	if (!FEFCalystoDirectorProbability::SelectThemedRooms(Config.RandomKey(), Opportunities,
		Config.AdditionalThemeChancePercent, ExpectedDecisions, Error)) return false;
	for (const auto& R : Rooms)
	{
		const auto* D = ExpectedDecisions.FindByPredicate([&R](const auto& Decision) { return Decision.RoomId == R.RoomId; });
		if (R.ThemeId != (D ? D->ThemeId : FGuid()) || R.bGuaranteedTheme != (D && D->bGuaranteed))
		{ Error = TEXT("Room Contexts does not match its frozen independent probability domains."); return false; }
	}
	Rooms.Sort([](const auto& A, const auto& B) { return A.RoomId < B.RoomId; });
	return true;
}

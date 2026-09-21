#include "Calysto/EFCalystoArchitecturePlanner.h"

namespace EFCalystoArchitecturePlanning
{
	bool ValidBounds(const FBox& B)
	{
		return B.IsValid && !B.Min.ContainsNaN() && !B.Max.ContainsNaN()
			&& B.Min.X < B.Max.X && B.Min.Y < B.Max.Y && B.Min.Z < B.Max.Z;
	}

	FTransform Variation(const FEFCalystoArchitectureRequest& R, const FEFCalystoArchitectureOpportunity& O,
		const FEFCalystoArchitectureEntry& E)
	{
		const auto U = [&](int64 Lane) { return FEFCalystoDirectorProbability::Unit(R.Random,
			EEFCalystoRandomDomain::Architecture, O.Id, Lane); };
		const auto Vector = [&](const FVector& Min, const FVector& Max, int64 Lane)
		{ return FVector(FMath::Lerp(Min.X, Max.X, U(Lane)), FMath::Lerp(Min.Y, Max.Y, U(Lane+1)), FMath::Lerp(Min.Z, Max.Z, U(Lane+2))); };
		const auto& V = E.Variation;
		FVector Scale = Vector(V.ScaleMinimum, V.ScaleMaximum, 16);
		if (V.bUniformScale) Scale = FVector(Scale.X);
		Scale *= E.Transform.bUniformScale ? FVector(E.Transform.Scale.X) : E.Transform.Scale;
		FRotator Rotation(FMath::Lerp(V.RotationMinimum.Pitch,V.RotationMaximum.Pitch,U(13)),
			FMath::Lerp(V.RotationMinimum.Yaw,V.RotationMaximum.Yaw,U(14)),
			FMath::Lerp(V.RotationMinimum.Roll,V.RotationMaximum.Roll,U(15)));
		Rotation += E.Transform.RotationOffset;
		if (E.Rotation == EEFCalystoArchitectureRotation::Degrees45) Rotation.Yaw += 45 * FMath::FloorToInt(U(19)*8);
		else if (E.Rotation == EEFCalystoArchitectureRotation::Degrees90) Rotation.Yaw += 90 * FMath::FloorToInt(U(19)*4);
		else if (E.Rotation == EEFCalystoArchitectureRotation::Full360) Rotation.Yaw += 360 * U(19);
		return FTransform(Rotation, E.Transform.LocationOffset + Vector(V.LocationMinimum,V.LocationMaximum,10), Scale) * O.NativeTransform;
	}
}

bool FEFCalystoArchitecturePlanner::Build(const FEFCalystoCompiledDirector& Config,
	const FEFCalystoArchitectureRequest& R, TArray<FEFCalystoArchitectureDecision>& Decisions, FString& Error)
{
	using namespace EFCalystoArchitecturePlanning;
	Decisions.Reset(); Error.Reset();
	const auto Fail = [&](const TCHAR* Message) { Decisions.Reset(); Error=Message; return false; };
	const auto* Style = Config.FindStyle(R.Random.StyleId);
	if (!Style || !FEFCalystoDirectorProbability::IsEligible(Style->Selection,R.Random.FloorNumber,{})
		|| R.Random.AttemptIndex < 0 || R.Rooms.Num()>2048 || R.Opportunities.Num()>4096
		|| R.ExistingReservedSpace.Num()>4096 || R.CoolingDownIds.Num()>4096)
		return Fail(TEXT("Architecture request has invalid identity or exceeds finite input limits."));
	TMap<int64,const FEFCalystoContentRoom*> Rooms;
	for (const auto& Room : R.Rooms)
	{
		const auto* Theme = Room.ThemeId.IsValid() ? Config.FindTheme(Room.ThemeId) : nullptr;
		if (Rooms.Contains(Room.RoomId) || (Room.ThemeId.IsValid() && (!Theme
			|| !FEFCalystoDirectorProbability::IsEligible(Theme->Selection,R.Random.FloorNumber,{}))))
			return Fail(TEXT("Architecture room identity is duplicated or references an unknown Theme."));
		Rooms.Add(Room.RoomId,&Room);
	}
	TArray<FBox> Reserved = R.ExistingReservedSpace;
	for (const auto& B : Reserved) if (!ValidBounds(B)) return Fail(TEXT("Existing architecture reservation bounds are invalid."));
	TArray<const FEFCalystoArchitectureOpportunity*> Ordered;
	TSet<FGuid> Ids; int64 Pairs=0;
	for (const auto& O : R.Opportunities)
	{
		Pairs += O.CompatibleEntryBounds.Num();
		if (!O.Id.IsValid() || Ids.Contains(O.Id) || !Rooms.Contains(O.RoomId) || O.NativeTransform.ContainsNaN()
			|| !O.NativeTransform.GetRotation().IsNormalized() || O.NativeTransform.GetScale3D().GetMin()<=0
			|| uint8(O.Zone)>uint8(EEFCalystoPlacementZone::Roof) || Pairs>16384)
			return Fail(TEXT("Architecture opportunity identity, transform, zone or compatibility limit is invalid."));
		for (const auto& Pair : O.CompatibleEntryBounds)
			if (!Pair.Key.IsValid() || !ValidBounds(Pair.Value)) return Fail(TEXT("Architecture compatibility must contain valid entry identities and enclosing bounds."));
		Ids.Add(O.Id); Ordered.Add(&O);
	}
	Ordered.Sort([&](const auto& A, const auto& B)
	{
		const auto AH=FEFCalystoDirectorProbability::Hash(R.Random,EEFCalystoRandomDomain::Architecture,A.Id,0);
		const auto BH=FEFCalystoDirectorProbability::Hash(R.Random,EEFCalystoRandomDomain::Architecture,B.Id,0);
		return AH==BH ? A.Id.ToString()<B.Id.ToString() : AH<BH;
	});
	TMap<int64,int32> Used; int64 Work=0; int32 RealizedCount=0;
	for (const auto* O : Ordered)
	{
		auto& D=Decisions.AddDefaulted_GetRef(); D.OpportunityId=O->Id; D.RoomId=O->RoomId; D.Zone=O->Zone;
		const auto* Room=Rooms.FindChecked(O->RoomId); D.ThemeId=Room->ThemeId;
		if (Room->Protection!=EEFCalystoProtectedRoom::None) continue;
		const auto* Theme=Config.FindTheme(Room->ThemeId);
		const auto FindZone=[&](const TArray<FEFCalystoSurfaceDecoration>& Rules)
		{ return Rules.FindByPredicate([&](const auto& Zone) { return Zone.Zone==O->Zone; }); };
		const auto* Rule=Theme ? FindZone(Theme->Architecture) : nullptr;
		if (!Rule) Rule=FindZone(Style->Architecture.Decoration);
		if (!Rule) continue;
		D.ChancePercent=FEFCalystoDirectorProbability::EvaluateLinearCurve(
			Rule->bOverrideDefaultChance ? Rule->Chance : Style->Architecture.DecorationChance,R.Random.FloorNumber);
		if (Used.FindRef(O->RoomId)>=Style->Architecture.MaximumDecorationsPerRoom)
		{ D.Outcome=EEFCalystoArchitectureOutcome::CapacityExhausted; continue; }
		if (Rule->Alternatives.Num()>256) return Fail(TEXT("Architecture zone exceeds 256 alternatives."));
		TArray<FEFCalystoWeightedAlternative> Alternatives;
		for (const auto& E : Rule->Alternatives)
		{
			if (++Work>500000) return Fail(TEXT("Architecture compatibility work limit exceeded."));
			if (!FEFCalystoDirectorProbability::IsEligible(E.Selection,R.Random.FloorNumber,R.CoolingDownIds)) continue;
			if (E.Payload!=EEFCalystoArchitecturePayload::Empty)
			{
				const auto* Bounds=O->CompatibleEntryBounds.Find(E.Selection.Id);
				if (!Bounds) continue;
				bool bOverlap=false;
				for (const auto& B : Reserved)
				{
					if (++Work>500000) return Fail(TEXT("Architecture spacing work limit exceeded."));
					if (Bounds->Intersect(B)) { bOverlap=true; break; }
				}
				if (bOverlap) continue;
			}
			Alternatives.Add({E.Selection.Id,E.Selection.Weight});
		}
		if (Alternatives.IsEmpty()) { D.Outcome=EEFCalystoArchitectureOutcome::NoCompatibleAlternative; continue; }
		D.bChanceRolled=true;
		if (!FEFCalystoDirectorProbability::RollChance(D.ChancePercent,
			FEFCalystoDirectorProbability::Unit(R.Random,EEFCalystoRandomDomain::Architecture,O->Id,1)))
		{ D.Outcome=EEFCalystoArchitectureOutcome::ChanceAbsent; continue; }
		FGuid Selected;
		if (!FEFCalystoDirectorProbability::SelectWeighted(Alternatives,
			FEFCalystoDirectorProbability::Unit(R.Random,EEFCalystoRandomDomain::Architecture,O->Id,2),Selected,Error))
		{ Decisions.Reset(); return false; }
		D.Entry=*Rule->Alternatives.FindByPredicate([&](const auto& E) { return E.Selection.Id==Selected; });
		if (D.Entry.Payload==EEFCalystoArchitecturePayload::Empty)
		{ D.Outcome=EEFCalystoArchitectureOutcome::ExplicitEmpty; continue; }
		if (++RealizedCount>512) return Fail(TEXT("Architecture output exceeds 512 reservations; selected content was not truncated."));
		D.WorldTransform=Variation(R,*O,D.Entry);
		D.ReservedBounds=O->CompatibleEntryBounds.FindChecked(Selected);
		if (D.WorldTransform.ContainsNaN()) return Fail(TEXT("Selected architecture variation produced an invalid transform."));
		D.Outcome=EEFCalystoArchitectureOutcome::Reserved;
		Reserved.Add(D.ReservedBounds); ++Used.FindOrAdd(O->RoomId);
	}
	return true;
}

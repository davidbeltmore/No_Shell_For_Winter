#include "Calysto/EFCalystoBakedArchitecture.h"

#include "PCGDataAsset.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Misc/SecureHash.h"

namespace EFCalystoBaked
{
	bool ValidTransform(const FTransform& T)
	{ return !T.ContainsNaN() && T.GetRotation().IsNormalized() && T.GetScale3D().GetMin()>0; }
	bool ValidBox(const FBox& B)
	{ return B.IsValid && !B.Min.ContainsNaN() && !B.Max.ContainsNaN() && (B.Max-B.Min).GetMin()>0; }
	FString TransformKey(const FTransform& T)
	{
		const FVector P=T.GetLocation(), S=T.GetScale3D(); FQuat Q=T.GetRotation();
		if (Q.W<0 || (Q.W==0 && (Q.Z<0 || (Q.Z==0 && (Q.Y<0 || (Q.Y==0 && Q.X<0)))))) Q*=-1.0;
		return FString::Printf(TEXT("%.17g,%.17g,%.17g/%.17g,%.17g,%.17g,%.17g/%.17g,%.17g,%.17g"),
			P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
	}
	FGuid Identity(const FString& Key)
	{ FGuid Id; FGuid::ParseExact(FMD5::HashAnsiString(*Key),EGuidFormats::Digits,Id); return Id; }
}

bool FEFCalystoBakedArchitecture::Decode(const UPCGDataAsset& Payload, TArray<FEFCalystoBakedMesh>& Children,
	TArray<FSoftObjectPath>& Dependencies, FString& Error)
{
	Children.Reset(); Dependencies.Reset(); Error.Reset();
	const auto Reject=[&Error](const FString& Reason) { Error=TEXT("Baked architecture: ")+Reason; return false; };
	const UPCGBasePointData* Root=nullptr; const UPCGBasePointData* Points=nullptr;
	if (Payload.Data.TaggedData.Num()!=2) return Reject(TEXT("the supported native contract requires exactly Root and Points."));
	for (const auto& Data:Payload.Data.TaggedData)
	{
		const auto* PointData=Cast<UPCGBasePointData>(Data.Data);
		if (!PointData) return Reject(TEXT("each native output must contain point data."));
		if (Data.Pin==TEXT("Root") && !Root) Root=PointData;
		else if (Data.Pin==TEXT("Points") && !Points) Points=PointData;
		else return Reject(TEXT("duplicate or unsupported output pin."));
	}
	if (!Root || !Points || Root->GetNumPoints()!=1 || Points->GetNumPoints()<1 || Points->GetNumPoints()>MaximumChildren+1)
		return Reject(TEXT("one root and at most 256 mesh children are supported."));
	const FPCGPoint RootPoint=FConstPCGPointValueRanges(Root).GetPoint(0);
	if (RootPoint.Transform.ContainsNaN() || !RootPoint.Transform.Equals(FTransform::Identity,0.000001)
		|| RootPoint.Density!=1.0f)
		return Reject(TEXT("the exported root must retain its native identity transform and unit density."));
	const UPCGMetadata* Metadata=Points->ConstMetadata();
	const FName MeshName=PCGLevelToAssetConstants::MeshAttributeName;
	const FName MaterialName=PCGLevelToAssetConstants::MaterialAttributeName;
	const FName SkeletalName=PCGLevelToAssetConstants::SkeletalMeshAttributeName;
	const auto* Mesh=Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(MeshName) : nullptr;
	if (!Mesh) return Reject(TEXT("Mesh must be the supported SoftObjectPath metadata attribute."));
	const auto* Material=Metadata->GetConstTypedAttribute<FSoftObjectPath>(MaterialName);
	const auto* Skeletal=Metadata->GetConstTypedAttribute<FSoftObjectPath>(SkeletalName);
	if ((Metadata->HasAttribute(MaterialName) && !Material) || (Metadata->HasAttribute(SkeletalName) && !Skeletal))
		return Reject(TEXT("Material and SkeletalMesh metadata, when present, must be SoftObjectPath."));
	// Compatibility is localized here: these are the actual PCG_SetLevelByTag consumers.
	// First matching Chance tag wins in its native 10/30/50/75/90 chain; RotateZ overrides Rotate.
	const FName Tags[]={TEXT("Chance10"),TEXT("Chance30"),TEXT("Chance50"),TEXT("Chance75"),TEXT("Chance90"),
		TEXT("Rotate"),TEXT("RotateZ"),TEXT("ProjectToLandscape"),TEXT("Door"),TEXT("AttachDetail")};
	const double Chances[]={10,30,50,75,90};
	TArray<const FPCGMetadataAttribute<bool>*> TagAttributes;
	for (const FName Tag:Tags)
	{
		const auto* Attribute=Metadata->GetConstTypedAttribute<bool>(Tag);
		if (Metadata->HasAttribute(Tag) && !Attribute)
			return Reject(FString::Printf(TEXT("native tag %s must be Boolean."),*Tag.ToString()));
		TagAttributes.Add(Attribute);
	}
	const auto* ActorIndex=Metadata->GetConstTypedAttribute<int64>(PCGLevelToAssetConstants::ActorIndexAttributeName);
	if (!ActorIndex) return Reject(TEXT("native ActorIndex must be int64 for stable child identity."));
	const FConstPCGPointValueRanges Ranges(Points);
	TArray<FEFCalystoBakedMesh> Proposed;
	TArray<FString> IdentityKeys;
	TSet<FSoftObjectPath> Required;
	int32 HierarchyRoots=0;
	for (int32 Index=0;Index<Points->GetNumPoints();++Index)
	{
		const FPCGPoint Point=Ranges.GetPoint(Index);
		const auto MeshPath=Mesh->GetValueFromItemKey(Point.MetadataEntry);
		const auto MaterialPath=Material ? Material->GetValueFromItemKey(Point.MetadataEntry) : FSoftObjectPath();
		const auto HasTag=[&](int32 Tag) { return TagAttributes[Tag] && TagAttributes[Tag]->GetValueFromItemKey(Point.MetadataEntry); };
		if (HasTag(7) || HasTag(8) || HasTag(9))
			return Reject(TEXT("landscape projection, progression Door and recursive AttachDetail tags require unsupported contracts."));
		if (Skeletal && !Skeletal->GetValueFromItemKey(Point.MetadataEntry).IsNull())
			return Reject(TEXT("skinned or gameplay actor payloads require their own tracked contract."));
		if (MeshPath.IsNull())
		{
			// UE's hierarchy sentinel has no renderable payload. Nothing else may silently disappear.
			if (++HierarchyRoots>1 || !MaterialPath.IsNull() || Point.Transform.ContainsNaN()
				|| !Point.Transform.Equals(FTransform::Identity,0.000001) || !Point.BoundsMin.IsZero()
				|| !Point.BoundsMax.IsZero() || Point.Density!=1.0f)
				return Reject(TEXT("a non-hierarchy point has no static mesh."));
			continue;
		}
		const FVector Scale=Point.Transform.GetScale3D();
		if (!MeshPath.IsValid() || !MeshPath.GetSubPathString().IsEmpty()
			|| (!MaterialPath.IsNull() && (!MaterialPath.IsValid() || !MaterialPath.GetSubPathString().IsEmpty()))
			|| Point.Transform.ContainsNaN() || !Point.Transform.GetRotation().IsNormalized()
			|| Scale.GetMin()<=0 || Point.BoundsMin.ContainsNaN() || Point.BoundsMax.ContainsNaN()
			|| (Point.BoundsMax-Point.BoundsMin).GetMin()<=0 || Point.Density!=1.0f || Proposed.Num()>=MaximumChildren)
			return Reject(TEXT("every mesh child requires finite nondegenerate bounds/transform, asset references and unit density."));
		auto& Child=Proposed.AddDefaulted_GetRef(); Child.Mesh=MeshPath; Child.Material=MaterialPath;
		Child.LocalTransform=Point.Transform; Child.ExportedLocalBounds=FBox(Point.BoundsMin,Point.BoundsMax);
		for (int32 Tag=0;Tag<5;++Tag) if (HasTag(Tag)) { Child.ChancePercent=Chances[Tag]; break; }
		Child.Rotation=HasTag(6) ? EEFCalystoBakedRotation::WorldYaw
			: HasTag(5) ? EEFCalystoBakedRotation::RelativeYaw : EEFCalystoBakedRotation::Preserve;
		IdentityKeys.Add(FString::Printf(TEXT("BakedChild/%lld/"),ActorIndex->GetValueFromItemKey(Point.MetadataEntry))
			+EFCalystoBaked::TransformKey(Point.Transform));
		Required.Add(MeshPath); if (!MaterialPath.IsNull()) Required.Add(MaterialPath);
	}
	if (Proposed.IsEmpty()) return Reject(TEXT("a selected baked payload contains no renderable children."));
	// The original actor's exported identity and geometry survive point-array reorder and labels/materials.
	// Coincident components retain multiplicity through canonical descriptor ordering, never array position.
	TArray<int32> Order; for (int32 Index=0;Index<Proposed.Num();++Index) Order.Add(Index);
	Order.Sort([&](int32 A,int32 B)
	{
		if (IdentityKeys[A]!=IdentityKeys[B]) return IdentityKeys[A]<IdentityKeys[B];
		const auto& X=Proposed[A]; const auto& Y=Proposed[B];
		if (X.Mesh!=Y.Mesh) return X.Mesh.ToString()<Y.Mesh.ToString();
		if (X.ChancePercent!=Y.ChancePercent) return X.ChancePercent<Y.ChancePercent;
		if (X.Rotation!=Y.Rotation) return uint8(X.Rotation)<uint8(Y.Rotation);
		return X.Material.ToString()<Y.Material.ToString();
	});
	TMap<FString,int32> Multiplicity;
	for (int32 Index:Order) Proposed[Index].Id=EFCalystoBaked::Identity(IdentityKeys[Index]
		+FString::Printf(TEXT("/%d"),Multiplicity.FindOrAdd(IdentityKeys[Index])++));
	Proposed.Sort([](const auto& A,const auto& B) { return A.Id.ToString()<B.Id.ToString(); });
	// Stable dependency order and exact child multiplicity; coincident children are never deduplicated.
	Dependencies=Required.Array(); Dependencies.Sort([](const auto& A,const auto& B){return A.ToString()<B.ToString();});
	Children=MoveTemp(Proposed); return true;
}

bool FEFCalystoBakedArchitecture::GetEnvelope(const FEFCalystoBakedMesh& Child, const FTransform& Parent,
	FBox& Envelope, FString& Error)
{
	using namespace EFCalystoBaked;
	Envelope=FBox(ForceInit); Error.Reset();
	if (!Child.Id.IsValid() || !ValidTransform(Parent) || !ValidTransform(Child.LocalTransform)
		|| !ValidBox(Child.ExportedLocalBounds) || !FMath::IsFinite(Child.ChancePercent)
		|| Child.ChancePercent<0 || Child.ChancePercent>100 || uint8(Child.Rotation)>uint8(EEFCalystoBakedRotation::WorldYaw))
	{ Error=TEXT("Baked child has invalid identity, transform, bounds or native probability/rotation."); return false; }
	const FTransform World=Child.LocalTransform*Parent;
	if (!ValidTransform(World)) { Error=TEXT("Baked child composition is nonfinite or degenerate."); return false; }
	if (Child.Rotation==EEFCalystoBakedRotation::Preserve) Envelope=Child.ExportedLocalBounds.TransformBy(World);
	else
	{
		// Native yaw rotates about the child's origin. The complete swept box is a
		// cylinder in that yaw frame, not a sphere extending below an upright floor payload.
		const FVector Low=Child.ExportedLocalBounds.Min*World.GetScale3D();
		const FVector High=Child.ExportedLocalBounds.Max*World.GetScale3D();
		const double Radius=FMath::Sqrt(FMath::Square(FMath::Max(FMath::Abs(Low.X),FMath::Abs(High.X)))
			+FMath::Square(FMath::Max(FMath::Abs(Low.Y),FMath::Abs(High.Y))));
		const FQuat Basis=Child.Rotation==EEFCalystoBakedRotation::WorldYaw ? FQuat::Identity : World.GetRotation();
		const FVector X=Basis.GetAxisX(),Y=Basis.GetAxisY(),Z=Basis.GetAxisZ();
		const FVector Center=World.GetLocation()+Z*((Low.Z+High.Z)*0.5);
		FVector Extent;
		for (int32 Axis=0;Axis<3;++Axis)
			Extent[Axis]=Radius*FMath::Sqrt(X[Axis]*X[Axis]+Y[Axis]*Y[Axis])+FMath::Abs(Z[Axis])*(High.Z-Low.Z)*0.5;
		Envelope=FBox(Center-Extent,Center+Extent);
	}
	if (!ValidBox(Envelope)) { Error=TEXT("Baked child envelope is nonfinite or degenerate."); return false; }
	return true;
}

bool FEFCalystoBakedArchitecture::Freeze(const FEFCalystoRandomKey& Random, const FGuid& ParentId,
	const FTransform& Parent, TConstArrayView<FEFCalystoBakedMesh> Children, const FBox& ReservedEnvelope,
	TArray<FEFCalystoBakedDecision>& Decisions, FString& Error)
{
	using namespace EFCalystoBaked;
	Decisions.Reset(); Error.Reset();
	if (!ParentId.IsValid() || !Random.StyleId.IsValid() || Random.FloorNumber<1 || Random.AttemptIndex<0
		|| Random.RerollIndex<0 || Children.IsEmpty() || Children.Num()>MaximumChildren || !ValidBox(ReservedEnvelope))
	{ Error=TEXT("Baked payload reservation has invalid identity, capacity or enclosing volume."); return false; }
	TArray<const FEFCalystoBakedMesh*> Ordered; TSet<FGuid> Ids;
	for (const auto& Child:Children)
	{
		FBox Envelope;
		if (!GetEnvelope(Child,Parent,Envelope,Error)) return false;
		if (Ids.Contains(Child.Id) || !ReservedEnvelope.IsInsideOrOn(Envelope.Min) || !ReservedEnvelope.IsInsideOrOn(Envelope.Max)
			|| !Child.Mesh.IsValid() || !Child.Mesh.GetSubPathString().IsEmpty()
			|| (!Child.Material.IsNull() && (!Child.Material.IsValid() || !Child.Material.GetSubPathString().IsEmpty())))
		{ Error=TEXT("Baked child is duplicated, has invalid resources or exceeds its preselected reservation."); return false; }
		Ids.Add(Child.Id); Ordered.Add(&Child);
	}
	Ordered.Sort([](const auto& A,const auto& B) { return A.Id.ToString()<B.Id.ToString(); });
	for (const auto* Child:Ordered)
	{
		auto& Decision=Decisions.AddDefaulted_GetRef(); Decision.Child=*Child;
		Decision.Id=Identity(ParentId.ToString(EGuidFormats::Digits)+Child->Id.ToString(EGuidFormats::Digits));
		Decision.WorldTransform=Child->LocalTransform*Parent;
		Decision.bSelected=FEFCalystoDirectorProbability::RollChance(Child->ChancePercent,
			FEFCalystoDirectorProbability::Unit(Random,EEFCalystoRandomDomain::Architecture,Decision.Id,32));
		if (Decision.bSelected && Child->Rotation!=EEFCalystoBakedRotation::Preserve)
		{
			const FQuat Yaw=FRotator(0,360*FEFCalystoDirectorProbability::Unit(Random,
				EEFCalystoRandomDomain::Architecture,Decision.Id,33),0).Quaternion();
			Decision.WorldTransform.SetRotation(Child->Rotation==EEFCalystoBakedRotation::WorldYaw
				? Yaw : Decision.WorldTransform.GetRotation()*Yaw);
		}
	}
	return true;
}

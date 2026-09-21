#include "Calysto/EFCalystoArchitectureManifest.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "PCGDataAsset.h"

namespace EFCalystoArchitectureFreezing
{
	bool BoxValid(const FBox& B)
	{ return B.IsValid && !B.Min.ContainsNaN() && !B.Max.ContainsNaN() && (B.Max-B.Min).GetMin()>0; }
	bool Encloses(const FBox& Parent,const FBox& Child)
	{ return BoxValid(Parent) && BoxValid(Child) && Parent.IsInsideOrOn(Child.Min) && Parent.IsInsideOrOn(Child.Max); }
	FGuid Identity(const FString& Text)
	{ FGuid Id; FGuid::ParseExact(FMD5::HashAnsiString(*Text),EGuidFormats::Digits,Id); return Id; }
	bool Materials(UStaticMesh* Mesh,const FSoftObjectPath& Override,TArray<FSoftObjectPath>& Out,FString& Error)
	{
		Out.Reset();
		if (!Mesh || Mesh->GetStaticMaterials().IsEmpty() || Mesh->GetStaticMaterials().Num()>64)
		{ Error=TEXT("Reserved architecture requires a resident exact mesh and 1..64 material slots."); return false; }
		for (int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)
		{
			UMaterialInterface* Material=Slot==0 && !Override.IsNull() ? Cast<UMaterialInterface>(Override.ResolveObject()) : Mesh->GetMaterial(Slot);
			if (!Material) { Error=TEXT("Reserved architecture has a missing material slot; no native/default substitution is permitted."); return false; }
			Out.Add(FSoftObjectPath(Material));
		}
		return true;
	}
	FString Pose(const FTransform& T)
	{
		const auto P=T.GetLocation(),S=T.GetScale3D(); auto Q=T.GetRotation();
		if (Q.W<0) Q*= -1.0;
		return FString::Printf(TEXT("%.17g,%.17g,%.17g|%.17g,%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
			P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
	}
}

bool FEFCalystoArchitectureManifest::Build(const FEFCalystoRandomKey& Random,
	TConstArrayView<FEFCalystoArchitectureDecision> Decisions,FEFCalystoArchitectureManifest& Manifest,FString& Error)
{
	using namespace EFCalystoArchitectureFreezing;
	Manifest={}; Error.Reset();
	if (!IsInGameThread() || !Random.StyleId.IsValid() || Random.FloorNumber<1 || Random.AttemptIndex<0
		|| Random.RerollIndex<0 || Decisions.Num()>4096)
	{ Error=TEXT("Architecture freezing requires the current finite random identity and native-slot decisions."); return false; }
	struct FPrepared
	{
		const FEFCalystoArchitectureDecision* Decision=nullptr;
		TArray<FEFCalystoBakedMesh> Children;
		TMap<FGuid,TArray<FSoftObjectPath>> Materials;
		TArray<FSoftObjectPath> MeshMaterials;
	};
	TSet<FGuid> OpportunityIds; TArray<FPrepared> Prepared; int32 TotalChildren=0;
	// Complete validation across every reserved parent before the first conditional child draw.
	for (const auto& D:Decisions)
	{
		if (!D.OpportunityId.IsValid() || OpportunityIds.Contains(D.OpportunityId)
			|| uint8(D.Outcome)>uint8(EEFCalystoArchitectureOutcome::Reserved))
		{ Error=TEXT("Architecture manifest has duplicate/invalid native opportunities or outcomes."); return false; }
		OpportunityIds.Add(D.OpportunityId);
		if (D.Outcome!=EEFCalystoArchitectureOutcome::Reserved) continue;
		if (Prepared.Num()>=512 || !D.Entry.Selection.Id.IsValid() || !D.WorldTransform.IsValid()
			|| D.WorldTransform.GetScale3D().GetMin()<=0 || !BoxValid(D.ReservedBounds))
		{ Error=TEXT("Reserved architecture parent identity, capacity, transform or envelope is invalid."); return false; }
		auto& P=Prepared.AddDefaulted_GetRef(); P.Decision=&D;
		if (D.Entry.Payload==EEFCalystoArchitecturePayload::Mesh)
		{
			auto* Mesh=D.Entry.Mesh.Get();
			if (!Materials(Mesh,{},P.MeshMaterials,Error)) return false;
			if (!Encloses(D.ReservedBounds,Mesh->GetBoundingBox().TransformBy(D.WorldTransform)))
			{ Error=TEXT("Selected mesh render geometry exceeds its preselected architecture reservation."); return false; }
			++TotalChildren;
		}
		else if (D.Entry.Payload==EEFCalystoArchitecturePayload::BakedPCG)
		{
			const auto* Payload=Cast<UPCGDataAsset>(D.Entry.BakedPCG.Get()); TArray<FSoftObjectPath> Dependencies;
			if (!Payload) { Error=TEXT("The exact reserved native baked payload is unavailable."); return false; }
			if (!FEFCalystoBakedArchitecture::Decode(*Payload,P.Children,Dependencies,Error)) return false;
			for (auto& Child:P.Children)
			{
				auto* Mesh=Cast<UStaticMesh>(Child.Mesh.ResolveObject()); TArray<FSoftObjectPath> Slots;
				if (!Materials(Mesh,Child.Material,Slots,Error)) return false;
				// Authored exported bounds alone cannot hide an actual larger resident mesh.
				Child.ExportedLocalBounds+=Mesh->GetBoundingBox(); FBox Envelope;
				if (!FEFCalystoBakedArchitecture::GetEnvelope(Child,D.WorldTransform,Envelope,Error)) return false;
				if (!Encloses(D.ReservedBounds,Envelope))
				{ Error=TEXT("A possible baked child exceeds the whole preselected geometry/rotation envelope."); return false; }
				P.Materials.Add(Child.Id,MoveTemp(Slots));
			}
			TotalChildren+=P.Children.Num();
		}
		else { Error=TEXT("This architecture materializer requires mesh or supported baked children; actor payloads require their tracked gameplay bridge."); return false; }
		if (TotalChildren>FEFCalystoArchitectureMeshBatch::MaximumInstances)
		{ Error=TEXT("Architecture children exceed finite materialization capacity; no selected output was truncated."); return false; }
	}
	Prepared.Sort([](const auto& A,const auto& B) { return A.Decision->OpportunityId.ToString()<B.Decision->OpportunityId.ToString(); });
	FEFCalystoArchitectureManifest Proposed; FString Canonical=TEXT("ArchitectureManifest\n");
	for (const auto& P:Prepared)
	{
		const auto& D=*P.Decision; auto& Parent=Proposed.Parents.AddDefaulted_GetRef();
		Parent.Id=Identity(TEXT("ArchitectureParent|")+D.OpportunityId.ToString(EGuidFormats::Digits));
		Parent.EntryId=D.Entry.Selection.Id; Parent.RoomId=D.RoomId; Parent.Bounds=D.ReservedBounds;
		if (!Parent.Id.IsValid() || Proposed.ReservedElements.Contains(Parent.Id))
		{ Error=TEXT("Architecture parent identity collision."); return false; }
		Proposed.ReservedElements.Add(Parent.Id);
		Canonical+=Parent.Id.ToString()+TEXT("|")+Parent.EntryId.ToString()+TEXT("|")
			+FString::Printf(TEXT("%lld|%.17g,%.17g,%.17g|%.17g,%.17g,%.17g\n"),D.RoomId,
				Parent.Bounds.Min.X,Parent.Bounds.Min.Y,Parent.Bounds.Min.Z,Parent.Bounds.Max.X,Parent.Bounds.Max.Y,Parent.Bounds.Max.Z);
		const auto AddMesh=[&](FGuid Id,const FSoftObjectPath& Mesh,const TArray<FSoftObjectPath>& Slots,const FTransform& Transform)
		{
			if (!Id.IsValid() || Proposed.ReservedElements.Contains(Id)) { Error=TEXT("Architecture child identity collision."); return false; }
			auto& M=Proposed.Meshes.AddDefaulted_GetRef(); M.Id=Id; M.ParentReservationId=Parent.Id;
			M.Mesh=Mesh; M.Materials=Slots; M.WorldTransform=Transform;
			Proposed.ReservedElements.Add(Id); Parent.SelectedChildren.Add(Id);
			Canonical+=Id.ToString()+TEXT("|")+Mesh.ToString()+TEXT("|")+Pose(Transform);
			for (const auto& Slot:Slots) Canonical+=TEXT("|")+Slot.ToString(); Canonical+=TEXT("\n"); return true;
		};
		if (D.Entry.Payload==EEFCalystoArchitecturePayload::Mesh)
		{
			if (!AddMesh(Identity(TEXT("ArchitectureMesh|")+Parent.Id.ToString(EGuidFormats::Digits)),
				D.Entry.Mesh.ToSoftObjectPath(),P.MeshMaterials,D.WorldTransform)) return false;
		}
		else
		{
			if (!FEFCalystoBakedArchitecture::Freeze(Random,Parent.Id,D.WorldTransform,P.Children,D.ReservedBounds,Parent.BakedChildren,Error)) return false;
			for (const auto& Child:Parent.BakedChildren)
			{
				Canonical+=Child.Id.ToString()+FString::Printf(TEXT("|Selected=%d\n"),int32(Child.bSelected));
				if (Child.bSelected && !AddMesh(Child.Id,Child.Child.Mesh,P.Materials.FindChecked(Child.Child.Id),Child.WorldTransform)) return false;
			}
		}
	}
	Proposed.Meshes.Sort([](const auto& A,const auto& B) { return A.Id.ToString()<B.Id.ToString(); });
	Proposed.Hash=FMD5::HashAnsiString(*Canonical); Proposed.bValid=true; Manifest=MoveTemp(Proposed); return true;
}

bool FEFCalystoArchitectureManifest::VerifyRealizedChildren(const TSet<FGuid>& ActualChildren,
	TSet<FGuid>& Verified,FString& Error) const
{
	Verified.Reset(); Error.Reset();
	if (!bValid || ActualChildren.Num()!=Meshes.Num())
	{ Error=TEXT("Architecture realization has a missing or additional tracked child."); return false; }
	for (const auto& M:Meshes) if (!ActualChildren.Contains(M.Id))
	{ Error=TEXT("A selected architecture child did not verify before acceptance."); return false; }
	Verified=ReservedElements; return true;
}

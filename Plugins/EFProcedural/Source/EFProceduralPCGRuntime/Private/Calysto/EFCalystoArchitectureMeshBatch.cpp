#include "Calysto/EFCalystoArchitectureMeshBatch.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace EFCalystoArchitectureMeshes
{
	bool ValidToken(const FEFCalystoAttemptToken& T) { return T.Request.IsValid() && T.Attempt.IsValid(); }
	bool ExactPose(const FTransform& A,const FTransform& B)
	{ return A.IsValid() && A.GetLocation().Equals(B.GetLocation(),0.1)
		&& FMath::RadiansToDegrees(A.GetRotation().AngularDistance(B.GetRotation()))<=0.01
		&& A.GetScale3D().Equals(B.GetScale3D(),0.0001); }
}

FEFCalystoArchitectureMeshBatch::~FEFCalystoArchitectureMeshBatch()
{
	ensureMsgf(State==EEFCalystoArchitectureMeshState::Idle || State==EEFCalystoArchitectureMeshState::Released,
		TEXT("Architecture instance ownership was destroyed before token-scoped release."));
	if (IsInGameThread() && EFCalystoArchitectureMeshes::ValidToken(Token)) Release(Token);
}

void FEFCalystoArchitectureMeshBatch::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& G:Groups) { Collector.AddReferencedObject(G.Mesh); Collector.AddReferencedObjects(G.Materials); Collector.AddReferencedObject(G.Component); }
}

bool FEFCalystoArchitectureMeshBatch::OwnerValid() const
{ return World.IsValid() && Owner.IsValid() && !Owner->IsActorBeingDestroyed() && Owner->GetWorld()==World.Get(); }

bool FEFCalystoArchitectureMeshBatch::Fail(const FString& Message,FString& Error)
{
	Error=Message; State=EEFCalystoArchitectureMeshState::Failed;
	for (auto& G:Groups) if (IsValid(G.Component)) G.Component->SetVisibility(false,false);
	return false;
}

bool FEFCalystoArchitectureMeshBatch::Begin(const FEFCalystoAttemptToken& InToken,UWorld* InWorld,AActor* AttemptOwner,
	TConstArrayView<FEFCalystoFrozenArchitectureMesh> Frozen,double DeadlineSeconds,FString& Error)
{
	using namespace EFCalystoArchitectureMeshes;
	Error.Reset();
	if (IsInGameThread() && State!=EEFCalystoArchitectureMeshState::Idle && State!=EEFCalystoArchitectureMeshState::Released)
	{
		bool Same=InToken==Token && InWorld==World.Get() && AttemptOwner==Owner.Get()
			&& DeadlineSeconds==Deadline && Frozen.Num()==Bindings.Num() && OwnerValid()
			&& (State==EEFCalystoArchitectureMeshState::Preparing || State==EEFCalystoArchitectureMeshState::Prepared || State==EEFCalystoArchitectureMeshState::Active);
		TMap<FGuid,const FEFCalystoFrozenArchitectureMesh*> Input;
		if (Same) for (const auto& F:Frozen) { if (Input.Contains(F.Id)) { Same=false; break; } Input.Add(F.Id,&F); }
		if (Same) for (const auto& B:Bindings)
		{
			const auto* Found=Input.Find(B.Frozen.Id); const auto* F=Found ? *Found : nullptr;
			if (!F || F->ParentReservationId!=B.Frozen.ParentReservationId || F->Mesh!=B.Frozen.Mesh
				|| F->Materials!=B.Frozen.Materials || !F->WorldTransform.Equals(B.Frozen.WorldTransform,0)) { Same=false; break; }
		}
		if (!Same) Error=TEXT("An active architecture batch cannot be replaced or revived by a different request.");
		return Same;
	}
	if (!IsInGameThread() || (State!=EEFCalystoArchitectureMeshState::Idle && State!=EEFCalystoArchitectureMeshState::Released)
		|| !ValidToken(InToken) || !IsValid(InWorld) || !IsValid(AttemptOwner) || AttemptOwner->IsActorBeingDestroyed()
		|| AttemptOwner->GetWorld()!=InWorld || !FMath::IsFinite(DeadlineSeconds) || DeadlineSeconds<=0
		|| Frozen.Num()>MaximumInstances)
	{ Error=TEXT("Architecture batch requires released ownership, an exact live attempt and finite capacity/deadline."); return false; }
	TArray<FGroup> ProposedGroups; TArray<FBinding> ProposedBindings; TMap<FString,int32> GroupKeys; TSet<FGuid> Ids;
	TArray<const FEFCalystoFrozenArchitectureMesh*> Ordered;
	for (const auto& F:Frozen) Ordered.Add(&F);
	Ordered.Sort([](const auto& A,const auto& B) { return A.Id.ToString()<B.Id.ToString(); });
	for (const auto* F:Ordered)
	{
		UStaticMesh* Mesh=Cast<UStaticMesh>(F->Mesh.ResolveObject());
		if (!F->Id.IsValid() || !F->ParentReservationId.IsValid() || Ids.Contains(F->Id) || !F->WorldTransform.IsValid()
			|| F->WorldTransform.GetScale3D().GetMin()<=0 || !Mesh || !Mesh->GetBodySetup()
			|| F->Materials.IsEmpty() || F->Materials.Num()>64 || Mesh->GetStaticMaterials().Num()!=F->Materials.Num())
		{ Error=TEXT("Every selected architecture child requires unique identity, loaded exact mesh/collision, transform and complete material slots."); return false; }
		FString Key=F->Mesh.ToString(); TArray<TObjectPtr<UMaterialInterface>> Materials;
		for (const auto& Path:F->Materials)
		{
			auto* Material=Cast<UMaterialInterface>(Path.ResolveObject());
			if (!Material) { Error=TEXT("A selected architecture material is not loaded; substitution is forbidden."); return false; }
			Materials.Add(Material); Key+=TEXT("|")+Path.ToString();
		}
		int32 Group=GroupKeys.FindRef(Key)-1;
		if (Group==INDEX_NONE)
		{
			Group=ProposedGroups.Num(); auto& G=ProposedGroups.AddDefaulted_GetRef();
			G.Mesh=Mesh; G.Materials=MoveTemp(Materials); GroupKeys.Add(Key,Group+1);
		}
		Ids.Add(F->Id); auto& B=ProposedBindings.AddDefaulted_GetRef(); B.Frozen=*F; B.Group=Group;
	}
	Token=InToken; World=InWorld; Owner=AttemptOwner; Deadline=DeadlineSeconds; LastNow=-1; Next=0;
	Groups=MoveTemp(ProposedGroups); Bindings=MoveTemp(ProposedBindings);
	State=Bindings.IsEmpty() ? EEFCalystoArchitectureMeshState::Prepared : EEFCalystoArchitectureMeshState::Preparing;
	return true;
}

bool FEFCalystoArchitectureMeshBatch::Prepare(double Now,int32 Operations,FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !FMath::IsFinite(Now) || Now<0 || Now<LastNow || Operations<1 || Operations>64)
		return Fail(TEXT("Architecture preparation requires a finite monotonic clock and 1..64 operations."),Error);
	LastNow=Now;
	if (Now>=Deadline) return Fail(TEXT("Architecture preparation exceeded its shared request deadline."),Error);
	if (State==EEFCalystoArchitectureMeshState::Prepared) { TSet<FGuid> Verified; return Verify(Verified,Error); }
	if (State!=EEFCalystoArchitectureMeshState::Preparing || !OwnerValid() || Now>=Deadline)
		return Fail(TEXT("Architecture preparation lost ownership or its shared request deadline."),Error);
	for (int32 Work=0;Work<Operations && Next<Bindings.Num();++Work,++Next)
	{
		auto& B=Bindings[Next]; auto& G=Groups[B.Group];
		if (!G.Component)
		{
			auto* C=NewObject<UInstancedStaticMeshComponent>(Owner.Get(),NAME_None,RF_Transient); G.Component=C;
			Owner->AddInstanceComponent(C); C->SetMobility(EComponentMobility::Static);
			C->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName); C->SetGenerateOverlapEvents(false);
			C->SetCanEverAffectNavigation(true); C->SetComponentTickEnabled(false); C->SetVisibility(false,false);
			C->SetStaticMesh(G.Mesh); for (int32 Slot=0;Slot<G.Materials.Num();++Slot) C->SetMaterial(Slot,G.Materials[Slot]);
			C->RegisterComponentWithWorld(World.Get());
			if (!OwnerValid() || !C->IsRegistered() || C->GetWorld()!=World.Get())
				return Fail(TEXT("A reserved architecture instance component did not register under its exact owner."),Error);
		}
		B.Instance=G.Component->AddInstance(B.Frozen.WorldTransform,true);
		if (B.Instance!=G.InstanceCount++) return Fail(TEXT("A reserved architecture mesh instance did not materialize at its exact index."),Error);
	}
	if (Next==Bindings.Num())
	{
		State=EEFCalystoArchitectureMeshState::Prepared; TSet<FGuid> Verified;
		if (!Verify(Verified,Error)) return Fail(Error,Error);
	}
	return true;
}

bool FEFCalystoArchitectureMeshBatch::Verify(TSet<FGuid>& Verified,FString& Error) const
{
	using namespace EFCalystoArchitectureMeshes;
	Verified.Reset(); Error.Reset();
	if (!IsInGameThread() || !OwnerValid() || (State!=EEFCalystoArchitectureMeshState::Prepared && State!=EEFCalystoArchitectureMeshState::Active))
	{ Error=TEXT("Architecture evidence requires a fully prepared exact live owner."); return false; }
	for (const auto& G:Groups)
	{
		const auto* C=G.Component.Get();
		if (!IsValid(C) || !C->IsRegistered() || C->GetWorld()!=World.Get() || C->GetOwner()!=Owner.Get()
			|| !Owner->GetInstanceComponents().Contains(C) || C->GetClass()!=UInstancedStaticMeshComponent::StaticClass()
			|| C->GetStaticMesh()!=G.Mesh || C->GetInstanceCount()!=G.InstanceCount || !C->IsPhysicsStateCreated()
			|| C->GetCollisionEnabled()!=ECollisionEnabled::QueryAndPhysics || C->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block
			|| C->GetCollisionProfileName()!=UCollisionProfile::BlockAll_ProfileName || !C->CanEverAffectNavigation()
			|| C->GetGenerateOverlapEvents() || C->IsComponentTickEnabled() || C->GetNumMaterials()!=G.Materials.Num()
			|| C->IsVisible()!=(State==EEFCalystoArchitectureMeshState::Active))
		{ Error=TEXT("Actual architecture component ownership, mesh, instance count, collision, navigation or staging differs from its reservation."); return false; }
		for (int32 Slot=0;Slot<G.Materials.Num();++Slot) if (C->GetMaterial(Slot)!=G.Materials[Slot])
		{ Error=TEXT("An actual architecture material slot differs from its exact selected reference."); return false; }
	}
	for (const auto& B:Bindings)
	{
		FTransform Actual;
		if (!Groups[B.Group].Component->GetInstanceTransform(B.Instance,Actual,true) || !ExactPose(Actual,B.Frozen.WorldTransform))
		{ Error=TEXT("A selected architecture mesh is missing or moved from its frozen transform."); return false; }
	}
	for (const auto& B:Bindings) Verified.Add(B.Frozen.Id);
	return true;
}

bool FEFCalystoArchitectureMeshBatch::Activate(const FEFCalystoAttemptToken& Accepted,FString& Error)
{
	Error.Reset(); TSet<FGuid> Verified;
	if (!(Token==Accepted)) { Error=TEXT("Architecture activation does not own the exact accepted token."); return false; }
	if (!Verify(Verified,Error)) return Fail(Error,Error);
	for (auto& G:Groups) G.Component->SetVisibility(true,false);
	State=EEFCalystoArchitectureMeshState::Active;
	if (!Verify(Verified,Error)) return Fail(Error,Error);
	return true;
}

void FEFCalystoArchitectureMeshBatch::Release(const FEFCalystoAttemptToken& Released)
{
	if (!IsInGameThread() || !(Released==Token) || State==EEFCalystoArchitectureMeshState::Idle || State==EEFCalystoArchitectureMeshState::Released) return;
	State=EEFCalystoArchitectureMeshState::Releasing;
	for (auto& G:Groups) if (IsValid(G.Component))
	{
		G.Component->SetVisibility(false,false); G.Component->SetGenerateOverlapEvents(false);
		G.Component->ClearInstances(); G.Component->DestroyComponent();
		if (Owner.IsValid()) Owner->RemoveInstanceComponent(G.Component);
	}
	for (const auto& G:Groups) if (G.Component && (G.Component->IsRegistered() || G.Component->IsPhysicsStateCreated())) return;
	Groups.Reset(); Bindings.Reset(); Owner.Reset(); World.Reset(); State=EEFCalystoArchitectureMeshState::Released;
}

FEFCalystoRollbackEvidence FEFCalystoArchitectureMeshBatch::GetReleaseEvidence() const
{
	FEFCalystoRollbackEvidence Evidence; Evidence.bPCGCleanupComplete=true;
	Evidence.Reservations=Bindings.Num(); Evidence.TransientReferences=Groups.Num();
	for (const auto& G:Groups) if (G.Component && G.Component->IsRegistered())
	{ Evidence.Instances+=G.Component->GetInstanceCount(); ++Evidence.PendingCallbacks; }
	return Evidence;
}

#include "DirtyPawnVolumeActors.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DirtyPawnComponent.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"

namespace
{
	bool IsWorldPointInsideBox(const UBoxComponent* Box, const FVector& WorldLocation)
	{
		if (!Box)
		{
			return false;
		}

		const FVector LocalLocation = Box->GetComponentTransform().InverseTransformPosition(WorldLocation);
		const FVector Extent = Box->GetUnscaledBoxExtent();
		return FMath::Abs(LocalLocation.X) <= Extent.X
			&& FMath::Abs(LocalLocation.Y) <= Extent.Y
			&& FMath::Abs(LocalLocation.Z) <= Extent.Z;
	}
}

ADirtyPawnVolumeBase::ADirtyPawnVolumeBase()
{
	PrimaryActorTick.bCanEverTick = true;

	ContactVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ContactVolume"));
	SetRootComponent(ContactVolume);
	ContactVolume->SetBoxExtent(FVector(200.0f, 200.0f, 100.0f));
	ContactVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContactVolume->SetCollisionObjectType(ECC_WorldDynamic);
	ContactVolume->SetCollisionResponseToAllChannels(ECR_Overlap);
	ContactVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ContactVolume->SetGenerateOverlapEvents(true);
}

void ADirtyPawnVolumeBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	RefreshContactPrimitives();
}

void ADirtyPawnVolumeBase::BeginPlay()
{
	Super::BeginPlay();

	RefreshContactPrimitives();
	ApplyCurrentOverlaps();
}

void ADirtyPawnVolumeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TimeUntilNextUpdate -= DeltaSeconds;
	if (TimeUntilNextUpdate > 0.0f)
	{
		return;
	}

	TimeUntilNextUpdate = FMath::Max(RuntimeUpdateInterval, 0.01f);

	for (int32 Index = OverlappingPawns.Num() - 1; Index >= 0; --Index)
	{
		UDirtyPawnComponent* DirtyPawn = OverlappingPawns[Index];
		if (!IsValid(DirtyPawn) || !IsValid(DirtyPawn->GetOwner()))
		{
			OverlappingPawns.RemoveAtSwap(Index);
			continue;
		}

		ApplyToDirtyPawn(DirtyPawn, this);
	}
}

void ADirtyPawnVolumeBase::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UDirtyPawnComponent* DirtyPawn = ResolveDirtyPawn(OtherActor, OtherComponent);
	if (!DirtyPawn)
	{
		if (bLogMissingComponent && IsValid(OtherActor))
		{
			UE_LOG(LogTemp, Warning, TEXT("[DirtyPawnRuntime] %s touched %s but has no DirtyPawnComponent."), *GetNameSafe(OtherActor), *GetNameSafe(this));
		}
		return;
	}

	OverlappingPawns.AddUnique(DirtyPawn);
	ApplyToDirtyPawn(DirtyPawn, this);
}

void ADirtyPawnVolumeBase::OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex)
{
	UDirtyPawnComponent* DirtyPawn = ResolveDirtyPawn(OtherActor, OtherComponent);
	if (!DirtyPawn)
	{
		return;
	}

	OverlappingPawns.Remove(DirtyPawn);
	EndApplyToDirtyPawn(DirtyPawn, this);
}

void ADirtyPawnVolumeBase::ApplyToDirtyPawn(UDirtyPawnComponent* DirtyPawn, AActor* SourceActor)
{
	if (!DirtyPawn)
	{
		return;
	}

	float NodeMinHeight = 0.0f;
	float NodeMaxHeight = 0.0f;
	if (!ResolveContactHeightBand(DirtyPawn, NodeMinHeight, NodeMaxHeight))
	{
		return;
	}

	switch (VolumeKind)
	{
	case EDirtyPawnVolumeKind::Water:
		DirtyPawn->WaterOverlapBand(SourceActor, NodeMinHeight, NodeMaxHeight, false, false);
		break;
	case EDirtyPawnVolumeKind::MudWater:
		DirtyPawn->WaterOverlapBand(SourceActor, NodeMinHeight, NodeMaxHeight, true, false);
		break;
	case EDirtyPawnVolumeKind::BleachWater:
		DirtyPawn->WaterOverlapBand(SourceActor, NodeMinHeight, NodeMaxHeight, false, true);
		break;
	case EDirtyPawnVolumeKind::FadeWash:
		DirtyPawn->SetFadeWashVariablesBand(NodeMinHeight, NodeMaxHeight, true, true, true);
		break;
	case EDirtyPawnVolumeKind::Sand:
		DirtyPawn->SetFadeSandSnowVariablesBand(NodeMinHeight, NodeMaxHeight, true, false);
		break;
	case EDirtyPawnVolumeKind::Snow:
		DirtyPawn->SetFadeSandSnowVariablesBand(NodeMinHeight, NodeMaxHeight, false, true);
		break;
	case EDirtyPawnVolumeKind::SandAndSnow:
		DirtyPawn->SetFadeSandSnowVariablesBand(NodeMinHeight, NodeMaxHeight, true, true);
		break;
	case EDirtyPawnVolumeKind::HurtBlood:
		DirtyPawn->BloodBandEvent(NodeMinHeight, NodeMaxHeight, Strength);
		break;
	case EDirtyPawnVolumeKind::HurtSmear:
		DirtyPawn->SmearBandEvent(NodeMinHeight, NodeMaxHeight, Strength);
		break;
	case EDirtyPawnVolumeKind::HurtDirt:
		DirtyPawn->DirtBandEvent(NodeMinHeight, NodeMaxHeight, Strength);
		break;
	case EDirtyPawnVolumeKind::HurtBurn:
		DirtyPawn->BurnBandEvent(NodeMinHeight, NodeMaxHeight, Strength);
		break;
	case EDirtyPawnVolumeKind::Interior:
		DirtyPawn->InteriorCheck();
		break;
	default:
		break;
	}
}

void ADirtyPawnVolumeBase::EndApplyToDirtyPawn(UDirtyPawnComponent* DirtyPawn, AActor* SourceActor)
{
	if (!DirtyPawn)
	{
		return;
	}

	if (VolumeKind == EDirtyPawnVolumeKind::Water || VolumeKind == EDirtyPawnVolumeKind::MudWater || VolumeKind == EDirtyPawnVolumeKind::BleachWater)
	{
		DirtyPawn->EndWaterOverlap(SourceActor);
	}
}

UDirtyPawnComponent* ADirtyPawnVolumeBase::ResolveDirtyPawn(AActor* OtherActor, UPrimitiveComponent* OtherComponent) const
{
	if (UDirtyPawnComponent* Component = ResolveDirtyPawnFromActorChain(OtherActor))
	{
		return Component;
	}

	if (OtherComponent)
	{
		if (UDirtyPawnComponent* Component = ResolveDirtyPawnFromActorChain(OtherComponent->GetOwner()))
		{
			return Component;
		}
	}

	return nullptr;
}

float ADirtyPawnVolumeBase::ResolveNodeHeight(UDirtyPawnComponent* DirtyPawn) const
{
	const FBox ContactBounds = ResolveContactBounds();
	float ContactWorldHeight = ContactBounds.IsValid ? ContactBounds.Max.Z : GetActorLocation().Z;

	AActor* TargetActor = DirtyPawn ? DirtyPawn->GetOwner() : nullptr;
	if (!bUseVolumeTopAsHeight && TargetActor)
	{
		FVector Origin;
		FVector Extent;
		TargetActor->GetActorBounds(true, Origin, Extent);
		ContactWorldHeight = FMath::Min(ContactWorldHeight, Origin.Z + Extent.Z);
	}

	return DirtyPawn
		? DirtyPawn->ResolveBodyLocalHeightFromWorldZ(ContactWorldHeight, true)
		: ContactWorldHeight;
}

bool ADirtyPawnVolumeBase::ResolveContactHeightBand(UDirtyPawnComponent* DirtyPawn, float& OutMinHeight, float& OutMaxHeight) const
{
	if (!DirtyPawn)
	{
		OutMinHeight = 0.0f;
		OutMaxHeight = 0.0f;
		return false;
	}

	const FBox ContactBounds = ResolveContactBounds();
	if (!ContactBounds.IsValid)
	{
		OutMinHeight = 0.0f;
		OutMaxHeight = ResolveNodeHeight(DirtyPawn);
		return true;
	}

	AActor* TargetActor = DirtyPawn->GetOwner();
	if (!TargetActor)
	{
		OutMinHeight = DirtyPawn->ResolveBodyLocalHeightFromWorldZ(ContactBounds.Min.Z, false);
		OutMaxHeight = DirtyPawn->ResolveBodyLocalHeightFromWorldZ(ContactBounds.Max.Z, true);
		return true;
	}

	const bool bUseVerticalBoundsContact =
		VolumeKind == EDirtyPawnVolumeKind::Water ||
		VolumeKind == EDirtyPawnVolumeKind::BleachWater ||
		VolumeKind == EDirtyPawnVolumeKind::FadeWash;

	TSet<USkeletalMeshComponent*> CandidateMeshes;
	if (!bUseVerticalBoundsContact)
	{
		for (const FDirtyPawnMaterialBinding& Binding : DirtyPawn->MaterialBindings)
		{
			if (IsValid(Binding.MeshComponent))
			{
				CandidateMeshes.Add(Binding.MeshComponent);
			}
		}

		if (CandidateMeshes.Num() == 0)
		{
			TInlineComponentArray<USkeletalMeshComponent*> ActorMeshes(TargetActor);
			for (USkeletalMeshComponent* MeshComponent : ActorMeshes)
			{
				if (IsValid(MeshComponent))
				{
					CandidateMeshes.Add(MeshComponent);
				}
			}
		}
	}

	bool bSampledMesh = false;
	bool bFoundContactBone = false;
	float BoneMinHeight = TNumericLimits<float>::Max();
	float BoneMaxHeight = -TNumericLimits<float>::Max();

	for (USkeletalMeshComponent* MeshComponent : CandidateMeshes)
	{
		if (!IsValid(MeshComponent) || MeshComponent->GetNumBones() <= 0)
		{
			continue;
		}

		bSampledMesh = true;
		const int32 BoneCount = MeshComponent->GetNumBones();
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FVector BoneWorldLocation = MeshComponent->GetBoneLocation(MeshComponent->GetBoneName(BoneIndex));
			const bool bInsideContact = IsValid(ContactVolume)
				? IsWorldPointInsideBox(ContactVolume, BoneWorldLocation)
				: ContactBounds.IsInsideOrOn(BoneWorldLocation);

			if (!bInsideContact)
			{
				continue;
			}

			const float BodyHeight = DirtyPawn->ResolveBodyLocalHeightFromWorldZ(BoneWorldLocation.Z, false);
			BoneMinHeight = FMath::Min(BoneMinHeight, BodyHeight);
			BoneMaxHeight = FMath::Max(BoneMaxHeight, BodyHeight);
			bFoundContactBone = true;
		}
	}

	if (bFoundContactBone)
	{
		const float Padding = FMath::Max(DirtyPawn->MinimumPaintBandHeight * 2.0f, 7.5f);
		OutMinHeight = BoneMinHeight - Padding;
		OutMaxHeight = BoneMaxHeight + Padding;
		return OutMaxHeight > OutMinHeight;
	}

	if (bSampledMesh)
	{
		return false;
	}

	// ResolveBodyLocalHeightFromWorldZ updates DirtyPawnActorBottom/Top. Intersecting
	// before converting keeps head-only and partial-body contacts as real bands.
	DirtyPawn->ResolveBodyLocalHeightFromWorldZ(TargetActor->GetActorLocation().Z, false);
	const float BodyWorldMin = DirtyPawn->DirtyPawnActorBottom;
	const float BodyWorldMax = DirtyPawn->DirtyPawnActorTop;
	const float ContactWorldMin = FMath::Max(ContactBounds.Min.Z, BodyWorldMin);
	const float ContactWorldMax = FMath::Min(ContactBounds.Max.Z, BodyWorldMax);

	if (ContactWorldMax <= ContactWorldMin)
	{
		return false;
	}

	OutMinHeight = DirtyPawn->ResolveBodyLocalHeightFromWorldZ(ContactWorldMin, false);
	OutMaxHeight = DirtyPawn->ResolveBodyLocalHeightFromWorldZ(ContactWorldMax, true);
	return OutMaxHeight > OutMinHeight;
}

void ADirtyPawnVolumeBase::RefreshContactPrimitives()
{
	ContactPrimitives.Reset();

	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	GetComponents(Primitives);

	if (CanUseContactPrimitive(ContactVolume))
	{
		ContactVolume->OnComponentBeginOverlap.RemoveDynamic(this, &ADirtyPawnVolumeBase::OnVolumeBeginOverlap);
		ContactVolume->OnComponentEndOverlap.RemoveDynamic(this, &ADirtyPawnVolumeBase::OnVolumeEndOverlap);
		ContactVolume->OnComponentBeginOverlap.AddDynamic(this, &ADirtyPawnVolumeBase::OnVolumeBeginOverlap);
		ContactVolume->OnComponentEndOverlap.AddDynamic(this, &ADirtyPawnVolumeBase::OnVolumeEndOverlap);
		ContactPrimitives.Add(ContactVolume);
		return;
	}

	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!CanUseContactPrimitive(Primitive) || !IsPreferredContactPrimitive(Primitive))
		{
			continue;
		}

		Primitive->OnComponentBeginOverlap.RemoveDynamic(this, &ADirtyPawnVolumeBase::OnVolumeBeginOverlap);
		Primitive->OnComponentEndOverlap.RemoveDynamic(this, &ADirtyPawnVolumeBase::OnVolumeEndOverlap);
		Primitive->OnComponentBeginOverlap.AddDynamic(this, &ADirtyPawnVolumeBase::OnVolumeBeginOverlap);
		Primitive->OnComponentEndOverlap.AddDynamic(this, &ADirtyPawnVolumeBase::OnVolumeEndOverlap);
		ContactPrimitives.AddUnique(Primitive);
	}

	if (ContactPrimitives.Num() == 0 && IsValid(ContactVolume))
	{
		ContactVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ContactVolume->SetCollisionResponseToAllChannels(ECR_Overlap);
		ContactVolume->SetGenerateOverlapEvents(true);
		ContactVolume->OnComponentBeginOverlap.RemoveDynamic(this, &ADirtyPawnVolumeBase::OnVolumeBeginOverlap);
		ContactVolume->OnComponentEndOverlap.RemoveDynamic(this, &ADirtyPawnVolumeBase::OnVolumeEndOverlap);
		ContactVolume->OnComponentBeginOverlap.AddDynamic(this, &ADirtyPawnVolumeBase::OnVolumeBeginOverlap);
		ContactVolume->OnComponentEndOverlap.AddDynamic(this, &ADirtyPawnVolumeBase::OnVolumeEndOverlap);
		ContactPrimitives.Add(ContactVolume);
	}
}

void ADirtyPawnVolumeBase::ApplyCurrentOverlaps()
{
	TSet<UDirtyPawnComponent*> ExistingDirtyPawns;

	for (UPrimitiveComponent* Primitive : ContactPrimitives)
	{
		if (!IsValid(Primitive))
		{
			continue;
		}

		TArray<AActor*> OverlappingActors;
		Primitive->GetOverlappingActors(OverlappingActors);
		for (AActor* Actor : OverlappingActors)
		{
			if (UDirtyPawnComponent* DirtyPawn = ResolveDirtyPawn(Actor, nullptr))
			{
				ExistingDirtyPawns.Add(DirtyPawn);
			}
		}

		TArray<UPrimitiveComponent*> OverlappingComponents;
		Primitive->GetOverlappingComponents(OverlappingComponents);
		for (UPrimitiveComponent* OtherPrimitive : OverlappingComponents)
		{
			if (UDirtyPawnComponent* DirtyPawn = ResolveDirtyPawn(OtherPrimitive ? OtherPrimitive->GetOwner() : nullptr, OtherPrimitive))
			{
				ExistingDirtyPawns.Add(DirtyPawn);
			}
		}
	}

	for (UDirtyPawnComponent* DirtyPawn : ExistingDirtyPawns)
	{
		if (!IsValid(DirtyPawn))
		{
			continue;
		}

		OverlappingPawns.AddUnique(DirtyPawn);
		ApplyToDirtyPawn(DirtyPawn, this);
	}
}

FBox ADirtyPawnVolumeBase::ResolveContactBounds() const
{
	FBox Bounds(ForceInit);
	for (const UPrimitiveComponent* Primitive : ContactPrimitives)
	{
		if (IsValid(Primitive))
		{
			Bounds += Primitive->Bounds.GetBox();
		}
	}

	if (!Bounds.IsValid && IsValid(ContactVolume))
	{
		Bounds += ContactVolume->Bounds.GetBox();
	}

	if (!Bounds.IsValid)
	{
		FVector Origin;
		FVector Extent;
		GetActorBounds(true, Origin, Extent);
		Bounds = FBox::BuildAABB(Origin, Extent);
	}

	return Bounds;
}

bool ADirtyPawnVolumeBase::CanUseContactPrimitive(const UPrimitiveComponent* Primitive)
{
	if (!IsValid(Primitive) || !Primitive->GetGenerateOverlapEvents())
	{
		return false;
	}

	const ECollisionEnabled::Type CollisionEnabled = Primitive->GetCollisionEnabled();
	return CollisionEnabled == ECollisionEnabled::QueryOnly || CollisionEnabled == ECollisionEnabled::QueryAndPhysics;
}

bool ADirtyPawnVolumeBase::IsPreferredContactPrimitive(const UPrimitiveComponent* Primitive) const
{
	if (!IsValid(Primitive))
	{
		return false;
	}

	if (Primitive == ContactVolume)
	{
		return true;
	}

	const FString Name = Primitive->GetName();
	return Name.Equals(TEXT("ContactVolume"), ESearchCase::IgnoreCase);
}

UDirtyPawnComponent* ADirtyPawnVolumeBase::ResolveDirtyPawnFromActorChain(AActor* Actor)
{
	for (AActor* Candidate = Actor; IsValid(Candidate); Candidate = Candidate->GetAttachParentActor())
	{
		if (UDirtyPawnComponent* Component = UDirtyPawnComponent::FindCanonicalDirtyPawnComponent(Candidate))
		{
			return Component;
		}
	}

	return nullptr;
}

ADirtyPawnWaterVolume::ADirtyPawnWaterVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::Water;
}

ADirtyPawnMudWaterVolume::ADirtyPawnMudWaterVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::MudWater;
}

ADirtyPawnBleachWaterVolume::ADirtyPawnBleachWaterVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::BleachWater;
}

ADirtyPawnFadeWashVolume::ADirtyPawnFadeWashVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::FadeWash;
}

ADirtyPawnFadeSandSnowVolume::ADirtyPawnFadeSandSnowVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::SandAndSnow;
}

ADirtyPawnBloodVolume::ADirtyPawnBloodVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::HurtBlood;
}

ADirtyPawnSmearVolume::ADirtyPawnSmearVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::HurtSmear;
}

ADirtyPawnDirtVolume::ADirtyPawnDirtVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::HurtDirt;
}

ADirtyPawnBurnVolume::ADirtyPawnBurnVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::HurtBurn;
}

ADirtyPawnSandVolume::ADirtyPawnSandVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::Sand;
}

ADirtyPawnSnowVolume::ADirtyPawnSnowVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::Snow;
}

ADirtyPawnHurtVolume::ADirtyPawnHurtVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::HurtBlood;
}

ADirtyPawnInteriorVolume::ADirtyPawnInteriorVolume()
{
	VolumeKind = EDirtyPawnVolumeKind::Interior;
}


#include "Calysto/EFCalystoFloorLightingComponent.h"

#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UEFCalystoFloorLightingComponent::UEFCalystoFloorLightingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = false;
}

bool UEFCalystoFloorLightingComponent::Initialize(const FEFCalystoRandomKey& Key,
	const FEFCalystoResolvedLighting& Lighting, TConstArrayView<FEFCalystoNativeLightBinding> NativeLights, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !GetWorld() || !GetWorld()->IsGameWorld() || !GetOwner()
		|| bInitialized || NativeLights.Num() > 256 || !FMath::IsFinite(Lighting.IntensityMultiplier)
		|| Lighting.IntensityMultiplier < 0 || Lighting.IntensityMultiplier > 4
		|| !FEFCalystoLightingResolver::ValidateFlicker(Lighting.Flicker, Error))
	{ if (Error.IsEmpty()) Error = TEXT("Native lighting ownership or resolved configuration is invalid."); return false; }
	TSet<FGuid> Ids;
	TSet<UPointLightComponent*> Lights;
	TArray<FBinding> Proposed;
	for (const auto& Native : NativeLights)
	{
		UPointLightComponent* Light = Native.Light.Get();
		AActor* Torch = Native.Torch.Get();
		if (!Native.Id.IsValid() || Ids.Contains(Native.Id) || !IsValid(Torch) || !IsValid(Light)
			|| Torch->GetWorld() != GetWorld() || Light->GetWorld() != GetWorld() || Light->GetOwner() != Torch
			|| !Light->IsRegistered() || Light->Mobility != EComponentMobility::Movable || Lights.Contains(Light)
			|| !FMath::IsFinite(Light->Intensity) || Light->Intensity < 0
			|| Light->IntensityUnits != ELightUnits::Unitless)
		{ Error = TEXT("A selected native torch has missing, duplicate, static or unsupported light components."); return false; }
		Ids.Add(Native.Id); Lights.Add(Light);
		FBinding& B = Proposed.AddDefaulted_GetRef(); B.Native = Native; B.Baseline = Light->Intensity;
		if (!FMath::IsFinite(float(double(B.Baseline) * Lighting.IntensityMultiplier)))
		{ Error = TEXT("Native torch intensity exceeds finite light-component capacity."); return false; }
	}
	Bindings = MoveTemp(Proposed); Random = Key; Resolved = Lighting;
	StartedAt = GetWorld()->GetTimeSeconds(); LastElapsed = 0.0; bInitialized = true;
	if (!ApplyAtElapsed(0.0, Error)) { Release(); return false; }
	SetComponentTickEnabled(Resolved.Flicker.bEnabled && !Bindings.IsEmpty());
	return Verify(Error);
}

bool UEFCalystoFloorLightingComponent::ApplyAtElapsed(double Seconds, FString& Error)
{
	Error.Reset();
	if (!bInitialized || !GetWorld() || !FMath::IsFinite(Seconds) || Seconds < LastElapsed)
	{ Error = TEXT("Native lighting observation is stale or nonfinite."); return false; }
	TArray<float, TInlineAllocator<32>> Targets;
	for (const auto& B : Bindings)
	{
		const UPointLightComponent* Light = B.Native.Light.Get();
		const AActor* Torch = B.Native.Torch.Get();
		if (!IsValid(Torch) || !IsValid(Light) || Light->GetOwner() != Torch || Light->GetWorld() != GetWorld()
			|| !Light->IsRegistered() || Light->Mobility != EComponentMobility::Movable)
		{ Error = TEXT("An owned native torch light became unavailable."); return false; }
		double Flicker = 1.0;
		if (!FEFCalystoLightingResolver::EvaluateFlicker(Resolved.Flicker, Random, B.Native.Id, Seconds, Flicker, Error)) return false;
		const float Target = float(double(B.Baseline) * Resolved.IntensityMultiplier * Flicker);
		if (!FMath::IsFinite(Target) || Target < 0) { Error = TEXT("Native light intensity is nonfinite."); return false; }
		Targets.Add(Target);
	}
	for (int32 I = 0; I < Bindings.Num(); ++I)
	{
		Bindings[I].Native.Light->SetIntensity(Targets[I]);
		Bindings[I].Expected = Targets[I];
		if (Bindings[I].Native.Light->Intensity != Targets[I])
		{ Error = TEXT("Native torch light did not accept its selected intensity."); return false; }
	}
	LastElapsed = Seconds; return true;
}

bool UEFCalystoFloorLightingComponent::Verify(FString& Error) const
{
	Error.Reset();
	if (!bInitialized || !Failure.IsEmpty())
	{ Error = Failure.IsEmpty() ? TEXT("Native lighting is not initialized.") : Failure; return false; }
	for (const auto& B : Bindings)
	{
		const auto* L = B.Native.Light.Get();
		if (!IsValid(B.Native.Torch.Get()) || !IsValid(L) || L->GetOwner() != B.Native.Torch.Get()
			|| L->GetWorld() != GetWorld() || !L->IsRegistered() || L->Mobility != EComponentMobility::Movable
			|| !FMath::IsFinite(L->Intensity) || L->Intensity != B.Expected)
		{ Error = TEXT("An owned native torch light no longer matches its realized intensity."); return false; }
	}
	return true;
}

void UEFCalystoFloorLightingComponent::TickComponent(float DeltaSeconds, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
	if (!bInitialized || !GetWorld() || !Failure.IsEmpty()) return;
	if (!ApplyAtElapsed(double(GetWorld()->GetTimeSeconds()) - StartedAt, Failure)) SetComponentTickEnabled(false);
}

void UEFCalystoFloorLightingComponent::Release()
{
	SetComponentTickEnabled(false);
	for (const auto& B : Bindings)
		if (auto* L = B.Native.Light.Get(); IsValid(L) && L->GetOwner() == B.Native.Torch.Get()
			&& L->GetWorld() == GetWorld() && L->IsRegistered() && L->Mobility == EComponentMobility::Movable)
			L->SetIntensity(B.Baseline);
	Bindings.Reset(); bInitialized = false; Failure.Reset(); StartedAt = 0.0; LastElapsed = 0.0;
}

void UEFCalystoFloorLightingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Release(); Super::EndPlay(Reason);
}

#include "Effects/ProjectBreathingFadeComponent.h"

#include "Components/ChildActorComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "UObject/UnrealType.h"

#include <initializer_list>

namespace ProjectBreathingFadeComponentPrivate
{
	static constexpr float MinPhaseDuration = 0.001f;

	bool ContainsAnyToken(const FString& Text, std::initializer_list<const TCHAR*> Tokens)
	{
		for (const TCHAR* Token : Tokens)
		{
			if (Text.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

UProjectBreathingFadeComponent::UProjectBreathingFadeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bAutoActivate = true;
	bTickInEditor = true;
}

void UProjectBreathingFadeComponent::OnRegister()
{
	Super::OnRegister();

	if (bAutoStart)
	{
		StartBreathingFade();
	}
}

void UProjectBreathingFadeComponent::OnUnregister()
{
	FogMesh.Reset();
	ChildActorComponent.Reset();
	bTemplateSettingsSynced = false;
	bHasActiveVisibleFogDensity = false;
	Super::OnUnregister();
}

void UProjectBreathingFadeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoStart)
	{
		StartBreathingFade();
	}
}

void UProjectBreathingFadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBreathingFade(true);
	Super::EndPlay(EndPlayReason);
}

void UProjectBreathingFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsRunning)
	{
		return;
	}

	const float EffectiveDeltaTime = DeltaTime > 0.0f ? DeltaTime : FApp::GetDeltaTime();
	PhaseTimeSeconds += FMath::Max(EffectiveDeltaTime, 0.0f);

	float PhaseDuration = GetCurrentPhaseDuration();
	while (PhaseTimeSeconds >= PhaseDuration)
	{
		PhaseTimeSeconds -= PhaseDuration;
		AdvancePhase();

		if (!bIsRunning)
		{
			return;
		}

		PhaseDuration = GetCurrentPhaseDuration();
	}

	ApplyCurrentFogDensity();
}

void UProjectBreathingFadeComponent::StartBreathingFade()
{
	bIsRunning = true;
	Phase = EBreathingFadePhase::FadeIn;
	PhaseTimeSeconds = 0.0f;
	bTemplateSettingsSynced = false;
	bHasActiveVisibleFogDensity = false;
	ActiveVisibleFogDensity = VisibleFogDensity;
	SetComponentTickEnabled(true);
	ApplyCurrentFogDensity();
}

void UProjectBreathingFadeComponent::StopBreathingFade(bool bRestoreFullIntensity)
{
	if (bRestoreFullIntensity)
	{
		ApplyFogDensity(GetVisibleFogDensity());
	}

	bIsRunning = false;
	SetComponentTickEnabled(false);
}

void UProjectBreathingFadeComponent::RestartBreathingFade()
{
	StopBreathingFade(false);
	StartBreathingFade();
}

UMeshComponent* UProjectBreathingFadeComponent::ResolveFogMesh()
{
	if (FogMesh.IsValid())
	{
		return FogMesh.Get();
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (UChildActorComponent* TargetChildActorComponent = ResolveTargetChildActorComponent())
	{
		if (AActor* ChildActor = TargetChildActorComponent->GetChildActor())
		{
			if (UMeshComponent* MeshComponent = ResolveFogMeshOnActor(ChildActor))
			{
				ChildActorComponent = TargetChildActorComponent;
				FogMesh = MeshComponent;
				return MeshComponent;
			}
		}
	}

	if (UMeshComponent* MeshComponent = ResolveFogMeshOnActor(Owner))
	{
		FogMesh = MeshComponent;
		return MeshComponent;
	}

	return nullptr;
}

UMeshComponent* UProjectBreathingFadeComponent::ResolveFogMeshOnActor(AActor* Actor) const
{
	if (!Actor)
	{
		return nullptr;
	}

	TArray<UMeshComponent*> MeshComponents;
	Actor->GetComponents<UMeshComponent>(MeshComponents);

	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (ComponentNameMatches(MeshComponent))
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

UChildActorComponent* UProjectBreathingFadeComponent::ResolveTargetChildActorComponent() const
{
	if (ChildActorComponent.IsValid())
	{
		return ChildActorComponent.Get();
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<UChildActorComponent*> ChildActorComponents;
	Owner->GetComponents<UChildActorComponent>(ChildActorComponents);

	for (UChildActorComponent* Candidate : ChildActorComponents)
	{
		if (ChildActorComponentNameMatches(Candidate))
		{
			return Candidate;
		}
	}

	return nullptr;
}

UChildActorComponent* UProjectBreathingFadeComponent::ResolveOwningChildActorComponent(const UMeshComponent* MeshComponent) const
{
	if (ChildActorComponent.IsValid())
	{
		return ChildActorComponent.Get();
	}

	const AActor* RuntimeActor = MeshComponent ? MeshComponent->GetOwner() : GetOwner();
	return RuntimeActor ? RuntimeActor->GetParentComponent() : nullptr;
}

AActor* UProjectBreathingFadeComponent::ResolveSceneChildTemplateActor(const UMeshComponent* MeshComponent) const
{
	const UChildActorComponent* OwningChildActorComponent = ResolveOwningChildActorComponent(MeshComponent);
	return OwningChildActorComponent ? OwningChildActorComponent->GetChildActorTemplate() : nullptr;
}

void UProjectBreathingFadeComponent::SyncFromSceneChildTemplate(UMeshComponent* MeshComponent)
{
	if (!bUseSceneChildTemplateSettings || bTemplateSettingsSynced || !MeshComponent)
	{
		return;
	}

	AActor* RuntimeActor = MeshComponent->GetOwner();
	AActor* TemplateActor = ResolveSceneChildTemplateActor(MeshComponent);
	if (!RuntimeActor || !TemplateActor || RuntimeActor == TemplateActor)
	{
		return;
	}

	CopyTemplateMaterials(TemplateActor, MeshComponent);
	CopyEditableFogSettings(TemplateActor, RuntimeActor, MeshComponent);

	float TemplateFogDensity = 0.0f;
	if (TryReadFloatProperty(TemplateActor, TEXT("FogDensity"), TemplateFogDensity))
	{
		ActiveVisibleFogDensity = FMath::Max(TemplateFogDensity, 0.0f);
		bHasActiveVisibleFogDensity = true;
	}

	InvokeFogRefreshFunction(RuntimeActor);
	bTemplateSettingsSynced = true;
}

void UProjectBreathingFadeComponent::CopyEditableFogSettings(AActor* TemplateActor, AActor* RuntimeActor, UMeshComponent* RuntimeMesh)
{
	if (!TemplateActor || !RuntimeActor || TemplateActor->GetClass() != RuntimeActor->GetClass())
	{
		return;
	}

	for (TFieldIterator<FProperty> PropertyIt(TemplateActor->GetClass()); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if (!ShouldCopyTemplateProperty(Property))
		{
			continue;
		}

		Property->CopyCompleteValue_InContainer(RuntimeActor, TemplateActor);
		ApplyCopiedPropertyToFogMaterial(Property, RuntimeActor, RuntimeMesh);
	}
}

bool UProjectBreathingFadeComponent::ShouldCopyTemplateProperty(const FProperty* Property) const
{
	if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
	{
		return false;
	}

	if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_Deprecated | CPF_InstancedReference))
	{
		return false;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		const UClass* PropertyClass = ObjectProperty->PropertyClass;
		if (PropertyClass && (PropertyClass->IsChildOf<AActor>() || PropertyClass->IsChildOf<UActorComponent>()))
		{
			return false;
		}
	}

	const FString PropertyName = Property->GetName();
	if (ProjectBreathingFadeComponentPrivate::ContainsAnyToken(PropertyName, { TEXT("MaterialInstance"), TEXT("UberGraphFrame") }))
	{
		return false;
	}

	FString SearchText = PropertyName;
#if WITH_EDITORONLY_DATA
	const FString Category = Property->GetMetaData(TEXT("Category"));
	const FString DisplayName = Property->GetMetaData(TEXT("DisplayName"));
	SearchText += TEXT(" ") + Category + TEXT(" ") + DisplayName;
#endif

	return ProjectBreathingFadeComponentPrivate::ContainsAnyToken(SearchText, {
		TEXT("Easyfog"),
		TEXT("Easy Fog"),
		TEXT("Fog"),
		TEXT("Flowmap"),
		TEXT("Flow Map"),
		TEXT("Base Color"),
		TEXT("Emissive"),
		TEXT("Normal"),
		TEXT("Roughness"),
		TEXT("Opacity"),
		TEXT("Atmosphere"),
		TEXT("Raytraced"),
		TEXT("Fading"),
		TEXT("View Angle"),
		TEXT("Border"),
		TEXT("Wind")
	});
}

void UProjectBreathingFadeComponent::ApplyCopiedPropertyToFogMaterial(const FProperty* Property, const void* SourceContainer, UMeshComponent* RuntimeMesh)
{
	if (!Property || !SourceContainer || !RuntimeMesh)
	{
		return;
	}

	FString ParameterNameString;
#if WITH_EDITORONLY_DATA
	ParameterNameString = Property->GetMetaData(TEXT("DisplayName"));
#endif
	if (ParameterNameString.IsEmpty())
	{
		ParameterNameString = Property->GetName();
	}

	const FName ParameterName(*ParameterNameString);
	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(SourceContainer);

	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		RuntimeMesh->SetScalarParameterValueOnMaterials(ParameterName, BoolProperty->GetPropertyValue(ValuePtr) ? 1.0f : 0.0f);
		return;
	}

	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		const float NumericValue = NumericProperty->IsFloatingPoint()
			? static_cast<float>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr))
			: static_cast<float>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		RuntimeMesh->SetScalarParameterValueOnMaterials(ParameterName, NumericValue);
		return;
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor& Color = *static_cast<const FLinearColor*>(ValuePtr);
			for (int32 MaterialIndex = 0; MaterialIndex < RuntimeMesh->GetNumMaterials(); ++MaterialIndex)
			{
				UMaterialInterface* SourceMaterial = RuntimeMesh->GetMaterial(MaterialIndex);
				UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(SourceMaterial);
				if (!DynamicMaterial)
				{
					DynamicMaterial = RuntimeMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(MaterialIndex, SourceMaterial);
				}

				if (DynamicMaterial)
				{
					DynamicMaterial->SetVectorParameterValue(ParameterName, Color);
				}
			}
		}
		return;
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (UTexture* Texture = Cast<UTexture>(ObjectProperty->GetObjectPropertyValue(ValuePtr)))
		{
			ApplyTextureParameterToFogMaterials(RuntimeMesh, ParameterName, Texture);
		}
	}
}

void UProjectBreathingFadeComponent::ApplyTextureParameterToFogMaterials(UMeshComponent* RuntimeMesh, FName ParameterName, UTexture* Texture)
{
	if (!RuntimeMesh || ParameterName.IsNone() || !Texture)
	{
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < RuntimeMesh->GetNumMaterials(); ++MaterialIndex)
	{
		UMaterialInterface* SourceMaterial = RuntimeMesh->GetMaterial(MaterialIndex);
		UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(SourceMaterial);
		if (!DynamicMaterial)
		{
			DynamicMaterial = RuntimeMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(MaterialIndex, SourceMaterial);
		}

		if (DynamicMaterial)
		{
			DynamicMaterial->SetTextureParameterValue(ParameterName, Texture);
		}
	}
}

void UProjectBreathingFadeComponent::CopyTemplateMaterials(AActor* TemplateActor, UMeshComponent* RuntimeMesh)
{
	UMeshComponent* TemplateMesh = ResolveFogMeshOnActor(TemplateActor);
	if (!TemplateMesh || !RuntimeMesh || TemplateMesh == RuntimeMesh)
	{
		return;
	}

	const int32 MaterialCount = TemplateMesh->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		if (UMaterialInterface* TemplateMaterial = TemplateMesh->GetMaterial(MaterialIndex))
		{
			RuntimeMesh->SetMaterial(MaterialIndex, TemplateMaterial);
		}
	}
}

void UProjectBreathingFadeComponent::InvokeFogRefreshFunction(AActor* RuntimeActor)
{
	if (!RuntimeActor)
	{
		return;
	}

	for (TFieldIterator<UFunction> FunctionIt(RuntimeActor->GetClass(), EFieldIteratorFlags::IncludeSuper); FunctionIt; ++FunctionIt)
	{
		UFunction* Function = *FunctionIt;
		if (!Function)
		{
			continue;
		}

		const FString FunctionName = Function->GetName();
		if (!ProjectBreathingFadeComponentPrivate::ContainsAnyToken(FunctionName, { TEXT("Manual"), TEXT("UpdateFog") }))
		{
			continue;
		}

		bool bHasParameters = false;
		for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
		{
			if (ParamIt->HasAnyPropertyFlags(CPF_Parm))
			{
				bHasParameters = true;
				break;
			}
		}

		if (!bHasParameters)
		{
			RuntimeActor->ProcessEvent(Function, nullptr);
		}
	}
}

bool UProjectBreathingFadeComponent::TryReadFloatProperty(const UObject* Object, FName PropertyName, float& OutValue) const
{
	if (!Object || PropertyName.IsNone())
	{
		return false;
	}

	const FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		OutValue = NumericProperty->IsFloatingPoint()
			? static_cast<float>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr))
			: static_cast<float>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		return true;
	}

	return false;
}

void UProjectBreathingFadeComponent::ApplyCurrentFogDensity()
{
	ApplyFogDensity(CalculateCurrentFogDensity());
}

void UProjectBreathingFadeComponent::ApplyFogDensity(float FogDensity)
{
	UMeshComponent* MeshComponent = ResolveFogMesh();
	if (!MeshComponent || FogDensityParameterName.IsNone())
	{
		return;
	}

	SyncFromSceneChildTemplate(MeshComponent);

	const float ClampedDensity = FMath::Max(FogDensity, 0.0f);
	MeshComponent->SetScalarParameterValueOnMaterials(FogDensityParameterName, ClampedDensity);
	MeshComponent->MarkRenderDynamicDataDirty();
}

void UProjectBreathingFadeComponent::AdvancePhase()
{
	switch (Phase)
	{
	case EBreathingFadePhase::FadeIn:
		Phase = EBreathingFadePhase::VisibleHold;
		break;
	case EBreathingFadePhase::VisibleHold:
		Phase = EBreathingFadePhase::FadeOut;
		break;
	case EBreathingFadePhase::FadeOut:
		Phase = EBreathingFadePhase::InvisibleHold;
		break;
	case EBreathingFadePhase::InvisibleHold:
	default:
		if (bLoop)
		{
			Phase = EBreathingFadePhase::FadeIn;
		}
		else
		{
			StopBreathingFade(false);
		}
		break;
	}
}

float UProjectBreathingFadeComponent::GetCurrentPhaseDuration() const
{
	switch (Phase)
	{
	case EBreathingFadePhase::FadeIn:
		return FMath::Max(FadeInSeconds, ProjectBreathingFadeComponentPrivate::MinPhaseDuration);
	case EBreathingFadePhase::VisibleHold:
		return FMath::Max(VisibleHoldSeconds, ProjectBreathingFadeComponentPrivate::MinPhaseDuration);
	case EBreathingFadePhase::FadeOut:
		return FMath::Max(FadeOutSeconds, ProjectBreathingFadeComponentPrivate::MinPhaseDuration);
	case EBreathingFadePhase::InvisibleHold:
	default:
		return FMath::Max(InvisibleHoldSeconds, ProjectBreathingFadeComponentPrivate::MinPhaseDuration);
	}
}

float UProjectBreathingFadeComponent::CalculateCurrentAlpha() const
{
	const float Duration = GetCurrentPhaseDuration();
	const float PhaseAlpha = FMath::Clamp(PhaseTimeSeconds / Duration, 0.0f, 1.0f);

	switch (Phase)
	{
	case EBreathingFadePhase::FadeIn:
		return ShapeAlpha(PhaseAlpha);
	case EBreathingFadePhase::VisibleHold:
		return 1.0f;
	case EBreathingFadePhase::FadeOut:
		return 1.0f - ShapeAlpha(PhaseAlpha);
	case EBreathingFadePhase::InvisibleHold:
	default:
		return 0.0f;
	}
}

float UProjectBreathingFadeComponent::ShapeAlpha(float Alpha) const
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (!bUseSmoothFade)
	{
		return ClampedAlpha;
	}

	return ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);
}

float UProjectBreathingFadeComponent::CalculateCurrentFogDensity() const
{
	return FMath::Lerp(InvisibleFogDensity, GetVisibleFogDensity(), CalculateCurrentAlpha());
}

float UProjectBreathingFadeComponent::GetVisibleFogDensity() const
{
	return bHasActiveVisibleFogDensity ? ActiveVisibleFogDensity : VisibleFogDensity;
}

bool UProjectBreathingFadeComponent::ComponentNameMatches(const UMeshComponent* Component) const
{
	if (!Component)
	{
		return false;
	}

	if (TargetComponentName.IsNone())
	{
		return true;
	}

	const FString TargetString = TargetComponentName.ToString();
	return Component->GetFName() == TargetComponentName || Component->GetName().Contains(TargetString);
}

bool UProjectBreathingFadeComponent::ChildActorComponentNameMatches(const UChildActorComponent* Component) const
{
	if (!Component)
	{
		return false;
	}

	if (TargetChildActorComponentName.IsNone())
	{
		return true;
	}

	const FString TargetString = TargetChildActorComponentName.ToString();
	return Component->GetFName() == TargetChildActorComponentName || Component->GetName().Contains(TargetString);
}

#if WITH_EDITOR
void UProjectBreathingFadeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FogMesh.Reset();
	ChildActorComponent.Reset();
	bTemplateSettingsSynced = false;
	bHasActiveVisibleFogDensity = false;
	if (bAutoStart)
	{
		StartBreathingFade();
	}
	else
	{
		ApplyFogDensity(VisibleFogDensity);
	}
}
#endif

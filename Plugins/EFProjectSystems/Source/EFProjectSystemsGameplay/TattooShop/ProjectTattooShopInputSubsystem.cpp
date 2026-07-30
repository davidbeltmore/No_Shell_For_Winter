#include "TattooShop/ProjectTattooShopInputSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/Image.h"
#include "Components/InputComponent.h"
#include "Components/PanelWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EFCharacterCustomizationComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectTattooShopInput, Log, All);

namespace ProjectTattooShopInputPrivate
{
	constexpr int32 WidgetZOrder = 10050;

	const TCHAR* TattooShopWidgetPath = TEXT("/Game/TattooShop/Blueprints/Widget/WBP_TattooShop.WBP_TattooShop_C");
	const TCHAR* TattooAssetPreviewWidgetPath = TEXT("/Game/TattooShop/Blueprints/Widget/WBP_AssetPreviewer.WBP_AssetPreviewer_C");
	const TCHAR* TattooCustomizationWidgetPath = TEXT("/Game/TattooShop/Blueprints/Widget/WBP_TattooCustomization.WBP_TattooCustomization_C");
	const TCHAR* TattooShopCharacterPathToken = TEXT("/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar");
	const FName InitializeEventName(TEXT("OC_InitializeTattooShop"));
	const FName WidgetPropertyName(TEXT("TattooShopParentWGT"));
	const FName ActorRefPropertyName(TEXT("ActorRef"));
	const FName AssetPreviewerPropertyName(TEXT("AssetPreviewer"));
	const FName PreviewGridPropertyName(TEXT("PreviewGrid"));
	const FName GridColumnLimitPropertyName(TEXT("GridColumnLimit"));
	const FName TattooCardTexturePropertyName(TEXT("AssetTexture"));
	const FName TattooCardNamePropertyName(TEXT("AssetfName"));
	const FName TattooCardAlternateNamePropertyName(TEXT("Assetname"));
	const FName TattooBaseComponentsPropertyName(TEXT("TatBaseComp"));
	const FName NewDynamicMaterialPropertyName(TEXT("NewDynamicMaterialInst"));
	const FName SelectedMIDPropertyName(TEXT("SelectedMID"));
	const FName TargetMIDPropertyName(TEXT("TargetMID"));
	const FName PreEditMIDPropertyName(TEXT("PreEditMID"));
	const FName SelectedAssetTexturePropertyName(TEXT("SelectedAssetTexture"));
	const TCHAR* TattooCardPathToken = TEXT("/Game/TattooShop/Blueprints/Widget/WBP_TattooCard");
	const TCHAR* TattooShopMaterialPath = TEXT("/Game/TattooShop/Materials/M_TattooShop.M_TattooShop");
	const TCHAR* HeartTexturePath = TEXT("/Game/TattooShop/Texture/T_Heart.T_Heart");
	const FName TextureParameterName(TEXT("Texture"));
	const FName ColorParameterName(TEXT("Color"));
	const FName EmissiveColorParameterName(TEXT("EmissiveColor"));
	const FName OffsetParameterName(TEXT("Offset"));

	FString NormalizeReflectionName(FString Name)
	{
		Name.ReplaceInline(TEXT(" "), TEXT(""));
		Name.ReplaceInline(TEXT("_"), TEXT(""));
		Name.ReplaceInline(TEXT("-"), TEXT(""));
		Name.ToLowerInline();
		return Name;
	}

	FProperty* FindPropertyByCompatibleName(const UClass* ObjectClass, const FName PreferredName)
	{
		if (!ObjectClass)
		{
			return nullptr;
		}

		if (FProperty* ExactProperty = FindFProperty<FProperty>(ObjectClass, PreferredName))
		{
			return ExactProperty;
		}

		const FString NormalizedPreferredName = NormalizeReflectionName(PreferredName.ToString());
		for (TFieldIterator<FProperty> PropertyIt(ObjectClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (Property && NormalizeReflectionName(Property->GetName()) == NormalizedPreferredName)
			{
				return Property;
			}
		}

		return nullptr;
	}

	FObjectPropertyBase* FindObjectPropertyByCompatibleName(const UClass* ObjectClass, const FName PreferredName)
	{
		return CastField<FObjectPropertyBase>(FindPropertyByCompatibleName(ObjectClass, PreferredName));
	}

	FArrayProperty* FindArrayPropertyByCompatibleName(const UClass* ObjectClass, const FName PreferredName)
	{
		return CastField<FArrayProperty>(FindPropertyByCompatibleName(ObjectClass, PreferredName));
	}

	bool SetObjectPropertyWhenCompatible(
		UObject* Owner,
		const FName PropertyName,
		UObject* Value,
		const bool bOnlyWhenNull)
	{
		if (!Owner || !Value)
		{
			return false;
		}

		FObjectPropertyBase* ObjectProperty = FindObjectPropertyByCompatibleName(Owner->GetClass(), PropertyName);
		if (!ObjectProperty || (ObjectProperty->PropertyClass && !Value->IsA(ObjectProperty->PropertyClass)))
		{
			return false;
		}

		UObject* ExistingValue = ObjectProperty->GetObjectPropertyValue_InContainer(Owner);
		if (bOnlyWhenNull && ExistingValue)
		{
			return ExistingValue == Value;
		}

		ObjectProperty->SetObjectPropertyValue_InContainer(Owner, Value);
		return true;
	}

	bool IsTattooCardWidget(const UWidget* Widget)
	{
		const UClass* WidgetClass = IsValid(Widget) ? Widget->GetClass() : nullptr;
		const FString ClassPath = WidgetClass ? WidgetClass->GetPathName() : FString();
		return ClassPath.Contains(TattooCardPathToken) || ClassPath.Contains(TEXT("WBP_TattooCard"));
	}

	void CollectTattooCardVisualIdentity(
		UWidget* Widget,
		TSet<const UWidget*>& VisitedWidgets,
		bool& bContainsTattooCard,
		FString& OutLabelIdentity,
		FString& OutBrushIdentity)
	{
		if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
		{
			return;
		}

		VisitedWidgets.Add(Widget);
		bContainsTattooCard |= IsTattooCardWidget(Widget);

		if (OutLabelIdentity.IsEmpty())
		{
			if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				const FString TextIdentity = TextBlock->GetText().ToString().TrimStartAndEnd();
				if (TextIdentity.StartsWith(TEXT("T_")) || TextIdentity.StartsWith(TEXT("Test")))
				{
					OutLabelIdentity = TextIdentity;
				}
			}
		}

		if (OutBrushIdentity.IsEmpty())
		{
			if (const UImage* Image = Cast<UImage>(Widget))
			{
				UObject* ResourceObject = Image->GetBrush().GetResourceObject();
				const FString ResourcePath = ResourceObject ? ResourceObject->GetPathName() : FString();
				if (ResourceObject
					&& (ResourceObject->IsA<UTexture>()
						|| ResourcePath.Contains(TEXT("/Game/TattooShop/Texture/"))))
				{
					OutBrushIdentity = ResourcePath;
				}
			}
		}

		if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UWidgetTree* WidgetTree = UserWidget->WidgetTree)
			{
				WidgetTree->ForEachWidget([&VisitedWidgets, &bContainsTattooCard, &OutLabelIdentity, &OutBrushIdentity](UWidget* ChildWidget)
				{
					CollectTattooCardVisualIdentity(ChildWidget, VisitedWidgets, bContainsTattooCard, OutLabelIdentity, OutBrushIdentity);
				});
			}
		}

		if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < PanelWidget->GetChildrenCount(); ++ChildIndex)
			{
				CollectTattooCardVisualIdentity(PanelWidget->GetChildAt(ChildIndex), VisitedWidgets, bContainsTattooCard, OutLabelIdentity, OutBrushIdentity);
			}
		}
	}

	void SetScalarIfPresent(UMaterialInstanceDynamic* Material, const FName ParameterName, const float Value)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(ParameterName, Value);
		}
	}

	float ReadFloatEnvironmentVariable(const TCHAR* Name, const float DefaultValue)
	{
		FString RawValue = FPlatformMisc::GetEnvironmentVariable(Name);
		RawValue.TrimStartAndEndInline();
		if (RawValue.IsEmpty())
		{
			return DefaultValue;
		}

		return FCString::Atof(*RawValue);
	}

	bool IsSkinnedDecalOverlayMaterial(UMaterialInterface* Material)
	{
		if (!Material)
		{
			return false;
		}

		if (Material->GetPathName().Contains(TEXT("SkinnedDecalOverlay"))
			|| Material->GetPathName().Contains(TEXT("/SkinnedDecalComponent/")))
		{
			return true;
		}

		UMaterial* BaseMaterial = Material->GetBaseMaterial();
		return BaseMaterial
			&& (BaseMaterial->GetPathName().Contains(TEXT("SkinnedDecalOverlay"))
				|| BaseMaterial->GetPathName().Contains(TEXT("/SkinnedDecalComponent/")));
	}

	APlayerController* ResolveLocalPlayerController(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PlayerController = It->Get();
			if (PlayerController && PlayerController->IsLocalController())
			{
				return PlayerController;
			}
		}

		return nullptr;
	}

}

void UProjectTattooShopInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedInputComponent = nullptr;
	TrackedTattooShopWidget = nullptr;
	TattooShopHostPanel = nullptr;
	TattooAssetPreviewHostPanel = nullptr;
	TattooShopWidgetClass = nullptr;
	TattooAssetPreviewWidgetClass = nullptr;
	TrackedAssetPreviewWidget = nullptr;
	MirroredOverlayTarget = nullptr;
	LastMirroredOverlayMaterial = nullptr;
	AutomationTattooBaseComponent = nullptr;
	RuntimeTattooCustomizationWidget = nullptr;
	RuntimeTattooMID = nullptr;
	LastRuntimeTattooTexture = nullptr;
	RuntimeTattooBaseComponent = nullptr;
	InputSnapshot = FProjectTattooShopInputSnapshot();
	bTattooShopOpen = false;
}

void UProjectTattooShopInputSubsystem::Deinitialize()
{
	DetachFromTrackedPlayerController();
	Super::Deinitialize();
}

void UProjectTattooShopInputSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();
	CaptureAssetPreviewWidget();
	NormalizeTattooCardGrid(TrackedTattooShopWidget.Get());
	NormalizeTattooCardGrid(TrackedAssetPreviewWidget.Get());
	SynchronizeTattooOverlayToVisibleSkin();
}

TStatId UProjectTattooShopInputSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectTattooShopInputSubsystem, STATGROUP_Tickables);
}

bool UProjectTattooShopInputSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->IsGameWorld();
}

bool UProjectTattooShopInputSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UProjectTattooShopInputSubsystem::RequestToggleTattooShop()
{
	TattooShopHostPanel = nullptr;
	TattooAssetPreviewHostPanel = nullptr;
	HandleTogglePressed();
}

void UProjectTattooShopInputSubsystem::RequestOpenTattooShopInHost(UPanelWidget* HostPanel)
{
	TattooShopHostPanel = HostPanel;
	TattooAssetPreviewHostPanel = nullptr;
	TryResolveRuntimeContext();
	OpenTattooShop();
}

void UProjectTattooShopInputSubsystem::RequestOpenTattooShopInHosts(UPanelWidget* HostPanel, UPanelWidget* AssetPreviewPanel)
{
	TattooShopHostPanel = HostPanel;
	TattooAssetPreviewHostPanel = AssetPreviewPanel;
	TryResolveRuntimeContext();
	OpenTattooShop();
}

void UProjectTattooShopInputSubsystem::RequestCloseTattooShop()
{
	CloseTattooShop();
	TattooShopHostPanel = nullptr;
	TattooAssetPreviewHostPanel = nullptr;
}

bool UProjectTattooShopInputSubsystem::IsTattooShopOpen() const
{
	return bTattooShopOpen;
}

UUserWidget* UProjectTattooShopInputSubsystem::GetTattooShopWidgetForAutomation() const
{
	return TrackedTattooShopWidget.Get();
}

UUserWidget* UProjectTattooShopInputSubsystem::GetAssetPreviewWidgetForAutomation() const
{
	return TrackedAssetPreviewWidget.Get();
}

void UProjectTattooShopInputSubsystem::NormalizeTattooCardGridsForAutomation() const
{
	NormalizeTattooCardGrid(TrackedTattooShopWidget.Get());
	NormalizeTattooCardGrid(TrackedAssetPreviewWidget.Get());
}

FString UProjectTattooShopInputSubsystem::GetTattooCardGridReportForAutomation() const
{
	return BuildTattooCardGridReport(TrackedAssetPreviewWidget.Get(), TrackedTattooShopWidget.Get());
}

bool UProjectTattooShopInputSubsystem::ApplyTattooTextureForAutomation(UTexture2D* TattooTexture)
{
	if (!TattooTexture)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("ApplyTattooTextureForAutomation failed: tattoo texture is null."));
		return false;
	}

	TryResolveRuntimeContext();

	APawn* Pawn = TrackedPlayerPawn.Get();
	if (!CanUseTattooShopPawn(Pawn))
	{
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* PlayerController =
				ProjectTattooShopInputPrivate::ResolveLocalPlayerController(World))
			{
				AttachToPlayerController(PlayerController);
				Pawn = PlayerController->GetPawn();
				TrackedPlayerPawn = Pawn;
			}
		}
	}

	if (!CanUseTattooShopPawn(Pawn))
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("ApplyTattooTextureForAutomation failed: no TattooShop pawn is available."));
		return false;
	}

	if (!bTattooShopOpen)
	{
		TattooShopHostPanel = nullptr;
		TattooAssetPreviewHostPanel = nullptr;
		OpenTattooShop();
	}

	const bool bUseSkinnedPreview = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_USE_SKINNED_PREVIEW"), 1.0f) > 0.5f;
	if (bUseSkinnedPreview)
	{
		if (USkeletalMeshComponent* ExistingAutomationBase = AutomationTattooBaseComponent.Get())
		{
			ExistingAutomationBase->SetHiddenInGame(true, true);
			ExistingAutomationBase->SetVisibility(false, true);
			ExistingAutomationBase->SetOverlayMaterial(nullptr);
		}

		if (UWorld* World = GetWorld())
		{
			if (UProjectDefaultTattooSkinnedDecalSubsystem* SkinnedDecalSubsystem = World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>())
			{
				return SkinnedDecalSubsystem->ApplyTattooShopPreviewForAutomation(Pawn);
			}
		}
	}

	UMaterialInterface* TattooMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		ProjectTattooShopInputPrivate::TattooShopMaterialPath);
	if (!TattooMaterial)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("ApplyTattooTextureForAutomation failed: could not load %s."), ProjectTattooShopInputPrivate::TattooShopMaterialPath);
		return false;
	}

	UMaterialInstanceDynamic* DynamicTattooMaterial = UMaterialInstanceDynamic::Create(TattooMaterial, Pawn);
	if (!DynamicTattooMaterial)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("ApplyTattooTextureForAutomation failed: could not create dynamic material."));
		return false;
	}

	const float ManualScale = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_SCALE"), 0.58f);
	const float ManualScaleY = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_SCALE_Y"), ManualScale);
	const float ManualOffsetX = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_OFFSET_X"), 0.0f);
	const float ManualOffsetY = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_OFFSET_Y"), 0.478f);
	const float ManualOffsetZ = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_OFFSET_Z"), 0.3f);
	const float ManualWorldOffset = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_WORLD_OFFSET"), 0.5f);
	const float ManualDepthMin = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_DEPTH_MIN"), -4.0f);
	const float ManualDepthMax = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_DEPTH_MAX"), 4.0f);

	DynamicTattooMaterial->SetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName, TattooTexture);
	DynamicTattooMaterial->SetVectorParameterValue(ProjectTattooShopInputPrivate::ColorParameterName, FLinearColor::Black);
	DynamicTattooMaterial->SetVectorParameterValue(ProjectTattooShopInputPrivate::EmissiveColorParameterName, FLinearColor::Black);
	DynamicTattooMaterial->SetVectorParameterValue(ProjectTattooShopInputPrivate::OffsetParameterName, FLinearColor(ManualOffsetX, ManualOffsetY, ManualOffsetZ, 1.0f));
	DynamicTattooMaterial->SetVectorParameterValue(FName(TEXT("ProjectionDirection")), FLinearColor(0.0f, 1.0f, 0.0f, -1.0f));
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("DepthMin")), ManualDepthMin);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("DepthMax")), ManualDepthMax);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("Scale")), ManualScale);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("ScaleY")), ManualScaleY);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("Rotation")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("EmissionStrength")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("Metallic")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("Roughness")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("Specular")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("UseT2T")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("TintBaseColor")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("UseBaseColorForEmissive")), 1.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("UseTexCoord")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(DynamicTattooMaterial, FName(TEXT("WorldPositionOffset")), ManualWorldOffset);

	if (FObjectPropertyBase* MaterialProperty = FindFProperty<FObjectPropertyBase>(
		Pawn->GetClass(),
		ProjectTattooShopInputPrivate::NewDynamicMaterialPropertyName))
	{
		if (!MaterialProperty->PropertyClass || DynamicTattooMaterial->IsA(MaterialProperty->PropertyClass))
		{
			MaterialProperty->SetObjectPropertyValue_InContainer(Pawn, DynamicTattooMaterial);
		}
	}

	TArray<USkeletalMeshComponent*> TattooBaseComponents;
	CollectTattooBaseComponents(Pawn, TattooBaseComponents);
	TSet<USkeletalMeshComponent*> TattooBaseSet;
	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		if (Component)
		{
			TattooBaseSet.Add(Component);
		}
	}
	USkeletalMeshComponent* TargetSkin = ResolveVisibleSkinComponent(Pawn, TattooBaseSet);
	if (TattooBaseComponents.IsEmpty() && !TargetSkin)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("ApplyTattooTextureForAutomation failed: no tattoo base component or visible skin component was found on %s."), *GetNameSafe(Pawn));
		return false;
	}

	if (TattooBaseComponents.IsEmpty() && TargetSkin)
	{
		USkeletalMeshComponent* TattooBaseComponent = AutomationTattooBaseComponent.Get();
		if (!IsValid(TattooBaseComponent))
		{
			TattooBaseComponent = NewObject<USkeletalMeshComponent>(
				Pawn,
				USkeletalMeshComponent::StaticClass(),
				TEXT("ProjectTattooShopAutomationTattooBase"));
			if (TattooBaseComponent)
			{
				Pawn->AddInstanceComponent(TattooBaseComponent);
				TattooBaseComponent->RegisterComponent();
				AutomationTattooBaseComponent = TattooBaseComponent;
			}
		}

		if (TattooBaseComponent)
		{
			TattooBaseComponent->SetSkeletalMeshAsset(TargetSkin->GetSkeletalMeshAsset());
			TattooBaseComponent->AttachToComponent(TargetSkin, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			TattooBaseComponent->SetRelativeLocation(FVector::ZeroVector);
			TattooBaseComponent->SetRelativeRotation(FRotator::ZeroRotator);
			TattooBaseComponent->SetRelativeScale3D(FVector(1.003f));
			TattooBaseComponent->SetLeaderPoseComponent(TargetSkin);
			TattooBaseComponent->TranslucencySortPriority = 10;
			TattooBaseComponent->SetBoundsScale(1.1f);
			TattooBaseComponents.Add(TattooBaseComponent);
		}
	}

	int32 AppliedComponentCount = 0;
	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		if (!Component)
		{
			continue;
		}

		Component->SetOverlayMaterial(DynamicTattooMaterial);
		for (int32 MaterialIndex = 0; MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
		{
			Component->SetMaterial(MaterialIndex, DynamicTattooMaterial);
		}
		Component->SetHiddenInGame(false, true);
		Component->SetVisibility(true, true);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->MarkRenderStateDirty();
		++AppliedComponentCount;
	}

	SynchronizeTattooOverlayToVisibleSkin();
	UE_LOG(
		LogProjectTattooShopInput,
		Display,
		TEXT("ApplyTattooTextureForAutomation applied %s to %d tattoo base component(s), target skin=%s."),
		*TattooTexture->GetPathName(),
		AppliedComponentCount,
		*GetNameSafe(TargetSkin));
	return AppliedComponentCount > 0 || TargetSkin != nullptr;
}

bool UProjectTattooShopInputSubsystem::ApplyDefaultHeartTattooForAutomation()
{
	UTexture2D* HeartTexture = LoadObject<UTexture2D>(nullptr, ProjectTattooShopInputPrivate::HeartTexturePath);
	return ApplyTattooTextureForAutomation(HeartTexture);
}

FString UProjectTattooShopInputSubsystem::GetTattooOverlayReportForAutomation() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	APawn* Pawn = TrackedPlayerPawn.Get();
	Root->SetBoolField(TEXT("tattoo_shop_open"), bTattooShopOpen);
	Root->SetStringField(TEXT("tracked_pawn"), GetPathNameSafe(Pawn));

	TArray<USkeletalMeshComponent*> TattooBaseComponents;
	CollectTattooBaseComponents(Pawn, TattooBaseComponents);
	TSet<USkeletalMeshComponent*> TattooBaseSet;
	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		if (Component)
		{
			TattooBaseSet.Add(Component);
		}
	}

	USkeletalMeshComponent* TargetSkin = ResolveVisibleSkinComponent(Pawn, TattooBaseSet);
	UMaterialInterface* ActiveOverlay = ResolveActiveTattooOverlayMaterial(Pawn, TattooBaseComponents);
	Root->SetStringField(TEXT("target_skin"), GetPathNameSafe(TargetSkin));
	Root->SetStringField(TEXT("target_skin_overlay"), GetPathNameSafe(TargetSkin ? TargetSkin->GetOverlayMaterial() : nullptr));
	Root->SetStringField(TEXT("active_tattoo_overlay"), GetPathNameSafe(ActiveOverlay));

	UObject* PawnDynamicMaterial = nullptr;
	if (Pawn)
	{
		if (FObjectPropertyBase* MaterialProperty = FindFProperty<FObjectPropertyBase>(
			Pawn->GetClass(),
			ProjectTattooShopInputPrivate::NewDynamicMaterialPropertyName))
		{
			PawnDynamicMaterial = MaterialProperty->GetObjectPropertyValue_InContainer(Pawn);
		}
	}
	Root->SetStringField(TEXT("pawn_dynamic_material"), GetPathNameSafe(PawnDynamicMaterial));

	TArray<TSharedPtr<FJsonValue>> ComponentValues;
	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		TSharedRef<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
		ComponentObject->SetStringField(TEXT("name"), GetNameSafe(Component));
		ComponentObject->SetStringField(TEXT("path"), GetPathNameSafe(Component));
		ComponentObject->SetBoolField(TEXT("visible"), Component && Component->IsVisible());
		ComponentObject->SetBoolField(TEXT("hidden_in_game"), Component && Component->bHiddenInGame);
		ComponentObject->SetStringField(TEXT("overlay_material"), GetPathNameSafe(Component ? Component->GetOverlayMaterial() : nullptr));
		ComponentValues.Add(MakeShared<FJsonValueObject>(ComponentObject));
	}
	Root->SetArrayField(TEXT("tattoo_base_components"), ComponentValues);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

void UProjectTattooShopInputSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->IsNetMode(NM_DedicatedServer))
	{
		DetachFromTrackedPlayerController();
		return;
	}

	APlayerController* PlayerController =
		ProjectTattooShopInputPrivate::ResolveLocalPlayerController(World);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController();
		return;
	}

	AttachToPlayerController(PlayerController);
}

void UProjectTattooShopInputSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindInputToTrackedPlayerController();
		if (TrackedPlayerPawn != PlayerController->GetPawn())
		{
			HandleTrackedPawnChanged(TrackedPlayerPawn, PlayerController->GetPawn());
		}
		return;
	}

	DetachFromTrackedPlayerController();

	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	BindInputToTrackedPlayerController();
}

void UProjectTattooShopInputSubsystem::DetachFromTrackedPlayerController()
{
	if (bTattooShopOpen)
	{
		CloseTattooShop();
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	UnbindInputFromTrackedPlayerController();
	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedTattooShopWidget = nullptr;
	TrackedAssetPreviewWidget = nullptr;
	TattooShopHostPanel = nullptr;
	TattooAssetPreviewHostPanel = nullptr;
	RuntimeTattooCustomizationWidget = nullptr;
	RuntimeTattooMID = nullptr;
	LastRuntimeTattooTexture = nullptr;
	RuntimeTattooBaseComponent = nullptr;
	ClearMirroredTattooOverlay();
	InputSnapshot = FProjectTattooShopInputSnapshot();
	bTattooShopOpen = false;
	bTattooShopHostedInPanel = false;
}

void UProjectTattooShopInputSubsystem::BindInputToTrackedPlayerController()
{
	// TattooShop is opened from Character Creation; standalone key binding stays disabled.
}

void UProjectTattooShopInputSubsystem::UnbindInputFromTrackedPlayerController()
{
	if (!TrackedInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(TrackedInputComponent);
	}

	if (TrackedInputComponent->IsRegistered())
	{
		TrackedInputComponent->DestroyComponent();
	}

	TrackedInputComponent = nullptr;
}

void UProjectTattooShopInputSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (bTattooShopOpen)
	{
		CloseTattooShop();
	}

	TrackedPlayerPawn = NewPawn;
	TrackedTattooShopWidget = nullptr;
	TrackedAssetPreviewWidget = nullptr;
	RuntimeTattooCustomizationWidget = nullptr;
	RuntimeTattooMID = nullptr;
	LastRuntimeTattooTexture = nullptr;
	RuntimeTattooBaseComponent = nullptr;
	ClearMirroredTattooOverlay();
}

void UProjectTattooShopInputSubsystem::HandleTogglePressed()
{
	if (bTattooShopOpen)
	{
		CloseTattooShop();
		return;
	}

	OpenTattooShop();
}

bool UProjectTattooShopInputSubsystem::CanUseTattooShopPawn(APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}

	for (const UClass* PawnClass = Pawn->GetClass(); PawnClass; PawnClass = PawnClass->GetSuperClass())
	{
		if (PawnClass->GetPathName().Contains(ProjectTattooShopInputPrivate::TattooShopCharacterPathToken))
		{
			return true;
		}
	}

	// The UE 5.8 player is composition-based and can use either the authoritative
	// Female or Male body in any gameplay map. Resolve the visible body instead
	// of depending on HUB, a legacy TattooShop pawn class, or one asset path.
	const TSet<USkeletalMeshComponent*> NoTattooBaseComponents;
	return ResolveVisibleSkinComponent(Pawn, NoTattooBaseComponents) != nullptr;
}

UUserWidget* UProjectTattooShopInputSubsystem::EnsureTattooShopWidget(APlayerController* PlayerController, APawn* Pawn)
{
	if (!PlayerController || !Pawn)
	{
		return nullptr;
	}

	if (TrackedTattooShopWidget && IsValid(TrackedTattooShopWidget))
	{
		return TrackedTattooShopWidget;
	}

	if (UUserWidget* ExistingWidget = GetTattooShopWidgetFromPawn(Pawn))
	{
		TrackedTattooShopWidget = ExistingWidget;
		return ExistingWidget;
	}

	if (UFunction* InitializeFunction = Pawn->FindFunction(ProjectTattooShopInputPrivate::InitializeEventName))
	{
		Pawn->ProcessEvent(InitializeFunction, nullptr);
		if (UUserWidget* InitializedWidget = GetTattooShopWidgetFromPawn(Pawn))
		{
			TrackedTattooShopWidget = InitializedWidget;
			return InitializedWidget;
		}
	}

	const TSubclassOf<UUserWidget> WidgetClass = ResolveTattooShopWidgetClass();
	if (!WidgetClass)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("[TattooShop] WBP_TattooShop class could not be loaded."));
		return nullptr;
	}

	UUserWidget* NewWidget = CreateWidget<UUserWidget>(PlayerController, WidgetClass, TEXT("ProjectTattooShopWidget"));
	if (!NewWidget)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("[TattooShop] Failed to create WBP_TattooShop."));
		return nullptr;
	}

	SetTattooShopActorReference(NewWidget, Pawn);
	SetTattooShopWidgetOnPawn(Pawn, NewWidget);
	TrackedTattooShopWidget = NewWidget;
	return NewWidget;
}

UUserWidget* UProjectTattooShopInputSubsystem::GetTattooShopWidgetFromPawn(APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}

	FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(
		Pawn->GetClass(),
		ProjectTattooShopInputPrivate::WidgetPropertyName);
	if (!WidgetProperty)
	{
		return nullptr;
	}

	return Cast<UUserWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(Pawn));
}

void UProjectTattooShopInputSubsystem::SetTattooShopWidgetOnPawn(APawn* Pawn, UUserWidget* Widget) const
{
	if (!Pawn)
	{
		return;
	}

	FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(
		Pawn->GetClass(),
		ProjectTattooShopInputPrivate::WidgetPropertyName);
	if (WidgetProperty)
	{
		WidgetProperty->SetObjectPropertyValue_InContainer(Pawn, Widget);
	}
}

void UProjectTattooShopInputSubsystem::SetTattooShopActorReference(UUserWidget* Widget, APawn* Pawn) const
{
	if (!Widget || !Pawn)
	{
		return;
	}

	FObjectPropertyBase* ActorRefProperty = FindFProperty<FObjectPropertyBase>(
		Widget->GetClass(),
		ProjectTattooShopInputPrivate::ActorRefPropertyName);
	if (ActorRefProperty)
	{
		ActorRefProperty->SetObjectPropertyValue_InContainer(Widget, Pawn);
	}
}

TSubclassOf<UUserWidget> UProjectTattooShopInputSubsystem::ResolveTattooShopWidgetClass()
{
	if (TattooShopWidgetClass)
	{
		return TattooShopWidgetClass;
	}

	TattooShopWidgetClass = Cast<UClass>(FSoftObjectPath(ProjectTattooShopInputPrivate::TattooShopWidgetPath).TryLoad());
	return TattooShopWidgetClass;
}

TSubclassOf<UUserWidget> UProjectTattooShopInputSubsystem::ResolveAssetPreviewWidgetClass()
{
	if (TattooAssetPreviewWidgetClass)
	{
		return TattooAssetPreviewWidgetClass;
	}

	TattooAssetPreviewWidgetClass = Cast<UClass>(FSoftObjectPath(ProjectTattooShopInputPrivate::TattooAssetPreviewWidgetPath).TryLoad());
	return TattooAssetPreviewWidgetClass;
}

void UProjectTattooShopInputSubsystem::MountWidgetInHostedPanel(UUserWidget* Widget, UPanelWidget* HostPanel, bool bClearHost) const
{
	if (!IsValid(Widget) || !IsValid(HostPanel))
	{
		return;
	}

	if (Widget->GetParent() == HostPanel)
	{
		return;
	}

	Widget->RemoveFromParent();
	if (bClearHost)
	{
		HostPanel->ClearChildren();
	}

	UPanelSlot* AddedSlot = HostPanel->AddChild(Widget);
	if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(AddedSlot))
	{
		VerticalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
		VerticalSlot->SetVerticalAlignment(VAlign_Fill);
	}
	else if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(AddedSlot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		CanvasSlot->SetOffsets(FMargin(0.0f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	}
}

void UProjectTattooShopInputSubsystem::CaptureAssetPreviewWidget()
{
	if (!bTattooShopOpen || !bTattooShopHostedInPanel || !IsValid(TattooAssetPreviewHostPanel))
	{
		return;
	}

	UUserWidget* PreviewWidget = TrackedAssetPreviewWidget.Get();
	if (!IsValid(PreviewWidget))
	{
		PreviewWidget = nullptr;

		if (UUserWidget* TattooShopWidget = TrackedTattooShopWidget.Get())
		{
			FObjectPropertyBase* AssetPreviewerProperty = FindFProperty<FObjectPropertyBase>(
				TattooShopWidget->GetClass(),
				ProjectTattooShopInputPrivate::AssetPreviewerPropertyName);
			if (AssetPreviewerProperty)
			{
				PreviewWidget = Cast<UUserWidget>(AssetPreviewerProperty->GetObjectPropertyValue_InContainer(TattooShopWidget));
			}
		}

		const TSubclassOf<UUserWidget> PreviewWidgetClass = ResolveAssetPreviewWidgetClass();
		if (!IsValid(PreviewWidget) && !PreviewWidgetClass)
		{
			return;
		}

		if (!IsValid(PreviewWidget))
		{
			TArray<UUserWidget*> FoundWidgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, FoundWidgets, PreviewWidgetClass, false);
			for (UUserWidget* Candidate : FoundWidgets)
			{
				if (IsValid(Candidate) && Candidate != TrackedTattooShopWidget)
				{
					PreviewWidget = Candidate;
					break;
				}
			}
		}
	}

	if (!IsValid(PreviewWidget))
	{
		return;
	}

	if (PreviewWidget == TrackedAssetPreviewWidget.Get() && PreviewWidget->GetParent() == TattooAssetPreviewHostPanel.Get())
	{
		return;
	}

	TrackedAssetPreviewWidget = PreviewWidget;
	MountWidgetInHostedPanel(PreviewWidget, TattooAssetPreviewHostPanel, true);
	NormalizeTattooCardGrid(PreviewWidget);
	UE_LOG(
		LogProjectTattooShopInput,
		Log,
		TEXT("[TattooShop] Captured original WBP_AssetPreviewer instance %s. Parent=%s Visibility=%d."),
		*GetNameSafe(PreviewWidget),
		*GetNameSafe(PreviewWidget->GetParent()),
		static_cast<int32>(PreviewWidget->GetVisibility()));
}

UUserWidget* UProjectTattooShopInputSubsystem::FindLiveTattooCustomizationWidget() const
{
	UClass* CustomizationClass = Cast<UClass>(FSoftObjectPath(ProjectTattooShopInputPrivate::TattooCustomizationWidgetPath).TryLoad());
	if (!CustomizationClass)
	{
		return nullptr;
	}

	// The mounted preview is a nested UserWidget, so querying viewport widgets
	// alone can return the dormant C_0 template. Resolve the real C_1 child
	// directly from the captured preview's WidgetTree first.
	if (UUserWidget* LivePreview = TrackedAssetPreviewWidget.Get())
	{
		UUserWidget* LiveCustomization = nullptr;
		if (LivePreview->WidgetTree)
		{
			LivePreview->WidgetTree->ForEachWidget([&LiveCustomization, CustomizationClass](UWidget* ChildWidget)
			{
				if (!LiveCustomization && IsValid(ChildWidget) && ChildWidget->IsA(CustomizationClass))
				{
					LiveCustomization = Cast<UUserWidget>(ChildWidget);
				}
			});
		}
		if (LiveCustomization)
		{
			return LiveCustomization;
		}
	}

	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, FoundWidgets, CustomizationClass, false);
	UUserWidget* BestWidget = nullptr;
	for (UUserWidget* Candidate : FoundWidgets)
	{
		if (!IsValid(Candidate) || Candidate->GetWorld() != GetWorld())
		{
			continue;
		}

		// The TattooShop contains more than one preview/customization template.
		// Always bind the instance mounted in the captured live AssetPreviewer;
		// self visibility alone does not account for a hidden ancestor widget.
		if (Candidate->GetTypedOuter<UUserWidget>() == TrackedAssetPreviewWidget.Get())
		{
			return Candidate;
		}

		BestWidget = Candidate;
		if (Candidate->GetVisibility() != ESlateVisibility::Collapsed
			&& Candidate->GetVisibility() != ESlateVisibility::Hidden)
		{
			break;
		}
	}

	return BestWidget;
}

void UProjectTattooShopInputSubsystem::AddTattooBaseComponentToPawnArray(
	APawn* Pawn,
	USkeletalMeshComponent* Component) const
{
	if (!Pawn || !Component)
	{
		return;
	}

	FArrayProperty* ArrayProperty = ProjectTattooShopInputPrivate::FindArrayPropertyByCompatibleName(
		Pawn->GetClass(),
		ProjectTattooShopInputPrivate::TattooBaseComponentsPropertyName);
	FObjectPropertyBase* InnerObjectProperty = ArrayProperty
		? CastField<FObjectPropertyBase>(ArrayProperty->Inner)
		: nullptr;
	if (!ArrayProperty || !InnerObjectProperty
		|| (InnerObjectProperty->PropertyClass && !Component->IsA(InnerObjectProperty->PropertyClass)))
	{
		return;
	}

	void* ArrayAddress = ArrayProperty->ContainerPtrToValuePtr<void>(Pawn);
	FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayAddress);
	for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
	{
		if (InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index)) == Component)
		{
			return;
		}
	}

	const int32 NewIndex = ArrayHelper.AddValue();
	InnerObjectProperty->SetObjectPropertyValue(ArrayHelper.GetRawPtr(NewIndex), Component);
}

USkeletalMeshComponent* UProjectTattooShopInputSubsystem::EnsureRuntimeTattooBaseComponent(
	APawn* Pawn,
	UMaterialInstanceDynamic* DynamicMaterial,
	bool& bOutCreatedComponent)
{
	bOutCreatedComponent = false;
	if (!Pawn || !DynamicMaterial)
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> TattooBaseComponents;
	CollectTattooBaseComponents(Pawn, TattooBaseComponents);
	USkeletalMeshComponent* TattooBaseComponent = RuntimeTattooBaseComponent.Get();
	if (!IsValid(TattooBaseComponent) && !TattooBaseComponents.IsEmpty())
	{
		TattooBaseComponent = TattooBaseComponents.Last();
	}

	TSet<USkeletalMeshComponent*> TattooBaseSet;
	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		if (IsValid(Component))
		{
			TattooBaseSet.Add(Component);
		}
	}
	USkeletalMeshComponent* TargetSkin = ResolveVisibleSkinComponent(Pawn, TattooBaseSet);
	if (!IsValid(TattooBaseComponent) && TargetSkin)
	{
		TattooBaseComponent = NewObject<USkeletalMeshComponent>(
			Pawn,
			USkeletalMeshComponent::StaticClass(),
			TEXT("ProjectTattooShopRuntimeTattooBase"));
		if (TattooBaseComponent)
		{
			Pawn->AddInstanceComponent(TattooBaseComponent);
			TattooBaseComponent->RegisterComponent();
			RuntimeTattooBaseComponent = TattooBaseComponent;
			AddTattooBaseComponentToPawnArray(Pawn, TattooBaseComponent);
			bOutCreatedComponent = true;
		}
	}

	if (!TattooBaseComponent)
	{
		return nullptr;
	}

	if (TargetSkin)
	{
		if (!TattooBaseComponent->GetSkeletalMeshAsset())
		{
			TattooBaseComponent->SetSkeletalMeshAsset(TargetSkin->GetSkeletalMeshAsset());
		}
		TattooBaseComponent->AttachToComponent(TargetSkin, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		TattooBaseComponent->SetRelativeTransform(FTransform::Identity);
		TattooBaseComponent->SetRelativeScale3D(FVector(1.003f));
		const USkeletalMesh* TattooMesh = TattooBaseComponent->GetSkeletalMeshAsset();
		const USkeletalMesh* SkinMesh = TargetSkin->GetSkeletalMeshAsset();
		if (TattooMesh && SkinMesh && TattooMesh->GetSkeleton() == SkinMesh->GetSkeleton())
		{
			TattooBaseComponent->SetLeaderPoseComponent(TargetSkin);
		}
	}

	TattooBaseComponent->SetOverlayMaterial(DynamicMaterial);
	for (int32 MaterialIndex = 0; MaterialIndex < TattooBaseComponent->GetNumMaterials(); ++MaterialIndex)
	{
		TattooBaseComponent->SetMaterial(MaterialIndex, DynamicMaterial);
	}
	TattooBaseComponent->SetHiddenInGame(false, true);
	TattooBaseComponent->SetVisibility(true, true);
	TattooBaseComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TattooBaseComponent->SetGenerateOverlapEvents(false);
	TattooBaseComponent->SetCastShadow(false);
	TattooBaseComponent->TranslucencySortPriority = 10;
	TattooBaseComponent->SetBoundsScale(1.1f);
	TattooBaseComponent->MarkRenderStateDirty();
	return TattooBaseComponent;
}

void UProjectTattooShopInputSubsystem::RepairTattooCustomizationRuntime()
{
	APawn* Pawn = TrackedPlayerPawn.Get();
	if (!CanUseTattooShopPawn(Pawn))
	{
		return;
	}

	UUserWidget* CustomizationWidget = FindLiveTattooCustomizationWidget();
	if (!CustomizationWidget)
	{
		return;
	}

	if (RuntimeTattooCustomizationWidget.Get() != CustomizationWidget)
	{
		RuntimeTattooCustomizationWidget = CustomizationWidget;
		RuntimeTattooMID = nullptr;
		LastRuntimeTattooTexture = nullptr;
	}

	FObjectPropertyBase* SelectedMIDProperty = ProjectTattooShopInputPrivate::FindObjectPropertyByCompatibleName(
		CustomizationWidget->GetClass(),
		ProjectTattooShopInputPrivate::SelectedMIDPropertyName);
	if (!SelectedMIDProperty)
	{
		return;
	}

	UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(
		SelectedMIDProperty->GetObjectPropertyValue_InContainer(CustomizationWidget));
	bool bCreatedMID = false;
	if (!DynamicMaterial)
	{
		UMaterialInterface* MasterMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			ProjectTattooShopInputPrivate::TattooShopMaterialPath);
		DynamicMaterial = MasterMaterial ? UMaterialInstanceDynamic::Create(MasterMaterial, Pawn) : nullptr;
		if (!DynamicMaterial)
		{
			return;
		}
		SelectedMIDProperty->SetObjectPropertyValue_InContainer(CustomizationWidget, DynamicMaterial);
		bCreatedMID = true;
	}
	RuntimeTattooMID = DynamicMaterial;

	ProjectTattooShopInputPrivate::SetObjectPropertyWhenCompatible(
		CustomizationWidget, ProjectTattooShopInputPrivate::TargetMIDPropertyName, DynamicMaterial, true);
	ProjectTattooShopInputPrivate::SetObjectPropertyWhenCompatible(
		CustomizationWidget, ProjectTattooShopInputPrivate::PreEditMIDPropertyName, DynamicMaterial, true);
	ProjectTattooShopInputPrivate::SetObjectPropertyWhenCompatible(
		Pawn, ProjectTattooShopInputPrivate::NewDynamicMaterialPropertyName, DynamicMaterial, false);

	UTexture* SelectedTexture = nullptr;
	if (FObjectPropertyBase* TextureProperty = ProjectTattooShopInputPrivate::FindObjectPropertyByCompatibleName(
		CustomizationWidget->GetClass(),
		ProjectTattooShopInputPrivate::SelectedAssetTexturePropertyName))
	{
		SelectedTexture = Cast<UTexture>(TextureProperty->GetObjectPropertyValue_InContainer(CustomizationWidget));
	}
	const bool bTextureChanged = SelectedTexture && LastRuntimeTattooTexture.Get() != SelectedTexture;
	if (bTextureChanged || (bCreatedMID && SelectedTexture))
	{
		DynamicMaterial->SetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName, SelectedTexture);
		LastRuntimeTattooTexture = SelectedTexture;
	}

	bool bCreatedComponent = false;
	USkeletalMeshComponent* TattooBaseComponent = EnsureRuntimeTattooBaseComponent(
		Pawn,
		DynamicMaterial,
		bCreatedComponent);
	if (TattooBaseComponent)
	{
		const bool bHasSelectedTexture = SelectedTexture != nullptr;
		TArray<USkeletalMeshComponent*> AllTattooBases;
		CollectTattooBaseComponents(Pawn, AllTattooBases);
		AllTattooBases.AddUnique(TattooBaseComponent);
		for (USkeletalMeshComponent* Component : AllTattooBases)
		{
			if (!IsValid(Component))
			{
				continue;
			}

			if (bHasSelectedTexture)
			{
				Component->SetOverlayMaterial(DynamicMaterial);
				for (int32 MaterialIndex = 0; MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
				{
					Component->SetMaterial(MaterialIndex, DynamicMaterial);
				}
			}
			Component->SetHiddenInGame(!bHasSelectedTexture, true);
			Component->SetVisibility(bHasSelectedTexture, true);
			Component->MarkRenderStateDirty();
		}
	}
	if (bCreatedMID || bCreatedComponent || bTextureChanged)
	{
		UE_LOG(
			LogProjectTattooShopInput,
			Display,
			TEXT("[TattooShop][RuntimeMID] Repaired=%s BaseCreated=%s Widget=%s MID=%s Base=%s Texture=%s Applied=%s"),
			bCreatedMID ? TEXT("true") : TEXT("false"),
			bCreatedComponent ? TEXT("true") : TEXT("false"),
			*CustomizationWidget->GetPathName(),
			*GetNameSafe(DynamicMaterial),
			*GetNameSafe(TattooBaseComponent),
			*GetPathNameSafe(SelectedTexture),
			bTextureChanged ? TEXT("true") : TEXT("false"));
	}
}

void UProjectTattooShopInputSubsystem::CollectTattooCardPanels(UUserWidget* Widget, TArray<UPanelWidget*>& OutPanels) const
{
	if (!IsValid(Widget))
	{
		return;
	}

	auto AddPanelIfItContainsCards = [this, &OutPanels](UPanelWidget* Panel)
	{
		if (IsValid(Panel) && CountTattooCardsInPanel(Panel) > 0)
		{
			OutPanels.AddUnique(Panel);
		}
	};

	if (FObjectPropertyBase* PreviewGridProperty = FindFProperty<FObjectPropertyBase>(
		Widget->GetClass(),
		ProjectTattooShopInputPrivate::PreviewGridPropertyName))
	{
		AddPanelIfItContainsCards(Cast<UPanelWidget>(PreviewGridProperty->GetObjectPropertyValue_InContainer(Widget)));
	}

	if (UWidgetTree* WidgetTree = Widget->WidgetTree)
	{
		WidgetTree->ForEachWidget([&AddPanelIfItContainsCards](UWidget* CandidateWidget)
		{
			AddPanelIfItContainsCards(Cast<UPanelWidget>(CandidateWidget));
		});
	}
}

int32 UProjectTattooShopInputSubsystem::CountTattooCardsInPanel(UPanelWidget* Panel) const
{
	if (!IsValid(Panel))
	{
		return 0;
	}

	int32 CardCount = 0;
	for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
	{
		if (!ResolveTattooCardIdentity(Panel->GetChildAt(ChildIndex)).IsEmpty())
		{
			++CardCount;
		}
	}

	return CardCount;
}

void UProjectTattooShopInputSubsystem::NormalizeTattooCardPanel(UPanelWidget* Panel, const int32 ColumnLimit) const
{
	if (!IsValid(Panel))
	{
		return;
	}

	TSet<FString> SeenTattooIds;
	TArray<UWidget*> KeptCards;
	TArray<UWidget*> DuplicateCards;
	for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
	{
		UWidget* Child = Panel->GetChildAt(ChildIndex);
		if (!IsValid(Child))
		{
			continue;
		}

		const FString TattooId = ResolveTattooCardIdentity(Child).ToLower();
		if (TattooId.IsEmpty())
		{
			continue;
		}

		if (SeenTattooIds.Contains(TattooId))
		{
			DuplicateCards.Add(Child);
			continue;
		}

		SeenTattooIds.Add(TattooId);
		KeptCards.Add(Child);
	}

	if (DuplicateCards.IsEmpty())
	{
		return;
	}

	for (UWidget* DuplicateCard : DuplicateCards)
	{
		if (IsValid(DuplicateCard))
		{
			Panel->RemoveChild(DuplicateCard);
		}
	}

	int32 VisibleCardIndex = 0;
	for (UWidget* KeptCard : KeptCards)
	{
		if (!IsValid(KeptCard) || KeptCard->GetParent() != Panel)
		{
			continue;
		}

		if (UGridSlot* GridSlot = Cast<UGridSlot>(KeptCard->Slot))
		{
			GridSlot->SetColumn(VisibleCardIndex % ColumnLimit);
			GridSlot->SetRow(VisibleCardIndex / ColumnLimit);
		}
		++VisibleCardIndex;
	}

	UE_LOG(
		LogProjectTattooShopInput,
		Log,
		TEXT("[TattooShop] Removed %d duplicate tattoo card(s) from %s. UniqueCards=%d."),
		DuplicateCards.Num(),
		*GetNameSafe(Panel),
		VisibleCardIndex);
}

void UProjectTattooShopInputSubsystem::NormalizeTattooCardGrid(UUserWidget* Widget) const
{
	if (!IsValid(Widget))
	{
		return;
	}

	int32 ColumnLimit = 4;
	if (FIntProperty* ColumnLimitProperty = FindFProperty<FIntProperty>(
		Widget->GetClass(),
		ProjectTattooShopInputPrivate::GridColumnLimitPropertyName))
	{
		ColumnLimit = FMath::Max(1, ColumnLimitProperty->GetPropertyValue_InContainer(Widget));
	}

	TArray<UPanelWidget*> TattooCardPanels;
	CollectTattooCardPanels(Widget, TattooCardPanels);
	for (UPanelWidget* TattooCardPanel : TattooCardPanels)
	{
		NormalizeTattooCardPanel(TattooCardPanel, ColumnLimit);
	}
}

FString UProjectTattooShopInputSubsystem::ResolveTattooCardIdentity(UWidget* Widget) const
{
	if (!IsValid(Widget))
	{
		return FString();
	}

	FString BrushIdentity;
	FString LabelIdentity;
	bool bContainsTattooCard = false;
	TSet<const UWidget*> VisitedWidgets;
	ProjectTattooShopInputPrivate::CollectTattooCardVisualIdentity(
		Widget,
		VisitedWidgets,
		bContainsTattooCard,
		LabelIdentity,
		BrushIdentity);

	if (!LabelIdentity.IsEmpty())
	{
		return LabelIdentity;
	}

	if (!BrushIdentity.IsEmpty())
	{
		return BrushIdentity;
	}

	if (!bContainsTattooCard)
	{
		return FString();
	}

	if (FObjectPropertyBase* TextureProperty = FindFProperty<FObjectPropertyBase>(
		Widget->GetClass(),
		ProjectTattooShopInputPrivate::TattooCardTexturePropertyName))
	{
		if (UObject* TextureObject = TextureProperty->GetObjectPropertyValue_InContainer(Widget))
		{
			return TextureObject->GetPathName();
		}
	}

	auto TryStringProperty = [Widget](const FName PropertyName) -> FString
	{
		if (FStrProperty* StringProperty = FindFProperty<FStrProperty>(Widget->GetClass(), PropertyName))
		{
			return StringProperty->GetPropertyValue_InContainer(Widget);
		}
		if (FNameProperty* NameProperty = FindFProperty<FNameProperty>(Widget->GetClass(), PropertyName))
		{
			return NameProperty->GetPropertyValue_InContainer(Widget).ToString();
		}
		if (FTextProperty* TextProperty = FindFProperty<FTextProperty>(Widget->GetClass(), PropertyName))
		{
			return TextProperty->GetPropertyValue_InContainer(Widget).ToString();
		}
		return FString();
	};

	FString NameIdentity = TryStringProperty(ProjectTattooShopInputPrivate::TattooCardNamePropertyName);
	if (NameIdentity.IsEmpty())
	{
		NameIdentity = TryStringProperty(ProjectTattooShopInputPrivate::TattooCardAlternateNamePropertyName);
	}
	if (!NameIdentity.IsEmpty())
	{
		return NameIdentity.TrimStartAndEnd();
	}

	return FString();
}

FString UProjectTattooShopInputSubsystem::BuildTattooCardGridReport(UUserWidget* PreferredWidget, UUserWidget* FallbackWidget) const
{
	auto ChooseBestPanel = [this](UUserWidget* Widget) -> UPanelWidget*
	{
		if (!IsValid(Widget))
		{
			return nullptr;
		}

		TArray<UPanelWidget*> CandidatePanels;
		CollectTattooCardPanels(Widget, CandidatePanels);

		UPanelWidget* BestPanel = nullptr;
		int32 BestCardCount = 0;
		for (UPanelWidget* CandidatePanel : CandidatePanels)
		{
			const int32 CandidateCardCount = CountTattooCardsInPanel(CandidatePanel);
			if (CandidateCardCount > BestCardCount)
			{
				BestPanel = CandidatePanel;
				BestCardCount = CandidateCardCount;
			}
		}

		return BestPanel;
	};

	UUserWidget* TargetWidget = PreferredWidget;
	UPanelWidget* PreviewPanel = ChooseBestPanel(TargetWidget);
	if (!PreviewPanel)
	{
		TargetWidget = FallbackWidget;
		PreviewPanel = ChooseBestPanel(TargetWidget);
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetBoolField(TEXT("asset_previewer_tracked"), IsValid(PreferredWidget));
	RootObject->SetStringField(TEXT("asset_previewer_class"), IsValid(PreferredWidget) ? PreferredWidget->GetClass()->GetName() : FString());
	RootObject->SetStringField(TEXT("asset_previewer_path"), IsValid(PreferredWidget) ? PreferredWidget->GetPathName() : FString());
	RootObject->SetBoolField(TEXT("tattoo_shop_tracked"), IsValid(FallbackWidget));
	RootObject->SetStringField(TEXT("tattoo_shop_class"), IsValid(FallbackWidget) ? FallbackWidget->GetClass()->GetName() : FString());
	RootObject->SetStringField(TEXT("tattoo_shop_path"), IsValid(FallbackWidget) ? FallbackWidget->GetPathName() : FString());
	RootObject->SetStringField(TEXT("target_widget_class"), IsValid(TargetWidget) ? TargetWidget->GetClass()->GetName() : FString());
	RootObject->SetStringField(TEXT("target_widget_path"), IsValid(TargetWidget) ? TargetWidget->GetPathName() : FString());
	RootObject->SetStringField(TEXT("target_panel_class"), IsValid(PreviewPanel) ? PreviewPanel->GetClass()->GetName() : FString());
	RootObject->SetStringField(TEXT("target_panel_name"), GetNameSafe(PreviewPanel));
	RootObject->SetBoolField(TEXT("preview_grid_present"), IsValid(PreviewPanel));

	TArray<TSharedPtr<FJsonValue>> CardValues;
	TMap<FString, int32> CardCounts;
	if (PreviewPanel)
	{
		for (int32 ChildIndex = 0; ChildIndex < PreviewPanel->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* Child = PreviewPanel->GetChildAt(ChildIndex);
			const FString TattooId = ResolveTattooCardIdentity(Child);
			if (TattooId.IsEmpty())
			{
				continue;
			}

			const FString NormalizedTattooId = TattooId.ToLower();
			CardCounts.FindOrAdd(NormalizedTattooId)++;

			TSharedRef<FJsonObject> CardObject = MakeShared<FJsonObject>();
			CardObject->SetNumberField(TEXT("index"), ChildIndex);
			CardObject->SetStringField(TEXT("id"), NormalizedTattooId);
			CardObject->SetStringField(TEXT("label_or_texture"), TattooId);
			CardObject->SetStringField(TEXT("class"), IsValid(Child) ? Child->GetClass()->GetName() : FString());
			CardValues.Add(MakeShared<FJsonValueObject>(CardObject));
		}
	}

	TSharedRef<FJsonObject> DuplicateObject = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& CardCountPair : CardCounts)
	{
		if (CardCountPair.Value > 1)
		{
			DuplicateObject->SetNumberField(CardCountPair.Key, CardCountPair.Value);
		}
	}

	RootObject->SetNumberField(TEXT("card_count"), CardValues.Num());
	RootObject->SetNumberField(TEXT("unique_count"), CardCounts.Num());
	RootObject->SetObjectField(TEXT("duplicates"), DuplicateObject);
	RootObject->SetArrayField(TEXT("cards"), CardValues);

	FString ReportString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportString);
	FJsonSerializer::Serialize(RootObject, Writer);
	return ReportString;
}

void UProjectTattooShopInputSubsystem::OpenTattooShop()
{
	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (!PlayerController)
	{
		if (UWorld* World = GetWorld())
		{
			PlayerController =
				ProjectTattooShopInputPrivate::ResolveLocalPlayerController(World);
			if (PlayerController && PlayerController->IsLocalController())
			{
				AttachToPlayerController(PlayerController);
				PlayerController = TrackedPlayerController.Get();
			}
		}
	}

	APawn* Pawn = TrackedPlayerPawn.Get();
	if (!Pawn && PlayerController)
	{
		Pawn = PlayerController->GetPawn();
		TrackedPlayerPawn = Pawn;
	}

	if (!PlayerController || !CanUseTattooShopPawn(Pawn))
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("[TattooShop] Open blocked. PlayerController=%s Pawn=%s"),
			*GetNameSafe(PlayerController),
			*GetNameSafe(Pawn));
		return;
	}

	UUserWidget* Widget = EnsureTattooShopWidget(PlayerController, Pawn);
	if (!Widget)
	{
		return;
	}

	Widget->RemoveFromParent();
	UPanelWidget* HostPanel = TattooShopHostPanel.Get();
	if (IsValid(HostPanel))
	{
		MountWidgetInHostedPanel(Widget, HostPanel, true);
		if (UPanelWidget* PreviewHostPanel = TattooAssetPreviewHostPanel.Get())
		{
			PreviewHostPanel->ClearChildren();
		}
		TrackedAssetPreviewWidget = nullptr;
		bTattooShopHostedInPanel = true;
	}
	else
	{
		Widget->AddToViewport(ProjectTattooShopInputPrivate::WidgetZOrder);
		bTattooShopHostedInPanel = false;
	}

	Widget->SetVisibility(ESlateVisibility::Visible);
	NormalizeTattooCardGrid(Widget);
	if (!bTattooShopHostedInPanel)
	{
		ApplyTattooShopInputMode(Widget);
	}

	bTattooShopOpen = true;
	UE_LOG(LogProjectTattooShopInput, Log, TEXT("[TattooShop] Opened for pawn %s with widget %s."), *GetNameSafe(Pawn), *GetNameSafe(Widget));
}

void UProjectTattooShopInputSubsystem::CloseTattooShop()
{
	UUserWidget* Widget = TrackedTattooShopWidget.Get();
	if (!Widget && TrackedPlayerPawn)
	{
		Widget = GetTattooShopWidgetFromPawn(TrackedPlayerPawn);
	}

	if (Widget)
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
		if (bTattooShopHostedInPanel)
		{
			Widget->RemoveFromParent();
		}
	}

	if (TrackedAssetPreviewWidget)
	{
		TrackedAssetPreviewWidget->RemoveFromParent();
		TrackedAssetPreviewWidget = nullptr;
	}

	if (TattooAssetPreviewHostPanel)
	{
		TattooAssetPreviewHostPanel->ClearChildren();
	}

	if (!bTattooShopHostedInPanel)
	{
		RestoreTattooShopInputMode();
	}

	bTattooShopOpen = false;
	bTattooShopHostedInPanel = false;
	UE_LOG(LogProjectTattooShopInput, Log, TEXT("[TattooShop] Closed."));
}

void UProjectTattooShopInputSubsystem::ApplyTattooShopInputMode(UUserWidget* Widget)
{
	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (!PlayerController)
	{
		return;
	}

	InputSnapshot = FProjectTattooShopInputSnapshot();
	InputSnapshot.bHasControllerState = true;
	InputSnapshot.bWasMouseCursorVisible = PlayerController->bShowMouseCursor;
	InputSnapshot.bWereClickEventsEnabled = PlayerController->bEnableClickEvents;
	InputSnapshot.bWereMouseOverEventsEnabled = PlayerController->bEnableMouseOverEvents;

	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(Widget ? Widget->TakeWidget() : TSharedPtr<SWidget>());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
}

void UProjectTattooShopInputSubsystem::RestoreTattooShopInputMode()
{
	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (!PlayerController || !InputSnapshot.bHasControllerState)
	{
		return;
	}

	PlayerController->bShowMouseCursor = InputSnapshot.bWasMouseCursorVisible;
	PlayerController->bEnableClickEvents = InputSnapshot.bWereClickEventsEnabled;
	PlayerController->bEnableMouseOverEvents = InputSnapshot.bWereMouseOverEventsEnabled;
	PlayerController->SetInputMode(FInputModeGameOnly());
	PlayerController->FlushPressedKeys();

	InputSnapshot = FProjectTattooShopInputSnapshot();
}

void UProjectTattooShopInputSubsystem::SynchronizeTattooOverlayToVisibleSkin()
{
	APawn* Pawn = TrackedPlayerPawn.Get();
	if (!CanUseTattooShopPawn(Pawn))
	{
		ClearMirroredTattooOverlay();
		return;
	}

	TArray<USkeletalMeshComponent*> TattooBaseComponents;
	CollectTattooBaseComponents(Pawn, TattooBaseComponents);
	TSet<USkeletalMeshComponent*> TattooBaseSet;
	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		if (Component)
		{
			TattooBaseSet.Add(Component);
		}
	}

	USkeletalMeshComponent* TargetSkin = ResolveVisibleSkinComponent(Pawn, TattooBaseSet);
	UMaterialInterface* ActiveTattooMaterial = ResolveActiveTattooOverlayMaterial(Pawn, TattooBaseComponents);
	bool bAutomaticSkinnedTattooActive = false;
	if (UWorld* World = GetWorld())
	{
		if (const UProjectDefaultTattooSkinnedDecalSubsystem* SkinnedDecalSubsystem = World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>())
		{
			bAutomaticSkinnedTattooActive = SkinnedDecalSubsystem->HasActiveAutomaticTattoo(Pawn);
		}
	}

	if (!TargetSkin || !ActiveTattooMaterial)
	{
		ClearMirroredTattooOverlay();
		if (!TattooBaseComponents.IsEmpty())
		{
			PrepareTattooBaseComponentsForVisibleSkin(Pawn, TattooBaseComponents, TargetSkin);
		}
		return;
	}

	UMaterialInterface* ExistingSkinOverlay = TargetSkin->GetOverlayMaterial();
	if (ExistingSkinOverlay
		&& ExistingSkinOverlay != LastMirroredOverlayMaterial.Get()
		&& ExistingSkinOverlay != ActiveTattooMaterial)
	{
		PrepareTattooBaseComponentsForVisibleSkin(Pawn, TattooBaseComponents, TargetSkin);
		MirroredOverlayTarget = nullptr;
		LastMirroredOverlayMaterial = nullptr;
		return;
	}

	const bool bActiveMaterialIsSkinnedDecal = ProjectTattooShopInputPrivate::IsSkinnedDecalOverlayMaterial(ActiveTattooMaterial);
	if (bAutomaticSkinnedTattooActive && !bActiveMaterialIsSkinnedDecal)
	{
		PrepareTattooBaseComponentsForVisibleSkin(Pawn, TattooBaseComponents, TargetSkin);
		MirroredOverlayTarget = nullptr;
		LastMirroredOverlayMaterial = nullptr;
		UE_LOG(
			LogProjectTattooShopInput,
			Verbose,
			TEXT("[TattooShop] Preserved SkinnedDecal skin overlay while TattooShop material stays on tattoo base components. Pawn=%s Material=%s."),
			*GetNameSafe(Pawn),
			*GetNameSafe(ActiveTattooMaterial));
		return;
	}

	ClearMirroredTattooOverlay();
	PrepareTattooBaseComponentsForVisibleSkin(Pawn, TattooBaseComponents, TargetSkin);
	TargetSkin->SetOverlayMaterial(ActiveTattooMaterial);
	MirroredOverlayTarget = TargetSkin;
	LastMirroredOverlayMaterial = ActiveTattooMaterial;
}

void UProjectTattooShopInputSubsystem::CollectTattooBaseComponents(APawn* Pawn, TArray<USkeletalMeshComponent*>& OutComponents) const
{
	OutComponents.Reset();
	if (!Pawn)
	{
		return;
	}

	const FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(
		Pawn->GetClass(),
		ProjectTattooShopInputPrivate::TattooBaseComponentsPropertyName);
	if (ArrayProperty && CastField<FObjectPropertyBase>(ArrayProperty->Inner))
	{
		const void* ArrayAddress = ArrayProperty->ContainerPtrToValuePtr<void>(Pawn);
		FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayAddress);
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(ArrayProperty->Inner);
			UObject* ComponentObject = ObjectProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index));
			if (USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(ComponentObject))
			{
				OutComponents.AddUnique(Component);
			}
		}
	}

	if (USkeletalMeshComponent* RuntimeComponent = RuntimeTattooBaseComponent.Get())
	{
		OutComponents.AddUnique(RuntimeComponent);
	}
}

USkeletalMeshComponent* UProjectTattooShopInputSubsystem::ResolveVisibleSkinComponent(
	APawn* Pawn,
	const TSet<USkeletalMeshComponent*>& TattooBaseComponents) const
{
	if (!Pawn)
	{
		return nullptr;
	}

	auto IsUsableVisibleSkin = [&TattooBaseComponents](USkeletalMeshComponent* Component)
	{
		if (!Component || TattooBaseComponents.Contains(Component))
		{
			return false;
		}

		const USkeletalMesh* SkeletalMesh = Component->GetSkeletalMeshAsset();
		if (!SkeletalMesh || !Component->IsVisible())
		{
			return false;
		}

		const FString ComponentName = Component->GetName();
		const FString MeshPath = SkeletalMesh->GetPathName();
		return !ComponentName.Contains(TEXT("Hair"), ESearchCase::IgnoreCase)
			&& !ComponentName.Contains(TEXT("Cloth"), ESearchCase::IgnoreCase)
			&& !ComponentName.Contains(TEXT("TattooBase"), ESearchCase::IgnoreCase)
			&& !MeshPath.Contains(TEXT("/Hair/"), ESearchCase::IgnoreCase)
			&& !MeshPath.Contains(TEXT("/Clothing/"), ESearchCase::IgnoreCase)
			&& !MeshPath.Contains(TEXT("/DazToUnreal/Multiple/"), ESearchCase::IgnoreCase);
	};

	if (const UEFCharacterCustomizationComponent* CustomizationComponent =
		Pawn->FindComponentByClass<UEFCharacterCustomizationComponent>())
	{
		if (USkeletalMeshComponent* BodyMesh = CustomizationComponent->GetBodyMeshComponent())
		{
			if (IsUsableVisibleSkin(BodyMesh))
			{
				return BodyMesh;
			}
		}
	}

	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			if (IsUsableVisibleSkin(CharacterMesh))
			{
				return CharacterMesh;
			}
		}
	}

	TInlineComponentArray<USkeletalMeshComponent*> Components(Pawn);
	USkeletalMeshComponent* BestCandidate = nullptr;
	int32 BestCandidateScore = MIN_int32;
	for (USkeletalMeshComponent* Component : Components)
	{
		if (!IsUsableVisibleSkin(Component))
		{
			continue;
		}

		const FString ComponentName = Component->GetName();
		const FString MeshPath = Component->GetSkeletalMeshAsset()->GetPathName();
		int32 Score = 0;
		if (MeshPath.Contains(TEXT("/DazToUnreal/Female/Female"), ESearchCase::IgnoreCase)
			|| MeshPath.Contains(TEXT("/DazToUnreal/Male/Male"), ESearchCase::IgnoreCase))
		{
			Score += 100;
		}
		if (ComponentName.Contains(TEXT("Body"), ESearchCase::IgnoreCase)
			|| ComponentName.Contains(TEXT("Female"), ESearchCase::IgnoreCase)
			|| ComponentName.Contains(TEXT("Male"), ESearchCase::IgnoreCase))
		{
			Score += 50;
		}
		if (Component->GetAttachParent() == Pawn->GetRootComponent())
		{
			Score += 10;
		}

		if (Score > BestCandidateScore)
		{
			BestCandidate = Component;
			BestCandidateScore = Score;
		}
	}

	return BestCandidate;
}

UMaterialInterface* UProjectTattooShopInputSubsystem::ResolveActiveTattooOverlayMaterial(
	APawn* Pawn,
	const TArray<USkeletalMeshComponent*>& TattooBaseComponents) const
{
	for (int32 Index = TattooBaseComponents.Num() - 1; Index >= 0; --Index)
	{
		USkeletalMeshComponent* Component = TattooBaseComponents[Index];
		if (!Component)
		{
			continue;
		}

		if (UMaterialInterface* OverlayMaterial = Component->GetOverlayMaterial())
		{
			return OverlayMaterial;
		}
	}

	if (Pawn)
	{
		if (FObjectPropertyBase* MaterialProperty = FindFProperty<FObjectPropertyBase>(
			Pawn->GetClass(),
			ProjectTattooShopInputPrivate::NewDynamicMaterialPropertyName))
		{
			return Cast<UMaterialInterface>(MaterialProperty->GetObjectPropertyValue_InContainer(Pawn));
		}
	}

	return nullptr;
}

void UProjectTattooShopInputSubsystem::PrepareTattooBaseComponentsForVisibleSkin(
	APawn* Pawn,
	const TArray<USkeletalMeshComponent*>& TattooBaseComponents,
	USkeletalMeshComponent* TargetSkin) const
{
	USkeletalMeshComponent* CharacterMesh = nullptr;
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		CharacterMesh = Character->GetMesh();
	}

	auto IsTattooShopMaterial = [](UMaterialInterface* Material)
	{
		if (!Material)
		{
			return false;
		}

		if (Material->GetPathName().Contains(TEXT("/Game/TattooShop/Materials/")))
		{
			return true;
		}

		UMaterial* BaseMaterial = Material->GetBaseMaterial();
		return BaseMaterial && BaseMaterial->GetPathName().Contains(TEXT("/Game/TattooShop/Materials/"));
	};

	for (USkeletalMeshComponent* Component : TattooBaseComponents)
	{
		if (!Component || Component == CharacterMesh || Component == TargetSkin)
		{
			continue;
		}

		bool bHasTattooMaterial = Component->GetOverlayMaterial() != nullptr;
		for (int32 MaterialIndex = 0; !bHasTattooMaterial && MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
		{
			if (IsTattooShopMaterial(Component->GetMaterial(MaterialIndex)))
			{
				bHasTattooMaterial = true;
			}
		}

		if (TargetSkin)
		{
			if (Component->GetAttachParent() != TargetSkin)
			{
				Component->AttachToComponent(TargetSkin, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			}

			Component->SetRelativeLocation(FVector::ZeroVector);
			Component->SetRelativeRotation(FRotator::ZeroRotator);
			Component->SetRelativeScale3D(Component == AutomationTattooBaseComponent.Get() ? FVector(1.003f) : FVector::OneVector);

			const USkeletalMesh* ComponentMesh = Component->GetSkeletalMeshAsset();
			const USkeletalMesh* TargetMesh = TargetSkin->GetSkeletalMeshAsset();
			if (ComponentMesh && TargetMesh && ComponentMesh->GetSkeleton() == TargetMesh->GetSkeleton())
			{
				Component->SetLeaderPoseComponent(TargetSkin);
			}
		}

		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);

		if (bHasTattooMaterial)
		{
			Component->SetHiddenInGame(false, true);
			Component->SetVisibility(true, true);
		}
	}
}

void UProjectTattooShopInputSubsystem::ClearMirroredTattooOverlay()
{
	USkeletalMeshComponent* TargetSkin = MirroredOverlayTarget.Get();
	UMaterialInterface* PreviousMaterial = LastMirroredOverlayMaterial.Get();
	if (TargetSkin && PreviousMaterial && TargetSkin->GetOverlayMaterial() == PreviousMaterial)
	{
		TargetSkin->SetOverlayMaterial(nullptr);
	}

	MirroredOverlayTarget = nullptr;
	LastMirroredOverlayMaterial = nullptr;
}

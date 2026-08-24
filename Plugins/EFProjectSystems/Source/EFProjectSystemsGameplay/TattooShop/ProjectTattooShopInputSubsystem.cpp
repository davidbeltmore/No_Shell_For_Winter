#include "TattooShop/ProjectTattooShopInputSubsystem.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
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
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"
#include "TattooShop/ProjectTattooShopSettings.h"
#include "TattooShop/UI/ProjectTattooShopEditorWidget.h"
#include "TattooShop/UI/ProjectTattooShopLibraryWidget.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "TimerManager.h"
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
	const FName TargetTatIndexPropertyName(TEXT("TargetTatIndex"));
	const FName TargetMIDPropertyName(TEXT("TargetMID"));
	const FName PreEditMIDPropertyName(TEXT("PreEditMID"));
	const FName SelectedAssetTexturePropertyName(TEXT("SelectedAssetTexture"));
	const FName EditModePropertyName(TEXT("EditMode"));
	const FName AcceptButtonName(TEXT("Accept"));
	const FName CancelButtonName(TEXT("Cancel"));
	const FName GrabAndUpdateUIEventName(TEXT("GrabAndUpdateUIValueFromSelectedMaterial"));
	const FName ApplyParamsEventName(TEXT("ApplyParamsOnTargetTatMaterial"));
	const TCHAR* TattooCardPathToken = TEXT("/Game/TattooShop/Blueprints/Widget/WBP_TattooCard");
	// The original TattooShop master is the canonical material contract.  A
	// native layer owns one MID from this master; no new layer inherits a MID
	// (or parameters) from the legacy widget pipeline.
	const TCHAR* TattooShopMaterialPath = TEXT("/Game/TattooShop/Materials/M_TattooShop.M_TattooShop");
	const TCHAR* TattooTransparentBaseMaterialPath = TEXT("/Game/TattooShop/Materials/M_Translucent.M_Translucent");
	const TCHAR* HeartTexturePath = TEXT("/Game/TattooShop/Texture/T_Heart.T_Heart");
	const TCHAR* TattooTexturePackageRoot = TEXT("/Game/TattooShop/Texture/");
	const TCHAR* ProjectTattooLibraryWidgetPath = TEXT("/Game/_Game/Widgets/TattooShop/Main/WBP_ProjectTattooLibrary.WBP_ProjectTattooLibrary_C");
	const TCHAR* ProjectTattooEditorWidgetPath = TEXT("/Game/_Game/Widgets/TattooShop/Main/WBP_ProjectTattooEditor.WBP_ProjectTattooEditor_C");
	const FName TextureParameterName(TEXT("Texture"));
	const FName ColorParameterName(TEXT("Color"));
	const FName EmissiveColorParameterName(TEXT("EmissiveColor"));
	const FName OffsetParameterName(TEXT("Offset"));
	const FName ManualTattooComponentTag(TEXT("ProjectManualTattoo"));
	const FName OpacityParameterName(TEXT("Opacity"));
	const FName ScaleParameterName(TEXT("Scale"));
	const FName ScaleYParameterName(TEXT("ScaleY"));
	const FName RotationParameterName(TEXT("Rotation"));

	const TArray<FName>& PersistedScalarParameterNames()
	{
		static const TArray<FName> Names = {
			TEXT("Scale"), TEXT("ScaleY"), TEXT("WorldPositionOffset"), TEXT("Rotation"),
			TEXT("EmissionStrength"), TEXT("UseBaseColorForEmissive"), TEXT("DepthMin"),
			TEXT("DepthMax"), TEXT("UseTexcoord"), TEXT("UseNormalMap"), TEXT("TintBaseColor"),
			TEXT("WarpStrength"), TEXT("OffsetU"), TEXT("OffsetV"), TEXT("PiFactor"),
			TEXT("RotationTimeRate"), TEXT("Metallic"), TEXT("Roughness"), TEXT("Specular"),
			TEXT("Opacity"), TEXT("UseT2T")
		};
		return Names;
	}

	const TArray<FName>& PersistedVectorParameterNames()
	{
		static const TArray<FName> Names = {
			TEXT("Color"), TEXT("EmissiveColor"), TEXT("ProjectionDirection"),
			TEXT("Offset"), TEXT("WarpDirection")
		};
		return Names;
	}

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

	bool ClearObjectPropertyWhenCompatible(UObject* Owner, const FName PropertyName)
	{
		if (!Owner)
		{
			return false;
		}

		FObjectPropertyBase* ObjectProperty = FindObjectPropertyByCompatibleName(Owner->GetClass(), PropertyName);
		if (!ObjectProperty)
		{
			return false;
		}

		ObjectProperty->SetObjectPropertyValue_InContainer(Owner, nullptr);
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
	TattooViewerCardWidgetClass = nullptr;
	RuntimeTattooTextureCache.Reset();
	ActiveDeleteMenuSlateWidget.Reset();
	TrackedAssetPreviewWidget = nullptr;
	TrackedProjectTattooLibraryWidget = nullptr;
	TrackedProjectTattooCatalogWidget = nullptr;
	TrackedProjectTattooEditorWidget = nullptr;
	MirroredOverlayTarget = nullptr;
	LastMirroredOverlayMaterial = nullptr;
	AutomationTattooBaseComponent = nullptr;
	RuntimeTattooCustomizationWidget = nullptr;
	RuntimeTattooMID = nullptr;
	LastRuntimeTattooTexture = nullptr;
	RuntimeTattooBaseComponent = nullptr;
	ManualTattooMIDs.Reset();
	ManualTattooComponents.Reset();
	ManualTattooEditSnapshots.Reset();
	ManualTattooPreviewParameters.Reset();
	ManualTattooLayerOrders.Reset();
	LastManualTattooTargetSkin = nullptr;
	ActiveManualTattooId.Invalidate();
	ActiveManualTattooSourceEntryId = NAME_None;
	LastSkinnedTattooSynchronizedPawn.Reset();
	bManualTattooPreviewPending = false;
	InputSnapshot = FProjectTattooShopInputSnapshot();
	bTattooShopOpen = false;
	bRuntimeTattooCardsInitialized = false;
}

void UProjectTattooShopInputSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ManualTattooPreviewTimerHandle);
	}
	DismissDeleteTattooTextureMenu();
	ResetManualTattooRuntime();
	RuntimeTattooTextureCache.Reset();
	DetachFromTrackedPlayerController();
	Super::Deinitialize();
}

void UProjectTattooShopInputSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();
	if (UsesSkinnedDecalTattooShop())
	{
		RemoveLegacyTattooRuntimeArtifacts(TrackedPlayerPawn.Get());
		if (LastSkinnedTattooSynchronizedPawn.Get() != TrackedPlayerPawn.Get())
		{
			RequestManualTattooSynchronization(true);
		}
		return;
	}
	CaptureAssetPreviewWidget();
	NormalizeTattooCardGrid(TrackedTattooShopWidget.Get());
	NormalizeTattooCardGrid(TrackedAssetPreviewWidget.Get());
	RehydrateManualTattoos();
	SynchronizeTattooOverlayToVisibleSkin();

	if (bTattooShopOpen && IsValid(TrackedTattooShopWidget))
	{
		BindTattooCustomizationRuntimeButtons(TrackedAssetPreviewWidget.Get());
		RepairTattooCustomizationRuntime();
		BindTattooShopRuntimeButtons(TrackedTattooShopWidget.Get());
		if (!bRuntimeTattooCardsInitialized)
		{
			if (UUserWidget* PreviewWidget = ResolveTrackedAssetPreviewWidget())
			{
				bRuntimeTattooCardsInitialized = RefreshRuntimeTattooCards(PreviewWidget);
			}
		}
	}
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

bool UProjectTattooShopInputSubsystem::UsesSkinnedDecalTattooShop() const
{
	const UProjectTattooShopSettings* Settings = UProjectTattooShopSettings::Get();
	return Settings && Settings->bUseSkinnedDecalTattooShop;
}

UProjectTattooShopStateSubsystem* UProjectTattooShopInputSubsystem::ResolveTattooState() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UProjectTattooShopStateSubsystem>() : nullptr;
}

FGuid UProjectTattooShopInputSubsystem::BeginCreateTattoo(const FProjectTattooParameters& InitialParameters)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	const UProjectTattooShopSettings* Settings = UProjectTattooShopSettings::Get();
	const int32 MaximumManualTattoos = Settings ? FMath::Clamp(Settings->MaxManualTattoos, 1, 32) : 32;
	if (!State || State->GetRecords().Num() >= MaximumManualTattoos)
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("[TattooShop] Manual tattoo limit reached (%d)."), MaximumManualTattoos);
		return FGuid();
	}

	const FGuid TattooId = State->BeginCreate(InitialParameters);
	if (TattooId.IsValid())
	{
		ActiveManualTattooId = TattooId;
		RequestManualTattooSynchronization(true);
	}
	return TattooId;
}

bool UProjectTattooShopInputSubsystem::BeginEditTattoo(const FGuid& TattooId)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State || !State->BeginEdit(TattooId))
	{
		return false;
	}
	ActiveManualTattooId = TattooId;
	return true;
}

bool UProjectTattooShopInputSubsystem::PreviewTattoo(
	const FGuid& TattooId,
	const FProjectTattooParameters& Parameters)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State || !State->Preview(TattooId, Parameters))
	{
		return false;
	}
	ActiveManualTattooId = TattooId;
	RequestManualTattooSynchronization(false);
	return true;
}

bool UProjectTattooShopInputSubsystem::CommitTattoo(const FGuid& TattooId)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State || !State->Commit(TattooId))
	{
		return false;
	}
	if (ActiveManualTattooId == TattooId)
	{
		ActiveManualTattooId.Invalidate();
		ActiveManualTattooSourceEntryId = NAME_None;
	}
	RequestManualTattooSynchronization(true);
	RefreshProjectTattooShopUI();
	return true;
}

bool UProjectTattooShopInputSubsystem::CancelTattoo(const FGuid& TattooId)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State || !State->Cancel(TattooId))
	{
		return false;
	}
	if (ActiveManualTattooId == TattooId)
	{
		ActiveManualTattooId.Invalidate();
		ActiveManualTattooSourceEntryId = NAME_None;
	}
	RequestManualTattooSynchronization(true);
	RefreshProjectTattooShopUI();
	return true;
}

bool UProjectTattooShopInputSubsystem::DeleteTattoo(const FGuid& TattooId)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State || !State->Delete(TattooId))
	{
		return false;
	}
	if (ActiveManualTattooId == TattooId)
	{
		ActiveManualTattooId.Invalidate();
		ActiveManualTattooSourceEntryId = NAME_None;
	}
	RequestManualTattooSynchronization(true);
	RefreshProjectTattooShopUI();
	return true;
}

TArray<FProjectTattooShopCardData> UProjectTattooShopInputSubsystem::GetTattooCatalog()
{
	TArray<FProjectTattooShopCardData> Result;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.ScanPathsSynchronous({ ProjectTattooShopInputPrivate::TattooTexturePackageRoot }, true);
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(ProjectTattooShopInputPrivate::TattooTexturePackageRoot));
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	TArray<FAssetData> TextureAssets;
	AssetRegistry.GetAssets(Filter, TextureAssets);
	for (const FAssetData& AssetData : TextureAssets)
	{
		FProjectTattooShopCardData& Card = Result.AddDefaulted_GetRef();
		Card.EntryId = AssetData.AssetName;
		Card.DisplayName = FText::FromName(AssetData.AssetName);
		Card.Subtitle = FText::FromString(TEXT("LIBRARY"));
		Card.ThumbnailTexture = TSoftObjectPtr<UTexture2D>(AssetData.ToSoftObjectPath());
		Card.Kind = EProjectTattooShopCardKind::Catalog;
	}

	const FString RuntimeDirectory = GetRuntimeTattooTextureDirectory();
	IFileManager::Get().MakeDirectory(*RuntimeDirectory, true);
	TArray<FString> RuntimePngFiles;
	IFileManager::Get().FindFilesRecursive(RuntimePngFiles, *RuntimeDirectory, TEXT("*.png"), true, false);
	RuntimePngFiles.Sort();
	for (const FString& RuntimePngFile : RuntimePngFiles)
	{
		const FString NormalizedPath = NormalizeTattooFilePath(RuntimePngFile);
		UTexture2D* Texture = RuntimeTattooTextureCache.FindRef(NormalizedPath);
		if (!IsValid(Texture))
		{
			Texture = LoadPngTextureFromFile(NormalizedPath, MakeRuntimeTattooDisplayName(NormalizedPath));
			if (Texture)
			{
				RuntimeTattooTextureCache.Add(NormalizedPath, Texture);
			}
		}
		if (!Texture)
		{
			continue;
		}
		FProjectTattooShopCardData& Card = Result.AddDefaulted_GetRef();
		Card.EntryId = FName(*FPaths::GetCleanFilename(NormalizedPath));
		Card.DisplayName = FText::FromString(MakeRuntimeTattooDisplayName(NormalizedPath));
		Card.Subtitle = FText::FromString(TEXT("UPLOADED PNG"));
		Card.RuntimeTexture = Texture;
		Card.Kind = EProjectTattooShopCardKind::Catalog;
	}

	Result.Sort([](const FProjectTattooShopCardData& Left, const FProjectTattooShopCardData& Right)
	{
		return Left.DisplayName.ToString() < Right.DisplayName.ToString();
	});
	return Result;
}

TArray<FProjectTattooShopCardData> UProjectTattooShopInputSubsystem::GetManualTattooLayers()
{
	TArray<FProjectTattooShopCardData> Result;
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State)
	{
		return Result;
	}
	for (const FProjectTattooRecord& Record : State->GetRecords())
	{
		FProjectTattooShopCardData& Card = Result.AddDefaulted_GetRef();
		Card.TattooId = Record.TattooId;
		Card.EntryId = FName(*Record.TattooId.ToString(EGuidFormats::Digits));
		Card.DisplayName = Record.Parameters.RuntimeTextureId.IsEmpty()
			? FText::FromString(Record.Parameters.TextureAssetPath.GetAssetName())
			: FText::FromString(FPaths::GetBaseFilename(Record.Parameters.RuntimeTextureId));
		Card.Kind = EProjectTattooShopCardKind::Manual;
		Card.bEnabled = Record.Parameters.bEnabled;
		Card.AccentColor = Record.Parameters.Color;
		bool bMissingRuntimeTexture = false;
		UTexture* Texture = ResolvePersistedTattooTexture(Record.Parameters, bMissingRuntimeTexture);
		Card.RuntimeTexture = Cast<UTexture2D>(Texture);
		if (Record.Parameters.TextureAssetPath.IsValid())
		{
			Card.ThumbnailTexture = TSoftObjectPtr<UTexture2D>(Record.Parameters.TextureAssetPath);
		}
		Card.Subtitle = (bMissingRuntimeTexture || !Texture)
			? FText::FromString(TEXT("MISSING RESOURCE"))
			: FText::FromString(FString::Printf(TEXT("LAYER %d"), Record.Parameters.LayerOrder));
	}
	return Result;
}

TArray<FProjectTattooShopCardData> UProjectTattooShopInputSubsystem::GetActiveAutomaticTattooLayers()
{
	TArray<FProjectTattooShopCardData> Result;
	UWorld* World = GetWorld();
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	if (!TattooSubsystem)
	{
		return Result;
	}
	TArray<FProjectAutomaticTattooRuntimeDebugSnapshot> Snapshots;
	TattooSubsystem->GetAutomaticTattooRuntimeDebugSnapshots(Snapshots);
	for (const FProjectAutomaticTattooRuntimeDebugSnapshot& Snapshot : Snapshots)
	{
		if (!Snapshot.bActive)
		{
			continue;
		}
		FProjectTattooShopCardData& Card = Result.AddDefaulted_GetRef();
		Card.EntryId = Snapshot.RowName;
		Card.DisplayName = FText::FromName(Snapshot.RowName);
		Card.Subtitle = FText::FromString(TEXT("AUTO / LOCKED"));
		Card.ThumbnailTexture = Snapshot.EffectiveRow.TattooTexture;
		Card.Kind = EProjectTattooShopCardKind::Automatic;
		Card.bReadOnly = true;
		Card.AccentColor = FLinearColor::White;
	}
	return Result;
}

void UProjectTattooShopInputSubsystem::BindProjectTattooShopUI()
{
	for (UProjectTattooShopLibraryWidget* LibraryWidget : {
		TrackedProjectTattooLibraryWidget.Get(),
		TrackedProjectTattooCatalogWidget.Get() })
	{
		if (!IsValid(LibraryWidget))
		{
			continue;
		}
		LibraryWidget->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooSelectionChanged);
		LibraryWidget->OnAddRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooAddRequested);
		LibraryWidget->OnEditRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooEditRequested);
		LibraryWidget->OnRemoveRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooRemoveRequested);
		LibraryWidget->OnUploadRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooUploadRequested);
		LibraryWidget->OnDeleteSourceRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooDeleteSourceRequested);
	}

	if (UProjectTattooShopEditorWidget* Editor = TrackedProjectTattooEditorWidget.Get())
	{
		Editor->OnPreviewChanged.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooPreviewChanged);
		Editor->OnAcceptRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooAcceptRequested);
		Editor->OnCancelRequested.AddUniqueDynamic(this, &ThisClass::HandleProjectTattooCancelRequested);
	}
}

void UProjectTattooShopInputSubsystem::RefreshProjectTattooShopUI()
{
	const TArray<FProjectTattooShopCardData> Catalog = GetTattooCatalog();
	const TArray<FProjectTattooShopCardData> Manual = GetManualTattooLayers();
	const TArray<FProjectTattooShopCardData> Automatic = GetActiveAutomaticTattooLayers();
	for (UProjectTattooShopLibraryWidget* LibraryWidget : {
		TrackedProjectTattooLibraryWidget.Get(),
		TrackedProjectTattooCatalogWidget.Get() })
	{
		if (IsValid(LibraryWidget))
		{
			LibraryWidget->SetLibraryEntries(Catalog);
			LibraryWidget->SetManualTattoos(Manual);
			LibraryWidget->SetAutomaticTattoos(Automatic);
		}
	}
}

FProjectTattooShopEditorModel UProjectTattooShopInputSubsystem::MakeEditorModel(
	const FProjectTattooRecord& Record,
	const FName SourceEntryId) const
{
	FProjectTattooShopEditorModel Model;
	Model.TattooId = Record.TattooId;
	Model.SourceEntryId = SourceEntryId;
	Model.DisplayName = Record.Parameters.RuntimeTextureId.IsEmpty()
		? FText::FromString(Record.Parameters.TextureAssetPath.GetAssetName())
		: FText::FromString(FPaths::GetBaseFilename(Record.Parameters.RuntimeTextureId));
	Model.Kind = EProjectTattooShopCardKind::Manual;
	Model.PlacementPreset = Record.Parameters.PlacementPreset;
	Model.AnchorBone = Record.Parameters.AnchorBone;
	Model.OffsetX = Record.Parameters.OffsetX;
	Model.OffsetY = Record.Parameters.OffsetY;
	Model.Size = Record.Parameters.Size;
	Model.RotationDegrees = Record.Parameters.RotationDegrees;
	Model.ProjectionDistance = Record.Parameters.ProjectionDistance;
	Model.Color = Record.Parameters.Color;
	Model.bTintEnabled = Record.Parameters.bUseTint;
	Model.Opacity = Record.Parameters.Opacity;
	Model.bEnabled = Record.Parameters.bEnabled;
	Model.bReadOnly = false;
	return Model;
}

FProjectTattooParameters UProjectTattooShopInputSubsystem::ApplyEditorModelToParameters(
	const FProjectTattooShopEditorModel& Model,
	const FProjectTattooParameters& Existing) const
{
	FProjectTattooParameters Result = Existing;
	Result.PlacementPreset = Model.PlacementPreset;
	Result.AnchorBone = Model.AnchorBone;
	Result.OffsetX = FMath::Clamp(Model.OffsetX, -30.0f, 30.0f);
	Result.OffsetY = FMath::Clamp(Model.OffsetY, -30.0f, 30.0f);
	Result.Size = FMath::Clamp(Model.Size, 1.0f, 50.0f);
	Result.RotationDegrees = FMath::Clamp(Model.RotationDegrees, -180.0f, 180.0f);
	Result.ProjectionDistance = FMath::Clamp(Model.ProjectionDistance, 0.0f, 50.0f);
	Result.Color = Model.Color;
	Result.bUseTint = Model.bTintEnabled;
	Result.Opacity = FMath::Clamp(Model.Opacity, 0.0f, 1.0f);
	Result.bEnabled = Model.bEnabled && !Result.bRuntimeTextureMissing;
	return Result;
}

bool UProjectTattooShopInputSubsystem::PopulateTextureIdentityFromCard(
	const FProjectTattooShopCardData& Card,
	FProjectTattooParameters& InOutParameters) const
{
	if (IsValid(Card.RuntimeTexture))
	{
		FString RuntimeFilePath;
		if (!FindRuntimeTattooFileForTexture(Card.RuntimeTexture, RuntimeFilePath))
		{
			return false;
		}
		InOutParameters.TextureAssetPath.Reset();
		InOutParameters.RuntimeTextureId = FPaths::GetCleanFilename(RuntimeFilePath);
		InOutParameters.bRuntimeTextureMissing = false;
		return true;
	}
	if (!Card.ThumbnailTexture.IsNull())
	{
		const FSoftObjectPath TexturePath = Card.ThumbnailTexture.ToSoftObjectPath();
		if (!TexturePath.IsValid() || !TexturePath.ToString().StartsWith(TEXT("/Game/")))
		{
			return false;
		}
		InOutParameters.TextureAssetPath = TexturePath;
		InOutParameters.RuntimeTextureId.Reset();
		InOutParameters.bRuntimeTextureMissing = false;
		return true;
	}
	return false;
}

void UProjectTattooShopInputSubsystem::ShowProjectTattooEditor(
	const FGuid& TattooId,
	const FName SourceEntryId)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	APlayerController* PlayerController = TrackedPlayerController.Get();
	if (!State || !PlayerController || !TattooId.IsValid())
	{
		return;
	}
	if (!State->IsTransactionActive(TattooId) && !BeginEditTattoo(TattooId))
	{
		return;
	}
	const FProjectTattooRecord* Record = State->FindRecord(TattooId);
	UProjectTattooShopEditorWidget* Editor = EnsureProjectTattooEditorWidget(PlayerController);
	if (!Record || !Editor)
	{
		CancelTattoo(TattooId);
		return;
	}
	ActiveManualTattooId = TattooId;
	ActiveManualTattooSourceEntryId = SourceEntryId;
	BindProjectTattooShopUI();
	Editor->ApplyEditorModel(MakeEditorModel(*Record, SourceEntryId));
	Editor->SetVisibility(ESlateVisibility::Visible);
	if (IsValid(TattooAssetPreviewHostPanel))
	{
		MountWidgetInHostedPanel(Editor, TattooAssetPreviewHostPanel, true);
		TrackedAssetPreviewWidget = Editor;
	}
	else
	{
		Editor->AddToViewport(ProjectTattooShopInputPrivate::WidgetZOrder + 2);
		TrackedAssetPreviewWidget = Editor;
	}
}

void UProjectTattooShopInputSubsystem::HideProjectTattooEditor()
{
	if (IsValid(TrackedProjectTattooEditorWidget))
	{
		TrackedProjectTattooEditorWidget->SetVisibility(ESlateVisibility::Collapsed);
		TrackedProjectTattooEditorWidget->RemoveFromParent();
	}
	if (UProjectTattooShopLibraryWidget* Catalog = TrackedProjectTattooCatalogWidget.Get())
	{
		Catalog->SetVisibility(ESlateVisibility::Visible);
		if (IsValid(TattooAssetPreviewHostPanel))
		{
			MountWidgetInHostedPanel(Catalog, TattooAssetPreviewHostPanel, true);
		}
		else if (!bTattooShopHostedInPanel)
		{
			Catalog->AddToViewport(ProjectTattooShopInputPrivate::WidgetZOrder + 1);
		}
		TrackedAssetPreviewWidget = Catalog;
	}
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooSelectionChanged(
	FProjectTattooShopCardData CardData)
{
	if (UProjectTattooShopLibraryWidget* Management = TrackedProjectTattooLibraryWidget.Get())
	{
		const FProjectTattooShopCardData Current = Management->GetSelectedEntry();
		const bool bSameSelection = Management->HasSelection()
			&& Current.EntryId == CardData.EntryId
			&& Current.TattooId == CardData.TattooId
			&& Current.Kind == CardData.Kind;
		if (!bSameSelection)
		{
			Management->SetSelectedEntry(CardData);
		}
	}
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooAddRequested(
	FProjectTattooShopCardData CardData)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State || CardData.Kind != EProjectTattooShopCardKind::Catalog)
	{
		return;
	}
	FProjectTattooParameters Parameters = State->GetDefaultTattooParameters();
	if (!PopulateTextureIdentityFromCard(CardData, Parameters))
	{
		return;
	}
	const FGuid TattooId = BeginCreateTattoo(Parameters);
	if (TattooId.IsValid())
	{
		ShowProjectTattooEditor(TattooId, CardData.EntryId);
		RefreshProjectTattooShopUI();
	}
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooEditRequested(const FGuid TattooId)
{
	ShowProjectTattooEditor(TattooId);
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooRemoveRequested(const FGuid TattooId)
{
	DeleteTattoo(TattooId);
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooUploadRequested()
{
	RequestUploadRuntimeTattooTexture();
	RefreshProjectTattooShopUI();
}

bool UProjectTattooShopInputSubsystem::IsRuntimeTextureInUse(
	const FProjectTattooShopCardData& Card) const
{
	if (!IsValid(Card.RuntimeTexture))
	{
		return false;
	}
	FString RuntimeFilePath;
	if (!FindRuntimeTattooFileForTexture(Card.RuntimeTexture, RuntimeFilePath))
	{
		return false;
	}
	const FString RuntimeTextureId = FPaths::GetCleanFilename(RuntimeFilePath);
	const UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	if (!State)
	{
		return false;
	}
	return State->GetRecords().ContainsByPredicate([&RuntimeTextureId](const FProjectTattooRecord& Record)
	{
		return Record.Parameters.RuntimeTextureId.Equals(RuntimeTextureId, ESearchCase::IgnoreCase);
	});
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooDeleteSourceRequested(
	FProjectTattooShopCardData CardData)
{
	UProjectTattooShopLibraryWidget* Management = TrackedProjectTattooLibraryWidget.Get();
	if (!IsValid(CardData.RuntimeTexture))
	{
		if (Management) Management->SetStatusText(FText::FromString(TEXT("Only uploaded PNG files can be deleted.")));
		return;
	}
	if (IsRuntimeTextureInUse(CardData))
	{
		if (Management) Management->SetStatusText(FText::FromString(TEXT("This PNG is used by an applied tattoo and cannot be deleted.")));
		return;
	}
	if (DeleteTattooTexture(CardData.RuntimeTexture, CardData.DisplayName.ToString()))
	{
		if (Management) Management->SetStatusText(FText::FromString(TEXT("Uploaded PNG deleted.")));
		RefreshProjectTattooShopUI();
	}
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooPreviewChanged(
	FProjectTattooShopEditorModel EditorModel)
{
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	FProjectTattooRecord Record;
	if (!State || !State->GetTattooRecord(EditorModel.TattooId, Record))
	{
		return;
	}
	PreviewTattoo(EditorModel.TattooId, ApplyEditorModelToParameters(EditorModel, Record.Parameters));
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooAcceptRequested(const FGuid TattooId)
{
	if (CommitTattoo(TattooId))
	{
		HideProjectTattooEditor();
	}
}

void UProjectTattooShopInputSubsystem::HandleProjectTattooCancelRequested(const FGuid TattooId)
{
	if (CancelTattoo(TattooId))
	{
		HideProjectTattooEditor();
	}
}

void UProjectTattooShopInputSubsystem::RequestManualTattooSynchronization(const bool bImmediate)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!bImmediate)
	{
		bManualTattooPreviewPending = true;
		const UProjectTattooShopSettings* Settings = UProjectTattooShopSettings::Get();
		const float Delay = Settings ? FMath::Clamp(Settings->PreviewDebounceSeconds, 0.0f, 0.25f) : 0.05f;
		if (Delay > KINDA_SMALL_NUMBER)
		{
			World->GetTimerManager().SetTimer(
				ManualTattooPreviewTimerHandle,
				this,
				&ThisClass::FlushManualTattooPreview,
				Delay,
				false);
			return;
		}
	}
	World->GetTimerManager().ClearTimer(ManualTattooPreviewTimerHandle);
	FlushManualTattooPreview();
}

void UProjectTattooShopInputSubsystem::FlushManualTattooPreview()
{
	bManualTattooPreviewPending = false;
	APawn* Pawn = TrackedPlayerPawn.Get();
	UProjectTattooShopStateSubsystem* State = ResolveTattooState();
	UWorld* World = GetWorld();
	UProjectDefaultTattooSkinnedDecalSubsystem* TattooSubsystem =
		World ? World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>() : nullptr;
	if (!CanUseTattooShopPawn(Pawn) || !State || !TattooSubsystem)
	{
		return;
	}

	TArray<FProjectTattooRecord> EffectiveRecords = State->GetRecords();
	TMap<FGuid, UTexture2D*> TextureByTattooId;
	for (FProjectTattooRecord& Record : EffectiveRecords)
	{
		bool bMissingRuntimeTexture = false;
		UTexture2D* Texture = Cast<UTexture2D>(ResolvePersistedTattooTexture(Record.Parameters, bMissingRuntimeTexture));
		if (bMissingRuntimeTexture || !Texture)
		{
			Record.Parameters.bEnabled = false;
			Record.Parameters.bRuntimeTextureMissing = !Record.Parameters.RuntimeTextureId.IsEmpty();
			continue;
		}
		TextureByTattooId.Add(Record.TattooId, Texture);
	}
	TattooSubsystem->SynchronizeManualTattoos(Pawn, EffectiveRecords, TextureByTattooId);
	LastSkinnedTattooSynchronizedPawn = Pawn;
}

void UProjectTattooShopInputSubsystem::RemoveLegacyTattooRuntimeArtifacts(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	ClearMirroredTattooOverlay();
	const bool bHasTrackedLegacyRuntime = !ManualTattooComponents.IsEmpty()
		|| !ManualTattooMIDs.IsEmpty()
		|| IsValid(RuntimeTattooBaseComponent)
		|| IsValid(RuntimeTattooMID)
		|| IsValid(RuntimeTattooCustomizationWidget);
	bool bHasLegacyPawnArrayEntries = false;
	if (FArrayProperty* ExistingArrayProperty = FindFProperty<FArrayProperty>(
		Pawn->GetClass(), ProjectTattooShopInputPrivate::TattooBaseComponentsPropertyName))
	{
		if (FObjectPropertyBase* ExistingInnerProperty = CastField<FObjectPropertyBase>(ExistingArrayProperty->Inner))
		{
			void* ExistingArrayAddress = ExistingArrayProperty->ContainerPtrToValuePtr<void>(Pawn);
			FScriptArrayHelper ExistingArrayHelper(ExistingArrayProperty, ExistingArrayAddress);
			for (int32 Index = 0; Index < ExistingArrayHelper.Num(); ++Index)
			{
				USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(
					ExistingInnerProperty->GetObjectPropertyValue(ExistingArrayHelper.GetRawPtr(Index)));
				if (IsValid(Component)
					&& (Component->GetName().Contains(TEXT("TattooBase"), ESearchCase::IgnoreCase)
						|| Component->GetName().StartsWith(TEXT("ProjectManualTattoo_"))
						|| Component->ComponentTags.Contains(ProjectTattooShopInputPrivate::ManualTattooComponentTag)))
				{
					bHasLegacyPawnArrayEntries = true;
					break;
				}
			}
		}
	}
	if (!bHasTrackedLegacyRuntime && !bHasLegacyPawnArrayEntries)
	{
		return;
	}
	TSet<USkeletalMeshComponent*> EmptyTattooSet;
	USkeletalMeshComponent* TargetSkin = ResolveVisibleSkinComponent(Pawn, EmptyTattooSet);
	if (FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(
		Pawn->GetClass(), ProjectTattooShopInputPrivate::TattooBaseComponentsPropertyName))
	{
		if (FObjectPropertyBase* InnerProperty = CastField<FObjectPropertyBase>(ArrayProperty->Inner))
		{
			void* ArrayAddress = ArrayProperty->ContainerPtrToValuePtr<void>(Pawn);
			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayAddress);
			for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
			{
				USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(
					InnerProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index)));
				const bool bLegacyTattooFollower = IsValid(Component)
					&& (Component->GetName().Contains(TEXT("TattooBase"), ESearchCase::IgnoreCase)
						|| Component->GetName().StartsWith(TEXT("ProjectManualTattoo_"))
						|| Component->ComponentTags.Contains(ProjectTattooShopInputPrivate::ManualTattooComponentTag));
				if (bLegacyTattooFollower && Component != TargetSkin)
				{
					Component->SetOverlayMaterial(nullptr);
					Component->SetHiddenInGame(true, true);
					Component->SetVisibility(false, true);
					Component->DestroyComponent();
					ArrayHelper.RemoveValues(Index, 1);
				}
			}
		}
	}

	TInlineComponentArray<USkeletalMeshComponent*> Components(Pawn);
	for (USkeletalMeshComponent* Component : Components)
	{
		if (!IsValid(Component) || Component == TargetSkin)
		{
			continue;
		}
		const bool bLegacyTattooFollower =
			Component->ComponentTags.Contains(ProjectTattooShopInputPrivate::ManualTattooComponentTag)
			|| Component->GetName().StartsWith(TEXT("ProjectManualTattoo_"))
			|| Component->GetName().Contains(TEXT("TattooBase"), ESearchCase::IgnoreCase);
		if (bLegacyTattooFollower)
		{
			Component->SetOverlayMaterial(nullptr);
			Component->SetHiddenInGame(true, true);
			Component->SetVisibility(false, true);
			Component->DestroyComponent();
		}
	}
	ManualTattooComponents.Reset();
	ManualTattooMIDs.Reset();
	ManualTattooEditSnapshots.Reset();
	ManualTattooPreviewParameters.Reset();
	ManualTattooLayerOrders.Reset();
	RuntimeTattooMID = nullptr;
	RuntimeTattooBaseComponent = nullptr;
	RuntimeTattooCustomizationWidget = nullptr;

	if (FObjectPropertyBase* MaterialProperty = FindFProperty<FObjectPropertyBase>(
		Pawn->GetClass(), ProjectTattooShopInputPrivate::NewDynamicMaterialPropertyName))
	{
		if (UMaterialInterface* Material = Cast<UMaterialInterface>(MaterialProperty->GetObjectPropertyValue_InContainer(Pawn)))
		{
			if (Material->GetPathName().Contains(TEXT("/Game/TattooShop/Materials/")))
			{
				MaterialProperty->SetObjectPropertyValue_InContainer(Pawn, nullptr);
			}
		}
	}
}

UMaterialInstanceDynamic* UProjectTattooShopInputSubsystem::BeginEdit(
	UMaterialInstanceDynamic* CandidateMID,
	const int32 LegacyTattooIndex,
	FGuid& OutTattooId)
{
	OutTattooId.Invalidate();
	if (UsesSkinnedDecalTattooShop())
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("[TattooShop] Legacy MID BeginEdit is disabled while SkinnedDecal TattooShop is active."));
		return nullptr;
	}
	TryResolveRuntimeContext();
	APawn* Pawn = TrackedPlayerPawn.Get();
	if (!CanUseTattooShopPawn(Pawn))
	{
		return nullptr;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectTattooShopStateSubsystem* State = GameInstance ? GameInstance->GetSubsystem<UProjectTattooShopStateSubsystem>() : nullptr;
	FGuid TattooId;
	// A valid persisted index is an explicit edit request.  A new tattoo must
	// never fall back to ActiveManualTattooId: that would turn its Add action
	// into an edit of the previous tattoo and make the previous layer disappear.
	// While a new session is open, the widget is rebound to its native MID below,
	// so FindTattooIdForMID is the only valid way to retain that in-progress
	// session across ticks.
	if (State && State->GetRecords().IsValidIndex(LegacyTattooIndex))
	{
		TattooId = State->GetRecords()[LegacyTattooIndex].TattooId;
	}
	if (!TattooId.IsValid())
	{
		TattooId = FindTattooIdForMID(CandidateMID);
	}

	if (!TattooId.IsValid())
	{
		TattooId = FGuid::NewGuid();
	}

	// Never construct one GUID from another MID.  Besides retaining stale
	// preview parameters, a MID cannot safely be used as another MID's parent.
	// Every tattoo starts at the canonical master and then receives only its own
	// saved/default parameters.
	UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		ProjectTattooShopInputPrivate::TattooShopMaterialPath);
	if (!SourceMaterial)
	{
		UE_LOG(LogProjectTattooShopInput, Error, TEXT("[TattooShop] Missing canonical tattoo material %s."), ProjectTattooShopInputPrivate::TattooShopMaterialPath);
		return nullptr;
	}
	UMaterialInstanceDynamic* IsolatedMID = ManualTattooMIDs.FindRef(TattooId);
	if (!IsolatedMID)
	{
		IsolatedMID = CreateIsolatedTattooMID(Pawn, SourceMaterial);
		if (!IsolatedMID)
		{
			return nullptr;
		}
		bool bLoadedExistingRecord = false;
		if (State)
		{
			if (const FProjectTattooRecord* ExistingRecord = State->FindRecord(TattooId))
			{
				bool bMissingRuntimeFile = false;
				UTexture* ExistingTexture = ResolvePersistedTattooTexture(ExistingRecord->Parameters, bMissingRuntimeFile);
				ApplyTattooParameters(IsolatedMID, ExistingRecord->Parameters, ExistingTexture);
				ManualTattooLayerOrders.Add(TattooId, ExistingRecord->Parameters.LayerOrder);
				bLoadedExistingRecord = true;
			}
		}
		if (!bLoadedExistingRecord)
		{
			// BP_TSChar used to initialize these through its interface. Composition-
			// based pawns do not implement that legacy event, so establish useful
			// per-tattoo defaults natively before the widget reads its controls.
			// Match WBP_TattooCustomization::ReceiveAssetForCustomization exactly.
			// These are shader-space values; changing them to decal-space tuning made
			// the same tattoo move and resize when Character Creation changed maps.
			IsolatedMID->SetScalarParameterValue(ProjectTattooShopInputPrivate::ScaleParameterName, 0.1f);
			IsolatedMID->SetScalarParameterValue(ProjectTattooShopInputPrivate::ScaleYParameterName, 0.1f);
			IsolatedMID->SetScalarParameterValue(ProjectTattooShopInputPrivate::RotationParameterName, 0.0f);
			IsolatedMID->SetScalarParameterValue(FName(TEXT("WorldPositionOffset")), 0.1f);
			IsolatedMID->SetScalarParameterValue(FName(TEXT("DepthMin")), -200.0f);
			IsolatedMID->SetScalarParameterValue(FName(TEXT("DepthMax")), 200.0f);
			// M_TattooShop's disabled TintBaseColor branch returns the source RGB.
			// Mask-style tattoo textures therefore render as their original black
			// silhouette even when the color picker contains a non-black value. The
			// enabled branch multiplies the tattoo mask by this MID's own Color, which
			// both honours the picker and keeps every GUID isolated from its siblings.
			IsolatedMID->SetScalarParameterValue(FName(TEXT("TintBaseColor")), 1.0f);
			IsolatedMID->SetVectorParameterValue(ProjectTattooShopInputPrivate::ColorParameterName, FLinearColor(1.0f, 0.539349f, 0.0f, 1.0f));
			IsolatedMID->SetVectorParameterValue(ProjectTattooShopInputPrivate::EmissiveColorParameterName, FLinearColor::White);
			IsolatedMID->SetVectorParameterValue(ProjectTattooShopInputPrivate::OffsetParameterName, FLinearColor(0.0f, 0.5f, 0.0f, 1.0f));
			IsolatedMID->SetVectorParameterValue(FName(TEXT("ProjectionDirection")), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));

			int32 NextLayerOrder = 0;
			if (State)
			{
				for (const FProjectTattooRecord& ExistingRecord : State->GetRecords())
				{
					NextLayerOrder = FMath::Max(NextLayerOrder, ExistingRecord.Parameters.LayerOrder + 1);
				}
			}
			ManualTattooLayerOrders.Add(TattooId, NextLayerOrder);
		}
		ManualTattooMIDs.Add(TattooId, IsolatedMID);
	}
	const bool bStartingEditSession = !ManualTattooEditSnapshots.Contains(TattooId);
	if (bStartingEditSession)
	{
		const int32 LayerOrder = ManualTattooLayerOrders.FindRef(TattooId);
		ManualTattooEditSnapshots.Add(TattooId, CaptureTattooParameters(IsolatedMID, LayerOrder));
	}
	ActiveManualTattooId = TattooId;
	OutTattooId = TattooId;

	if (UUserWidget* Widget = FindLiveTattooCustomizationWidget())
	{
		BindCustomizationWidgetToMID(Widget, IsolatedMID, bStartingEditSession);
	}
	return IsolatedMID;
}

bool UProjectTattooShopInputSubsystem::Preview(const FGuid& TattooId, const FProjectTattooParameters& Parameters)
{
	if (UsesSkinnedDecalTattooShop())
	{
		return PreviewTattoo(TattooId, Parameters);
	}
	UMaterialInstanceDynamic* MID = ManualTattooMIDs.FindRef(TattooId);
	if (!MID)
	{
		return false;
	}
	bool bMissingRuntimeFile = false;
	UTexture* Texture = ResolvePersistedTattooTexture(Parameters, bMissingRuntimeFile);
	ApplyTattooParameters(MID, Parameters, Texture);
	FProjectTattooParameters PreviewParameters = Parameters;
	PreviewParameters.bEnabled = PreviewParameters.bEnabled && !bMissingRuntimeFile;
	ManualTattooPreviewParameters.Add(TattooId, PreviewParameters);
	ManualTattooLayerOrders.Add(TattooId, Parameters.LayerOrder);
	RehydrateManualTattoos();
	return true;
}

bool UProjectTattooShopInputSubsystem::Commit(const FGuid& TattooId)
{
	if (UsesSkinnedDecalTattooShop())
	{
		return CommitTattoo(TattooId);
	}
	UMaterialInstanceDynamic* MID = ManualTattooMIDs.FindRef(TattooId);
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectTattooShopStateSubsystem* State = GameInstance ? GameInstance->GetSubsystem<UProjectTattooShopStateSubsystem>() : nullptr;
	if (!MID || !State)
	{
		return false;
	}

	FProjectTattooRecord Record;
	Record.TattooId = TattooId;
	Record.Parameters = CaptureTattooParameters(MID, ManualTattooLayerOrders.FindRef(TattooId));
	Record.Parameters.bEnabled = Record.Parameters.TextureAssetPath.IsValid()
		|| !Record.Parameters.RuntimeTextureId.IsEmpty();
	if (!Record.Parameters.bEnabled)
	{
		return false;
	}
	State->UpsertRecord(Record, true);
	ManualTattooPreviewParameters.Remove(TattooId);
	ManualTattooEditSnapshots.Remove(TattooId);
	RehydrateManualTattoos();
	return true;
}

bool UProjectTattooShopInputSubsystem::Cancel(const FGuid& TattooId)
{
	if (UsesSkinnedDecalTattooShop())
	{
		return CancelTattoo(TattooId);
	}
	const FProjectTattooParameters* Snapshot = ManualTattooEditSnapshots.Find(TattooId);
	if (!Snapshot)
	{
		return false;
	}
	const FProjectTattooParameters Restored = *Snapshot;
	UMaterialInstanceDynamic* MID = ManualTattooMIDs.FindRef(TattooId);
	bool bMissingRuntimeFile = false;
	UTexture* Texture = ResolvePersistedTattooTexture(Restored, bMissingRuntimeFile);
	ApplyTattooParameters(MID, Restored, Texture);
	ManualTattooPreviewParameters.Remove(TattooId);
	ManualTattooEditSnapshots.Remove(TattooId);
	RehydrateManualTattoos();
	return MID != nullptr;
}

bool UProjectTattooShopInputSubsystem::Delete(const FGuid& TattooId)
{
	if (UsesSkinnedDecalTattooShop())
	{
		return DeleteTattoo(TattooId);
	}
	if (USkeletalMeshComponent* Component = ManualTattooComponents.FindRef(TattooId))
	{
		Component->SetVisibility(false, true);
		Component->DestroyComponent();
	}
	ManualTattooComponents.Remove(TattooId);
	ManualTattooMIDs.Remove(TattooId);
	ManualTattooEditSnapshots.Remove(TattooId);
	ManualTattooPreviewParameters.Remove(TattooId);
	ManualTattooLayerOrders.Remove(TattooId);
	if (ActiveManualTattooId == TattooId)
	{
		ActiveManualTattooId.Invalidate();
	}
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectTattooShopStateSubsystem* State = GameInstance ? GameInstance->GetSubsystem<UProjectTattooShopStateSubsystem>() : nullptr;
	const bool bRemoved = State ? State->RemoveRecord(TattooId, true) : false;
	RehydrateManualTattoos();
	return bRemoved;
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

	const float ManualScale = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_SCALE"), 0.58f);
	const float ManualScaleY = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_SCALE_Y"), ManualScale);
	const float ManualOffsetX = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_OFFSET_X"), 0.0f);
	const float ManualOffsetY = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_OFFSET_Y"), 0.478f);
	const float ManualOffsetZ = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_OFFSET_Z"), 0.3f);
	const float ManualWorldOffset = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_WORLD_OFFSET"), 0.5f);
	const float ManualDepthMin = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_DEPTH_MIN"), -4.0f);
	const float ManualDepthMax = ProjectTattooShopInputPrivate::ReadFloatEnvironmentVariable(TEXT("PROJECT_MANUAL_TATTOO_DEPTH_MAX"), 4.0f);

	FGuid AutomationTattooId;
	UMaterialInstanceDynamic* IsolatedAutomationMID = BeginEdit(nullptr, INDEX_NONE, AutomationTattooId);
	if (!IsolatedAutomationMID || !AutomationTattooId.IsValid())
	{
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("ApplyTattooTextureForAutomation failed: isolated tattoo instance could not be created."));
		return false;
	}
	IsolatedAutomationMID->SetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName, TattooTexture);
	IsolatedAutomationMID->SetVectorParameterValue(ProjectTattooShopInputPrivate::ColorParameterName, FLinearColor(1.0f, 0.539349f, 0.0f, 1.0f));
	IsolatedAutomationMID->SetVectorParameterValue(ProjectTattooShopInputPrivate::EmissiveColorParameterName, FLinearColor::White);
	IsolatedAutomationMID->SetVectorParameterValue(ProjectTattooShopInputPrivate::OffsetParameterName, FLinearColor(ManualOffsetX, ManualOffsetY, ManualOffsetZ, 1.0f));
	IsolatedAutomationMID->SetVectorParameterValue(FName(TEXT("ProjectionDirection")), FLinearColor(0.0f, 1.0f, 0.0f, -1.0f));
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("DepthMin")), ManualDepthMin);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("DepthMax")), ManualDepthMax);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("Scale")), ManualScale);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("ScaleY")), ManualScaleY);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("Rotation")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("EmissionStrength")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("Metallic")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("Roughness")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("Specular")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("UseT2T")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("TintBaseColor")), 1.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("UseBaseColorForEmissive")), 1.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("UseTexCoord")), 0.0f);
	ProjectTattooShopInputPrivate::SetScalarIfPresent(IsolatedAutomationMID, FName(TEXT("WorldPositionOffset")), ManualWorldOffset);
	Commit(AutomationTattooId);
	SynchronizeTattooOverlayToVisibleSkin();
	UE_LOG(
		LogProjectTattooShopInput,
		Display,
		TEXT("ApplyTattooTextureForAutomation applied isolated tattoo %s (%s)."),
		*AutomationTattooId.ToString(EGuidFormats::DigitsWithHyphens),
		*TattooTexture->GetPathName());
	return true;
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
	ResetManualTattooRuntime();
	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedTattooShopWidget = nullptr;
	TrackedAssetPreviewWidget = nullptr;
	bRuntimeTattooCardsInitialized = false;
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

	ResetManualTattooRuntime();
	TrackedPlayerPawn = NewPawn;
	if (!UsesSkinnedDecalTattooShop())
	{
		TrackedTattooShopWidget = nullptr;
		TrackedAssetPreviewWidget = nullptr;
	}
	bRuntimeTattooCardsInitialized = false;
	RuntimeTattooCustomizationWidget = nullptr;
	RuntimeTattooMID = nullptr;
	LastRuntimeTattooTexture = nullptr;
	RuntimeTattooBaseComponent = nullptr;
	ClearMirroredTattooOverlay();
	if (UsesSkinnedDecalTattooShop())
	{
		LastSkinnedTattooSynchronizedPawn.Reset();
		RemoveLegacyTattooRuntimeArtifacts(NewPawn);
		RequestManualTattooSynchronization(true);
	}
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
	if (UsesSkinnedDecalTattooShop())
	{
		return EnsureProjectTattooLibraryWidget(PlayerController);
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

UProjectTattooShopLibraryWidget* UProjectTattooShopInputSubsystem::EnsureProjectTattooLibraryWidget(
	APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return nullptr;
	}
	if (IsValid(TrackedProjectTattooLibraryWidget))
	{
		return TrackedProjectTattooLibraryWidget;
	}

	const UProjectTattooShopSettings* Settings = UProjectTattooShopSettings::Get();
	FSoftClassPath ConfiguredClass;
	if (Settings && !Settings->LibraryWidgetClass.IsNull())
	{
		ConfiguredClass = FSoftClassPath(Settings->LibraryWidgetClass.ToSoftObjectPath().ToString());
	}
	else
	{
		ConfiguredClass = FSoftClassPath(ProjectTattooShopInputPrivate::ProjectTattooLibraryWidgetPath);
	}
	const TSubclassOf<UProjectTattooShopLibraryWidget> WidgetClass =
		ProjectWidgetClassResolver::ResolveWidgetClassWithPriority<UProjectTattooShopLibraryWidget>(
			ConfiguredClass,
			TEXT("ProjectTattooShopLibrary"),
			TEXT("TattooShop"));
	TrackedProjectTattooLibraryWidget = CreateWidget<UProjectTattooShopLibraryWidget>(
		PlayerController,
		WidgetClass ? WidgetClass.Get() : UProjectTattooShopLibraryWidget::StaticClass(),
		TEXT("ProjectTattooLibraryManagement"));
	if (TrackedProjectTattooLibraryWidget)
	{
		TrackedProjectTattooLibraryWidget->SetPresentationMode(EProjectTattooShopLibraryPresentationMode::Management);
		TrackedTattooShopWidget = TrackedProjectTattooLibraryWidget;
	}
	return TrackedProjectTattooLibraryWidget;
}

UProjectTattooShopLibraryWidget* UProjectTattooShopInputSubsystem::EnsureProjectTattooCatalogWidget(
	APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return nullptr;
	}
	if (IsValid(TrackedProjectTattooCatalogWidget))
	{
		return TrackedProjectTattooCatalogWidget;
	}
	UProjectTattooShopLibraryWidget* ManagementWidget = EnsureProjectTattooLibraryWidget(PlayerController);
	UClass* WidgetClass = ManagementWidget ? ManagementWidget->GetClass() : UProjectTattooShopLibraryWidget::StaticClass();
	TrackedProjectTattooCatalogWidget = CreateWidget<UProjectTattooShopLibraryWidget>(
		PlayerController,
		WidgetClass,
		TEXT("ProjectTattooLibraryCatalog"));
	if (TrackedProjectTattooCatalogWidget)
	{
		TrackedProjectTattooCatalogWidget->SetPresentationMode(EProjectTattooShopLibraryPresentationMode::Catalog);
	}
	return TrackedProjectTattooCatalogWidget;
}

UProjectTattooShopEditorWidget* UProjectTattooShopInputSubsystem::EnsureProjectTattooEditorWidget(
	APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return nullptr;
	}
	if (IsValid(TrackedProjectTattooEditorWidget))
	{
		return TrackedProjectTattooEditorWidget;
	}

	const UProjectTattooShopSettings* Settings = UProjectTattooShopSettings::Get();
	FSoftClassPath ConfiguredClass;
	if (Settings && !Settings->EditorWidgetClass.IsNull())
	{
		ConfiguredClass = FSoftClassPath(Settings->EditorWidgetClass.ToSoftObjectPath().ToString());
	}
	else
	{
		ConfiguredClass = FSoftClassPath(ProjectTattooShopInputPrivate::ProjectTattooEditorWidgetPath);
	}
	const TSubclassOf<UProjectTattooShopEditorWidget> WidgetClass =
		ProjectWidgetClassResolver::ResolveWidgetClassWithPriority<UProjectTattooShopEditorWidget>(
			ConfiguredClass,
			TEXT("ProjectTattooEditor"),
			TEXT("TattooShop"));
	TrackedProjectTattooEditorWidget = CreateWidget<UProjectTattooShopEditorWidget>(
		PlayerController,
		WidgetClass ? WidgetClass.Get() : UProjectTattooShopEditorWidget::StaticClass(),
		TEXT("ProjectTattooEditor"));
	return TrackedProjectTattooEditorWidget;
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
	bRuntimeTattooCardsInitialized = RefreshRuntimeTattooCards(PreviewWidget);
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

void UProjectTattooShopInputSubsystem::ResetManualTattooRuntime()
{
	for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ManualTattooComponents)
	{
		if (USkeletalMeshComponent* Component = Pair.Value.Get())
		{
			Component->DestroyComponent();
		}
	}
	ManualTattooComponents.Reset();
	ManualTattooMIDs.Reset();
	ManualTattooEditSnapshots.Reset();
	ManualTattooPreviewParameters.Reset();
	ManualTattooLayerOrders.Reset();
	LastManualTattooTargetSkin = nullptr;
	ActiveManualTattooId.Invalidate();
}

void UProjectTattooShopInputSubsystem::DisableLegacyTattooMeshComponents(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> Components(Pawn);
	for (USkeletalMeshComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		const bool bProjectManual = Component->ComponentTags.Contains(ProjectTattooShopInputPrivate::ManualTattooComponentTag)
			|| Component->GetName().StartsWith(TEXT("ProjectManualTattoo_"));
		if (bProjectManual)
		{
			Component->SetOverlayMaterial(nullptr);
			Component->SetHiddenInGame(true, true);
			Component->SetVisibility(false, true);
			Component->DestroyComponent();
		}
	}
	ManualTattooComponents.Reset();
}

UMaterialInstanceDynamic* UProjectTattooShopInputSubsystem::CreateIsolatedTattooMID(
	APawn* Pawn,
	UMaterialInterface* SourceMaterial) const
{
	if (!Pawn || !SourceMaterial)
	{
		return nullptr;
	}

	// UE does not permit a MID to be the parent of another MID.  The legacy
	// TattooShop passes its current dynamic instance here, so unwrap only the
	// dynamic part of the chain and copy its overrides after creation.
	while (const UMaterialInstanceDynamic* DynamicSource = Cast<UMaterialInstanceDynamic>(SourceMaterial))
	{
		UMaterialInterface* StableParent = DynamicSource->Parent;
		if (!StableParent || StableParent == SourceMaterial)
		{
			return nullptr;
		}
		SourceMaterial = StableParent;
	}
	return UMaterialInstanceDynamic::Create(SourceMaterial, Pawn);
}

USkeletalMeshComponent* UProjectTattooShopInputSubsystem::CreateManualTattooComponent(
	APawn* Pawn,
	USkeletalMeshComponent* TargetSkin,
	const FGuid& TattooId)
{
	if (!Pawn || !TargetSkin || !TattooId.IsValid())
	{
		return nullptr;
	}

	const FName ComponentName(*FString::Printf(TEXT("ProjectManualTattoo_%s"), *TattooId.ToString(EGuidFormats::Digits)));
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Pawn, USkeletalMeshComponent::StaticClass(), ComponentName);
	if (!Component)
	{
		return nullptr;
	}

	Pawn->AddInstanceComponent(Component);
	Component->ComponentTags.AddUnique(ProjectTattooShopInputPrivate::ManualTattooComponentTag);
	Component->SetSkeletalMeshAsset(TargetSkin->GetSkeletalMeshAsset());
	Component->SetupAttachment(TargetSkin);
	Component->RegisterComponent();
	Component->AttachToComponent(TargetSkin, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Component->SetRelativeTransform(FTransform::Identity);
	// Match BP_TSChar's SnapToTarget follower transform exactly. The tattoo
	// material already supplies its own small world-position offset.
	Component->SetRelativeScale3D(FVector::OneVector);
	if (Component->GetSkeletalMeshAsset()
		&& TargetSkin->GetSkeletalMeshAsset()
		&& Component->GetSkeletalMeshAsset()->GetSkeleton() == TargetSkin->GetSkeletalMeshAsset()->GetSkeleton())
	{
		Component->SetLeaderPoseComponent(TargetSkin);
	}
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCastShadow(false);
	Component->SetBoundsScale(1.1f);
	Component->TranslucencySortPriority = ManualTattooComponents.Num();
	// TatBaseComp belongs to the vendor Blueprint pipeline.  Adding native
	// layers to it lets its BPI events repaint every layer with the newest MID.
	// Native GUID layers are deliberately kept outside that collection.
	return Component;
}

bool UProjectTattooShopInputSubsystem::ConfigureManualTattooMaterials(
	USkeletalMeshComponent* Component,
	UMaterialInstanceDynamic* TattooMID) const
{
	if (!Component || !TattooMID)
	{
		return false;
	}

	// BP_TSChar's ForceUseOverlaySlot contract keeps the full duplicate mesh
	// invisible with M_Translucent and renders the tattoo MID only through the
	// overlay pass. Assigning M_TattooShop to every skin/eye/mouth slot paints
	// every UV island and produces the large coloured body patches.
	UMaterialInterface* TransparentBase = LoadObject<UMaterialInterface>(
		nullptr,
		ProjectTattooShopInputPrivate::TattooTransparentBaseMaterialPath);
	if (!TransparentBase)
	{
		UE_LOG(
			LogProjectTattooShopInput,
			Error,
			TEXT("[TattooShop] Cannot configure isolated tattoo component %s: missing transparent base %s."),
			*GetNameSafe(Component),
			ProjectTattooShopInputPrivate::TattooTransparentBaseMaterialPath);
		Component->SetOverlayMaterial(nullptr);
		Component->SetVisibility(false, true);
		Component->SetHiddenInGame(true, true);
		return false;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < Component->GetNumMaterials(); ++MaterialIndex)
	{
		Component->SetMaterial(MaterialIndex, TransparentBase);
	}
	Component->SetOverlayMaterial(TattooMID);
	Component->MarkRenderStateDirty();
	return true;
}

FGuid UProjectTattooShopInputSubsystem::FindTattooIdForMID(const UMaterialInstanceDynamic* MID) const
{
	if (!MID)
	{
		return FGuid();
	}
	for (const TPair<FGuid, TObjectPtr<UMaterialInstanceDynamic>>& Pair : ManualTattooMIDs)
	{
		if (Pair.Value == MID)
		{
			return Pair.Key;
		}
	}
	return FGuid();
}

FGuid UProjectTattooShopInputSubsystem::FindTattooIdForComponent(const USkeletalMeshComponent* Component) const
{
	if (!Component)
	{
		return FGuid();
	}
	for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ManualTattooComponents)
	{
		if (Pair.Value == Component)
		{
			return Pair.Key;
		}
	}
	return FGuid();
}

FProjectTattooParameters UProjectTattooShopInputSubsystem::CaptureTattooParameters(
	UMaterialInstanceDynamic* MID,
	const int32 LayerOrder) const
{
	FProjectTattooParameters Parameters;
	Parameters.LayerOrder = LayerOrder;
	if (!MID)
	{
		return Parameters;
	}

	for (const FName ParameterName : ProjectTattooShopInputPrivate::PersistedScalarParameterNames())
	{
		float Value = 0.0f;
		if (MID->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), Value))
		{
			Parameters.ScalarParameters.Add(ParameterName, Value);
		}
	}
	for (const FName ParameterName : ProjectTattooShopInputPrivate::PersistedVectorParameterNames())
	{
		FLinearColor Value = FLinearColor::White;
		if (MID->GetVectorParameterValue(FHashedMaterialParameterInfo(ParameterName), Value))
		{
			Parameters.VectorParameters.Add(ParameterName, Value);
		}
	}

	Parameters.Color = Parameters.VectorParameters.FindRef(ProjectTattooShopInputPrivate::ColorParameterName);
	if (!Parameters.VectorParameters.Contains(ProjectTattooShopInputPrivate::ColorParameterName))
	{
		Parameters.Color = FLinearColor::White;
	}
	Parameters.Opacity = Parameters.ScalarParameters.Contains(ProjectTattooShopInputPrivate::OpacityParameterName)
		? FMath::Clamp(Parameters.ScalarParameters.FindRef(ProjectTattooShopInputPrivate::OpacityParameterName), 0.0f, 1.0f)
		: 1.0f;
	const float CapturedScaleX = Parameters.ScalarParameters.Contains(ProjectTattooShopInputPrivate::ScaleParameterName)
		? Parameters.ScalarParameters.FindRef(ProjectTattooShopInputPrivate::ScaleParameterName)
		: 1.0f;
	const float CapturedScaleY = Parameters.ScalarParameters.Contains(ProjectTattooShopInputPrivate::ScaleYParameterName)
		? Parameters.ScalarParameters.FindRef(ProjectTattooShopInputPrivate::ScaleYParameterName)
		: CapturedScaleX;
	Parameters.Scale = FVector2D(
		FMath::IsNearlyZero(CapturedScaleX) ? 1.0f : CapturedScaleX,
		FMath::IsNearlyZero(CapturedScaleY) ? 1.0f : CapturedScaleY);
	Parameters.Offset = Parameters.VectorParameters.Contains(ProjectTattooShopInputPrivate::OffsetParameterName)
		? Parameters.VectorParameters.FindRef(ProjectTattooShopInputPrivate::OffsetParameterName)
		: FLinearColor(0.0f, 0.5f, 0.0f, 1.0f);
	Parameters.Rotation = Parameters.ScalarParameters.Contains(ProjectTattooShopInputPrivate::RotationParameterName)
		? Parameters.ScalarParameters.FindRef(ProjectTattooShopInputPrivate::RotationParameterName)
		: 0.0f;

	if (UTexture* Texture = MID->K2_GetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName))
	{
		const FString TexturePath = Texture->GetPathName();
		if (TexturePath.StartsWith(TEXT("/Game/")))
		{
			Parameters.TextureAssetPath = FSoftObjectPath(TexturePath);
		}
		else
		{
			FString RuntimeFile;
			if (FindRuntimeTattooFileForTexture(Cast<UTexture2D>(Texture), RuntimeFile))
			{
				Parameters.RuntimeTextureId = FPaths::GetCleanFilename(RuntimeFile);
			}
		}
	}
	return Parameters;
}

void UProjectTattooShopInputSubsystem::ApplyTattooParameters(
	UMaterialInstanceDynamic* MID,
	const FProjectTattooParameters& Parameters,
	UTexture* ResolvedTexture) const
{
	if (!MID)
	{
		return;
	}
	if (ResolvedTexture)
	{
		MID->SetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName, ResolvedTexture);
	}
	for (const TPair<FName, float>& Pair : Parameters.ScalarParameters)
	{
		MID->SetScalarParameterValue(Pair.Key, Pair.Value);
	}
	for (const TPair<FName, FLinearColor>& Pair : Parameters.VectorParameters)
	{
		MID->SetVectorParameterValue(Pair.Key, Pair.Value);
	}
	MID->SetVectorParameterValue(ProjectTattooShopInputPrivate::ColorParameterName, Parameters.Color);
	MID->SetScalarParameterValue(ProjectTattooShopInputPrivate::OpacityParameterName, Parameters.Opacity);
	MID->SetScalarParameterValue(ProjectTattooShopInputPrivate::ScaleParameterName, Parameters.Scale.X);
	MID->SetScalarParameterValue(ProjectTattooShopInputPrivate::ScaleYParameterName, Parameters.Scale.Y);
	MID->SetVectorParameterValue(ProjectTattooShopInputPrivate::OffsetParameterName, Parameters.Offset);
	MID->SetScalarParameterValue(ProjectTattooShopInputPrivate::RotationParameterName, Parameters.Rotation);
}

UTexture* UProjectTattooShopInputSubsystem::ResolvePersistedTattooTexture(
	const FProjectTattooParameters& Parameters,
	bool& bOutMissingRuntimeFile)
{
	bOutMissingRuntimeFile = false;
	if (Parameters.TextureAssetPath.IsValid())
	{
		return Cast<UTexture>(Parameters.TextureAssetPath.TryLoad());
	}
	if (Parameters.RuntimeTextureId.IsEmpty())
	{
		return nullptr;
	}

	// Never accept a persisted directory traversal or absolute path.
	const FString SafeId = FPaths::GetCleanFilename(Parameters.RuntimeTextureId);
	if (SafeId != Parameters.RuntimeTextureId)
	{
		bOutMissingRuntimeFile = true;
		return nullptr;
	}
	const FString FilePath = FPaths::Combine(GetRuntimeTattooTextureDirectory(), SafeId);
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		bOutMissingRuntimeFile = true;
		UE_LOG(LogProjectTattooShopInput, Warning, TEXT("[TattooShop] Disabled only tattoo with missing runtime texture %s."), *SafeId);
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Cached = RuntimeTattooTextureCache.Find(NormalizeTattooFilePath(FilePath)))
	{
		return Cached->Get();
	}
	UTexture2D* Loaded = LoadPngTextureFromFile(FilePath, FString::Printf(TEXT("RuntimeTattoo_%s"), *FPaths::GetBaseFilename(SafeId)));
	if (Loaded)
	{
		RuntimeTattooTextureCache.Add(NormalizeTattooFilePath(FilePath), Loaded);
	}
	else
	{
		bOutMissingRuntimeFile = true;
	}
	return Loaded;
}

void UProjectTattooShopInputSubsystem::BindCustomizationWidgetToMID(
	UUserWidget* Widget,
	UMaterialInstanceDynamic* MID,
	const bool bSetPreEditSnapshot)
{
	if (!Widget || !MID)
	{
		return;
	}
	ProjectTattooShopInputPrivate::SetObjectPropertyWhenCompatible(Widget, ProjectTattooShopInputPrivate::SelectedMIDPropertyName, MID, false);
	ProjectTattooShopInputPrivate::SetObjectPropertyWhenCompatible(Widget, ProjectTattooShopInputPrivate::TargetMIDPropertyName, MID, false);
	// WBP_TattooCustomization was authored to call BP_TSChar BPI events.  The
	// project adapter captures selection and control values directly, so leaving
	// this child bound to the pawn would create a second legacy MID in parallel.
	// The parent previewer is also cleared by CaptureAssetPreviewWidget.
	ProjectTattooShopInputPrivate::ClearObjectPropertyWhenCompatible(Widget, ProjectTattooShopInputPrivate::ActorRefPropertyName);
	if (bSetPreEditSnapshot)
	{
		UMaterialInstanceDynamic* SnapshotMID = CreateIsolatedTattooMID(TrackedPlayerPawn.Get(), MID);
		if (SnapshotMID)
		{
			SnapshotMID->CopyInterpParameters(MID);
			ProjectTattooShopInputPrivate::SetObjectPropertyWhenCompatible(Widget, ProjectTattooShopInputPrivate::PreEditMIDPropertyName, SnapshotMID, false);
		}
	}
}

bool UProjectTattooShopInputSubsystem::InvokeCustomizationMaterialEvent(
	UUserWidget* Widget,
	const FName EventName,
	UMaterialInstanceDynamic* MID) const
{
	if (!IsValid(Widget) || !IsValid(MID))
	{
		return false;
	}

	UFunction* Function = Widget->FindFunction(EventName);
	if (!Function || Function->ParmsSize <= 0)
	{
		return false;
	}

	TArray<uint8> Parameters;
	Parameters.SetNumZeroed(Function->ParmsSize);
	bool bAssignedMaterial = false;
	for (TFieldIterator<FObjectPropertyBase> It(Function); It; ++It)
	{
		FObjectPropertyBase* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}
		if (!Property->PropertyClass || MID->IsA(Property->PropertyClass))
		{
			Property->SetObjectPropertyValue_InContainer(Parameters.GetData(), MID);
			bAssignedMaterial = true;
			break;
		}
	}
	if (!bAssignedMaterial)
	{
		return false;
	}

	Widget->ProcessEvent(Function, Parameters.GetData());
	return true;
}

void UProjectTattooShopInputSubsystem::BindTattooCustomizationRuntimeButtons(UUserWidget* AssetPreviewWidget)
{
	// Accept/Cancel are owned by WBP_AssetPreviewer, not by its nested
	// WBP_TattooCustomization controls. Binding the child silently missed the
	// actual click, leaving ActiveManualTattooId alive after Accept. The next
	// Add then reused the first tattoo's MID. Bind the preview owner first and
	// retain the child fallback for older TattooShop layouts.
	UUserWidget* ButtonOwner = IsValid(AssetPreviewWidget)
		? AssetPreviewWidget
		: FindLiveTattooCustomizationWidget();
	if (!IsValid(ButtonOwner) || !ButtonOwner->WidgetTree)
	{
		return;
	}

	auto BindButton = [ButtonOwner, this](const FName ButtonName, const bool bAccept)
	{
		UButton* Button = Cast<UButton>(ButtonOwner->WidgetTree->FindWidget(ButtonName));
		if (!Button)
		{
			return;
		}
		if (bAccept)
		{
			Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleTattooCustomizationAcceptClicked);
		}
		else
		{
			Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleTattooCustomizationCancelClicked);
		}
	};
	BindButton(ProjectTattooShopInputPrivate::AcceptButtonName, true);
	BindButton(ProjectTattooShopInputPrivate::CancelButtonName, false);
}

void UProjectTattooShopInputSubsystem::HandleTattooCustomizationAcceptClicked()
{
	if (ActiveManualTattooId.IsValid())
	{
		const FGuid TattooId = ActiveManualTattooId;
		if (Commit(TattooId))
		{
			ActiveManualTattooId.Invalidate();
			RuntimeTattooMID = nullptr;
		}
	}
}

void UProjectTattooShopInputSubsystem::HandleTattooCustomizationCancelClicked()
{
	if (ActiveManualTattooId.IsValid())
	{
		const FGuid TattooId = ActiveManualTattooId;
		if (Cancel(TattooId))
		{
			ActiveManualTattooId.Invalidate();
			RuntimeTattooMID = nullptr;
		}
	}
}

void UProjectTattooShopInputSubsystem::RehydrateManualTattoos()
{
	APawn* Pawn = TrackedPlayerPawn.Get();
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UProjectTattooShopStateSubsystem* State = GameInstance ? GameInstance->GetSubsystem<UProjectTattooShopStateSubsystem>() : nullptr;
	if (!CanUseTattooShopPawn(Pawn) || !State)
	{
		return;
	}
	if (UsesSkinnedDecalTattooShop())
	{
		RequestManualTattooSynchronization(true);
		return;
	}

	TArray<FProjectTattooRecord> EffectiveRecords = State->GetRecords();
	for (const TPair<FGuid, FProjectTattooParameters>& PreviewPair : ManualTattooPreviewParameters)
	{
		FProjectTattooRecord* Existing = EffectiveRecords.FindByPredicate([&PreviewPair](const FProjectTattooRecord& Record)
		{
			return Record.TattooId == PreviewPair.Key;
		});
		if (!Existing)
		{
			Existing = &EffectiveRecords.AddDefaulted_GetRef();
			Existing->TattooId = PreviewPair.Key;
		}
		Existing->Parameters = PreviewPair.Value;
	}

	TSet<USkeletalMeshComponent*> TattooBaseSet;
	for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ManualTattooComponents)
	{
		if (IsValid(Pair.Value))
		{
			TattooBaseSet.Add(Pair.Value);
		}
	}
	USkeletalMeshComponent* TargetSkin = ResolveVisibleSkinComponent(Pawn, TattooBaseSet);
	if (!IsValid(TargetSkin))
	{
		return;
	}
	if (!EffectiveRecords.IsEmpty() || !ManualTattooPreviewParameters.IsEmpty())
	{
		// Do this before native layers are refreshed.  It removes only legacy
		// tattoo followers, never the visible body or automatic SkinnedDecal.
		SuppressLegacyTattooBaseComponents(Pawn, TargetSkin);
	}

	// Manual TattooShop tattoos use their original tri-planar material on one
	// transparent leader-pose mesh per GUID. This keeps the shader's position,
	// scale and rotation identical on HUB, StorySelection and every other map.
	// SkinnedDecal remains exclusively responsible for the automatic tattoo.
	TSet<FGuid> DesiredTattooIds;
	for (FProjectTattooRecord& Record : EffectiveRecords)
	{
		if (!Record.TattooId.IsValid())
		{
			continue;
		}
		bool bMissingRuntimeFile = false;
		UTexture* Texture = ResolvePersistedTattooTexture(Record.Parameters, bMissingRuntimeFile);
		if (bMissingRuntimeFile || !Texture)
		{
			Record.Parameters.bEnabled = false;
			continue;
		}
		if (!Record.Parameters.bEnabled)
		{
			continue;
		}

		UMaterialInstanceDynamic* MID = ManualTattooMIDs.FindRef(Record.TattooId);
		if (!IsValid(MID))
		{
			UMaterialInterface* MasterMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				ProjectTattooShopInputPrivate::TattooShopMaterialPath);
			MID = CreateIsolatedTattooMID(Pawn, MasterMaterial);
			if (!IsValid(MID))
			{
				continue;
			}
			ManualTattooMIDs.Add(Record.TattooId, MID);
		}
		ApplyTattooParameters(MID, Record.Parameters, Texture);
		ManualTattooLayerOrders.Add(Record.TattooId, Record.Parameters.LayerOrder);

		USkeletalMeshComponent* Component = ManualTattooComponents.FindRef(Record.TattooId);
		if (!IsValid(Component)
			|| Component->GetOwner() != Pawn
			|| Component->GetSkeletalMeshAsset() != TargetSkin->GetSkeletalMeshAsset())
		{
			if (IsValid(Component))
			{
				Component->DestroyComponent();
			}
			Component = CreateManualTattooComponent(Pawn, TargetSkin, Record.TattooId);
			ManualTattooComponents.Add(Record.TattooId, Component);
		}
	if (ConfigureManualTattooMaterials(Component, MID))
	{
		Component->TranslucencySortPriority = Record.Parameters.LayerOrder;
		Component->SetHiddenInGame(false, true);
		Component->SetVisibility(true, true);
		DesiredTattooIds.Add(Record.TattooId);
		UE_LOG(
			LogProjectTattooShopInput,
			Verbose,
			TEXT("[TattooShop] Rehydrated manual layer %s on %s using original material space scale=(%.3f,%.3f) offset=(%.3f,%.3f)."),
			*Record.TattooId.ToString(EGuidFormats::Digits),
			*GetNameSafe(TargetSkin),
			Record.Parameters.Scale.X,
			Record.Parameters.Scale.Y,
			Record.Parameters.Offset.R,
			Record.Parameters.Offset.G);
	}
	}

	for (auto It = ManualTattooComponents.CreateIterator(); It; ++It)
	{
		if (!DesiredTattooIds.Contains(It.Key()))
		{
			if (USkeletalMeshComponent* Component = It.Value().Get())
			{
				Component->SetOverlayMaterial(nullptr);
				Component->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
	LastManualTattooTargetSkin = TargetSkin;

	if (UProjectDefaultTattooSkinnedDecalSubsystem* SkinnedDecalSubsystem = GetWorld()->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>())
	{
		SkinnedDecalSubsystem->SynchronizeManualTattoos(Pawn, TArray<FProjectTattooRecord>(), TMap<FGuid, UTexture2D*>());
	}
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

	UUserWidget* AssetPreviewWidget = TrackedAssetPreviewWidget.Get();
	if (!IsValid(AssetPreviewWidget) || !AssetPreviewWidget->IsVisible())
	{
		return;
	}

	UUserWidget* CustomizationWidget = FindLiveTattooCustomizationWidget();
	if (!CustomizationWidget)
	{
		return;
	}
	// WBP_TattooCustomization exists in the nested preview tree even while the
	// selection panel is collapsed. Do not manufacture an edit session until the
	// user actually pressed Add/Edit and the panel became visible.
	if (!CustomizationWidget->IsVisible())
	{
		return;
	}

	FObjectPropertyBase* SelectedMIDProperty = ProjectTattooShopInputPrivate::FindObjectPropertyByCompatibleName(
		CustomizationWidget->GetClass(),
		ProjectTattooShopInputPrivate::SelectedMIDPropertyName);
	if (!SelectedMIDProperty)
	{
		return;
	}

	UMaterialInstanceDynamic* CandidateMID = Cast<UMaterialInstanceDynamic>(
		SelectedMIDProperty->GetObjectPropertyValue_InContainer(CustomizationWidget));
	bool bEditMode = false;
	if (FBoolProperty* EditModeProperty = CastField<FBoolProperty>(
		ProjectTattooShopInputPrivate::FindPropertyByCompatibleName(
			CustomizationWidget->GetClass(),
			ProjectTattooShopInputPrivate::EditModePropertyName)))
	{
		bEditMode = EditModeProperty->GetPropertyValue_InContainer(CustomizationWidget);
	}
	int32 TargetTattooIndex = INDEX_NONE;
	if (bEditMode)
	{
		if (FIntProperty* TargetIndexProperty = CastField<FIntProperty>(
			ProjectTattooShopInputPrivate::FindPropertyByCompatibleName(
				CustomizationWidget->GetClass(),
				ProjectTattooShopInputPrivate::TargetTatIndexPropertyName)))
		{
			TargetTattooIndex = TargetIndexProperty->GetPropertyValue_InContainer(CustomizationWidget);
		}
	}
	else if (!ActiveManualTattooId.IsValid())
	{
		// The reusable widget retains SelectedMID after Accept/Cancel. A fresh Add
		// must create a fresh GUID/MID instead of editing the last accepted tattoo.
		CandidateMID = nullptr;
	}
	const bool bWidgetChanged = RuntimeTattooCustomizationWidget.Get() != CustomizationWidget;
	FGuid TattooId;
	UMaterialInstanceDynamic* DynamicMaterial = BeginEdit(CandidateMID, TargetTattooIndex, TattooId);
	if (!DynamicMaterial)
	{
		return;
	}
	const bool bMIDChanged = RuntimeTattooMID.Get() != DynamicMaterial;
	RuntimeTattooCustomizationWidget = CustomizationWidget;
	RuntimeTattooMID = DynamicMaterial;
	if (bWidgetChanged || bMIDChanged)
	{
		BindCustomizationWidgetToMID(CustomizationWidget, DynamicMaterial, true);
		InvokeCustomizationMaterialEvent(
			CustomizationWidget,
			ProjectTattooShopInputPrivate::GrabAndUpdateUIEventName,
			DynamicMaterial);
	}

	UTexture* SelectedTexture = nullptr;
	if (FObjectPropertyBase* TextureProperty = ProjectTattooShopInputPrivate::FindObjectPropertyByCompatibleName(
		CustomizationWidget->GetClass(),
		ProjectTattooShopInputPrivate::SelectedAssetTexturePropertyName))
	{
		SelectedTexture = Cast<UTexture>(TextureProperty->GetObjectPropertyValue_InContainer(CustomizationWidget));
	}
	// This write is intentionally per-MID.  A global "last selected texture"
	// cache caused a second use of T_Bunny to skip the write entirely, leaving
	// the new layer with the master's default (black) texture.
	if (SelectedTexture
		&& DynamicMaterial->K2_GetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName) != SelectedTexture)
	{
		DynamicMaterial->SetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName, SelectedTexture);
	}
	LastRuntimeTattooTexture = SelectedTexture;
	// On composition-based Player pawns the legacy BPI_UpdateParent call has no
	// BP_TSChar implementation. Pull the live controls into this tattoo's MID so
	// every slider/color picker still previews through the native GUID layer.
	InvokeCustomizationMaterialEvent(
		CustomizationWidget,
		ProjectTattooShopInputPrivate::ApplyParamsEventName,
		DynamicMaterial);
	FProjectTattooParameters PreviewParameters = CaptureTattooParameters(
		DynamicMaterial,
		ManualTattooLayerOrders.FindRef(TattooId));
	PreviewParameters.bEnabled = SelectedTexture != nullptr
		|| DynamicMaterial->K2_GetTextureParameterValue(ProjectTattooShopInputPrivate::TextureParameterName) != nullptr;
	if (SelectedTexture)
	{
		const FString TexturePath = SelectedTexture->GetPathName();
		if (TexturePath.StartsWith(TEXT("/Game/")))
		{
			PreviewParameters.TextureAssetPath = FSoftObjectPath(TexturePath);
		}
	}
	ManualTattooPreviewParameters.Add(TattooId, PreviewParameters);
	RehydrateManualTattoos();
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

void UProjectTattooShopInputSubsystem::OpenSkinnedDecalTattooShop(
	APlayerController* PlayerController,
	APawn* Pawn)
{
	UProjectTattooShopLibraryWidget* ManagementWidget = EnsureProjectTattooLibraryWidget(PlayerController);
	UProjectTattooShopLibraryWidget* CatalogWidget = EnsureProjectTattooCatalogWidget(PlayerController);
	if (!ManagementWidget || !CatalogWidget)
	{
		UE_LOG(LogProjectTattooShopInput, Error, TEXT("[TattooShop] Project-owned Tattoo UI could not be created."));
		return;
	}

	RemoveLegacyTattooRuntimeArtifacts(Pawn);
	ManagementWidget->SetPresentationMode(EProjectTattooShopLibraryPresentationMode::Management);
	CatalogWidget->SetPresentationMode(EProjectTattooShopLibraryPresentationMode::Catalog);
	ManagementWidget->SetVisibility(ESlateVisibility::Visible);
	CatalogWidget->SetVisibility(ESlateVisibility::Visible);
	if (IsValid(TattooShopHostPanel))
	{
		MountWidgetInHostedPanel(ManagementWidget, TattooShopHostPanel, true);
		bTattooShopHostedInPanel = true;
	}
	else
	{
		ManagementWidget->AddToViewport(ProjectTattooShopInputPrivate::WidgetZOrder);
		bTattooShopHostedInPanel = false;
	}
	if (IsValid(TattooAssetPreviewHostPanel))
	{
		MountWidgetInHostedPanel(CatalogWidget, TattooAssetPreviewHostPanel, true);
		TrackedAssetPreviewWidget = CatalogWidget;
	}
	else if (!bTattooShopHostedInPanel)
	{
		CatalogWidget->AddToViewport(ProjectTattooShopInputPrivate::WidgetZOrder + 1);
		TrackedAssetPreviewWidget = CatalogWidget;
	}

	bTattooShopOpen = true;
	BindProjectTattooShopUI();
	RefreshProjectTattooShopUI();
	RequestManualTattooSynchronization(true);
	if (!bTattooShopHostedInPanel)
	{
		ApplyTattooShopInputMode(ManagementWidget);
	}

	UE_LOG(
		LogProjectTattooShopInput,
		Display,
		TEXT("[TattooShop] SkinnedDecal Character Creation UI opened for %s. No BP_TSChar, M_TattooShop MID, or follower mesh was instantiated."),
		*GetNameSafe(Pawn));
}

void UProjectTattooShopInputSubsystem::CloseSkinnedDecalTattooShop()
{
	if (ActiveManualTattooId.IsValid())
	{
		CancelTattoo(ActiveManualTattooId);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ManualTattooPreviewTimerHandle);
	}
	bManualTattooPreviewPending = false;
	DismissDeleteTattooTextureMenu();

	for (UUserWidget* Widget : {
		static_cast<UUserWidget*>(TrackedProjectTattooLibraryWidget.Get()),
		static_cast<UUserWidget*>(TrackedProjectTattooCatalogWidget.Get()),
		static_cast<UUserWidget*>(TrackedProjectTattooEditorWidget.Get()) })
	{
		if (IsValid(Widget))
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
			Widget->RemoveFromParent();
		}
	}
	if (IsValid(TattooShopHostPanel))
	{
		TattooShopHostPanel->ClearChildren();
	}
	if (IsValid(TattooAssetPreviewHostPanel))
	{
		TattooAssetPreviewHostPanel->ClearChildren();
	}
	if (!bTattooShopHostedInPanel)
	{
		RestoreTattooShopInputMode();
	}
	TrackedAssetPreviewWidget = nullptr;
	bTattooShopOpen = false;
	bTattooShopHostedInPanel = false;
	UE_LOG(LogProjectTattooShopInput, Log, TEXT("[TattooShop] SkinnedDecal Character Creation UI closed; active draft cancelled."));
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

	if (UsesSkinnedDecalTattooShop())
	{
		OpenSkinnedDecalTattooShop(PlayerController, Pawn);
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
	bTattooShopOpen = true;
	CaptureAssetPreviewWidget();
	BindTattooShopRuntimeButtons(Widget);
	NormalizeTattooCardGrid(Widget);
	if (!bTattooShopHostedInPanel)
	{
		ApplyTattooShopInputMode(Widget);
	}
	bRuntimeTattooCardsInitialized = RefreshRuntimeTattooCards(ResolveTrackedAssetPreviewWidget());

	UE_LOG(
		LogProjectTattooShopInput,
		Log,
		TEXT("[TattooShop] Restored original WBP_TattooShop/WBP_AssetPreviewer presentation for pawn %s. Hosted=%d ShopHost=%s PreviewHost=%s."),
		*GetNameSafe(Pawn),
		bTattooShopHostedInPanel ? 1 : 0,
		*GetNameSafe(TattooShopHostPanel.Get()),
		*GetNameSafe(TattooAssetPreviewHostPanel.Get()));
}

void UProjectTattooShopInputSubsystem::CloseTattooShop()
{
	if (UsesSkinnedDecalTattooShop())
	{
		CloseSkinnedDecalTattooShop();
		return;
	}
	if (ActiveManualTattooId.IsValid())
	{
		// Closing Character Creation is a cancel operation for an edit that has
		// not reached the previewer's Accept button.  Auto-committing here made
		// an accidental tab/Period close persist an incomplete or textureless
		// tattoo.
		Cancel(ActiveManualTattooId);
		ActiveManualTattooId.Invalidate();
	}
	DismissDeleteTattooTextureMenu();
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
	bRuntimeTattooCardsInitialized = false;
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
	// Native manual layers always take precedence over the legacy shared
	// follower/overlay path.  This must happen before the "no active material"
	// fallback below, otherwise it can re-enable a just-suppressed legacy mesh.
	if (!ManualTattooComponents.IsEmpty())
	{
		ClearMirroredTattooOverlay();
		SuppressLegacyTattooBaseComponents(Pawn, TargetSkin);
		return;
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
	for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ManualTattooComponents)
	{
		if (USkeletalMeshComponent* Component = Pair.Value.Get(); Component && Component->GetOwner() == Pawn)
		{
			OutComponents.AddUnique(Component);
		}
	}
}

void UProjectTattooShopInputSubsystem::IsolateLegacyTattooShopWidgets(UUserWidget* RootWidget) const
{
	if (!IsValid(RootWidget))
	{
		return;
	}

	// These widgets are retained as a selector/editor surface.  Do not allow a
	// click inside them to invoke BP_TSChar's BPI add/register/remove flow; the
	// native subsystem supplies the selected texture and owns Commit/Cancel.
	ProjectTattooShopInputPrivate::ClearObjectPropertyWhenCompatible(
		RootWidget,
		ProjectTattooShopInputPrivate::ActorRefPropertyName);
	if (UWidgetTree* Tree = RootWidget->WidgetTree)
	{
		// ForEachWidget already traverses the full nested tree.  Keep this a
		// single pass; recursing into each UUserWidget could revisit the root.
		Tree->ForEachWidget([](UWidget* ChildWidget)
		{
			if (UUserWidget* ChildUserWidget = Cast<UUserWidget>(ChildWidget))
			{
				ProjectTattooShopInputPrivate::ClearObjectPropertyWhenCompatible(
					ChildUserWidget,
					ProjectTattooShopInputPrivate::ActorRefPropertyName);
			}
		});
	}
}

void UProjectTattooShopInputSubsystem::SuppressLegacyTattooBaseComponents(
	APawn* Pawn,
	USkeletalMeshComponent* TargetSkin) const
{
	if (!Pawn)
	{
		return;
	}

	const FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(
		Pawn->GetClass(),
		ProjectTattooShopInputPrivate::TattooBaseComponentsPropertyName);
	const FObjectPropertyBase* InnerObjectProperty = ArrayProperty
		? CastField<FObjectPropertyBase>(ArrayProperty->Inner)
		: nullptr;
	if (!ArrayProperty || !InnerObjectProperty)
	{
		return;
	}

	void* ArrayAddress = ArrayProperty->ContainerPtrToValuePtr<void>(Pawn);
	FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayAddress);
	for (int32 Index = ArrayHelper.Num() - 1; Index >= 0; --Index)
	{
		USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(
			InnerObjectProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index)));
		const bool bNativeManual = IsValid(Component)
			&& Component->ComponentTags.Contains(ProjectTattooShopInputPrivate::ManualTattooComponentTag);
		if (bNativeManual || Component == TargetSkin)
		{
			// A prior build registered native layers in TatBaseComp.  Remove that
			// stale reference so future vendor BPI calls cannot reach their MIDs.
			if (bNativeManual)
			{
				ArrayHelper.RemoveValues(Index, 1);
			}
			continue;
		}

		if (IsValid(Component))
		{
			Component->SetOverlayMaterial(nullptr);
			Component->SetHiddenInGame(true, true);
			Component->SetVisibility(false, true);
			Component->MarkRenderStateDirty();
		}
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

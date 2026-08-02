#include "TattooShop/ProjectTattooShopInputSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_EDITOR
#include "ObjectTools.h"
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogProjectTattooShopRuntimeTextures, Log, All);

namespace ProjectTattooShopRuntimeTexturesPrivate
{
	constexpr int32 DeleteMenuZOrder = 10060;
	constexpr int32 MinimumOpaqueTattooCanvasSize = 256;
	constexpr int32 MaximumOpaqueTattooCanvasSize = 1024;
	constexpr int32 MaximumRuntimeTattooSourceDimension = 16384;
	constexpr int64 MaximumRuntimeTattooSourcePixels = 64ll * 1024ll * 1024ll;
	constexpr float OpaqueTattooCanvasFill = 0.65f;
	constexpr uint8 FullyOpaqueAlpha = 255;
	const TCHAR* TattooViewerCardWidgetPath = TEXT("/Game/TattooShop/Blueprints/Widget/WBP_TattooviewerInst.WBP_TattooviewerInst_C");
	const TCHAR* TattooTexturePackageRoot = TEXT("/Game/TattooShop/Texture/");
	const FString RuntimeTattooDisplayPrefix(TEXT("User_"));

	const FName AssetPreviewerPropertyName(TEXT("AssetPreviewer"));
	const FName PreviewGridPropertyName(TEXT("PreviewGrid"));
	const FName AssetPreviewGridPropertyName(TEXT("AssetPreviewGrid"));
	const FName GridColumnLimitPropertyName(TEXT("GridColumnLimit"));
	const FName TattooCardTexturePropertyName(TEXT("AssetTexture"));
	const FName TattooCardNamePropertyName(TEXT("AssetfName"));
	const FName TattooCardAlternateNamePropertyName(TEXT("Assetname"));
	const FName RuntimeTattooCardNamePropertyName(TEXT("AssetNameTxt"));
	const FName RuntimeTattooCardParentPropertyName(TEXT("Parent"));
	const FName SelectedAssetTexturePropertyName(TEXT("SelectedAssetTexture"));
	const FName SelectedAssetNamePropertyName(TEXT("SelectedAssetName"));

	const FName UploadButtonNames[] = { TEXT("UploadButton"), TEXT("Upload") };
	const FName DeleteButtonNames[] = { TEXT("DeleteButton"), TEXT("Delete") };

	bool HasMeaningfulTransparency(const TArray64<uint8>& RawBgraData, const int64 PixelCount)
	{
		if (PixelCount <= 0 || RawBgraData.Num() < PixelCount * static_cast<int64>(sizeof(FColor)))
		{
			return false;
		}

		const FColor* Pixels = reinterpret_cast<const FColor*>(RawBgraData.GetData());
		for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			if (Pixels[PixelIndex].A < FullyOpaqueAlpha)
			{
				return true;
			}
		}
		return false;
	}

	FColor BilinearSampleBgra(
		const FColor* SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight,
		const float SourceX,
		const float SourceY)
	{
		const int32 X0 = FMath::Clamp(FMath::FloorToInt(SourceX), 0, SourceWidth - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SourceY), 0, SourceHeight - 1);
		const int32 X1 = FMath::Min(X0 + 1, SourceWidth - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, SourceHeight - 1);
		const float FractionX = FMath::Clamp(SourceX - static_cast<float>(X0), 0.0f, 1.0f);
		const float FractionY = FMath::Clamp(SourceY - static_cast<float>(Y0), 0.0f, 1.0f);

		const FColor& TopLeft = SourcePixels[Y0 * SourceWidth + X0];
		const FColor& TopRight = SourcePixels[Y0 * SourceWidth + X1];
		const FColor& BottomLeft = SourcePixels[Y1 * SourceWidth + X0];
		const FColor& BottomRight = SourcePixels[Y1 * SourceWidth + X1];

		auto InterpolateChannel = [FractionX, FractionY](
			const uint8 TopLeftValue,
			const uint8 TopRightValue,
			const uint8 BottomLeftValue,
			const uint8 BottomRightValue) -> uint8
		{
			const float Top = FMath::Lerp(static_cast<float>(TopLeftValue), static_cast<float>(TopRightValue), FractionX);
			const float Bottom = FMath::Lerp(static_cast<float>(BottomLeftValue), static_cast<float>(BottomRightValue), FractionX);
			return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, FractionY)), 0, 255));
		};

		return FColor(
			InterpolateChannel(TopLeft.R, TopRight.R, BottomLeft.R, BottomRight.R),
			InterpolateChannel(TopLeft.G, TopRight.G, BottomLeft.G, BottomRight.G),
			InterpolateChannel(TopLeft.B, TopRight.B, BottomLeft.B, BottomRight.B),
			InterpolateChannel(TopLeft.A, TopRight.A, BottomLeft.A, BottomRight.A));
	}

	bool BuildOpaqueTattooCanvas(
		const TArray64<uint8>& SourceBgraData,
		const int32 SourceWidth,
		const int32 SourceHeight,
		TArray64<uint8>& OutBgraData,
		int32& OutCanvasWidth,
		int32& OutCanvasHeight,
		int32& OutDrawWidth,
		int32& OutDrawHeight)
	{
		OutBgraData.Reset();
		OutCanvasWidth = 0;
		OutCanvasHeight = 0;
		OutDrawWidth = 0;
		OutDrawHeight = 0;

		const int64 SourcePixelCount = static_cast<int64>(SourceWidth) * static_cast<int64>(SourceHeight);
		if (SourceWidth <= 0
			|| SourceHeight <= 0
			|| SourceBgraData.Num() < SourcePixelCount * static_cast<int64>(sizeof(FColor)))
		{
			return false;
		}

		const int32 SourceMaximumDimension = FMath::Max(SourceWidth, SourceHeight);
		const int32 DesiredCanvasSize = FMath::RoundUpToPowerOfTwo(
			FMath::Max(1, FMath::CeilToInt(static_cast<float>(SourceMaximumDimension) / OpaqueTattooCanvasFill)));
		const int32 CanvasSize = FMath::Clamp(
			DesiredCanvasSize,
			MinimumOpaqueTattooCanvasSize,
			MaximumOpaqueTattooCanvasSize);
		const int32 MaximumDrawDimension = FMath::Max(1, FMath::FloorToInt(static_cast<float>(CanvasSize) * OpaqueTattooCanvasFill));
		const float FitScale = FMath::Min(
			static_cast<float>(MaximumDrawDimension) / static_cast<float>(SourceWidth),
			static_cast<float>(MaximumDrawDimension) / static_cast<float>(SourceHeight));

		const int32 DrawWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SourceWidth) * FitScale));
		const int32 DrawHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SourceHeight) * FitScale));
		const int32 DrawStartX = (CanvasSize - DrawWidth) / 2;
		const int32 DrawStartY = (CanvasSize - DrawHeight) / 2;

		OutBgraData.SetNumZeroed(static_cast<int64>(CanvasSize) * static_cast<int64>(CanvasSize) * static_cast<int64>(sizeof(FColor)));
		const FColor* SourcePixels = reinterpret_cast<const FColor*>(SourceBgraData.GetData());
		FColor* DestinationPixels = reinterpret_cast<FColor*>(OutBgraData.GetData());

		for (int32 DestinationY = 0; DestinationY < DrawHeight; ++DestinationY)
		{
			const float SourceY = ((static_cast<float>(DestinationY) + 0.5f) / static_cast<float>(DrawHeight))
				* static_cast<float>(SourceHeight) - 0.5f;
			for (int32 DestinationX = 0; DestinationX < DrawWidth; ++DestinationX)
			{
				const float SourceX = ((static_cast<float>(DestinationX) + 0.5f) / static_cast<float>(DrawWidth))
					* static_cast<float>(SourceWidth) - 0.5f;
				DestinationPixels[(DrawStartY + DestinationY) * CanvasSize + DrawStartX + DestinationX] =
					BilinearSampleBgra(SourcePixels, SourceWidth, SourceHeight, SourceX, SourceY);
			}
		}

		OutCanvasWidth = CanvasSize;
		OutCanvasHeight = CanvasSize;
		OutDrawWidth = DrawWidth;
		OutDrawHeight = DrawHeight;
		return true;
	}
}

bool UProjectTattooShopInputSubsystem::RequestUploadRuntimeTattooTexture()
{
	FString SelectedFilePath;
	if (!OpenNativePngFileDialog(TEXT("Select PNG tattoo texture"), SelectedFilePath))
	{
		return false;
	}

	SelectedFilePath = NormalizeTattooFilePath(SelectedFilePath);
	if (!FPaths::GetExtension(SelectedFilePath).Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Upload rejected; only PNG is supported: %s"), *SelectedFilePath);
		return false;
	}

	if (!LoadPngTextureFromFile(SelectedFilePath, TEXT("RuntimeTattooUploadValidation")))
	{
		UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Upload rejected; PNG decode failed: %s"), *SelectedFilePath);
		return false;
	}

	const FString RuntimeDirectory = GetRuntimeTattooTextureDirectory();
	if (!IFileManager::Get().MakeDirectory(*RuntimeDirectory, true))
	{
		UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Could not create runtime texture directory: %s"), *RuntimeDirectory);
		return false;
	}

	const FString DestinationFilePath = MakeUniqueRuntimeTattooDestination(SelectedFilePath);
	if (!FPlatformFileManager::Get().GetPlatformFile().CopyFile(*DestinationFilePath, *SelectedFilePath))
	{
		UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Could not copy %s to %s."), *SelectedFilePath, *DestinationFilePath);
		return false;
	}

	RuntimeTattooTextureCache.Remove(NormalizeTattooFilePath(DestinationFilePath));
	bRuntimeTattooCardsInitialized = false;
	const bool bRefreshed = RefreshRuntimeTattooCards(ResolveTrackedAssetPreviewWidget());
	bRuntimeTattooCardsInitialized = bRefreshed;
	UE_LOG(LogProjectTattooShopRuntimeTextures, Display, TEXT("[TattooShop] Uploaded PNG: %s Refresh=%d."), *DestinationFilePath, bRefreshed ? 1 : 0);
	return true;
}

bool UProjectTattooShopInputSubsystem::RequestDeleteRuntimeTattooTexture()
{
	return ShowDeleteTattooTextureMenu();
}

void UProjectTattooShopInputSubsystem::HandleRuntimeUploadClicked()
{
	RequestUploadRuntimeTattooTexture();
}

void UProjectTattooShopInputSubsystem::HandleRuntimeDeleteClicked()
{
	RequestDeleteRuntimeTattooTexture();
}

void UProjectTattooShopInputSubsystem::BindTattooShopRuntimeButtons(UUserWidget* TattooShopWidget)
{
	if (!IsValid(TattooShopWidget))
	{
		return;
	}

	auto FindButton = [TattooShopWidget](const FName ButtonName) -> UButton*
	{
		if (FObjectPropertyBase* ButtonProperty = FindFProperty<FObjectPropertyBase>(TattooShopWidget->GetClass(), ButtonName))
		{
			if (UButton* PropertyButton = Cast<UButton>(ButtonProperty->GetObjectPropertyValue_InContainer(TattooShopWidget)))
			{
				return PropertyButton;
			}
		}
		return TattooShopWidget->WidgetTree ? Cast<UButton>(TattooShopWidget->WidgetTree->FindWidget(ButtonName)) : nullptr;
	};

	for (const FName ButtonName : ProjectTattooShopRuntimeTexturesPrivate::UploadButtonNames)
	{
		if (UButton* UploadButton = FindButton(ButtonName))
		{
			UploadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRuntimeUploadClicked);
			break;
		}
	}

	for (const FName ButtonName : ProjectTattooShopRuntimeTexturesPrivate::DeleteButtonNames)
	{
		if (UButton* DeleteButton = FindButton(ButtonName))
		{
			DeleteButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRuntimeDeleteClicked);
			break;
		}
	}
}

TSubclassOf<UUserWidget> UProjectTattooShopInputSubsystem::ResolveTattooViewerCardWidgetClass()
{
	if (!TattooViewerCardWidgetClass)
	{
		TattooViewerCardWidgetClass = Cast<UClass>(FSoftObjectPath(ProjectTattooShopRuntimeTexturesPrivate::TattooViewerCardWidgetPath).TryLoad());
	}
	return TattooViewerCardWidgetClass;
}

UUserWidget* UProjectTattooShopInputSubsystem::ResolveTrackedAssetPreviewWidget()
{
	if (IsValid(TrackedAssetPreviewWidget))
	{
		return TrackedAssetPreviewWidget.Get();
	}

	if (UUserWidget* TattooShopWidget = TrackedTattooShopWidget.Get())
	{
		if (FObjectPropertyBase* AssetPreviewerProperty = FindFProperty<FObjectPropertyBase>(TattooShopWidget->GetClass(), ProjectTattooShopRuntimeTexturesPrivate::AssetPreviewerPropertyName))
		{
			if (UUserWidget* PreviewWidget = Cast<UUserWidget>(AssetPreviewerProperty->GetObjectPropertyValue_InContainer(TattooShopWidget)))
			{
				TrackedAssetPreviewWidget = PreviewWidget;
				return PreviewWidget;
			}
		}
	}

	const TSubclassOf<UUserWidget> PreviewWidgetClass = ResolveAssetPreviewWidgetClass();
	if (!PreviewWidgetClass)
	{
		return nullptr;
	}

	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, FoundWidgets, PreviewWidgetClass, false);
	for (UUserWidget* Candidate : FoundWidgets)
	{
		if (IsValid(Candidate) && Candidate != TrackedTattooShopWidget.Get())
		{
			TrackedAssetPreviewWidget = Candidate;
			return Candidate;
		}
	}
	return nullptr;
}

UGridPanel* UProjectTattooShopInputSubsystem::ResolveAssetPreviewGrid(UUserWidget* PreviewWidget) const
{
	if (!IsValid(PreviewWidget))
	{
		return nullptr;
	}

	auto TryGridProperty = [PreviewWidget](const FName PropertyName) -> UGridPanel*
	{
		if (FObjectPropertyBase* GridProperty = FindFProperty<FObjectPropertyBase>(PreviewWidget->GetClass(), PropertyName))
		{
			return Cast<UGridPanel>(GridProperty->GetObjectPropertyValue_InContainer(PreviewWidget));
		}
		return nullptr;
	};

	if (UGridPanel* Grid = TryGridProperty(ProjectTattooShopRuntimeTexturesPrivate::AssetPreviewGridPropertyName))
	{
		return Grid;
	}
	if (UGridPanel* Grid = TryGridProperty(ProjectTattooShopRuntimeTexturesPrivate::PreviewGridPropertyName))
	{
		return Grid;
	}
	if (!PreviewWidget->WidgetTree)
	{
		return nullptr;
	}
	if (UGridPanel* Grid = Cast<UGridPanel>(PreviewWidget->WidgetTree->FindWidget(ProjectTattooShopRuntimeTexturesPrivate::AssetPreviewGridPropertyName)))
	{
		return Grid;
	}
	if (UGridPanel* Grid = Cast<UGridPanel>(PreviewWidget->WidgetTree->FindWidget(ProjectTattooShopRuntimeTexturesPrivate::PreviewGridPropertyName)))
	{
		return Grid;
	}

	UGridPanel* FirstGrid = nullptr;
	UGridPanel* FirstGridWithCards = nullptr;
	PreviewWidget->WidgetTree->ForEachWidget([this, &FirstGrid, &FirstGridWithCards](UWidget* CandidateWidget)
	{
		UGridPanel* CandidateGrid = Cast<UGridPanel>(CandidateWidget);
		if (!CandidateGrid)
		{
			return;
		}
		FirstGrid = FirstGrid ? FirstGrid : CandidateGrid;
		if (!FirstGridWithCards && CountTattooCardsInPanel(CandidateGrid) > 0)
		{
			FirstGridWithCards = CandidateGrid;
		}
	});
	return FirstGridWithCards ? FirstGridWithCards : FirstGrid;
}

bool UProjectTattooShopInputSubsystem::RefreshRuntimeTattooCards(UUserWidget* PreferredPreviewWidget)
{
	UUserWidget* PreviewWidget = IsValid(PreferredPreviewWidget) ? PreferredPreviewWidget : ResolveTrackedAssetPreviewWidget();
	UGridPanel* PreviewGrid = ResolveAssetPreviewGrid(PreviewWidget);
	if (!IsValid(PreviewWidget) || !IsValid(PreviewGrid))
	{
		return false;
	}

	int32 ColumnLimit = 4;
	if (FIntProperty* ColumnLimitProperty = FindFProperty<FIntProperty>(PreviewWidget->GetClass(), ProjectTattooShopRuntimeTexturesPrivate::GridColumnLimitPropertyName))
	{
		ColumnLimit = FMath::Max(1, ColumnLimitProperty->GetPropertyValue_InContainer(PreviewWidget));
	}

	TArray<UWidget*> RuntimeCardsToRemove;
	for (int32 ChildIndex = 0; ChildIndex < PreviewGrid->GetChildrenCount(); ++ChildIndex)
	{
		UWidget* Child = PreviewGrid->GetChildAt(ChildIndex);
		if (IsRuntimeTattooCard(Child))
		{
			RuntimeCardsToRemove.Add(Child);
		}
	}
	for (UWidget* RuntimeCard : RuntimeCardsToRemove)
	{
		PreviewGrid->RemoveChild(RuntimeCard);
	}

	const FString RuntimeDirectory = GetRuntimeTattooTextureDirectory();
	IFileManager::Get().MakeDirectory(*RuntimeDirectory, true);
	TArray<FString> RuntimePngFiles;
	IFileManager::Get().FindFilesRecursive(RuntimePngFiles, *RuntimeDirectory, TEXT("*.png"), true, false);
	RuntimePngFiles.Sort();

	int32 AddedCount = 0;
	for (const FString& RuntimePngFile : RuntimePngFiles)
	{
		const FString RuntimePngPath = NormalizeTattooFilePath(RuntimePngFile);
		UTexture2D* RuntimeTexture = RuntimeTattooTextureCache.FindRef(RuntimePngPath);
		if (!IsValid(RuntimeTexture))
		{
			RuntimeTexture = LoadPngTextureFromFile(RuntimePngPath, MakeRuntimeTattooDisplayName(RuntimePngPath));
			if (RuntimeTexture)
			{
				RuntimeTattooTextureCache.Add(RuntimePngPath, RuntimeTexture);
			}
		}
		if (!RuntimeTexture)
		{
			continue;
		}

		UUserWidget* CardWidget = CreateRuntimeTattooCard(PreviewWidget, RuntimeTexture, FText::FromString(MakeRuntimeTattooDisplayName(RuntimePngPath)));
		if (!CardWidget)
		{
			continue;
		}
		const int32 NewCardIndex = PreviewGrid->GetChildrenCount();
		if (UGridSlot* GridSlot = PreviewGrid->AddChildToGrid(CardWidget, NewCardIndex / ColumnLimit, NewCardIndex % ColumnLimit))
		{
			GridSlot->SetRow(NewCardIndex / ColumnLimit);
			GridSlot->SetColumn(NewCardIndex % ColumnLimit);
		}
		++AddedCount;
	}

	NormalizeTattooCardPanel(PreviewGrid, ColumnLimit);
	LayoutRuntimeTattooCardPanel(PreviewGrid, ColumnLimit);
	UE_LOG(LogProjectTattooShopRuntimeTextures, Log, TEXT("[TattooShop] Runtime PNG refresh: files=%d removed=%d added=%d."), RuntimePngFiles.Num(), RuntimeCardsToRemove.Num(), AddedCount);
	return true;
}

UTexture2D* UProjectTattooShopInputSubsystem::LoadPngTextureFromFile(const FString& FilePath, const FString& TextureObjectName)
{
	const FString NormalizedFilePath = NormalizeTattooFilePath(FilePath);
	if (!FPaths::GetExtension(NormalizedFilePath).Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		return nullptr;
	}

	TArray<uint8> CompressedData;
	if (!FFileHelper::LoadFileToArray(CompressedData, *NormalizedFilePath) || CompressedData.IsEmpty())
	{
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
	{
		return nullptr;
	}

	const int64 Width = ImageWrapper->GetWidth();
	const int64 Height = ImageWrapper->GetHeight();
	if (Width <= 0
		|| Height <= 0
		|| Width > ProjectTattooShopRuntimeTexturesPrivate::MaximumRuntimeTattooSourceDimension
		|| Height > ProjectTattooShopRuntimeTexturesPrivate::MaximumRuntimeTattooSourceDimension)
	{
		UE_LOG(
			LogProjectTattooShopRuntimeTextures,
			Warning,
			TEXT("[TattooShop] PNG dimensions are outside the runtime tattoo safety limits: %s (%lldx%lld)."),
			*NormalizedFilePath,
			Width,
			Height);
		return nullptr;
	}

	const int64 PixelCount = Width * Height;
	if (PixelCount <= 0 || PixelCount > ProjectTattooShopRuntimeTexturesPrivate::MaximumRuntimeTattooSourcePixels)
	{
		UE_LOG(
			LogProjectTattooShopRuntimeTextures,
			Warning,
			TEXT("[TattooShop] PNG pixel count is outside the runtime tattoo safety limit: %s (%lld pixels)."),
			*NormalizedFilePath,
			PixelCount);
		return nullptr;
	}

	TArray64<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData)
		|| RawData.Num() != PixelCount * static_cast<int64>(sizeof(FColor)))
	{
		return nullptr;
	}

	int32 TextureWidth = static_cast<int32>(Width);
	int32 TextureHeight = static_cast<int32>(Height);
	int32 OpaqueDrawWidth = TextureWidth;
	int32 OpaqueDrawHeight = TextureHeight;
	TArray64<uint8> OpaqueCanvasData;
	const bool bHasMeaningfulTransparency = ProjectTattooShopRuntimeTexturesPrivate::HasMeaningfulTransparency(RawData, PixelCount);
	const bool bBuiltOpaqueCanvas = !bHasMeaningfulTransparency
		&& ProjectTattooShopRuntimeTexturesPrivate::BuildOpaqueTattooCanvas(
			RawData,
			TextureWidth,
			TextureHeight,
			OpaqueCanvasData,
			TextureWidth,
			TextureHeight,
			OpaqueDrawWidth,
			OpaqueDrawHeight);
	const TArray64<uint8>& TexturePixelData = bBuiltOpaqueCanvas ? OpaqueCanvasData : RawData;

	const FString SafeTextureName = TextureObjectName.IsEmpty() ? MakeRuntimeTattooDisplayName(NormalizedFilePath) : TextureObjectName;
	const FName UniqueTextureName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(*SafeTextureName));
	UTexture2D* Texture = UTexture2D::CreateTransient(TextureWidth, TextureHeight, PF_B8G8R8A8, UniqueTextureName);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return nullptr;
	}

	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->LODGroup = TEXTUREGROUP_UI;
	if (bBuiltOpaqueCanvas)
	{
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
	}
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!TextureData)
	{
		Mip.BulkData.Unlock();
		return nullptr;
	}
	FMemory::Memcpy(TextureData, TexturePixelData.GetData(), TexturePixelData.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	if (bBuiltOpaqueCanvas)
	{
		UE_LOG(
			LogProjectTattooShopRuntimeTextures,
			Display,
			TEXT("[TattooShop] Prepared opaque PNG as a bounded tattoo card: %s source=%lldx%lld canvas=%dx%d image=%dx%d fill=%.2f."),
			*NormalizedFilePath,
			Width,
			Height,
			TextureWidth,
			TextureHeight,
			OpaqueDrawWidth,
			OpaqueDrawHeight,
			ProjectTattooShopRuntimeTexturesPrivate::OpaqueTattooCanvasFill);
	}
	return Texture;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectTattooShopOpaqueCanvasTest,
	"Project.TattooShop.RuntimeTextures.OpaqueCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectTattooShopOpaqueCanvasTest::RunTest(const FString& Parameters)
{
	TArray64<uint8> OpaquePixels;
	OpaquePixels.SetNumUninitialized(4 * 2 * sizeof(FColor));
	FColor* OpaqueColors = reinterpret_cast<FColor*>(OpaquePixels.GetData());
	for (int32 PixelIndex = 0; PixelIndex < 8; ++PixelIndex)
	{
		OpaqueColors[PixelIndex] = PixelIndex < 4 ? FColor::Red : FColor::Blue;
	}

	TestFalse(
		TEXT("Fully opaque PNG data is detected as opaque"),
		ProjectTattooShopRuntimeTexturesPrivate::HasMeaningfulTransparency(OpaquePixels, 8));

	TArray64<uint8> CanvasPixels;
	int32 CanvasWidth = 0;
	int32 CanvasHeight = 0;
	int32 DrawWidth = 0;
	int32 DrawHeight = 0;
	TestTrue(
		TEXT("Opaque PNG data can be fitted to a bounded canvas"),
		ProjectTattooShopRuntimeTexturesPrivate::BuildOpaqueTattooCanvas(
			OpaquePixels,
			4,
			2,
			CanvasPixels,
			CanvasWidth,
			CanvasHeight,
			DrawWidth,
			DrawHeight));
	TestEqual(TEXT("Opaque tattoo canvas is square"), CanvasWidth, CanvasHeight);
	TestTrue(TEXT("Opaque tattoo canvas has transparent padding"), DrawWidth < CanvasWidth && DrawHeight < CanvasHeight);

	const FColor* CanvasColors = reinterpret_cast<const FColor*>(CanvasPixels.GetData());
	TestEqual(TEXT("Opaque tattoo canvas corner is transparent"), CanvasColors[0].A, static_cast<uint8>(0));
	TestTrue(
		TEXT("Opaque tattoo image remains visible at canvas center"),
		CanvasColors[(CanvasHeight / 2) * CanvasWidth + CanvasWidth / 2].A == ProjectTattooShopRuntimeTexturesPrivate::FullyOpaqueAlpha);

	TArray64<uint8> TransparentPixels = OpaquePixels;
	reinterpret_cast<FColor*>(TransparentPixels.GetData())[0].A = 0;
	TestTrue(
		TEXT("Existing transparent PNG data stays on the original path"),
		ProjectTattooShopRuntimeTexturesPrivate::HasMeaningfulTransparency(TransparentPixels, 8));
	return true;
}
#endif

bool UProjectTattooShopInputSubsystem::OpenNativePngFileDialog(const FString& DialogTitle, FString& OutSelectedFilePath) const
{
	OutSelectedFilePath.Reset();
#if PLATFORM_WINDOWS
	WCHAR FilenameBuffer[32768] = {};
	OPENFILENAMEW OpenFileName = {};
	OpenFileName.lStructSize = sizeof(OPENFILENAMEW);
	OpenFileName.hwndOwner = GetActiveWindow();
	OpenFileName.lpstrFile = FilenameBuffer;
	OpenFileName.nMaxFile = UE_ARRAY_COUNT(FilenameBuffer);
	OpenFileName.lpstrFilter = L"PNG Files (*.png)\0*.png\0\0";
	OpenFileName.nFilterIndex = 1;
	OpenFileName.lpstrTitle = *DialogTitle;
	OpenFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&OpenFileName))
	{
		const DWORD DialogError = CommDlgExtendedError();
		if (DialogError != 0)
		{
			UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Windows PNG dialog failed. Error=%lu"), static_cast<unsigned long>(DialogError));
		}
		return false;
	}
	OutSelectedFilePath = NormalizeTattooFilePath(FString(FilenameBuffer));
	return !OutSelectedFilePath.IsEmpty();
#else
	UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] PNG dialog is supported only on Windows."));
	return false;
#endif
}

FString UProjectTattooShopInputSubsystem::GetRuntimeTattooTextureDirectory() const
{
	FString RuntimeDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TattooShop"), TEXT("Texture")));
	FPaths::NormalizeDirectoryName(RuntimeDirectory);
	return RuntimeDirectory;
}

FString UProjectTattooShopInputSubsystem::NormalizeTattooFilePath(const FString& FilePath) const
{
	FString NormalizedFilePath = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::NormalizeFilename(NormalizedFilePath);
	return NormalizedFilePath;
}

FString UProjectTattooShopInputSubsystem::MakeRuntimeTattooDisplayName(const FString& StoredFilePath) const
{
	const FString BaseName = FPaths::GetBaseFilename(StoredFilePath).TrimStartAndEnd();
	return BaseName.StartsWith(ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooDisplayPrefix)
		? BaseName
		: ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooDisplayPrefix + BaseName;
}

FString UProjectTattooShopInputSubsystem::MakeUniqueRuntimeTattooDestination(const FString& SourceFilePath) const
{
	FString SanitizedBaseName;
	for (const TCHAR Character : FPaths::GetBaseFilename(SourceFilePath).TrimStartAndEnd())
	{
		SanitizedBaseName.AppendChar(FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-') ? Character : TEXT('_'));
	}
	if (SanitizedBaseName.IsEmpty())
	{
		SanitizedBaseName = TEXT("Tattoo");
	}
	if (!SanitizedBaseName.StartsWith(ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooDisplayPrefix))
	{
		SanitizedBaseName = ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooDisplayPrefix + SanitizedBaseName;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString CandidateFilePath = NormalizeTattooFilePath(FPaths::Combine(GetRuntimeTattooTextureDirectory(), SanitizedBaseName + TEXT(".png")));
	for (int32 Suffix = 2; PlatformFile.FileExists(*CandidateFilePath); ++Suffix)
	{
		CandidateFilePath = NormalizeTattooFilePath(FPaths::Combine(GetRuntimeTattooTextureDirectory(), FString::Printf(TEXT("%s_%d.png"), *SanitizedBaseName, Suffix)));
	}
	return CandidateFilePath;
}

bool UProjectTattooShopInputSubsystem::IsRuntimeTattooCard(UWidget* CardWidget) const
{
	if (!IsValid(CardWidget))
	{
		return false;
	}
	if (GetRuntimeWidgetTextProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooCardNamePropertyName).StartsWith(ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooDisplayPrefix))
	{
		return true;
	}
	UTexture2D* CardTexture = ResolveTattooCardTexture(CardWidget);
	for (const TPair<FString, TObjectPtr<UTexture2D>>& RuntimeTexturePair : RuntimeTattooTextureCache)
	{
		if (RuntimeTexturePair.Value.Get() == CardTexture)
		{
			return true;
		}
	}
	return false;
}

bool UProjectTattooShopInputSubsystem::FindRuntimeTattooFileForTexture(UTexture2D* Texture, FString& OutFilePath) const
{
	OutFilePath.Reset();
	for (const TPair<FString, TObjectPtr<UTexture2D>>& RuntimeTexturePair : RuntimeTattooTextureCache)
	{
		if (RuntimeTexturePair.Value.Get() == Texture)
		{
			OutFilePath = RuntimeTexturePair.Key;
			return true;
		}
	}
	return false;
}

UTexture2D* UProjectTattooShopInputSubsystem::ResolveTattooCardTexture(UWidget* CardWidget) const
{
	if (!IsValid(CardWidget))
	{
		return nullptr;
	}
	if (FObjectPropertyBase* TextureProperty = FindFProperty<FObjectPropertyBase>(CardWidget->GetClass(), ProjectTattooShopRuntimeTexturesPrivate::TattooCardTexturePropertyName))
	{
		if (UTexture2D* Texture = Cast<UTexture2D>(TextureProperty->GetObjectPropertyValue_InContainer(CardWidget)))
		{
			return Texture;
		}
	}

	if (UUserWidget* UserWidget = Cast<UUserWidget>(CardWidget))
	{
		UTexture2D* FoundTexture = nullptr;
		if (UserWidget->WidgetTree)
		{
			UserWidget->WidgetTree->ForEachWidget([&FoundTexture](UWidget* ChildWidget)
			{
				if (!FoundTexture)
				{
					if (const UImage* Image = Cast<UImage>(ChildWidget))
					{
						FoundTexture = Cast<UTexture2D>(Image->GetBrush().GetResourceObject());
					}
				}
			});
		}
		return FoundTexture;
	}
	return nullptr;
}

FString UProjectTattooShopInputSubsystem::ResolveTattooCardDisplayName(UWidget* CardWidget, UTexture2D* Texture) const
{
	FString DisplayName = GetRuntimeWidgetTextProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooCardNamePropertyName).TrimStartAndEnd();
	if (DisplayName.IsEmpty())
	{
		DisplayName = GetRuntimeWidgetTextProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::TattooCardNamePropertyName).TrimStartAndEnd();
	}
	if (DisplayName.IsEmpty())
	{
		DisplayName = GetRuntimeWidgetTextProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::TattooCardAlternateNamePropertyName).TrimStartAndEnd();
	}
	if (DisplayName.IsEmpty())
	{
		DisplayName = ResolveTattooCardIdentity(CardWidget).TrimStartAndEnd();
	}
	return DisplayName.IsEmpty() && Texture ? Texture->GetName() : DisplayName;
}

void UProjectTattooShopInputSubsystem::GatherDeletableTattooTextures(TArray<UTexture2D*>& OutTextures, TArray<FString>& OutDisplayNames)
{
	OutTextures.Reset();
	OutDisplayNames.Reset();
	TSet<FString> SeenKeys;
	auto AddTexture = [this, &SeenKeys, &OutTextures, &OutDisplayNames](UTexture2D* Texture, const FString& DisplayName)
	{
		if (!IsValid(Texture))
		{
			return;
		}
		FString RuntimeFilePath;
		const bool bRuntimeTexture = FindRuntimeTattooFileForTexture(Texture, RuntimeFilePath);
		const FString PackageName = Texture->GetOutermost() ? Texture->GetOutermost()->GetName() : FString();
		if (!bRuntimeTexture && !PackageName.StartsWith(ProjectTattooShopRuntimeTexturesPrivate::TattooTexturePackageRoot))
		{
			return;
		}
		const FString Key = bRuntimeTexture ? RuntimeFilePath : PackageName;
		if (Key.IsEmpty() || SeenKeys.Contains(Key))
		{
			return;
		}
		SeenKeys.Add(Key);
		OutTextures.Add(Texture);
		OutDisplayNames.Add(DisplayName.IsEmpty() ? Texture->GetName() : DisplayName);
	};

	UUserWidget* PreviewWidget = ResolveTrackedAssetPreviewWidget();
	if (UGridPanel* PreviewGrid = ResolveAssetPreviewGrid(PreviewWidget))
	{
		for (int32 ChildIndex = 0; ChildIndex < PreviewGrid->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* Child = PreviewGrid->GetChildAt(ChildIndex);
			UTexture2D* Texture = ResolveTattooCardTexture(Child);
			AddTexture(Texture, ResolveTattooCardDisplayName(Child, Texture));
		}
	}
	for (const TPair<FString, TObjectPtr<UTexture2D>>& RuntimeTexturePair : RuntimeTattooTextureCache)
	{
		AddTexture(RuntimeTexturePair.Value.Get(), MakeRuntimeTattooDisplayName(RuntimeTexturePair.Key));
	}
}

bool UProjectTattooShopInputSubsystem::ShowDeleteTattooTextureMenu()
{
	RefreshRuntimeTattooCards(ResolveTrackedAssetPreviewWidget());
	TArray<UTexture2D*> Textures;
	TArray<FString> DisplayNames;
	GatherDeletableTattooTextures(Textures, DisplayNames);
	UWorld* World = GetWorld();
	UGameViewportClient* GameViewport = World ? World->GetGameViewport() : nullptr;
	if (Textures.IsEmpty() || !GameViewport)
	{
		UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Delete menu has no available tattoo textures or viewport."));
		return false;
	}

	DismissDeleteTattooTextureMenu();
	TSharedRef<SVerticalBox> Entries = SNew(SVerticalBox);
	const TWeakObjectPtr<UProjectTattooShopInputSubsystem> WeakThis(this);
	for (int32 Index = 0; Index < Textures.Num(); ++Index)
	{
		const TWeakObjectPtr<UTexture2D> WeakTexture(Textures[Index]);
		const FString DisplayName = DisplayNames.IsValidIndex(Index) ? DisplayNames[Index] : Textures[Index]->GetName();
		Entries->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(10.0f, 7.0f))
			.OnClicked_Lambda([WeakThis, WeakTexture, DisplayName]()
			{
				if (UProjectTattooShopInputSubsystem* Subsystem = WeakThis.Get())
				{
					if (UTexture2D* Texture = WeakTexture.Get())
					{
						Subsystem->DeleteTattooTexture(Texture, DisplayName);
					}
					Subsystem->DismissDeleteTattooTextureMenu();
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(DisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15))
			]
		];
	}

	TSharedRef<SOverlay> MenuRoot =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(420.0f)
			.MaxDesiredHeight(460.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
				.BorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.025f, 0.98f))
				.Padding(14.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("DELETE TATTOO")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 21))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							Entries
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(10.0f, 7.0f))
						.OnClicked_Lambda([WeakThis]()
						{
							if (UProjectTattooShopInputSubsystem* Subsystem = WeakThis.Get())
							{
								Subsystem->DismissDeleteTattooTextureMenu();
							}
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("CANCEL")))
						]
					]
				]
			]
		];

	ActiveDeleteMenuSlateWidget = MenuRoot;
	GameViewport->AddViewportWidgetContent(MenuRoot, ProjectTattooShopRuntimeTexturesPrivate::DeleteMenuZOrder);
	return true;
}

void UProjectTattooShopInputSubsystem::DismissDeleteTattooTextureMenu()
{
	if (ActiveDeleteMenuSlateWidget.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* GameViewport = World->GetGameViewport())
			{
				GameViewport->RemoveViewportWidgetContent(ActiveDeleteMenuSlateWidget.ToSharedRef());
			}
		}
		ActiveDeleteMenuSlateWidget.Reset();
	}
}

bool UProjectTattooShopInputSubsystem::DeleteTattooTexture(UTexture2D* Texture, const FString& DisplayName)
{
	UUserWidget* PreviewWidget = ResolveTrackedAssetPreviewWidget();
	if (!IsValid(PreviewWidget) || !IsValid(Texture))
	{
		return false;
	}

	FString RuntimeFilePath;
	if (FindRuntimeTattooFileForTexture(Texture, RuntimeFilePath))
	{
		if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*RuntimeFilePath))
		{
			UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Failed to delete runtime PNG: %s"), *RuntimeFilePath);
			return false;
		}
		RuntimeTattooTextureCache.Remove(RuntimeFilePath);
		SetRuntimeWidgetObjectProperty(PreviewWidget, ProjectTattooShopRuntimeTexturesPrivate::SelectedAssetTexturePropertyName, nullptr);
		SetRuntimeWidgetTextProperty(PreviewWidget, ProjectTattooShopRuntimeTexturesPrivate::SelectedAssetNamePropertyName, FText::GetEmpty());
		bRuntimeTattooCardsInitialized = RefreshRuntimeTattooCards(PreviewWidget);
		return true;
	}

	const FString PackageName = Texture->GetOutermost() ? Texture->GetOutermost()->GetName() : FString();
	if (!PackageName.StartsWith(ProjectTattooShopRuntimeTexturesPrivate::TattooTexturePackageRoot))
	{
		return false;
	}
	TArray<UWidget*> CardsToRemove;
	if (UGridPanel* PreviewGrid = ResolveAssetPreviewGrid(PreviewWidget))
	{
		CollectTattooCardsForTexture(PreviewGrid, Texture, DisplayName, CardsToRemove);
	}
	if (!DeleteGameTattooTextureAsset(Texture))
	{
		return false;
	}
	for (UWidget* CardToRemove : CardsToRemove)
	{
		if (IsValid(CardToRemove))
		{
			CardToRemove->RemoveFromParent();
		}
	}
	SetRuntimeWidgetObjectProperty(PreviewWidget, ProjectTattooShopRuntimeTexturesPrivate::SelectedAssetTexturePropertyName, nullptr);
	SetRuntimeWidgetTextProperty(PreviewWidget, ProjectTattooShopRuntimeTexturesPrivate::SelectedAssetNamePropertyName, FText::GetEmpty());
	NormalizeTattooCardGrid(PreviewWidget);
	return true;
}

bool UProjectTattooShopInputSubsystem::DeleteGameTattooTextureAsset(UTexture2D* Texture) const
{
	if (!IsValid(Texture))
	{
		return false;
	}
	const FString PackageName = Texture->GetOutermost() ? Texture->GetOutermost()->GetName() : FString();
	if (!PackageName.StartsWith(ProjectTattooShopRuntimeTexturesPrivate::TattooTexturePackageRoot))
	{
		return false;
	}
#if WITH_EDITOR
	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Add(Texture);
	return ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete) > 0;
#else
	UE_LOG(LogProjectTattooShopRuntimeTextures, Warning, TEXT("[TattooShop] Cooked /Game assets cannot be deleted at runtime: %s"), *PackageName);
	return false;
#endif
}

void UProjectTattooShopInputSubsystem::CollectTattooCardsForTexture(
	UPanelWidget* Panel,
	UTexture2D* Texture,
	const FString& TextureName,
	TArray<UWidget*>& OutCards) const
{
	if (!IsValid(Panel) || !IsValid(Texture))
	{
		return;
	}
	for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
	{
		UWidget* Child = Panel->GetChildAt(ChildIndex);
		const FString Identity = ResolveTattooCardIdentity(Child).TrimStartAndEnd();
		if (ResolveTattooCardTexture(Child) == Texture
			|| Identity.Equals(TextureName, ESearchCase::IgnoreCase)
			|| Identity.Equals(Texture->GetName(), ESearchCase::IgnoreCase)
			|| Identity.Equals(Texture->GetPathName(), ESearchCase::IgnoreCase))
		{
			OutCards.Add(Child);
		}
	}
}

UUserWidget* UProjectTattooShopInputSubsystem::CreateRuntimeTattooCard(UUserWidget* PreviewWidget, UTexture2D* Texture, const FText& DisplayName)
{
	if (!IsValid(PreviewWidget) || !IsValid(Texture))
	{
		return nullptr;
	}
	const TSubclassOf<UUserWidget> CardWidgetClass = ResolveTattooViewerCardWidgetClass();
	if (!CardWidgetClass)
	{
		return nullptr;
	}

	UUserWidget* CardWidget = TrackedPlayerController
		? CreateWidget<UUserWidget>(TrackedPlayerController.Get(), CardWidgetClass)
		: CreateWidget<UUserWidget>(GetWorld(), CardWidgetClass);
	if (!CardWidget)
	{
		return nullptr;
	}
	SetRuntimeWidgetObjectProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooCardParentPropertyName, PreviewWidget);
	SetRuntimeWidgetObjectProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::TattooCardTexturePropertyName, Texture);
	SetRuntimeWidgetTextProperty(CardWidget, ProjectTattooShopRuntimeTexturesPrivate::RuntimeTattooCardNamePropertyName, DisplayName);

	if (CardWidget->WidgetTree)
	{
		if (UImage* AssetPreviewImage = Cast<UImage>(CardWidget->WidgetTree->FindWidget(TEXT("AssetPreview"))))
		{
			FSlateBrush Brush = AssetPreviewImage->GetBrush();
			Brush.SetResourceObject(Texture);
			AssetPreviewImage->SetBrush(Brush);
		}
		if (UTextBlock* AssetNameText = Cast<UTextBlock>(CardWidget->WidgetTree->FindWidget(TEXT("AssetfName"))))
		{
			AssetNameText->SetText(DisplayName);
		}
	}
	return CardWidget;
}

void UProjectTattooShopInputSubsystem::SetRuntimeWidgetObjectProperty(UObject* Target, const FName PropertyName, UObject* Value) const
{
	if (IsValid(Target))
	{
		if (FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(Target->GetClass(), PropertyName))
		{
			ObjectProperty->SetObjectPropertyValue_InContainer(Target, Value);
		}
	}
}

void UProjectTattooShopInputSubsystem::SetRuntimeWidgetTextProperty(UObject* Target, const FName PropertyName, const FText& Value) const
{
	if (!IsValid(Target))
	{
		return;
	}
	if (FTextProperty* TextProperty = FindFProperty<FTextProperty>(Target->GetClass(), PropertyName))
	{
		TextProperty->SetPropertyValue_InContainer(Target, Value);
	}
	else if (FStrProperty* StringProperty = FindFProperty<FStrProperty>(Target->GetClass(), PropertyName))
	{
		StringProperty->SetPropertyValue_InContainer(Target, Value.ToString());
	}
	else if (FNameProperty* NameProperty = FindFProperty<FNameProperty>(Target->GetClass(), PropertyName))
	{
		NameProperty->SetPropertyValue_InContainer(Target, FName(*Value.ToString()));
	}
}

FString UProjectTattooShopInputSubsystem::GetRuntimeWidgetTextProperty(UObject* Target, const FName PropertyName) const
{
	if (!IsValid(Target))
	{
		return FString();
	}
	if (FTextProperty* TextProperty = FindFProperty<FTextProperty>(Target->GetClass(), PropertyName))
	{
		return TextProperty->GetPropertyValue_InContainer(Target).ToString();
	}
	if (FStrProperty* StringProperty = FindFProperty<FStrProperty>(Target->GetClass(), PropertyName))
	{
		return StringProperty->GetPropertyValue_InContainer(Target);
	}
	if (FNameProperty* NameProperty = FindFProperty<FNameProperty>(Target->GetClass(), PropertyName))
	{
		return NameProperty->GetPropertyValue_InContainer(Target).ToString();
	}
	return FString();
}

void UProjectTattooShopInputSubsystem::LayoutRuntimeTattooCardPanel(UPanelWidget* Panel, const int32 ColumnLimit) const
{
	if (!IsValid(Panel))
	{
		return;
	}
	const int32 SafeColumnLimit = FMath::Max(1, ColumnLimit);
	int32 VisibleCardIndex = 0;
	for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
	{
		UWidget* Child = Panel->GetChildAt(ChildIndex);
		if (!IsValid(Child) || ResolveTattooCardIdentity(Child).IsEmpty())
		{
			continue;
		}
		if (UGridSlot* GridSlot = Cast<UGridSlot>(Child->Slot))
		{
			GridSlot->SetColumn(VisibleCardIndex % SafeColumnLimit);
			GridSlot->SetRow(VisibleCardIndex / SafeColumnLimit);
		}
		++VisibleCardIndex;
	}
}

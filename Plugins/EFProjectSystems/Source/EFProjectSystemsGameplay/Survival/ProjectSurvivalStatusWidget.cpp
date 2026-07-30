#include "Survival/ProjectSurvivalStatusWidget.h"

#include "EFProjectUIPalette.h"
#include "Survival/ProjectSurvivalStatusCatalog.h"
#include "Survival/ProjectSurvivalStatusComponent.h"
#include "Survival/ProjectSurvivalStatusSettings.h"
#include "UI/ProjectWidgetClassResolver.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Blueprint.h"
#include "GameFramework/PlayerController.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Styling/CoreStyle.h"

namespace ProjectSurvivalStatusWidgetPrivate
{
	const FLinearColor BlackoutColor(0.f, 0.f, 0.f, 1.f);
	const FLinearColor HiddenPanelTint(0.f, 0.f, 0.f, 0.f);
	FLinearColor FallbackTextTint()
	{
		return EFProjectUIPalette::PrimaryText();
	}
	FLinearColor FallbackDescriptionTint()
	{
		return EFProjectUIPalette::SecondaryText();
	}
	FLinearColor FallbackMetaTint()
	{
		return EFProjectUIPalette::SecondaryText(0.90f);
	}
	const FVector2D DefaultSlotSize(300.f, 110.f);
	const FVector2D DefaultIconSize(70.f, 70.f);
	const FVector2D DefaultPanelSize(1320.f, 52.f);
	const FVector2D DefaultHudOffset(456.f, 466.f);
	const FName StatusSlotsRootPath(TEXT("/Game/_Game/Widgets/Status/Slots"));
	constexpr int32 SlotsPerColumn = 4;

	TMap<FName, TWeakObjectPtr<UClass>> GIndividualSlotClassCache;

	bool HasPositiveSize(const FVector2D& Value)
	{
		return Value.X > 0.f && Value.Y > 0.f;
	}

	bool IsLegacyGeneratedSize(const FVector2D& Value, const FVector2D& LegacySize)
	{
		return FMath::IsNearlyEqual(Value.X, LegacySize.X, 0.01f)
			&& FMath::IsNearlyEqual(Value.Y, LegacySize.Y, 0.01f);
	}

	FVector2D ResolveConfiguredSlotSize(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		if (!HasPositiveSize(Snapshot.HudSlotSize)
			|| IsLegacyGeneratedSize(Snapshot.HudSlotSize, FVector2D(58.f, 58.f)))
		{
			return DefaultSlotSize;
		}

		return Snapshot.HudSlotSize;
	}

	FVector2D ResolveIconSize(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		if (!HasPositiveSize(Snapshot.HudIconSize)
			|| IsLegacyGeneratedSize(Snapshot.HudIconSize, FVector2D(40.f, 40.f)))
		{
			return DefaultIconSize;
		}

		return Snapshot.HudIconSize;
	}

	struct FStatusSlotLayoutMetrics
	{
		FVector2D SlotSize = DefaultSlotSize;
		FVector2D IconSize = DefaultIconSize;
		float TextLeft = 36.f;
		float TextRight = 4.f;
		float NameTop = 4.f;
		float DescriptionTop = 18.f;
		float TextWrapAt = 160.f;
	};

	int32 ResolveFontSize(const int32 FontSize, const int32 FallbackSize)
	{
		return FMath::Max(1, FontSize > 0 ? FontSize : FallbackSize);
	}

	FStatusSlotLayoutMetrics ResolveSlotLayoutMetrics(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		FStatusSlotLayoutMetrics Metrics;
		Metrics.IconSize = ResolveIconSize(Snapshot);

		const int32 NameFontSize = ResolveFontSize(Snapshot.HudNameFontSize, 18);
		const int32 DescriptionFontSize = ResolveFontSize(Snapshot.HudDescriptionFontSize, 13);
		const int32 MetaFontSize = ResolveFontSize(Snapshot.HudMetaFontSize, 9);
		const bool bUsesExpandedVisuals =
			Metrics.IconSize.X > DefaultIconSize.X + 1.f
			|| Metrics.IconSize.Y > DefaultIconSize.Y + 1.f
			|| NameFontSize > 10
			|| DescriptionFontSize > 8
			|| MetaFontSize > 9;

		Metrics.TextLeft = bUsesExpandedVisuals ? FMath::Max(36.f, Metrics.IconSize.X + 12.f) : 36.f;
		Metrics.TextRight = bUsesExpandedVisuals ? 12.f : 4.f;
		Metrics.DescriptionTop = bUsesExpandedVisuals ? FMath::Max(18.f, 6.f + static_cast<float>(NameFontSize) + 2.f) : 18.f;
		Metrics.SlotSize = ResolveConfiguredSlotSize(Snapshot);

		if (bUsesExpandedVisuals)
		{
			const float MinimumTextWidth = FMath::Max(
				112.f,
				FMath::Max(static_cast<float>(NameFontSize) * 6.f, static_cast<float>(DescriptionFontSize) * 12.f));
			const float MinimumWidth = Metrics.TextLeft + MinimumTextWidth + Metrics.TextRight;
			const float MinimumHeight = FMath::Max(
				Metrics.IconSize.Y + 8.f,
				Metrics.DescriptionTop + static_cast<float>(DescriptionFontSize) * 2.4f + 8.f);
			Metrics.SlotSize.X = FMath::Max(Metrics.SlotSize.X, MinimumWidth);
			Metrics.SlotSize.Y = FMath::Max(Metrics.SlotSize.Y, MinimumHeight);
		}

		Metrics.TextWrapAt = FMath::Max(
			bUsesExpandedVisuals ? 90.f : 40.f,
			Metrics.SlotSize.X - Metrics.TextLeft - Metrics.TextRight);
		return Metrics;
	}

	FVector2D ResolveSlotSize(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		return ResolveSlotLayoutMetrics(Snapshot).SlotSize;
	}

	float ResolveStatusSlotSpacing()
	{
		const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
		return Settings ? FMath::Max(0.f, Settings->StatusIconSpacing) : 8.f;
	}

	float ResolveStatusColumnSpacing()
	{
		return FMath::Max(ResolveStatusSlotSpacing(), 16.f);
	}

	FVector2D ResolveStatusGridOrigin(const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D ResolveStatusGridPosition(
		const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots,
		const int32 Index,
		const FVector2D& Origin)
	{
		if (!VisibleSnapshots.IsValidIndex(Index))
		{
			return Origin;
		}

		const int32 Column = Index / SlotsPerColumn;
		const int32 Row = Index % SlotsPerColumn;
		const float RowSpacing = ResolveStatusSlotSpacing();
		const float ColumnSpacing = ResolveStatusColumnSpacing();

		float PositionX = Origin.X;
		for (int32 ColumnIndex = 0; ColumnIndex < Column; ++ColumnIndex)
		{
			float ColumnWidth = 0.f;
			for (int32 RowIndex = 0; RowIndex < SlotsPerColumn; ++RowIndex)
			{
				const int32 CandidateIndex = (ColumnIndex * SlotsPerColumn) + RowIndex;
				if (!VisibleSnapshots.IsValidIndex(CandidateIndex))
				{
					continue;
				}

				ColumnWidth = FMath::Max(ColumnWidth, ResolveSlotSize(VisibleSnapshots[CandidateIndex]).X);
			}
			PositionX += ColumnWidth + ColumnSpacing;
		}

		float PositionY = Origin.Y;
		for (int32 RowIndex = 0; RowIndex < Row; ++RowIndex)
		{
			const int32 CandidateIndex = (Column * SlotsPerColumn) + RowIndex;
			if (!VisibleSnapshots.IsValidIndex(CandidateIndex))
			{
				continue;
			}

			PositionY += ResolveSlotSize(VisibleSnapshots[CandidateIndex]).Y + RowSpacing;
		}

		return FVector2D(PositionX, PositionY) + VisibleSnapshots[Index].HudSlotOffset;
	}

	void ApplyOverlaySlotLayout(
		UWidget* Widget,
		const EHorizontalAlignment HorizontalAlignment,
		const EVerticalAlignment VerticalAlignment,
		const FMargin& Padding)
	{
		if (UOverlaySlot* OverlaySlot = Widget ? Cast<UOverlaySlot>(Widget->Slot) : nullptr)
		{
			OverlaySlot->SetHorizontalAlignment(HorizontalAlignment);
			OverlaySlot->SetVerticalAlignment(VerticalAlignment);
			OverlaySlot->SetPadding(Padding);
		}
	}

	void ApplySizeBoxSlotLayout(
		UWidget* Widget,
		const EHorizontalAlignment HorizontalAlignment,
		const EVerticalAlignment VerticalAlignment)
	{
		if (USizeBoxSlot* SizeBoxSlot = Widget ? Cast<USizeBoxSlot>(Widget->Slot) : nullptr)
		{
			SizeBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
			SizeBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}

	void ApplyCanvasSlotSize(UWidget* Widget, const FVector2D& Size)
	{
		if (UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
		{
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetSize(Size);
		}
	}

	void ClearSizeBoxLimits(USizeBox* SizeBox)
	{
		if (!SizeBox)
		{
			return;
		}

		SizeBox->ClearMinDesiredWidth();
		SizeBox->ClearMinDesiredHeight();
		SizeBox->ClearMaxDesiredWidth();
		SizeBox->ClearMaxDesiredHeight();
	}

	void ApplySizeBoxSize(USizeBox* SizeBox, const FVector2D& Size)
	{
		if (!SizeBox)
		{
			return;
		}

		ClearSizeBoxLimits(SizeBox);
		SizeBox->SetWidthOverride(Size.X);
		SizeBox->SetHeightOverride(Size.Y);
		ApplyCanvasSlotSize(SizeBox, Size);
	}

	bool IsInvisibleColor(const FLinearColor& Value)
	{
		return Value.A <= KINDA_SMALL_NUMBER;
	}

	bool IsPlainWhite(const FLinearColor& Value)
	{
		return Value.A > KINDA_SMALL_NUMBER
			&& Value.R >= 0.98f
			&& Value.G >= 0.98f
			&& Value.B >= 0.98f;
	}

	FLinearColor ResolveTextColor(const FLinearColor& Value, const FLinearColor& Fallback)
	{
		return IsInvisibleColor(Value) ? Fallback : Value;
	}

	float PreserveAuthoredAlpha(const FLinearColor& AuthoredColor, const float FallbackAlpha = 1.0f)
	{
		return FMath::IsFinite(AuthoredColor.A)
			? FMath::Clamp(AuthoredColor.A, 0.0f, 1.0f)
			: FMath::Clamp(FallbackAlpha, 0.0f, 1.0f);
	}

	FLinearColor WithAuthoredAlpha(
		const FLinearColor& SemanticColor,
		const FLinearColor& AuthoredColor,
		const float FallbackAlpha = 1.0f)
	{
		return SemanticColor.CopyWithNewOpacity(
			PreserveAuthoredAlpha(AuthoredColor, FallbackAlpha));
	}

	bool IsPositiveStatus(const FName StatusName)
	{
		return StatusName == TEXT("GraceStep")
			|| StatusName == TEXT("ExhaustedRecovery");
	}

	bool IsSevereNegativeStatus(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		return Snapshot.DamagePerSecond > KINDA_SMALL_NUMBER
			|| Snapshot.bBlocksHealthRecovery
			|| Snapshot.bTriggersExhaustionSequence
			|| Snapshot.StatusName == TEXT("ExtremePain")
			|| Snapshot.StatusName == TEXT("KnockedOut");
	}

	FLinearColor ResolveStatusSemanticTint(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		const float Alpha = PreserveAuthoredAlpha(Snapshot.Tint);

		if (IsPositiveStatus(Snapshot.StatusName))
		{
			return EFProjectUIPalette::Positive(Alpha);
		}

		if (IsSevereNegativeStatus(Snapshot))
		{
			return EFProjectUIPalette::Negative(Alpha);
		}

		if (Snapshot.StatusName == TEXT("Frenzy")
			|| Snapshot.StatusName == TEXT("Fear"))
		{
			return EFProjectUIPalette::Warning(Alpha);
		}

		if (Snapshot.StatusName == TEXT("Cursed"))
		{
			return EFProjectUIPalette::InnerStateCurse(Alpha);
		}

		if (Snapshot.StatusName == TEXT("Tired")
			|| Snapshot.StatusName == TEXT("SleepDeprived")
			|| Snapshot.StatusName == TEXT("Exhausted"))
		{
			return EFProjectUIPalette::InnerStateSleep(Alpha);
		}

		if (Snapshot.StatusName == TEXT("Dirty"))
		{
			return EFProjectUIPalette::OutlineDim(Alpha);
		}

		if (Snapshot.StatusName == TEXT("Sweaty")
			|| Snapshot.StatusName == TEXT("Dizzy"))
		{
			return EFProjectUIPalette::AccentMuted(Alpha);
		}

		if (!Snapshot.SourceNeedName.IsNone())
		{
			return EFProjectUIPalette::InnerStateForName(
				Snapshot.SourceNeedName,
				Alpha);
		}

		return EFProjectUIPalette::Accent(Alpha);
	}

	FProjectSurvivalStatusSnapshot ResolveThemedStatusSnapshot(
		const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		FProjectSurvivalStatusSnapshot Resolved = Snapshot;
		Resolved.Tint = ResolveStatusSemanticTint(Snapshot);
		Resolved.HudNameTextColor = WithAuthoredAlpha(
			EFProjectUIPalette::PrimaryText(),
			Snapshot.HudNameTextColor);
		Resolved.HudDescriptionTextColor = WithAuthoredAlpha(
			EFProjectUIPalette::SecondaryText(),
			Snapshot.HudDescriptionTextColor,
			0.96f);
		Resolved.HudMetaTextColor = WithAuthoredAlpha(
			EFProjectUIPalette::SecondaryText(),
			Snapshot.HudMetaTextColor,
			0.90f);
		return Resolved;
	}

	TArray<FProjectSurvivalStatusSnapshot> ResolveThemedStatusSnapshots(
		const TArray<FProjectSurvivalStatusSnapshot>& Snapshots)
	{
		TArray<FProjectSurvivalStatusSnapshot> ResolvedSnapshots;
		ResolvedSnapshots.Reserve(Snapshots.Num());
		for (const FProjectSurvivalStatusSnapshot& Snapshot : Snapshots)
		{
			ResolvedSnapshots.Add(ResolveThemedStatusSnapshot(Snapshot));
		}
		return ResolvedSnapshots;
	}

	FLinearColor ResolveFallbackGlyphColor(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		return IsPlainWhite(Snapshot.Tint)
			? ResolveTextColor(Snapshot.HudMetaTextColor, FallbackMetaTint())
			: Snapshot.Tint;
	}

	FSlateFontInfo ResolveFontInfo(
		const TSoftObjectPtr<UObject>& FontAsset,
		const int32 FontSize,
		const FName Typeface,
		const int32 FallbackSize)
	{
		const int32 ResolvedSize = FMath::Max(1, FontSize > 0 ? FontSize : FallbackSize);
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle(Typeface, ResolvedSize);
		if (!FontAsset.IsNull())
		{
			if (UObject* FontObject = FontAsset.LoadSynchronous())
			{
				FontInfo.FontObject = FontObject;
			}
		}
		return FontInfo;
	}

	void ApplyTextStyle(
		UTextBlock* TextBlock,
		const TSoftObjectPtr<UObject>& FontAsset,
		const int32 FontSize,
		const FName Typeface,
		const int32 FallbackSize,
		const FLinearColor& Color,
		const FLinearColor& FallbackColor,
		const FVector2D& RenderOffset)
	{
		if (!TextBlock)
		{
			return;
		}

		TextBlock->SetFont(ResolveFontInfo(FontAsset, FontSize, Typeface, FallbackSize));
		TextBlock->SetColorAndOpacity(FSlateColor(ResolveTextColor(Color, FallbackColor)));
		TextBlock->SetRenderTranslation(RenderOffset);
	}

	void AddCanvasChild(
		UCanvasPanel* Canvas,
		UWidget* Widget,
		const FAnchors& Anchors,
		const FVector2D& Alignment,
		const FVector2D& Position,
		const FVector2D& Size,
		const int32 ZOrder,
		const bool bAutoSize = false)
	{
		if (!Canvas || !Widget)
		{
			return;
		}

		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetAnchors(Anchors);
			Slot->SetAlignment(Alignment);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
			Slot->SetZOrder(ZOrder);
			Slot->SetAutoSize(bAutoSize);
		}
	}

	FText MakeDurationText(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		if (!Snapshot.bTimed || Snapshot.RemainingDurationSeconds <= 0.f)
		{
			return FText::GetEmpty();
		}

		return FText::FromString(FString::Printf(TEXT("%.0fs"), FMath::CeilToFloat(Snapshot.RemainingDurationSeconds)));
	}

	FText MakeDamageText(const FProjectSurvivalStatusSnapshot& Snapshot)
	{
		if (Snapshot.DamagePerSecond <= 0.f)
		{
			return FText::GetEmpty();
		}

		return FText::FromString(FString::Printf(TEXT("%.1f/s"), Snapshot.DamagePerSecond));
	}

	const FProjectSurvivalStatusDefinition* FindCatalogStatusDefinition(const FName StatusName)
	{
		if (StatusName.IsNone())
		{
			return nullptr;
		}

		for (const FProjectSurvivalStatusDefinition& Definition : GetProjectSurvivalStatusCatalog().StatusDefinitions)
		{
			if (Definition.StatusName == StatusName)
			{
				return &Definition;
			}
		}

		return nullptr;
	}

	FName ExtractStatusNameFromDesignerContext(const FCodeWidgetDesignerBuildContext& BuildContext)
	{
		if (FindCatalogStatusDefinition(BuildContext.PriorityGroup))
		{
			return BuildContext.PriorityGroup;
		}

		TArray<FString> Candidates;
		Candidates.Add(BuildContext.AssetNameOverride);
		Candidates.Add(FPackageName::GetLongPackageAssetName(BuildContext.TargetAssetPath));
		Candidates.Add(BuildContext.TargetAssetPath);

		for (FString Candidate : Candidates)
		{
			Candidate.RemoveFromStart(TEXT("WBP_ProjectSurvivalStatus_"));
			Candidate.RemoveFromStart(TEXT("ProjectSurvivalStatus_"));
			Candidate.RemoveFromEnd(TEXT("Slot"));

			if (FindCatalogStatusDefinition(FName(*Candidate)))
			{
				return FName(*Candidate);
			}
		}

		return TEXT("Bleeding");
	}

	FProjectSurvivalStatusSnapshot MakeDesignerPreviewStatusSnapshot(const FName StatusName)
	{
		const FProjectSurvivalStatusDefinition* Definition = FindCatalogStatusDefinition(StatusName);
		if (!Definition)
		{
			Definition = FindCatalogStatusDefinition(TEXT("Bleeding"));
		}

		FProjectSurvivalStatusSnapshot Snapshot;
		if (Definition)
		{
			Snapshot.StatusName = Definition->StatusName;
			Snapshot.DisplayName = Definition->DisplayName;
			Snapshot.Description = Definition->Description;
			Snapshot.SourceNeedName = Definition->SourceNeedName;
			Snapshot.MinimalIconName = Definition->MinimalIconName;
			Snapshot.DamagePerSecond = Definition->DamagePerSecond;
			Snapshot.RemainingDurationSeconds = Definition->DurationSeconds > 0.f ? Definition->DurationSeconds : 0.f;
			Snapshot.bBlocksHealthRecovery = Definition->bBlocksHealthRecovery;
			Snapshot.bTriggersExhaustionSequence = Definition->bTriggersExhaustionSequence;
			Snapshot.bInvertMovementInput = Definition->bInvertMovementInput;
			Snapshot.MovementInputScale = Definition->MovementInputScale;
			Snapshot.bTimed = Definition->DurationSeconds > 0.f;
			Snapshot.Tint = Definition->Tint;
			Snapshot.HudPriority = Definition->HudPriority;
			Snapshot.HudSlotSize = Definition->HudSlotSize;
			Snapshot.HudIconSize = Definition->HudIconSize;
			Snapshot.HudIconSlotOffset = Definition->HudIconSlotOffset;
			Snapshot.HudSlotOffset = FVector2D::ZeroVector;
			Snapshot.HudNameFontAsset = Definition->HudNameFontAsset;
			Snapshot.HudDescriptionFontAsset = Definition->HudDescriptionFontAsset;
			Snapshot.HudMetaFontAsset = Definition->HudMetaFontAsset;
			Snapshot.HudNameFontSize = Definition->HudNameFontSize;
			Snapshot.HudDescriptionFontSize = Definition->HudDescriptionFontSize;
			Snapshot.HudMetaFontSize = Definition->HudMetaFontSize;
			Snapshot.HudNameTextColor = Definition->HudNameTextColor;
			Snapshot.HudDescriptionTextColor = Definition->HudDescriptionTextColor;
			Snapshot.HudMetaTextColor = Definition->HudMetaTextColor;
			Snapshot.HudNameTextOffset = Definition->HudNameTextOffset;
			Snapshot.HudDescriptionTextOffset = Definition->HudDescriptionTextOffset;
			Snapshot.HudDurationTextOffset = Definition->HudDurationTextOffset;
			Snapshot.HudDamageTextOffset = Definition->HudDamageTextOffset;
		}

		Snapshot.bActive = true;
		return ResolveThemedStatusSnapshot(Snapshot);
	}

	FProjectSurvivalStatusSnapshot MakeDesignerPreviewStatusSnapshot(const FCodeWidgetDesignerBuildContext& BuildContext)
	{
		return MakeDesignerPreviewStatusSnapshot(ExtractStatusNameFromDesignerContext(BuildContext));
	}

	TArray<FProjectSurvivalStatusSnapshot> MakeDesignerPreviewStatusSnapshots()
	{
		TArray<FProjectSurvivalStatusSnapshot> Snapshots;
		const FName PreviewStatusNames[] = {
			TEXT("Bleeding"),
			TEXT("Frenzy"),
			TEXT("Cursed"),
			TEXT("Dirty"),
			TEXT("Dizzy")
		};

		for (const FName StatusName : PreviewStatusNames)
		{
			Snapshots.Add(MakeDesignerPreviewStatusSnapshot(StatusName));
		}

		return Snapshots;
	}

	void AddDesignerPreviewStatusSlot(
		UWidgetTree* TargetWidgetTree,
		UCanvasPanel* TargetCanvas,
		const TArray<FProjectSurvivalStatusSnapshot>& PreviewSnapshots,
		const FProjectSurvivalStatusSnapshot& Snapshot,
		const int32 Index,
		const FString& NamePrefix)
	{
		if (!TargetWidgetTree || !TargetCanvas)
		{
			return;
		}

		const FStatusSlotLayoutMetrics Layout = ResolveSlotLayoutMetrics(Snapshot);
		const FVector2D Position = ResolveStatusGridPosition(PreviewSnapshots, Index, FVector2D::ZeroVector);
		const FString SafePrefix = NamePrefix.IsEmpty() ? TEXT("DesignerPreviewStatus") : NamePrefix;
		const FName RootName(*FString::Printf(TEXT("%s_%s"), *SafePrefix, *Snapshot.StatusName.ToString()));

		USizeBox* PreviewRoot = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), RootName);
		PreviewRoot->SetWidthOverride(Layout.SlotSize.X);
		PreviewRoot->SetHeightOverride(Layout.SlotSize.Y);
		AddCanvasChild(
			TargetCanvas,
			PreviewRoot,
			FAnchors(0.f, 0.f),
			FVector2D::ZeroVector,
			Position,
			Layout.SlotSize,
			Index);

		UOverlay* PreviewOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Overlay_%s"), *SafePrefix, *Snapshot.StatusName.ToString())));
		PreviewRoot->AddChild(PreviewOverlay);

		UTextBlock* PreviewGlyph = TargetWidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Glyph_%s"), *SafePrefix, *Snapshot.StatusName.ToString())));
		PreviewGlyph->SetFont(ResolveFontInfo(Snapshot.HudMetaFontAsset, Snapshot.HudMetaFontSize + 6, TEXT("Bold"), 15));
		PreviewGlyph->SetJustification(ETextJustify::Center);
		PreviewGlyph->SetColorAndOpacity(FSlateColor(ResolveFallbackGlyphColor(Snapshot)));
		PreviewGlyph->SetText(FText::FromString(Snapshot.DisplayName.IsEmpty() ? TEXT("?") : Snapshot.DisplayName.Left(1)));
		if (UOverlaySlot* GlyphSlot = PreviewOverlay->AddChildToOverlay(PreviewGlyph))
		{
			GlyphSlot->SetHorizontalAlignment(HAlign_Left);
			GlyphSlot->SetVerticalAlignment(VAlign_Center);
			GlyphSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		}

		UTextBlock* PreviewName = TargetWidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Name_%s"), *SafePrefix, *Snapshot.StatusName.ToString())));
		ApplyTextStyle(PreviewName, Snapshot.HudNameFontAsset, Snapshot.HudNameFontSize, TEXT("Bold"), 18, Snapshot.HudNameTextColor, FallbackTextTint(), Snapshot.HudNameTextOffset);
		PreviewName->SetJustification(ETextJustify::Left);
		PreviewName->SetWrapTextAt(Layout.TextWrapAt);
		PreviewName->SetText(FText::FromString(Snapshot.DisplayName));
		if (UOverlaySlot* NameSlot = PreviewOverlay->AddChildToOverlay(PreviewName))
		{
			NameSlot->SetHorizontalAlignment(HAlign_Left);
			NameSlot->SetVerticalAlignment(VAlign_Top);
			NameSlot->SetPadding(FMargin(Layout.TextLeft, Layout.NameTop, Layout.TextRight, 0.f));
		}

		UTextBlock* PreviewDescription = TargetWidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Description_%s"), *SafePrefix, *Snapshot.StatusName.ToString())));
		ApplyTextStyle(PreviewDescription, Snapshot.HudDescriptionFontAsset, Snapshot.HudDescriptionFontSize, TEXT("Regular"), 13, Snapshot.HudDescriptionTextColor, FallbackDescriptionTint(), Snapshot.HudDescriptionTextOffset);
		PreviewDescription->SetJustification(ETextJustify::Left);
		PreviewDescription->SetWrapTextAt(Layout.TextWrapAt);
		PreviewDescription->SetText(FText::FromString(Snapshot.Description));
		PreviewDescription->SetVisibility(Snapshot.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* DescriptionSlot = PreviewOverlay->AddChildToOverlay(PreviewDescription))
		{
			DescriptionSlot->SetHorizontalAlignment(HAlign_Left);
			DescriptionSlot->SetVerticalAlignment(VAlign_Top);
			DescriptionSlot->SetPadding(FMargin(Layout.TextLeft, Layout.DescriptionTop, Layout.TextRight, 0.f));
		}

		UTextBlock* PreviewDuration = TargetWidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Duration_%s"), *SafePrefix, *Snapshot.StatusName.ToString())));
		ApplyTextStyle(PreviewDuration, Snapshot.HudMetaFontAsset, Snapshot.HudMetaFontSize, TEXT("Bold"), 9, Snapshot.HudMetaTextColor, FallbackMetaTint(), Snapshot.HudDurationTextOffset);
		PreviewDuration->SetJustification(ETextJustify::Right);
		const FText DurationText = MakeDurationText(Snapshot);
		PreviewDuration->SetText(DurationText);
		PreviewDuration->SetVisibility(DurationText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* DurationSlot = PreviewOverlay->AddChildToOverlay(PreviewDuration))
		{
			DurationSlot->SetHorizontalAlignment(HAlign_Right);
			DurationSlot->SetVerticalAlignment(VAlign_Top);
			DurationSlot->SetPadding(FMargin(0.f, 4.f, 4.f, 0.f));
		}

		UTextBlock* PreviewDamage = TargetWidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Damage_%s"), *SafePrefix, *Snapshot.StatusName.ToString())));
		ApplyTextStyle(PreviewDamage, Snapshot.HudMetaFontAsset, Snapshot.HudMetaFontSize, TEXT("Bold"), 9, Snapshot.HudMetaTextColor, FallbackMetaTint(), Snapshot.HudDamageTextOffset);
		PreviewDamage->SetJustification(ETextJustify::Right);
		const FText DamageText = MakeDamageText(Snapshot);
		PreviewDamage->SetText(DamageText);
		PreviewDamage->SetVisibility(DamageText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		if (UOverlaySlot* DamageSlot = PreviewOverlay->AddChildToOverlay(PreviewDamage))
		{
			DamageSlot->SetHorizontalAlignment(HAlign_Right);
			DamageSlot->SetVerticalAlignment(VAlign_Bottom);
			DamageSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 4.f));
		}
	}

	void AddDesignerPreviewStatusSlots(UWidgetTree* TargetWidgetTree, UCanvasPanel* TargetCanvas)
	{
		if (!TargetWidgetTree || !TargetCanvas)
		{
			return;
		}

		const TArray<FProjectSurvivalStatusSnapshot> PreviewSnapshots = MakeDesignerPreviewStatusSnapshots();
		for (int32 Index = 0; Index < PreviewSnapshots.Num(); ++Index)
		{
			AddDesignerPreviewStatusSlot(TargetWidgetTree, TargetCanvas, PreviewSnapshots, PreviewSnapshots[Index], Index, TEXT("DesignerPreviewStatusSlot"));
		}
	}

	bool GetGeneratedClassObjectPath(const FAssetData& AssetData, FString& OutClassObjectPath)
	{
		FString GeneratedClassExportPath;
		if (!AssetData.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassExportPath)
			&& !AssetData.GetTagValue(FName(TEXT("GeneratedClassPath")), GeneratedClassExportPath))
		{
			return false;
		}

		OutClassObjectPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassExportPath);
		return !OutClassObjectPath.IsEmpty();
	}

	UClass* DiscoverIndividualSlotClass(const FName StatusName)
	{
		if (StatusName.IsNone())
		{
			return nullptr;
		}

		if (TWeakObjectPtr<UClass>* CachedClass = GIndividualSlotClassCache.Find(StatusName))
		{
			return CachedClass->Get();
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		AssetRegistry.ScanPathsSynchronous({ StatusSlotsRootPath.ToString() }, true);

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(StatusSlotsRootPath);
		Filter.bRecursiveClasses = true;
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
		{
			return Left.PackageName.LexicalLess(Right.PackageName);
		});

		const FString StatusNameString = StatusName.ToString();
		for (const FAssetData& AssetData : Assets)
		{
			const FString PackageName = AssetData.PackageName.ToString();
			if (!PackageName.Contains(StatusNameString))
			{
				continue;
			}

			FString GeneratedClassObjectPath;
			if (!GetGeneratedClassObjectPath(AssetData, GeneratedClassObjectPath))
			{
				continue;
			}

			UClass* GeneratedClass = LoadObject<UClass>(nullptr, *GeneratedClassObjectPath);
			if (GeneratedClass
				&& GeneratedClass != UProjectSurvivalStatusSlotWidget::StaticClass()
				&& GeneratedClass->IsChildOf(UProjectSurvivalStatusSlotWidget::StaticClass()))
			{
				GIndividualSlotClassCache.Add(StatusName, GeneratedClass);
				return GeneratedClass;
			}
		}

		GIndividualSlotClassCache.Add(StatusName, nullptr);
		return nullptr;
	}
}

UProjectSurvivalStatusSlotWidget::UProjectSurvivalStatusSlotWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UProjectSurvivalStatusSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyStatusVisualData();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectSurvivalStatusSlotWidget::SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext)
{
	DesignerBuildContext = InBuildContext;
}

bool UProjectSurvivalStatusSlotWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	const bool bBuilt = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuilt)
	{
		CurrentSnapshot = ProjectSurvivalStatusWidgetPrivate::MakeDesignerPreviewStatusSnapshot(DesignerBuildContext);
		InitializeVisualTree();
		ApplyStatusVisualData();
	}
	return bBuilt;
}

void UProjectSurvivalStatusSlotWidget::ApplyStatusSnapshot(const FProjectSurvivalStatusSnapshot& InSnapshot)
{
	// Snapshot/DataTable colors are authored configuration, not live visual
	// state. Resolve their semantic roles on every data refresh so a stale
	// serialized RGB value cannot overwrite the currently selected HUD theme.
	CurrentSnapshot = ProjectSurvivalStatusWidgetPrivate::ResolveThemedStatusSnapshot(InSnapshot);
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyStatusVisualData();
	OnStatusSlotDataApplied(CurrentSnapshot);
}

void UProjectSurvivalStatusSlotWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	Super::NativeOnProjectThemeApplied(Preset, Theme, Revision);

	if (CurrentSnapshot.StatusName.IsNone())
	{
		return;
	}

	CurrentSnapshot =
		ProjectSurvivalStatusWidgetPrivate::ResolveThemedStatusSnapshot(CurrentSnapshot);
	ApplyStatusVisualData();
	OnStatusSlotDataApplied(CurrentSnapshot);
}

FProjectSurvivalStatusSnapshot UProjectSurvivalStatusSlotWidget::GetCurrentStatusSnapshot() const
{
	return CurrentSnapshot;
}

void UProjectSurvivalStatusSlotWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootSizeBox || WidgetTree->RootWidget)
	{
		if (!SlotRootCanvas)
		{
			SlotRootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		}
		if (!RootSizeBox)
		{
			RootSizeBox = Cast<USizeBox>(WidgetTree->RootWidget);
		}
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectSurvivalStatusSlotWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	SlotRootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SlotRootCanvas"));
	TargetWidgetTree->RootWidget = SlotRootCanvas;

	RootSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	RootSizeBox->SetWidthOverride(ProjectSurvivalStatusWidgetPrivate::DefaultSlotSize.X);
	RootSizeBox->SetHeightOverride(ProjectSurvivalStatusWidgetPrivate::DefaultSlotSize.Y);
	ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
		SlotRootCanvas,
		RootSizeBox,
		FAnchors(0.f, 0.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		ProjectSurvivalStatusWidgetPrivate::DefaultSlotSize,
		0);

	RootOverlay = TargetWidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	RootSizeBox->AddChild(RootOverlay);

	SlotBackgroundBorder = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotBackgroundBorder"));
	SlotBackgroundBorder->SetBrushColor(ProjectSurvivalStatusWidgetPrivate::HiddenPanelTint);
	SlotBackgroundBorder->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* BackgroundSlot = RootOverlay->AddChildToOverlay(SlotBackgroundBorder))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	IconSizeBox = TargetWidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconSizeBox"));
	IconSizeBox->SetWidthOverride(ProjectSurvivalStatusWidgetPrivate::DefaultIconSize.X);
	IconSizeBox->SetHeightOverride(ProjectSurvivalStatusWidgetPrivate::DefaultIconSize.Y);
	if (UOverlaySlot* IconSlot = RootOverlay->AddChildToOverlay(IconSizeBox))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Left);
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
	}

	IconImage = TargetWidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
	IconSizeBox->AddChild(IconImage);
	ProjectSurvivalStatusWidgetPrivate::ApplySizeBoxSlotLayout(IconImage, HAlign_Center, VAlign_Center);

	FallbackText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FallbackText"));
	if (UOverlaySlot* FallbackSlot = RootOverlay->AddChildToOverlay(FallbackText))
	{
		FallbackSlot->SetHorizontalAlignment(HAlign_Left);
		FallbackSlot->SetVerticalAlignment(VAlign_Center);
		FallbackSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
	}

	NameText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NameText"));
	if (UOverlaySlot* NameSlot = RootOverlay->AddChildToOverlay(NameText))
	{
		NameSlot->SetHorizontalAlignment(HAlign_Left);
		NameSlot->SetVerticalAlignment(VAlign_Top);
		NameSlot->SetPadding(FMargin(36.f, 4.f, 4.f, 0.f));
	}

	DescriptionText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DescriptionText"));
	DescriptionText->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* DescriptionSlot = RootOverlay->AddChildToOverlay(DescriptionText))
	{
		DescriptionSlot->SetHorizontalAlignment(HAlign_Left);
		DescriptionSlot->SetVerticalAlignment(VAlign_Top);
		DescriptionSlot->SetPadding(FMargin(36.f, 18.f, 4.f, 0.f));
	}

	DurationText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DurationText"));
	if (UOverlaySlot* DurationSlot = RootOverlay->AddChildToOverlay(DurationText))
	{
		DurationSlot->SetHorizontalAlignment(HAlign_Right);
		DurationSlot->SetVerticalAlignment(VAlign_Top);
		DurationSlot->SetPadding(FMargin(0.f, 4.f, 4.f, 0.f));
	}

	DamageText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DamageText"));
	if (UOverlaySlot* DamageSlot = RootOverlay->AddChildToOverlay(DamageText))
	{
		DamageSlot->SetHorizontalAlignment(HAlign_Right);
		DamageSlot->SetVerticalAlignment(VAlign_Bottom);
		DamageSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 4.f));
	}

	return true;
}

void UProjectSurvivalStatusSlotWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (bUsingNativeFallbackTree)
	{
		if (SlotBackgroundBorder)
		{
			SlotBackgroundBorder->SetBrushColor(ProjectSurvivalStatusWidgetPrivate::HiddenPanelTint);
			SlotBackgroundBorder->SetPadding(FMargin(0.f));
			SlotBackgroundBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (FallbackText)
		{
			FallbackText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15));
			FallbackText->SetJustification(ETextJustify::Center);
			FallbackText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::FallbackMetaTint()));
		}
		if (NameText)
		{
			NameText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10));
			NameText->SetJustification(ETextJustify::Left);
			NameText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::FallbackTextTint()));
			NameText->SetWrapTextAt(112.f);
		}
		if (DescriptionText)
		{
			DescriptionText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
			DescriptionText->SetJustification(ETextJustify::Left);
			DescriptionText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::FallbackDescriptionTint()));
			DescriptionText->SetWrapTextAt(112.f);
		}
		if (DurationText)
		{
			DurationText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9));
			DurationText->SetJustification(ETextJustify::Right);
			DurationText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::FallbackMetaTint()));
		}
		if (DamageText)
		{
			DamageText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9));
			DamageText->SetJustification(ETextJustify::Right);
			DamageText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::FallbackMetaTint()));
		}
	}

	bVisualTreeInitialized = true;
}

void UProjectSurvivalStatusSlotWidget::ApplyStatusVisualData()
{
	const ProjectSurvivalStatusWidgetPrivate::FStatusSlotLayoutMetrics Layout =
		ProjectSurvivalStatusWidgetPrivate::ResolveSlotLayoutMetrics(CurrentSnapshot);
	const FVector2D SlotSize = Layout.SlotSize;
	if (RootSizeBox)
	{
		ProjectSurvivalStatusWidgetPrivate::ApplySizeBoxSize(RootSizeBox, SlotSize);
	}

	const FVector2D IconSize = Layout.IconSize;
	if (IconSizeBox)
	{
		ProjectSurvivalStatusWidgetPrivate::ApplySizeBoxSize(IconSizeBox, IconSize);
		IconSizeBox->SetRenderTranslation(CurrentSnapshot.HudIconSlotOffset);
	}
	if (IconImage)
	{
		IconImage->SetDesiredSizeOverride(IconSize);
		ProjectSurvivalStatusWidgetPrivate::ApplyCanvasSlotSize(IconImage, IconSize);
		ProjectSurvivalStatusWidgetPrivate::ApplySizeBoxSlotLayout(IconImage, HAlign_Center, VAlign_Center);
	}

	ProjectSurvivalStatusWidgetPrivate::ApplyOverlaySlotLayout(
		IconSizeBox,
		HAlign_Left,
		VAlign_Center,
		FMargin(4.f, 0.f, 0.f, 0.f));
	ProjectSurvivalStatusWidgetPrivate::ApplyOverlaySlotLayout(
		FallbackText,
		HAlign_Left,
		VAlign_Center,
		FMargin(4.f, 0.f, 0.f, 0.f));
	ProjectSurvivalStatusWidgetPrivate::ApplyOverlaySlotLayout(
		NameText,
		HAlign_Left,
		VAlign_Top,
		FMargin(Layout.TextLeft, Layout.NameTop, Layout.TextRight, 0.f));
	ProjectSurvivalStatusWidgetPrivate::ApplyOverlaySlotLayout(
		DescriptionText,
		HAlign_Left,
		VAlign_Top,
		FMargin(Layout.TextLeft, Layout.DescriptionTop, Layout.TextRight, 0.f));

	if (SlotBackgroundBorder)
	{
		SlotBackgroundBorder->SetBrushColor(ProjectSurvivalStatusWidgetPrivate::HiddenPanelTint);
		SlotBackgroundBorder->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IconImage)
	{
		if (CurrentSnapshot.IconTexture)
		{
			IconImage->SetBrushFromTexture(ResolveProjectThemeTexture(CurrentSnapshot.IconTexture), true);
			FSlateBrush Brush = IconImage->GetBrush();
			Brush.ImageSize = IconSize;
			IconImage->SetBrush(Brush);
			IconImage->SetDesiredSizeOverride(IconSize);
			ProjectSurvivalStatusWidgetPrivate::ApplyCanvasSlotSize(IconImage, IconSize);
			IconImage->SetColorAndOpacity(ResolveProjectThemeImageTint(
				IconImage,
				CurrentSnapshot.Tint));
			IconImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (FallbackText)
	{
		const FString FallbackLabel = CurrentSnapshot.DisplayName.IsEmpty()
			? TEXT("?")
			: CurrentSnapshot.DisplayName.Left(1);
		FallbackText->SetFont(ProjectSurvivalStatusWidgetPrivate::ResolveFontInfo(
			CurrentSnapshot.HudMetaFontAsset,
			CurrentSnapshot.HudMetaFontSize + 6,
			TEXT("Bold"),
			15));
		FallbackText->SetText(FText::FromString(FallbackLabel));
		FallbackText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::ResolveFallbackGlyphColor(CurrentSnapshot)));
		FallbackText->SetVisibility(CurrentSnapshot.IconTexture ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (NameText)
	{
		ProjectSurvivalStatusWidgetPrivate::ApplyTextStyle(
			NameText,
			CurrentSnapshot.HudNameFontAsset,
			CurrentSnapshot.HudNameFontSize,
			TEXT("Bold"),
			18,
			CurrentSnapshot.HudNameTextColor,
			ProjectSurvivalStatusWidgetPrivate::FallbackTextTint(),
			CurrentSnapshot.HudNameTextOffset);
		NameText->SetJustification(ETextJustify::Left);
		NameText->SetWrapTextAt(Layout.TextWrapAt);
		NameText->SetText(FText::FromString(CurrentSnapshot.DisplayName));
	}

	if (DescriptionText)
	{
		ProjectSurvivalStatusWidgetPrivate::ApplyTextStyle(
			DescriptionText,
			CurrentSnapshot.HudDescriptionFontAsset,
			CurrentSnapshot.HudDescriptionFontSize,
			TEXT("Regular"),
			13,
			CurrentSnapshot.HudDescriptionTextColor,
			ProjectSurvivalStatusWidgetPrivate::FallbackDescriptionTint(),
			CurrentSnapshot.HudDescriptionTextOffset);
		DescriptionText->SetJustification(ETextJustify::Left);
		DescriptionText->SetWrapTextAt(Layout.TextWrapAt);
		DescriptionText->SetText(FText::FromString(CurrentSnapshot.Description));
		DescriptionText->SetVisibility(CurrentSnapshot.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (DurationText)
	{
		ProjectSurvivalStatusWidgetPrivate::ApplyTextStyle(
			DurationText,
			CurrentSnapshot.HudMetaFontAsset,
			CurrentSnapshot.HudMetaFontSize,
			TEXT("Bold"),
			9,
			CurrentSnapshot.HudMetaTextColor,
			ProjectSurvivalStatusWidgetPrivate::FallbackMetaTint(),
			CurrentSnapshot.HudDurationTextOffset);
		DurationText->SetJustification(ETextJustify::Right);
		const FText Duration = ProjectSurvivalStatusWidgetPrivate::MakeDurationText(CurrentSnapshot);
		DurationText->SetText(Duration);
		DurationText->SetVisibility(Duration.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (DamageText)
	{
		ProjectSurvivalStatusWidgetPrivate::ApplyTextStyle(
			DamageText,
			CurrentSnapshot.HudMetaFontAsset,
			CurrentSnapshot.HudMetaFontSize,
			TEXT("Bold"),
			9,
			CurrentSnapshot.HudMetaTextColor,
			ProjectSurvivalStatusWidgetPrivate::FallbackMetaTint(),
			CurrentSnapshot.HudDamageTextOffset);
		DamageText->SetJustification(ETextJustify::Right);
		const FText Damage = ProjectSurvivalStatusWidgetPrivate::MakeDamageText(CurrentSnapshot);
		DamageText->SetText(Damage);
		DamageText->SetVisibility(Damage.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

UProjectSurvivalStatusSlotsGlobalWidget::UProjectSurvivalStatusSlotsGlobalWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UProjectSurvivalStatusSlotsGlobalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectSurvivalStatusSlotsGlobalWidget::SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext)
{
	DesignerBuildContext = InBuildContext;
}

bool UProjectSurvivalStatusSlotsGlobalWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bUsingNativeFallbackTree = true;
	const bool bBuilt = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuilt && SlotsCanvas)
	{
		ProjectSurvivalStatusWidgetPrivate::AddDesignerPreviewStatusSlots(TargetWidgetTree, SlotsCanvas);
		OverflowCount = 2;
		SyncOverflowText(ProjectSurvivalStatusWidgetPrivate::MakeDesignerPreviewStatusSnapshots());
	}
	return bBuilt;
}

void UProjectSurvivalStatusSlotsGlobalWidget::ApplyStatusSnapshots(
	const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots,
	const int32 InOverflowCount,
	TSubclassOf<UProjectSurvivalStatusSlotWidget> DefaultSlotWidgetClass)
{
	BuildWidgetTree();

	const TArray<FProjectSurvivalStatusSnapshot> ThemedSnapshots =
		ProjectSurvivalStatusWidgetPrivate::ResolveThemedStatusSnapshots(VisibleSnapshots);
	OverflowCount = FMath::Max(0, InOverflowCount);
	if (DoesLayoutNeedRebuild(ThemedSnapshots))
	{
		RebuildStatusSlots(ThemedSnapshots, DefaultSlotWidgetClass);
	}

	for (int32 Index = 0; Index < ThemedSnapshots.Num(); ++Index)
	{
		const FProjectSurvivalStatusSnapshot& Snapshot = ThemedSnapshots[Index];
		if (TObjectPtr<UProjectSurvivalStatusSlotWidget>* ExistingSlot = SlotWidgetsByName.Find(Snapshot.StatusName))
		{
			if (UProjectSurvivalStatusSlotWidget* SlotWidget = ExistingSlot->Get())
			{
				SlotWidget->ApplyStatusSnapshot(Snapshot);
				ApplySlotLayout(SlotWidget, Snapshot, Index, ThemedSnapshots);
			}
		}
	}

	SyncOverflowText(ThemedSnapshots);
	OnStatusSlotsApplied(ThemedSnapshots, OverflowCount);
}

void UProjectSurvivalStatusSlotsGlobalWidget::ClearStatusSlots()
{
	if (SlotsCanvas)
	{
		SlotsCanvas->ClearChildren();
	}
	SlotWidgetsByName.Empty();
	CachedStatusOrder.Reset();
	OverflowCount = 0;
	SyncOverflowText(TArray<FProjectSurvivalStatusSnapshot>());
}

int32 UProjectSurvivalStatusSlotsGlobalWidget::GetOverflowCount() const
{
	return OverflowCount;
}

void UProjectSurvivalStatusSlotsGlobalWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootCanvas || WidgetTree->RootWidget)
	{
		if (!RootCanvas)
		{
			RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		}
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectSurvivalStatusSlotsGlobalWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	TargetWidgetTree->RootWidget = RootCanvas;

	SlotsCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SlotsCanvas"));
	ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
		RootCanvas,
		SlotsCanvas,
		FAnchors(0.f, 0.f, 1.f, 1.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		ProjectSurvivalStatusWidgetPrivate::DefaultPanelSize,
		0);

	OverflowText = TargetWidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OverflowText"));
	OverflowText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15));
	OverflowText->SetColorAndOpacity(FSlateColor(ProjectSurvivalStatusWidgetPrivate::FallbackTextTint()));
	OverflowText->SetVisibility(ESlateVisibility::Collapsed);
	ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
		RootCanvas,
		OverflowText,
		FAnchors(0.f, 0.f),
		FVector2D::ZeroVector,
		FVector2D(320.f, 16.f),
		FVector2D(48.f, 28.f),
		10,
		true);

	return true;
}

bool UProjectSurvivalStatusSlotsGlobalWidget::DoesLayoutNeedRebuild(
	const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots) const
{
	if (CachedStatusOrder.Num() != VisibleSnapshots.Num())
	{
		return true;
	}

	for (int32 Index = 0; Index < VisibleSnapshots.Num(); ++Index)
	{
		if (CachedStatusOrder[Index] != VisibleSnapshots[Index].StatusName)
		{
			return true;
		}
	}

	return false;
}

void UProjectSurvivalStatusSlotsGlobalWidget::RebuildStatusSlots(
	const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots,
	TSubclassOf<UProjectSurvivalStatusSlotWidget> DefaultSlotWidgetClass)
{
	if (!SlotsCanvas)
	{
		return;
	}

	SlotsCanvas->ClearChildren();
	SlotWidgetsByName.Empty();
	CachedStatusOrder.Reset();

	for (const FProjectSurvivalStatusSnapshot& Snapshot : VisibleSnapshots)
	{
		TSubclassOf<UProjectSurvivalStatusSlotWidget> SlotClass = ResolveSlotWidgetClassForStatus(
			Snapshot.StatusName,
			DefaultSlotWidgetClass);
		if (!SlotClass)
		{
			SlotClass = UProjectSurvivalStatusSlotWidget::StaticClass();
		}

		UProjectSurvivalStatusSlotWidget* SlotWidget = nullptr;
		if (APlayerController* OwningPlayer = GetOwningPlayer())
		{
			SlotWidget = CreateWidget<UProjectSurvivalStatusSlotWidget>(OwningPlayer, SlotClass);
		}
		if (!SlotWidget && GetWorld())
		{
			SlotWidget = CreateWidget<UProjectSurvivalStatusSlotWidget>(GetWorld(), SlotClass);
		}
		if (!SlotWidget)
		{
			continue;
		}

		SlotsCanvas->AddChildToCanvas(SlotWidget);
		SlotWidgetsByName.Add(Snapshot.StatusName, SlotWidget);
		CachedStatusOrder.Add(Snapshot.StatusName);
	}
}

void UProjectSurvivalStatusSlotsGlobalWidget::ApplySlotLayout(
	UProjectSurvivalStatusSlotWidget* SlotWidget,
	const FProjectSurvivalStatusSnapshot& Snapshot,
	const int32 Index,
	const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots) const
{
	if (!SlotWidget)
	{
		return;
	}

	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(SlotWidget->Slot);
	if (!CanvasSlot)
	{
		return;
	}

	const FVector2D SlotSize = ProjectSurvivalStatusWidgetPrivate::ResolveSlotSize(Snapshot);
	const FVector2D Origin = ProjectSurvivalStatusWidgetPrivate::ResolveStatusGridOrigin(VisibleSnapshots);
	const FVector2D Position = ProjectSurvivalStatusWidgetPrivate::ResolveStatusGridPosition(VisibleSnapshots, Index, Origin);

	CanvasSlot->SetAutoSize(false);
	CanvasSlot->SetSize(SlotSize);
	CanvasSlot->SetPosition(Position);
	CanvasSlot->SetZOrder(Index);
}

void UProjectSurvivalStatusSlotsGlobalWidget::SyncOverflowText(
	const TArray<FProjectSurvivalStatusSnapshot>& VisibleSnapshots)
{
	if (!OverflowText)
	{
		return;
	}

	if (OverflowCount <= 0)
	{
		OverflowText->SetVisibility(ESlateVisibility::Collapsed);
		OverflowText->SetText(FText::GetEmpty());
		return;
	}

	const int32 LastVisibleIndex = VisibleSnapshots.Num() - 1;
	const FVector2D Origin = ProjectSurvivalStatusWidgetPrivate::ResolveStatusGridOrigin(VisibleSnapshots);
	const FVector2D SlotSize = LastVisibleIndex >= 0
		? ProjectSurvivalStatusWidgetPrivate::ResolveSlotSize(VisibleSnapshots[LastVisibleIndex])
		: ProjectSurvivalStatusWidgetPrivate::DefaultSlotSize;
	const FVector2D SlotPosition = LastVisibleIndex >= 0
		? ProjectSurvivalStatusWidgetPrivate::ResolveStatusGridPosition(VisibleSnapshots, LastVisibleIndex, Origin)
		: Origin;
	const FVector2D Position = SlotPosition + FVector2D(
		SlotSize.X + ProjectSurvivalStatusWidgetPrivate::ResolveStatusColumnSpacing() * 0.5f,
		SlotSize.Y * 0.28f);

	if (UCanvasPanelSlot* OverflowSlot = Cast<UCanvasPanelSlot>(OverflowText->Slot))
	{
		OverflowSlot->SetPosition(Position);
		OverflowSlot->SetAutoSize(true);
		OverflowSlot->SetZOrder(100);
	}

	OverflowText->SetText(FText::FromString(FString::Printf(TEXT("+%d"), OverflowCount)));
	OverflowText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

TSubclassOf<UProjectSurvivalStatusSlotWidget> UProjectSurvivalStatusSlotsGlobalWidget::ResolveSlotWidgetClassForStatus(
	const FName StatusName,
	TSubclassOf<UProjectSurvivalStatusSlotWidget> DefaultSlotWidgetClass) const
{
	if (UClass* IndividualClass = ProjectSurvivalStatusWidgetPrivate::DiscoverIndividualSlotClass(StatusName))
	{
		return IndividualClass;
	}

	return DefaultSlotWidgetClass
		? DefaultSlotWidgetClass
		: TSubclassOf<UProjectSurvivalStatusSlotWidget>(UProjectSurvivalStatusSlotWidget::StaticClass());
}

UProjectSurvivalStatusWidget::UProjectSurvivalStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ZOrder = 180;
}

void UProjectSurvivalStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildWidgetTree();
	InitializeVisualTree();
	ApplyHudVisibility();
	RefreshDisplay();
	ReapplyProjectThemeAfterNativeConstruct();
}

void UProjectSurvivalStatusWidget::NativeDestruct()
{
	StatusComponent = nullptr;
	SlotsGlobalWidget = nullptr;
	Super::NativeDestruct();
}

void UProjectSurvivalStatusWidget::SetCodeWidgetDesignerBuildContext(const FCodeWidgetDesignerBuildContext& InBuildContext)
{
	DesignerBuildContext = InBuildContext;
}

bool UProjectSurvivalStatusWidget::BuildCodeWidgetDesignerTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree)
	{
		return false;
	}

	WidgetTree = TargetWidgetTree;
	bVisualTreeInitialized = false;
	bUsingNativeFallbackTree = true;
	const bool bBuilt = BuildDefaultWidgetTree(TargetWidgetTree);
	if (bBuilt)
	{
		InitializeVisualTree();
		DesignerPreviewCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DesignerPreviewCanvas"));
		const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
		const FVector2D HudOffset = Settings ? Settings->StatusHudOffset : ProjectSurvivalStatusWidgetPrivate::DefaultHudOffset;
		ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
			RootCanvas,
			DesignerPreviewCanvas,
			FAnchors(0.f, 1.f),
			FVector2D(0.f, 1.f),
			FVector2D(HudOffset.X, -HudOffset.Y),
			ProjectSurvivalStatusWidgetPrivate::DefaultPanelSize,
			20);
		ProjectSurvivalStatusWidgetPrivate::AddDesignerPreviewStatusSlots(TargetWidgetTree, DesignerPreviewCanvas);
	}
	return bBuilt;
}

bool UProjectSurvivalStatusWidget::GatherCodeWidgetDesignerConversionManifest(
	FCodeWidgetDesignerConversionManifest& OutManifest) const
{
	OutManifest = FCodeWidgetDesignerConversionManifest();
	OutManifest.SystemName = TEXT("Status");
	OutManifest.RootPath = TEXT("/Game/_Game/Widgets/Status");
	OutManifest.MainFolder = TEXT("Main");
	OutManifest.GlobalFolder = TEXT("Global");
	OutManifest.HostWidget.WidgetClass = UProjectSurvivalStatusWidget::StaticClass();
	OutManifest.HostWidget.TargetAssetPath = TEXT("/Game/_Game/Widgets/Status/Main/WBP_ProjectSurvivalStatusWidget");
	OutManifest.HostWidget.Role = ECodeWidgetDesignerAssetRole::Host;
	OutManifest.HostWidget.PriorityGroup = TEXT("Status");
	OutManifest.HostWidget.PriorityRank = 10000;
	OutManifest.HostWidget.ExpectedWidgetNames = {
		TEXT("RootCanvas"),
		TEXT("BlackoutOverlay"),
		TEXT("SlotsGlobalWidget"),
		TEXT("DesignerPreviewCanvas")
	};
	OutManifest.HostWidget.ExpectedBlueprintEvents = { TEXT("OnStatusHudDataApplied") };

	const auto AddAsset = [&OutManifest](
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& TargetAssetPath,
		const FString& RelativeFolder,
		const FString& AssetNameOverride,
		const ECodeWidgetDesignerAssetRole Role,
		const FName PriorityGroup,
		const int32 PriorityRank,
		const bool bRuntimeDefault,
		const bool bRequiresStableRootWrapper,
		TArray<FName> ExpectedWidgetNames,
		TArray<FName> ExpectedBlueprintEvents)
	{
		FCodeWidgetDesignerWidgetAssetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.TargetAssetPath = TargetAssetPath;
		Spec.RelativeFolder = RelativeFolder;
		Spec.AssetNameOverride = AssetNameOverride;
		Spec.Role = Role;
		Spec.PriorityGroup = PriorityGroup;
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.bRequiresStableRootWrapper = bRequiresStableRootWrapper;
		Spec.ExpectedWidgetNames = MoveTemp(ExpectedWidgetNames);
		Spec.ExpectedBlueprintEvents = MoveTemp(ExpectedBlueprintEvents);
		OutManifest.WidgetAssets.Add(Spec);
	};

	const TArray<FName> SlotWidgetNames = {
		TEXT("SlotRootCanvas"),
		TEXT("RootSizeBox"),
		TEXT("RootOverlay"),
		TEXT("SlotBackgroundBorder"),
		TEXT("IconSizeBox"),
		TEXT("IconImage"),
		TEXT("FallbackText"),
		TEXT("NameText"),
		TEXT("DescriptionText"),
		TEXT("DurationText"),
		TEXT("DamageText")
	};

	AddAsset(
		UProjectSurvivalStatusSlotsGlobalWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/Status/Global/WBP_ProjectSurvivalStatusSlotsGlobal"),
		TEXT("Global"),
		TEXT("WBP_ProjectSurvivalStatusSlotsGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		TEXT("Status"),
		9000,
		true,
		false,
		{ TEXT("RootCanvas"), TEXT("SlotsCanvas"), TEXT("OverflowText") },
		{ TEXT("OnStatusSlotsApplied") });

	AddAsset(
		UProjectSurvivalStatusSlotWidget::StaticClass(),
		TEXT("/Game/_Game/Widgets/Status/Global/WBP_ProjectSurvivalStatusSlotGlobal"),
		TEXT("Global"),
		TEXT("WBP_ProjectSurvivalStatusSlotGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		TEXT("Status"),
		10000,
		true,
		false,
		SlotWidgetNames,
		{ TEXT("OnStatusSlotDataApplied") });

	for (const FProjectSurvivalStatusDefinition& Definition : GetProjectSurvivalStatusCatalog().StatusDefinitions)
	{
		if (Definition.StatusName.IsNone())
		{
			continue;
		}

		const FString AssetName = FString::Printf(TEXT("WBP_ProjectSurvivalStatus_%sSlot"), *Definition.StatusName.ToString());
		AddAsset(
			UProjectSurvivalStatusSlotWidget::StaticClass(),
			FString::Printf(TEXT("/Game/_Game/Widgets/Status/Slots/%s"), *AssetName),
			TEXT("Slots"),
			AssetName,
			ECodeWidgetDesignerAssetRole::Individual,
			Definition.StatusName,
			500,
			false,
			false,
			SlotWidgetNames,
			{ TEXT("OnStatusSlotDataApplied") });
	}

	return true;
}

void UProjectSurvivalStatusWidget::GatherCodeWidgetDesignerChildWidgetSpecs(
	TArray<FCodeWidgetDesignerChildWidgetSpec>& OutWidgetSpecs) const
{
	const auto AddSpec = [&OutWidgetSpecs](
		TSubclassOf<UUserWidget> WidgetClass,
		const FString& RelativeFolder,
		const FString& AssetNameOverride,
		const ECodeWidgetDesignerAssetRole Role,
		const int32 PriorityRank,
		const FName PriorityGroup,
		const bool bRuntimeDefault = false,
		const bool bRequiresStableRootWrapper = false,
		TArray<FName> ExpectedWidgetNames = TArray<FName>(),
		TArray<FName> ExpectedBlueprintEvents = TArray<FName>())
	{
		FCodeWidgetDesignerChildWidgetSpec Spec;
		Spec.WidgetClass = WidgetClass;
		Spec.RelativeFolder = RelativeFolder;
		Spec.AssetNameOverride = AssetNameOverride;
		Spec.Role = Role;
		Spec.PriorityGroup = PriorityGroup;
		Spec.PriorityRank = PriorityRank;
		Spec.bRuntimeDefault = bRuntimeDefault;
		Spec.bRequiresStableRootWrapper = bRequiresStableRootWrapper;
		Spec.ExpectedWidgetNames = MoveTemp(ExpectedWidgetNames);
		Spec.ExpectedBlueprintEvents = MoveTemp(ExpectedBlueprintEvents);
		OutWidgetSpecs.Add(Spec);
	};

	const TArray<FName> SlotWidgetNames = {
		TEXT("SlotRootCanvas"),
		TEXT("RootSizeBox"),
		TEXT("RootOverlay"),
		TEXT("SlotBackgroundBorder"),
		TEXT("IconSizeBox"),
		TEXT("IconImage"),
		TEXT("FallbackText"),
		TEXT("NameText"),
		TEXT("DescriptionText"),
		TEXT("DurationText"),
		TEXT("DamageText")
	};

	AddSpec(
		UProjectSurvivalStatusSlotsGlobalWidget::StaticClass(),
		TEXT("Global"),
		TEXT("WBP_ProjectSurvivalStatusSlotsGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalPanel,
		9000,
		TEXT("Status"),
		true,
		false,
		{ TEXT("RootCanvas"), TEXT("SlotsCanvas"), TEXT("OverflowText") },
		{ TEXT("OnStatusSlotsApplied") });

	AddSpec(
		UProjectSurvivalStatusSlotWidget::StaticClass(),
		TEXT("Global"),
		TEXT("WBP_ProjectSurvivalStatusSlotGlobal"),
		ECodeWidgetDesignerAssetRole::GlobalTemplate,
		10000,
		TEXT("Status"),
		true,
		false,
		SlotWidgetNames,
		{ TEXT("OnStatusSlotDataApplied") });

	for (const FProjectSurvivalStatusDefinition& Definition : GetProjectSurvivalStatusCatalog().StatusDefinitions)
	{
		if (Definition.StatusName.IsNone())
		{
			continue;
		}

		AddSpec(
			UProjectSurvivalStatusSlotWidget::StaticClass(),
			TEXT("Slots"),
			FString::Printf(TEXT("WBP_ProjectSurvivalStatus_%sSlot"), *Definition.StatusName.ToString()),
			ECodeWidgetDesignerAssetRole::Individual,
			500,
			Definition.StatusName,
			false,
			false,
			SlotWidgetNames,
			{ TEXT("OnStatusSlotDataApplied") });
	}
}

void UProjectSurvivalStatusWidget::SetStatusComponent(UProjectSurvivalStatusComponent* InStatusComponent)
{
	if (StatusComponent == InStatusComponent)
	{
		return;
	}

	StatusComponent = InStatusComponent;
	RefreshDisplay();
}

void UProjectSurvivalStatusWidget::RefreshDisplay()
{
	BuildWidgetTree();
	InitializeVisualTree();
	EnsureSlotsGlobalWidget();

	const bool bBlackoutActive = StatusComponent && StatusComponent->IsBlackoutActive();
	ApplyBlackoutVisibility(bBlackoutActive);
	ApplyHudVisibility();

	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	const int32 MaxVisibleStatuses = Settings ? Settings->MaxVisibleStatuses : 5;

	int32 Overflow = 0;
	const TArray<FProjectSurvivalStatusSnapshot> RawVisibleSnapshots = StatusComponent
		? StatusComponent->BuildVisibleStatusSnapshots(MaxVisibleStatuses, Overflow)
		: TArray<FProjectSurvivalStatusSnapshot>();
	const TArray<FProjectSurvivalStatusSnapshot> VisibleSnapshots =
		ProjectSurvivalStatusWidgetPrivate::ResolveThemedStatusSnapshots(
			RawVisibleSnapshots);

	if (SlotsGlobalWidget)
	{
		const bool bShowSlots = bHudVisible && VisibleSnapshots.Num() > 0;
		SlotsGlobalWidget->SetVisibility(bShowSlots ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowSlots)
		{
			SlotsGlobalWidget->ApplyStatusSnapshots(VisibleSnapshots, Overflow, ResolveSlotWidgetClass());
		}
		else
		{
			SlotsGlobalWidget->ClearStatusSlots();
		}
	}

	OnStatusHudDataApplied(VisibleSnapshots, Overflow, bBlackoutActive);
}

void UProjectSurvivalStatusWidget::SetHudVisible(const bool bVisible)
{
	if (bHudVisible == bVisible)
	{
		return;
	}

	bHudVisible = bVisible;
	ApplyHudVisibility();
	RefreshDisplay();
}

bool UProjectSurvivalStatusWidget::IsHudVisible() const
{
	return bHudVisible;
}

void UProjectSurvivalStatusWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	if (RootCanvas || WidgetTree->RootWidget)
	{
		if (!RootCanvas)
		{
			RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		}
		return;
	}

	bUsingNativeFallbackTree = true;
	BuildDefaultWidgetTree(WidgetTree);
}

bool UProjectSurvivalStatusWidget::BuildDefaultWidgetTree(UWidgetTree* TargetWidgetTree)
{
	if (!TargetWidgetTree || TargetWidgetTree->RootWidget)
	{
		return false;
	}

	RootCanvas = TargetWidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	TargetWidgetTree->RootWidget = RootCanvas;

	BlackoutOverlay = TargetWidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BlackoutOverlay"));
	ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
		RootCanvas,
		BlackoutOverlay,
		FAnchors(0.f, 0.f, 1.f, 1.f),
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		FVector2D::ZeroVector,
		0);

	SlotsGlobalWidget = TargetWidgetTree->ConstructWidget<UProjectSurvivalStatusSlotsGlobalWidget>(
		UProjectSurvivalStatusSlotsGlobalWidget::StaticClass(),
		TEXT("SlotsGlobalWidget"));

	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	const FVector2D HudOffset = Settings ? Settings->StatusHudOffset : ProjectSurvivalStatusWidgetPrivate::DefaultHudOffset;
	ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
		RootCanvas,
		SlotsGlobalWidget,
		FAnchors(0.f, 1.f),
		FVector2D(0.f, 1.f),
		FVector2D(HudOffset.X, -HudOffset.Y),
		ProjectSurvivalStatusWidgetPrivate::DefaultPanelSize,
		10);

	return true;
}

void UProjectSurvivalStatusWidget::InitializeVisualTree()
{
	if (bVisualTreeInitialized)
	{
		return;
	}

	if (BlackoutOverlay)
	{
		BlackoutOverlay->SetBrushColor(ProjectSurvivalStatusWidgetPrivate::BlackoutColor);
		BlackoutOverlay->SetPadding(FMargin(0.f));
		BlackoutOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (DesignerPreviewCanvas && !IsDesignTime())
	{
		DesignerPreviewCanvas->SetVisibility(ESlateVisibility::Collapsed);
	}

	bVisualTreeInitialized = true;
}

void UProjectSurvivalStatusWidget::EnsureSlotsGlobalWidget()
{
	if (!RootCanvas)
	{
		return;
	}

	const TSubclassOf<UProjectSurvivalStatusSlotsGlobalWidget> ResolvedClass = ResolveSlotsGlobalWidgetClass();
	if (!ResolvedClass)
	{
		return;
	}

	const bool bShouldReplaceNativeFallback =
		SlotsGlobalWidget
		&& SlotsGlobalWidget->GetClass() == UProjectSurvivalStatusSlotsGlobalWidget::StaticClass()
		&& ResolvedClass.Get() != UProjectSurvivalStatusSlotsGlobalWidget::StaticClass();
	if (SlotsGlobalWidget && !bShouldReplaceNativeFallback)
	{
		return;
	}

	if (SlotsGlobalWidget)
	{
		SlotsGlobalWidget->RemoveFromParent();
		SlotsGlobalWidget = nullptr;
	}

	UProjectSurvivalStatusSlotsGlobalWidget* NewGlobalWidget = nullptr;
	if (APlayerController* OwningPlayer = GetOwningPlayer())
	{
		NewGlobalWidget = CreateWidget<UProjectSurvivalStatusSlotsGlobalWidget>(OwningPlayer, ResolvedClass);
	}
	if (!NewGlobalWidget && GetWorld())
	{
		NewGlobalWidget = CreateWidget<UProjectSurvivalStatusSlotsGlobalWidget>(GetWorld(), ResolvedClass);
	}
	if (!NewGlobalWidget)
	{
		return;
	}

	SlotsGlobalWidget = NewGlobalWidget;
	const UProjectSurvivalStatusSettings* Settings = UProjectSurvivalStatusSettings::Get();
	const FVector2D HudOffset = Settings ? Settings->StatusHudOffset : ProjectSurvivalStatusWidgetPrivate::DefaultHudOffset;
	ProjectSurvivalStatusWidgetPrivate::AddCanvasChild(
		RootCanvas,
		SlotsGlobalWidget,
		FAnchors(0.f, 1.f),
		FVector2D(0.f, 1.f),
		FVector2D(HudOffset.X, -HudOffset.Y),
		ProjectSurvivalStatusWidgetPrivate::DefaultPanelSize,
		10);
}

void UProjectSurvivalStatusWidget::ApplyHudVisibility()
{
	const bool bBlackoutActive = StatusComponent && StatusComponent->IsBlackoutActive();
	SetVisibility((bHudVisible || bBlackoutActive) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

	if (SlotsGlobalWidget)
	{
		SlotsGlobalWidget->SetVisibility(bHudVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UProjectSurvivalStatusWidget::ApplyBlackoutVisibility(const bool bBlackoutActive)
{
	if (BlackoutOverlay)
	{
		BlackoutOverlay->SetVisibility(bBlackoutActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

TSubclassOf<UProjectSurvivalStatusSlotsGlobalWidget> UProjectSurvivalStatusWidget::ResolveSlotsGlobalWidgetClass() const
{
	if (!SlotsGlobalWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = SlotsGlobalWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	if (UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		UProjectSurvivalStatusSlotsGlobalWidget::StaticClass(),
		TEXT("ProjectSurvivalStatusSlotsGlobal")))
	{
		return ResolvedClass;
	}

	return UProjectSurvivalStatusSlotsGlobalWidget::StaticClass();
}

TSubclassOf<UProjectSurvivalStatusSlotWidget> UProjectSurvivalStatusWidget::ResolveSlotWidgetClass() const
{
	if (!SlotWidgetClass.IsNull())
	{
		if (UClass* LoadedClass = SlotWidgetClass.LoadSynchronous())
		{
			return LoadedClass;
		}
	}

	if (UClass* ResolvedClass = ProjectWidgetClassResolver::ResolveWidgetClass(
		FSoftClassPath(),
		UProjectSurvivalStatusSlotWidget::StaticClass(),
		TEXT("ProjectSurvivalStatusSlot")))
	{
		return ResolvedClass;
	}

	return UProjectSurvivalStatusSlotWidget::StaticClass();
}

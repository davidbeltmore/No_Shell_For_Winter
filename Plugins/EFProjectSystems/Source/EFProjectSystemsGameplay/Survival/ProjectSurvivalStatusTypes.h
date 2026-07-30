#pragma once

#include "CoreMinimal.h"
#include "EFProjectUIPalette.h"
#include "Engine/DataTable.h"
#include "ProjectSurvivalStatusTypes.generated.h"

class UTexture2D;
class UObject;

UENUM(BlueprintType)
enum class EProjectSurvivalStatusRefreshPolicy : uint8
{
	RefreshDuration UMETA(DisplayName = "Refresh Duration"),
	IgnoreIfActive UMETA(DisplayName = "Ignore If Active")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalStatusAttributeModifier
{
	GENERATED_BODY()

	FProjectSurvivalStatusAttributeModifier()
		: AttributeName(NAME_None)
		, Multiplier(1.f)
	{
	}

	FProjectSurvivalStatusAttributeModifier(const FName InAttributeName, const float InMultiplier)
		: AttributeName(InAttributeName)
		, Multiplier(InMultiplier)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float Multiplier;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalStatusNeedDecayModifier
{
	GENERATED_BODY()

	FProjectSurvivalStatusNeedDecayModifier()
		: NeedName(NAME_None)
		, DecayMultiplier(1.f)
	{
	}

	FProjectSurvivalStatusNeedDecayModifier(const FName InNeedName, const float InDecayMultiplier)
		: NeedName(InNeedName)
		, DecayMultiplier(InDecayMultiplier)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName NeedName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DecayMultiplier;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalStatusSensationModifier
{
	GENERATED_BODY()

	FProjectSurvivalStatusSensationModifier()
		: SensationName(NAME_None)
		, DeltaPerSecond(0.f)
	{
	}

	FProjectSurvivalStatusSensationModifier(const FName InSensationName, const float InDeltaPerSecond)
		: SensationName(InSensationName)
		, DeltaPerSecond(InDeltaPerSecond)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName SensationName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DeltaPerSecond;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalStatusDefinition
{
	GENERATED_BODY()

	FProjectSurvivalStatusDefinition()
		: StatusName(NAME_None)
		, DisplayName(TEXT(""))
		, Description(TEXT(""))
		, SourceNeedName(NAME_None)
		, IconTextureAsset(nullptr)
		, MinimalIconName(NAME_None)
		, DamagePerSecond(0.f)
		, DurationSeconds(0.f)
		, bBlocksHealthRecovery(false)
		, bTriggerAtNeedEmpty(false)
		, bTriggersExhaustionSequence(false)
		, bInvertMovementInput(false)
		, MovementInputScale(1.f)
		, ReapplyPolicy(EProjectSurvivalStatusRefreshPolicy::RefreshDuration)
		, Tint(FLinearColor::White)
		, HudPriority(0)
		, HudSlotSize(FVector2D(300.f, 110.f))
		, HudIconSize(FVector2D(70.f, 70.f))
		, HudIconSlotOffset(FVector2D(0.f, -17.f))
		, HudSlotOffset(FVector2D::ZeroVector)
		, HudNameFontAsset(nullptr)
		, HudDescriptionFontAsset(nullptr)
		, HudMetaFontAsset(nullptr)
		, HudNameFontSize(18)
		, HudDescriptionFontSize(13)
		, HudMetaFontSize(9)
		, HudNameTextColor(EFProjectUIPalette::PrimaryText())
		, HudDescriptionTextColor(EFProjectUIPalette::SecondaryText())
		, HudMetaTextColor(EFProjectUIPalette::SecondaryText(0.90f))
		, HudNameTextOffset(FVector2D::ZeroVector)
		, HudDescriptionTextOffset(FVector2D::ZeroVector)
		, HudDurationTextOffset(FVector2D::ZeroVector)
		, HudDamageTextOffset(FVector2D::ZeroVector)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName StatusName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName SourceNeedName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TSoftObjectPtr<UTexture2D> IconTextureAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName MinimalIconName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DamagePerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bBlocksHealthRecovery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bTriggerAtNeedEmpty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bTriggersExhaustionSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bInvertMovementInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MovementInputScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	EProjectSurvivalStatusRefreshPolicy ReapplyPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalStatusAttributeModifier> AttributeModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalStatusNeedDecayModifier> NeedDecayModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalStatusSensationModifier> SensationModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FLinearColor Tint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	int32 HudPriority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudSlotSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudIconSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudIconSlotOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudSlotOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudNameFontAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudDescriptionFontAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudMetaFontAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text", meta = (ClampMin = "1", UIMin = "1"))
	int32 HudNameFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text", meta = (ClampMin = "1", UIMin = "1"))
	int32 HudDescriptionFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text", meta = (ClampMin = "1", UIMin = "1"))
	int32 HudMetaFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FLinearColor HudNameTextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FLinearColor HudDescriptionTextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FLinearColor HudMetaTextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudNameTextOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudDescriptionTextOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudDurationTextOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudDamageTextOffset;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalStatusTableRow : public FTableRowBase
{
	GENERATED_BODY()

	FProjectSurvivalStatusTableRow()
		: StatusName(NAME_None)
		, DisplayName(TEXT(""))
		, Description(TEXT(""))
		, SourceNeedName(NAME_None)
		, IconTextureAsset(nullptr)
		, MinimalIconName(NAME_None)
		, DamagePerSecond(0.f)
		, DurationSeconds(0.f)
		, bBlocksHealthRecovery(false)
		, bTriggerAtNeedEmpty(false)
		, bTriggersExhaustionSequence(false)
		, bInvertMovementInput(false)
		, MovementInputScale(1.f)
		, ReapplyPolicy(EProjectSurvivalStatusRefreshPolicy::RefreshDuration)
		, Tint(FLinearColor::White)
		, HudPriority(0)
		, HudSlotSize(FVector2D(300.f, 110.f))
		, HudIconSize(FVector2D(70.f, 70.f))
		, HudIconSlotOffset(FVector2D(0.f, -17.f))
		, HudSlotOffset(FVector2D::ZeroVector)
		, HudNameFontAsset(nullptr)
		, HudDescriptionFontAsset(nullptr)
		, HudMetaFontAsset(nullptr)
		, HudNameFontSize(18)
		, HudDescriptionFontSize(13)
		, HudMetaFontSize(9)
		, HudNameTextColor(EFProjectUIPalette::PrimaryText())
		, HudDescriptionTextColor(EFProjectUIPalette::SecondaryText())
		, HudMetaTextColor(EFProjectUIPalette::SecondaryText(0.90f))
		, HudNameTextOffset(FVector2D::ZeroVector)
		, HudDescriptionTextOffset(FVector2D::ZeroVector)
		, HudDurationTextOffset(FVector2D::ZeroVector)
		, HudDamageTextOffset(FVector2D::ZeroVector)
	{
	}

	FProjectSurvivalStatusDefinition ToStatusDefinition(const FName RowName) const
	{
		FProjectSurvivalStatusDefinition Definition;
		Definition.StatusName = StatusName.IsNone() ? RowName : StatusName;
		Definition.DisplayName = DisplayName;
		Definition.Description = Description;
		Definition.SourceNeedName = SourceNeedName;
		Definition.IconTextureAsset = IconTextureAsset;
		Definition.MinimalIconName = MinimalIconName;
		Definition.DamagePerSecond = DamagePerSecond;
		Definition.DurationSeconds = DurationSeconds;
		Definition.bBlocksHealthRecovery = bBlocksHealthRecovery;
		Definition.bTriggerAtNeedEmpty = bTriggerAtNeedEmpty;
		Definition.bTriggersExhaustionSequence = bTriggersExhaustionSequence;
		Definition.bInvertMovementInput = bInvertMovementInput;
		Definition.MovementInputScale = MovementInputScale;
		Definition.ReapplyPolicy = ReapplyPolicy;
		Definition.AttributeModifiers = AttributeModifiers;
		Definition.NeedDecayModifiers = NeedDecayModifiers;
		Definition.SensationModifiers = SensationModifiers;
		Definition.Tint = Tint;
		Definition.HudPriority = HudPriority;
		Definition.HudSlotSize = HudSlotSize;
		Definition.HudIconSize = HudIconSize;
		Definition.HudIconSlotOffset = HudIconSlotOffset;
		Definition.HudSlotOffset = HudSlotOffset;
		Definition.HudNameFontAsset = HudNameFontAsset;
		Definition.HudDescriptionFontAsset = HudDescriptionFontAsset;
		Definition.HudMetaFontAsset = HudMetaFontAsset;
		Definition.HudNameFontSize = HudNameFontSize;
		Definition.HudDescriptionFontSize = HudDescriptionFontSize;
		Definition.HudMetaFontSize = HudMetaFontSize;
		Definition.HudNameTextColor = HudNameTextColor;
		Definition.HudDescriptionTextColor = HudDescriptionTextColor;
		Definition.HudMetaTextColor = HudMetaTextColor;
		Definition.HudNameTextOffset = HudNameTextOffset;
		Definition.HudDescriptionTextOffset = HudDescriptionTextOffset;
		Definition.HudDurationTextOffset = HudDurationTextOffset;
		Definition.HudDamageTextOffset = HudDamageTextOffset;
		return Definition;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName StatusName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName SourceNeedName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TSoftObjectPtr<UTexture2D> IconTextureAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FName MinimalIconName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DamagePerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float DurationSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bBlocksHealthRecovery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bTriggerAtNeedEmpty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bTriggersExhaustionSequence;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	bool bInvertMovementInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	float MovementInputScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	EProjectSurvivalStatusRefreshPolicy ReapplyPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalStatusAttributeModifier> AttributeModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalStatusNeedDecayModifier> NeedDecayModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	TArray<FProjectSurvivalStatusSensationModifier> SensationModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival")
	FLinearColor Tint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	int32 HudPriority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudSlotSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudIconSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudIconSlotOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD")
	FVector2D HudSlotOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudNameFontAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudDescriptionFontAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudMetaFontAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text", meta = (ClampMin = "1", UIMin = "1"))
	int32 HudNameFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text", meta = (ClampMin = "1", UIMin = "1"))
	int32 HudDescriptionFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text", meta = (ClampMin = "1", UIMin = "1"))
	int32 HudMetaFontSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FLinearColor HudNameTextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FLinearColor HudDescriptionTextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FLinearColor HudMetaTextColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudNameTextOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudDescriptionTextOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudDurationTextOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|HUD|Text")
	FVector2D HudDamageTextOffset;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectSurvivalStatusSnapshot
{
	GENERATED_BODY()

	FProjectSurvivalStatusSnapshot()
		: StatusName(NAME_None)
		, DisplayName(TEXT(""))
		, Description(TEXT(""))
		, SourceNeedName(NAME_None)
		, MinimalIconName(NAME_None)
		, DamagePerSecond(0.f)
		, RemainingDurationSeconds(0.f)
		, bBlocksHealthRecovery(false)
		, bTriggersExhaustionSequence(false)
		, bInvertMovementInput(false)
		, MovementInputScale(1.f)
		, bActive(false)
		, bTimed(false)
		, IconTexture(nullptr)
		, Tint(FLinearColor::White)
		, HudPriority(0)
		, HudSlotSize(FVector2D(300.f, 110.f))
		, HudIconSize(FVector2D(70.f, 70.f))
		, HudIconSlotOffset(FVector2D(0.f, -17.f))
		, HudSlotOffset(FVector2D::ZeroVector)
		, HudNameFontAsset(nullptr)
		, HudDescriptionFontAsset(nullptr)
		, HudMetaFontAsset(nullptr)
		, HudNameFontSize(18)
		, HudDescriptionFontSize(13)
		, HudMetaFontSize(9)
		, HudNameTextColor(EFProjectUIPalette::PrimaryText())
		, HudDescriptionTextColor(EFProjectUIPalette::SecondaryText())
		, HudMetaTextColor(EFProjectUIPalette::SecondaryText(0.90f))
		, HudNameTextOffset(FVector2D::ZeroVector)
		, HudDescriptionTextOffset(FVector2D::ZeroVector)
		, HudDurationTextOffset(FVector2D::ZeroVector)
		, HudDamageTextOffset(FVector2D::ZeroVector)
	{
	}

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	FName StatusName;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	FName SourceNeedName;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	FName MinimalIconName;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float DamagePerSecond;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float RemainingDurationSeconds;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	bool bBlocksHealthRecovery;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	bool bTriggersExhaustionSequence;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	bool bInvertMovementInput;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float MovementInputScale;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	bool bActive;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	bool bTimed;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Survival")
	TObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	FLinearColor Tint;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD")
	int32 HudPriority;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD")
	FVector2D HudSlotSize;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD")
	FVector2D HudIconSize;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD")
	FVector2D HudIconSlotOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD")
	FVector2D HudSlotOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudNameFontAsset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudDescriptionFontAsset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	TSoftObjectPtr<UObject> HudMetaFontAsset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	int32 HudNameFontSize;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	int32 HudDescriptionFontSize;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	int32 HudMetaFontSize;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FLinearColor HudNameTextColor;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FLinearColor HudDescriptionTextColor;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FLinearColor HudMetaTextColor;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FVector2D HudNameTextOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FVector2D HudDescriptionTextOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FVector2D HudDurationTextOffset;

	UPROPERTY(BlueprintReadOnly, Category = "Survival|HUD|Text")
	FVector2D HudDamageTextOffset;
};

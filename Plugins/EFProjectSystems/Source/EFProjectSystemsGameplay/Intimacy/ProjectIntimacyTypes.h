#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include "ProjectIntimacyTypes.generated.h"

UENUM(BlueprintType)
enum class EProjectIntimacyPersonality : uint8
{
	Auto UMETA(DisplayName = "Auto"),
	Chill UMETA(DisplayName = "Chill"),
	Nice UMETA(DisplayName = "Nice"),
	Stallion UMETA(DisplayName = "Stallion")
};

UENUM(BlueprintType)
enum class EProjectIntimacyRelationship : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	Familiar UMETA(DisplayName = "Familiar"),
	Close UMETA(DisplayName = "Close"),
	Ally UMETA(DisplayName = "Ally")
};

UENUM(BlueprintType)
enum class EProjectIntimacyHudMode : uint8
{
	Main UMETA(DisplayName = "Main"),
	Talk UMETA(DisplayName = "Talk"),
	Items UMETA(DisplayName = "Items"),
	Please UMETA(DisplayName = "Please")
};

/** Session-local recipient for Climax gains and orgasm presentation. */
UENUM(BlueprintType)
enum class EProjectIntimacyClimaxTarget : uint8
{
	Player UMETA(DisplayName = "Player"),
	Partner UMETA(DisplayName = "Partner")
};

/** Orgasm Rush is deliberately local to an active Intimacy session. */
UENUM(BlueprintType)
enum class EProjectIntimacySessionState : uint8
{
	BuildingClimax UMETA(DisplayName = "Building Climax"),
	OrgasmRush UMETA(DisplayName = "Orgasm Rush")
};

UENUM(BlueprintType)
enum class EProjectIntimacyTalkAction : uint8
{
	None = 0 UMETA(DisplayName = "None"),
	SpeedSlow = 1 UMETA(DisplayName = "Speed Slow"),
	SpeedNormal = 2 UMETA(DisplayName = "Speed Normal"),
	SpeedIntense = 3 UMETA(DisplayName = "Speed Intense"),
	Compliment = 4 UMETA(DisplayName = "Compliment"),
	Unavailable05 = 5 UMETA(Hidden),
	More = 6 UMETA(DisplayName = "More"),
	Unavailable07 = 7 UMETA(Hidden),
	Back = 8 UMETA(DisplayName = "Back")
};

UENUM(BlueprintType)
enum class EProjectIntimacyEligibilityFailure : uint8
{
	None UMETA(DisplayName = "None"),
	ContentDisabled UMETA(DisplayName = "Content Disabled"),
	PlayerNotAdultVerified UMETA(DisplayName = "Player Not Adult Verified"),
	PartnerNotAdultVerified UMETA(DisplayName = "Partner Not Adult Verified"),
	ConsentMissing UMETA(DisplayName = "Consent Missing"),
	ParticipantDead UMETA(DisplayName = "Participant Dead"),
	ParticipantUnconscious UMETA(DisplayName = "Participant Unconscious"),
	PartnerHostile UMETA(DisplayName = "Partner Hostile"),
	InCombat UMETA(DisplayName = "In Combat"),
	ZoneNotAllowed UMETA(DisplayName = "Zone Not Allowed"),
	CharismaMasteryRequired UMETA(DisplayName = "Charisma Mastery Required")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyEligibilityContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bContentAllowed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPlayerAdultVerified = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPartnerAdultVerified = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bExplicitConsent = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPlayerAlive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPartnerAlive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPlayerConscious = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPartnerConscious = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bPartnerNonHostile = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bOutsideCombat = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bZoneAllowed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Eligibility")
	bool bCharismaMasteryUnlocked = false;
};

UENUM(BlueprintType)
enum class EProjectIntimacyMediaType : uint8
{
	None UMETA(DisplayName = "None"),
	Image UMETA(DisplayName = "Image"),
	Gif UMETA(DisplayName = "GIF"),
	Video UMETA(DisplayName = "Video"),
	Animation UMETA(DisplayName = "Animation")
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyPartnerProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FString PartnerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 Encounters = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 SatisfiedWins = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 SessionPeakCount = 0;

	/** Historical player orgasms reached in sessions with this partner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 PlayerOrgasmCount = 0;

	/** Historical partner orgasms. SessionPeakCount remains as a save compatibility alias. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 PartnerOrgasmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FDateTime FirstEncounterUtc = FDateTime();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float TotalIntimateTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyPersonality Personality = EProjectIntimacyPersonality::Nice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyRelationship Relationship = EProjectIntimacyRelationship::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTagContainer RelationshipTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTag GenderTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 Affect = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bHasFirstEncounter = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bHasHusbandRing = false;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyTalkOptionRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyTalkAction Action = EProjectIntimacyTalkAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName CategoryId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTag RequiredPersonalityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTagContainer TalkTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float SessionProgressGain = 0.0f;

	/** Preferred rework field. Legacy rows fall back to SessionProgressGain when this is zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float ClimaxGain = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyClimaxTarget ClimaxTarget = EProjectIntimacyClimaxTarget::Partner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 AffectDelta = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float AnimationRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bCanBeCorrectTalkOption = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bUsesTalkCooldown = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bCanBeFlavorCorrectOption = true;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyPartnerResponseRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTag PersonalityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText AcceptedResponse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText RefusedResponse;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyMediaCueRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName CueId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName TriggerOptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyTalkAction TriggerTalkAction = EProjectIntimacyTalkAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName TriggerEventId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyMediaType MediaType = EProjectIntimacyMediaType::Image;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FSoftObjectPath TextureAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FString SourceImagePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FSoftObjectPath SoundAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float HoldSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FVector2D SourceMediaSize = FVector2D(1080.0f, 720.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyPersonalityRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyPersonality Personality = EProjectIntimacyPersonality::Nice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTag PersonalityTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float DefaultAnimationRate = 1.0f;

};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyTalkAffinityRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyPersonality Personality = EProjectIntimacyPersonality::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTag RelationshipTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTagContainer PreferredTalkTags;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyItemEffectRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float SessionProgressBonus = 0.0f;

	/** Preferred rework field. Legacy rows fall back to SessionProgressBonus when this is zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float ClimaxBonus = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyClimaxTarget ClimaxTarget = EProjectIntimacyClimaxTarget::Partner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 AffectBonus = 0;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacyHudOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName OptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText Label;
};

USTRUCT(BlueprintType)
struct EFPROJECTSYSTEMSGAMEPLAY_API FProjectIntimacySessionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bHudVisible = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyHudMode HudMode = EProjectIntimacyHudMode::Main;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText PartnerDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FString PartnerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyPersonality Personality = EProjectIntimacyPersonality::Nice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyPersonality EffectivePersonality = EProjectIntimacyPersonality::Nice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	EProjectIntimacyRelationship Relationship = EProjectIntimacyRelationship::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTagContainer RelationshipTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText RelationshipText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FGameplayTag GenderTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText GenderText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SessionProgress = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SessionPeak = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy", meta = (ClampMin = "0.0"))
	float SessionProgressPerSecond = 0.0f;

	/** Temporary player Climax for this session. It cycles at 100 and never ends the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float PlayerClimax = 0.0f;

	/** Temporary partner Climax for this session. It cycles at 100 and never ends the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float PartnerClimax = 0.0f;

	/** Normalized percentage points per second, independent of ClimaxMaximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0.0"))
	float PlayerClimaxPerSecond = 0.0f;

	/** Normalized percentage points per second, independent of ClimaxMaximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0.0"))
	float PartnerClimaxPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax")
	EProjectIntimacySessionState SessionState = EProjectIntimacySessionState::BuildingClimax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax")
	EProjectIntimacyClimaxTarget OrgasmRushTarget = EProjectIntimacyClimaxTarget::Partner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax")
	bool bPlayerOrgasmRush = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax")
	bool bPartnerOrgasmRush = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0.0"))
	float PlayerOrgasmRushRemaining = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0.0"))
	float PartnerOrgasmRushRemaining = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0"))
	int32 PlayerOrgasmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax", meta = (ClampMin = "0"))
	int32 PartnerOrgasmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Curse", meta = (ClampMin = "0.0"))
	float CurseReductionPercentPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 Affect = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 SatisfiedWins = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 Encounters = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float TotalIntimateTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bProfileHistoryVisible = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	bool bPleaseActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy|Climax")
	EProjectIntimacyClimaxTarget PleaseClimaxTarget = EProjectIntimacyClimaxTarget::Partner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 PleaseAttemptIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 PleaseAttemptCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 PleaseSuccessCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float PleaseCursorValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float PleaseTargetCenter = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float PleaseTargetHalfRange = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float PleasePreviewProgress = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	float TalkCooldownRemaining = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FName CorrectTalkOptionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	int32 SelectedOptionIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	TArray<FProjectIntimacyHudOption> Options;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText StatusText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project|Intimacy")
	FText HintText;
};

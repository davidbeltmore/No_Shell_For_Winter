#include "Defeat/ProjectDefeatFlowSettings.h"

UProjectDefeatFlowSettings::UProjectDefeatFlowSettings()
{
	KnockoutStruggleWidgetClass = TSoftClassPtr<UProjectKnockoutStruggleWidget>(FSoftClassPath(TEXT("/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget.WBP_ProjectKnockoutStruggleWidget_C")));
	DefaultSceneDefinition.InteractionId = TEXT("Intimacy.Solo.Private01");
	DefaultSceneDefinition.bAllowCancel = true;
	DefaultSceneDefinition.CancelFadeSeconds = 0.20f;
	bEnableAdvancedDefeatFlow = true;
	bEnablePainDebugWidget = false;
	bEnablePainDebugLogs = true;
	PainDebugWidgetZOrder = 260;
	PainPerAppliedDamage = 0.01f;
	DefeatedCancelMovementRestoreDelaySeconds = 1.0f;
	DefeatedTravelDelaySeconds = 2.0f;
	KnockoutOutOfCombatRecoverySeconds = 30.0f;
	StruggleEnemyRadius = 1000.f;
	BaseStruggleNoteCount = 4;
	MaxStruggleNoteCount = 24;
	MinTotalStruggleNoteCount = 20;
	BaseHitWindowSeconds = 0.30f;
	MinHitWindowSeconds = 0.18f;
	BaseTravelTimeSeconds = 1.75f;
	MinTravelTimeSeconds = 1.20f;
	MaxStruggleMisses = 5;
	StruggleNoteSpacingMinSeconds = 0.32f;
	StruggleNoteSpacingMaxSeconds = 0.56f;
}

const UProjectDefeatFlowSettings* UProjectDefeatFlowSettings::Get()
{
	return GetDefault<UProjectDefeatFlowSettings>();
}

float UProjectDefeatFlowSettings::ComputePainFromAppliedDamage(const float AppliedDamage, const float InPainPerAppliedDamage)
{
	if (AppliedDamage <= 0.f || InPainPerAppliedDamage <= 0.f)
	{
		return 0.f;
	}

	return AppliedDamage * InPainPerAppliedDamage;
}

FName UProjectDefeatFlowSettings::GetCategoryName() const
{
	return TEXT("Game");
}

#include "Survival/ProjectSurvivalStatusCatalog.h"

#include "EFProjectUIPalette.h"
namespace
{
	FProjectSurvivalStatusDefinition MakeStatus(
		const FName StatusName,
		const TCHAR* DisplayName,
		const FName MinimalIconName,
		const float DamagePerSecond = 0.f,
		const float DurationSeconds = 0.f,
		const int32 HudPriority = 0,
		const TCHAR* Description = TEXT(""))
	{
		FProjectSurvivalStatusDefinition Definition;
		Definition.StatusName = StatusName;
		Definition.DisplayName = DisplayName;
		Definition.Description = Description;
		Definition.MinimalIconName = MinimalIconName;
		Definition.DamagePerSecond = DamagePerSecond;
		Definition.DurationSeconds = DurationSeconds;
		Definition.MovementInputScale = 1.f;
		Definition.ReapplyPolicy = EProjectSurvivalStatusRefreshPolicy::RefreshDuration;
		Definition.Tint = FLinearColor::White;
		Definition.HudPriority = HudPriority;
		Definition.HudSlotSize = FVector2D(300.f, 110.f);
		Definition.HudIconSize = FVector2D(70.f, 70.f);
		Definition.HudIconSlotOffset = FVector2D(0.f, -17.f);
		return Definition;
	}
}

const FProjectSurvivalStatusCatalog& GetProjectSurvivalStatusCatalog()
{
	static const FProjectSurvivalStatusCatalog Catalog = []()
	{
		FProjectSurvivalStatusCatalog Data;
		Data.StatusHudOffset = FVector2D(456.f, 466.f);
		Data.StatusIconSize = FVector2D(70.f, 70.f);
		Data.StatusIconSpacing = 8.f;
		Data.StatusIconsPerRow = 5;
		Data.bEnableDebugStatusCycling = true;
		Data.DebugCycleStatusNames = {
			TEXT("Bleeding"),
			TEXT("Dizzy"),
			TEXT("Fear"),
			TEXT("Tired"),
			TEXT("SleepDeprived"),
			TEXT("Cursed"),
			TEXT("Frenzy"),
			TEXT("ExtremePain"),
			TEXT("Dirty"),
			TEXT("Sweaty"),
			TEXT("WellFed"),
			TEXT("Alcoholized"),
			TEXT("KnockedOut")
		};

		FProjectSurvivalStatusDefinition Starving = MakeStatus(TEXT("Starving"), TEXT("STARVING"), TEXT("Status.Starving"), 1.f, 0.f, 620, TEXT("Hunger is empty; health recovery is blocked and damage starts ticking."));
		Starving.SourceNeedName = TEXT("Hunger");
		Starving.bBlocksHealthRecovery = true;
		Starving.bTriggerAtNeedEmpty = true;

		FProjectSurvivalStatusDefinition Thirst = MakeStatus(TEXT("Thirst"), TEXT("THIRST"), TEXT("Status.Thirst"), 1.f, 0.f, 630, TEXT("Thirst is empty; health recovery is blocked and damage starts ticking."));
		Thirst.SourceNeedName = TEXT("Thirst");
		Thirst.bBlocksHealthRecovery = true;
		Thirst.bTriggerAtNeedEmpty = true;

		FProjectSurvivalStatusDefinition WellFed = MakeStatus(TEXT("WellFed"), TEXT("WELL FED"), TEXT("Status.WellFed"), 0.f, 0.f, 400, TEXT("Hunger is above ninety percent."));
		WellFed.SourceEntryName = TEXT("Hunger");
		WellFed.SourceType = EProjectSurvivalStatusSourceType::Need;
		WellFed.ThresholdMode = EProjectSurvivalStatusThresholdMode::AtOrAbove;
		WellFed.ActivationThresholdNormalized = 0.90f;
		WellFed.DeactivationThresholdNormalized = 0.75f;
		WellFed.Tint = EFProjectUIPalette::InnerStateHunger();

		FProjectSurvivalStatusDefinition Alcoholized = MakeStatus(TEXT("Alcoholized"), TEXT("ALCOHOLIZED"), TEXT("Status.Alcoholized"), 0.f, 0.f, 710, TEXT("Alcohol is impairing movement while it is metabolized."));
		Alcoholized.SourceEntryName = TEXT("Alcohol");
		Alcoholized.SourceType = EProjectSurvivalStatusSourceType::Sensation;
		Alcoholized.ThresholdMode = EProjectSurvivalStatusThresholdMode::AtOrAbove;
		Alcoholized.ActivationThresholdNormalized = 0.25f;
		Alcoholized.DeactivationThresholdNormalized = 0.10f;
		Alcoholized.MovementInputScale = 0.85f;
		Alcoholized.Tint = EFProjectUIPalette::Warning();

		FProjectSurvivalStatusDefinition SleepDeprived = MakeStatus(TEXT("SleepDeprived"), TEXT("SLEEP DEPRIVED"), TEXT("Status.Exhausted"), 0.f, 0.f, 520, TEXT("Sleep is empty and exhaustion pressure is rising."));
		SleepDeprived.SourceNeedName = TEXT("Sleep");
		SleepDeprived.bTriggerAtNeedEmpty = true;

		FProjectSurvivalStatusDefinition Tired = MakeStatus(TEXT("Tired"), TEXT("TIRED"), TEXT("Status.Exhausted"), 0.f, 0.f, 510, TEXT("Sleep drains faster and Madness rises until the character sleeps."));
		Tired.NeedDecayModifiers = {
			FProjectSurvivalStatusNeedDecayModifier(TEXT("Sleep"), 1.25f)
		};
		Tired.SensationModifiers = {
			FProjectSurvivalStatusSensationModifier(TEXT("Madness"), 0.2f)
		};

		FProjectSurvivalStatusDefinition Exhausted = MakeStatus(TEXT("Exhausted"), TEXT("EXHAUSTED"), TEXT("Status.Exhausted"), 0.f, 15.f, 760, TEXT("The character is collapsing into an exhaustion blackout."));
		Exhausted.bTriggerAtNeedEmpty = false;
		Exhausted.bTriggersExhaustionSequence = true;

		FProjectSurvivalStatusDefinition ExhaustedRecovery = MakeStatus(TEXT("ExhaustedRecovery"), TEXT("RECOVERY"), TEXT("Status.Exhausted"), 0.f, 8.f, 500, TEXT("Temporary weakness after recovering from exhaustion."));
		ExhaustedRecovery.MovementInputScale = 0.85f;
		ExhaustedRecovery.AttributeModifiers = {
			FProjectSurvivalStatusAttributeModifier(TEXT("MeleeDamage"), 0.85f),
			FProjectSurvivalStatusAttributeModifier(TEXT("RangedDamage"), 0.85f),
			FProjectSurvivalStatusAttributeModifier(TEXT("SpellDamage"), 0.85f)
		};

		FProjectSurvivalStatusDefinition Frenzy = MakeStatus(TEXT("Frenzy"), TEXT("FRENZY"), TEXT("Status.Dizzy"), 0.f, 0.f, 820, TEXT("Movement is unstable and partially inverted."));
		Frenzy.bInvertMovementInput = true;
		Frenzy.MovementInputScale = 0.80f;
		Frenzy.Tint = EFProjectUIPalette::Warning();

		FProjectSurvivalStatusDefinition Cursed = MakeStatus(TEXT("Cursed"), TEXT("CURSED"), TEXT("Status.Fear"), 0.f, 8.f, 800, TEXT("The curse has peaked, slowing movement until the surge passes."));
		Cursed.MovementInputScale = 0.80f;
		Cursed.ReapplyPolicy = EProjectSurvivalStatusRefreshPolicy::IgnoreIfActive;
		Cursed.Tint = EFProjectUIPalette::AccentSoft();

		FProjectSurvivalStatusDefinition ExtremePain = MakeStatus(TEXT("ExtremePain"), TEXT("EXTREME PAIN"), TEXT("Status.Bleeding"), 0.f, 10.f, 900, TEXT("Pain has reached a dangerous overload state."));
		ExtremePain.Tint = EFProjectUIPalette::InnerStatePain();

		FProjectSurvivalStatusDefinition GraceStep = MakeStatus(TEXT("GraceStep"), TEXT("GRACE STEP"), TEXT("Status.Dizzy"), 0.f, 2.f, 420, TEXT("A brief movement grace window is active."));
		GraceStep.Tint = EFProjectUIPalette::AccentMuted();

		FProjectSurvivalStatusDefinition KnockedOut = MakeStatus(TEXT("KnockedOut"), TEXT("KNOCKED OUT"), TEXT("Status.Exhausted"), 0.f, 10.f, 1000, TEXT("The character is incapacitated."));
		KnockedOut.Tint = EFProjectUIPalette::MutedText();

		FProjectSurvivalStatusDefinition Bleeding = MakeStatus(TEXT("Bleeding"), TEXT("BLEEDING"), TEXT("Status.Bleeding"), 1.f, 5.f, 850, TEXT("Taking periodic damage from an open wound."));
		Bleeding.bTriggerAtNeedEmpty = false;

		FProjectSurvivalStatusDefinition Dizzy = MakeStatus(TEXT("Dizzy"), TEXT("DIZZY"), TEXT("Status.Dizzy"), 0.f, 5.f, 700, TEXT("Movement direction is confused and heavily reduced."));
		Dizzy.bTriggerAtNeedEmpty = false;
		Dizzy.bInvertMovementInput = true;
		Dizzy.MovementInputScale = 0.55f;

		FProjectSurvivalStatusDefinition Fear = MakeStatus(TEXT("Fear"), TEXT("FEAR"), TEXT("Status.Fear"), 0.f, 5.f, 680, TEXT("Combat damage output is reduced by fear."));
		Fear.bTriggerAtNeedEmpty = false;
		Fear.AttributeModifiers = {
			FProjectSurvivalStatusAttributeModifier(TEXT("MeleeDamage"), 0.8f),
			FProjectSurvivalStatusAttributeModifier(TEXT("RangedDamage"), 0.8f),
			FProjectSurvivalStatusAttributeModifier(TEXT("SpellDamage"), 0.8f)
		};

		FProjectSurvivalStatusDefinition Dirty = MakeStatus(TEXT("Dirty"), TEXT("DIRTY"), TEXT("Status.Dirty"), 0.f, 0.f, 300, TEXT("Dirt or grime is active on the character."));
		Dirty.bTriggerAtNeedEmpty = false;
		Dirty.Tint = EFProjectUIPalette::OutlineDim();

		FProjectSurvivalStatusDefinition Sweaty = MakeStatus(TEXT("Sweaty"), TEXT("SWEATY"), TEXT("Status.Dirty"), 0.f, 0.f, 310, TEXT("Sweat has fully built up and will remain noticeable until washed away."));
		Sweaty.bTriggerAtNeedEmpty = false;
		Sweaty.Tint = EFProjectUIPalette::AccentMuted();

		Data.StatusDefinitions = {
			Starving,
			Thirst,
			WellFed,
			Alcoholized,
			Tired,
			SleepDeprived,
			Exhausted,
			ExhaustedRecovery,
			Frenzy,
			Cursed,
			ExtremePain,
			GraceStep,
			KnockedOut,
			Bleeding,
			Dizzy,
			Fear,
			Dirty,
			Sweaty
		};

		FProjectSurvivalStatusIncomingHitRule BleedingRule;
		BleedingRule.StatusName = TEXT("Bleeding");
		BleedingRule.SourceClassNameHints = { TEXT("MeleeMale"), TEXT("ACFMeleeEnemyBP") };
		BleedingRule.ApplyChance = 0.10f;

		FProjectSurvivalStatusIncomingHitRule DizzyRule;
		DizzyRule.StatusName = TEXT("Dizzy");
		DizzyRule.SourceClassNameHints = { TEXT("MageMale"), TEXT("ACFMageEnemyBP") };
		DizzyRule.ApplyChance = 1.f;

		Data.IncomingHitRules = {
			BleedingRule,
			DizzyRule
		};

		return Data;
	}();

	return Catalog;
}

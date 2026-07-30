#pragma once

#include "CoreMinimal.h"
#include "EFProjectUITheme.h"

namespace EFProjectUIPalette
{
	inline FLinearColor PanelFill(float A = 0.96f) { return EFProjectUITheme::PanelFill(A); }
	inline FLinearColor PanelFillDeep(float A = 0.98f) { return EFProjectUITheme::PanelFillDeep(A); }
	inline FLinearColor SectionFill(float A = 0.95f) { return EFProjectUITheme::SectionFill(A); }
	inline FLinearColor Outline(float A = 0.86f) { return EFProjectUITheme::Outline(A); }
	inline FLinearColor OutlineDim(float A = 0.42f) { return EFProjectUITheme::OutlineDim(A); }
	inline FLinearColor Haze(float A = 0.16f) { return EFProjectUITheme::Haze(A); }
	inline FLinearColor Title(float A = 1.0f) { return EFProjectUITheme::TitleText(A); }
	inline FLinearColor Accent(float A = 1.0f) { return EFProjectUITheme::Accent(A); }
	inline FLinearColor AccentSoft(float A = 1.0f) { return EFProjectUITheme::AccentSoft(A); }
	inline FLinearColor AccentMuted(float A = 1.0f) { return EFProjectUITheme::AccentMuted(A); }
	inline FLinearColor Warning(float A = 1.0f) { return EFProjectUITheme::Warning(A); }
	inline FLinearColor PrimaryText(float A = 1.0f) { return EFProjectUITheme::PrimaryText(A); }
	inline FLinearColor SecondaryText(float A = 0.96f) { return EFProjectUITheme::SecondaryText(A); }
	inline FLinearColor MutedText(float A = 1.0f) { return EFProjectUITheme::MutedText(A); }
	inline FLinearColor BadgeFill(float A = 0.98f) { return EFProjectUITheme::BadgeFill(A); }
	inline FLinearColor BadgeText(float A = 1.0f) { return EFProjectUITheme::BadgeText(A); }
	inline FLinearColor Positive(float A = 1.0f) { return EFProjectUITheme::Positive(A); }
	inline FLinearColor Negative(float A = 1.0f) { return EFProjectUITheme::Negative(A); }

	inline FLinearColor AttributeWillpower(float A = 1.0f) { return AccentSoft(A); }
	inline FLinearColor AttributeOffensive(float A = 1.0f) { return Accent(A); }
	inline FLinearColor AttributeDefensive(float A = 1.0f) { return AccentMuted(A); }
	inline FLinearColor AttributeFaith(float A = 1.0f) { return Warning(A); }
	inline FLinearColor AttributeCunning(float A = 1.0f) { return Accent(A); }
	inline FLinearColor AttributeCelerity(float A = 1.0f) { return Warning(A); }
	inline FLinearColor AttributeCharisma(float A = 1.0f) { return AccentSoft(A); }

	inline FLinearColor InnerStateHunger(float A = 1.0f) { return AccentSoft(A); }
	inline FLinearColor InnerStateThirst(float A = 1.0f) { return Accent(A); }
	inline FLinearColor InnerStateSleep(float A = 1.0f) { return AccentMuted(A); }
	inline FLinearColor InnerStateMadness(float A = 1.0f) { return AccentMuted(A); }
	inline FLinearColor InnerStateCurse(float A = 1.0f) { return AccentSoft(A); }
	inline FLinearColor InnerStateAdrenaline(float A = 1.0f) { return InnerStateCurse(A); }
	inline FLinearColor InnerStatePain(float A = 1.0f) { return Warning(A); }

	/** Resolve authored semantic roles at use time so a theme switch cannot
	 * leave DeveloperSettings/CDO colors frozen in the previous profile. */
	inline FLinearColor AttributeForName(const FName AttributeName, float A = 1.0f)
	{
		if (AttributeName == TEXT("Willpower")) return AttributeWillpower(A);
		if (AttributeName == TEXT("Offensive")) return AttributeOffensive(A);
		if (AttributeName == TEXT("Defensive")) return AttributeDefensive(A);
		if (AttributeName == TEXT("Faith")) return AttributeFaith(A);
		if (AttributeName == TEXT("Cunning")) return AttributeCunning(A);
		if (AttributeName == TEXT("Celerity")) return AttributeCelerity(A);
		if (AttributeName == TEXT("Charisma")) return AttributeCharisma(A);
		return Accent(A);
	}

	inline FLinearColor InnerStateForName(const FName EntryName, float A = 1.0f)
	{
		if (EntryName == TEXT("Hunger")) return InnerStateHunger(A);
		if (EntryName == TEXT("Thirst")) return InnerStateThirst(A);
		if (EntryName == TEXT("Sleep")) return InnerStateSleep(A);
		if (EntryName == TEXT("Madness")) return InnerStateMadness(A);
		if (EntryName == TEXT("Curse")) return InnerStateCurse(A);
		if (EntryName == TEXT("Pain")) return InnerStatePain(A);
		return Accent(A);
	}

	inline FLinearColor ChronicleAccentForChannel(
		const FName ChannelName,
		float A = 1.0f)
	{
		if (ChannelName == TEXT("System")) return SecondaryText(A);
		if (ChannelName == TEXT("Loot")) return AccentSoft(A);
		if (ChannelName == TEXT("Experience")) return Accent(A);
		if (ChannelName == TEXT("Combat")) return Warning(A);
		if (ChannelName == TEXT("Status")) return Title(A);
		if (ChannelName == TEXT("Dialogue")) return PrimaryText(A);
		return Accent(A);
	}

	inline FLinearColor ChronicleBadgeFillForChannel(
		const FName ChannelName,
		float A = 0.96f)
	{
		if (ChannelName == TEXT("System")) return BadgeFill(A * 0.84f);
		if (ChannelName == TEXT("Loot")) return BadgeFill(A * 0.92f);
		if (ChannelName == TEXT("Experience")) return BadgeFill(A);
		if (ChannelName == TEXT("Combat")) return AccentMuted(A);
		if (ChannelName == TEXT("Status")) return AccentMuted(A * 0.92f);
		if (ChannelName == TEXT("Dialogue")) return OutlineDim(A);
		return BadgeFill(A);
	}
}

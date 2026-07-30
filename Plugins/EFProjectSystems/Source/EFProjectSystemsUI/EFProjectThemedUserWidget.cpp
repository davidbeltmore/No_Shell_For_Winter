#include "EFProjectThemedUserWidget.h"

#include "Components/Image.h"
#include "EFProjectDynamicThemeSubsystem.h"
#include "EFProjectUISettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UEFProjectThemedUserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
				GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>())
			{
				ThemeSubsystem->RegisterThemedWidget(this);
			}
		}
	}
}

void UEFProjectThemedUserWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
				GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>())
			{
				ThemeSubsystem->UnregisterThemedWidget(this);
			}
		}
	}

	Super::NativeDestruct();
}

void UEFProjectThemedUserWidget::ReapplyProjectThemeAfterNativeConstruct()
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
				GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>())
			{
				ThemeSubsystem->ReapplyCurrentThemeToWidget(this);
			}
		}
	}
}

UTexture2D* UEFProjectThemedUserWidget::ResolveProjectThemeTexture(
	UTexture2D* SourceTexture) const
{
	if (!SourceTexture)
	{
		return nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UEFProjectDynamicThemeSubsystem* ThemeSubsystem =
				GameInstance->GetSubsystem<UEFProjectDynamicThemeSubsystem>())
			{
				return ThemeSubsystem->ResolveCurrentThemeTexture(SourceTexture);
			}
		}
	}

	return SourceTexture;
}

FLinearColor UEFProjectThemedUserWidget::ResolveProjectThemeImageTint(
	const UImage* Image,
	const FLinearColor& RequestedTint) const
{
	const UObject* ResourceObject =
		Image ? Image->GetBrush().GetResourceObject() : nullptr;
	const UEFProjectUISettings* Settings = UEFProjectUISettings::Get();
	FString NativeThemeTextureRoot =
		Settings ? Settings->NativeThemeTextureRoot : FString();
	while (NativeThemeTextureRoot.EndsWith(TEXT("/")))
	{
		NativeThemeTextureRoot.LeftChopInline(1, EAllowShrinking::No);
	}

	if (ResourceObject && !NativeThemeTextureRoot.IsEmpty())
	{
		const FString NativeThemeTexturePrefix =
			NativeThemeTextureRoot + TEXT("/");
		if (ResourceObject->GetPathName().StartsWith(
			NativeThemeTexturePrefix,
			ESearchCase::IgnoreCase))
		{
			return FLinearColor(
				1.0f,
				1.0f,
				1.0f,
				RequestedTint.A);
		}
	}

	return RequestedTint;
}

void UEFProjectThemedUserWidget::NativeOnProjectThemeApplied(
	const EEFProjectHUDThemePreset,
	const FProjectHUDThemeColors&,
	const int32)
{
}

void UEFProjectThemedUserWidget::NotifyProjectThemeApplied(
	const EEFProjectHUDThemePreset Preset,
	const FProjectHUDThemeColors& Theme,
	const int32 Revision)
{
	if (LastAppliedThemeRevision == Revision)
	{
		return;
	}

	LastAppliedThemeRevision = Revision;
	NativeOnProjectThemeApplied(Preset, Theme, Revision);
	OnProjectThemeApplied(Preset, Theme, Revision);
}

#include "EFProjectInputSettings.h"

UEFProjectInputSettings::UEFProjectInputSettings()
{
	ToggleWalkKey = EKeys::N;
	ToggleCrawlKey = EKeys::C;
	ToggleInteractionMenuKey = EKeys::Y;
	ToggleNeedsHudKey = EKeys::Comma;
	ToggleActivityFeedKey = EKeys::J;
	ToggleGameplayDebugMenuKey = EKeys::L;
	ToggleGameplayFreeCameraKey = EKeys::O;
	SurrenderKey = EKeys::Down;
}

const UEFProjectInputSettings* UEFProjectInputSettings::Get()
{
	return GetDefault<UEFProjectInputSettings>();
}

FName UEFProjectInputSettings::GetCategoryName() const
{
	return TEXT("EF Project Systems");
}

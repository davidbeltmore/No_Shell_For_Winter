#include "EFBlinkSettings.h"

UEFBlinkSettings::UEFBlinkSettings()
{
	MorphTargets = EFBlinkDefaults::MakeMorphTargets();
	PreferredMeshNameTokens = EFBlinkDefaults::MakePreferredMeshNameTokens();
}

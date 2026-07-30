#pragma once

class APawn;
class UEFProceduralSettings;

class EFPROCEDURALACFURUNTIME_API FEFProceduralACFU
{
public:
	static bool LooksLikeSupportedEnemyPawn(const APawn* Pawn, const UEFProceduralSettings* Settings);
	static void ApplyFallbackAIControllerClass(APawn* Pawn, const UEFProceduralSettings* Settings);
};

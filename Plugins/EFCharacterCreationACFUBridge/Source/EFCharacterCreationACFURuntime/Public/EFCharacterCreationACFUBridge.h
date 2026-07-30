#pragma once

class APawn;

class EFCHARACTERCREATIONACFURUNTIME_API FEFCharacterCreationACFUBridge
{
public:
	static void SetPawnCanMove(APawn* Pawn, bool bCanMove);
	static void CancelPawnAbilities(APawn* Pawn);
};

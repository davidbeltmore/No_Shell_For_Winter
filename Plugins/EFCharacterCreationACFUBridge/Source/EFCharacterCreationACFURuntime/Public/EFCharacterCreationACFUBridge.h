#pragma once

class APawn;
class AActor;

class EFCHARACTERCREATIONACFURUNTIME_API FEFCharacterCreationACFUBridge
{
public:
	static void SetPawnCanMove(APawn* Pawn, bool bCanMove);
	static void CancelPawnAbilities(APawn* Pawn);
	static void RestoreAnimationAfterBodyChange(AActor* Actor);
};

#include "EFBlinkTypes.h"

namespace EFBlinkDefaults
{
	TArray<FEFBlinkMorphTarget> MakeMorphTargets()
	{
		return {
			FEFBlinkMorphTarget(TEXT("Blink"), 1.2f),
			FEFBlinkMorphTarget(TEXT("Eyelid Thickness"), 1.0f),
			FEFBlinkMorphTarget(TEXT("Eyelid Angle Upper Outer"), 1.0f),
			FEFBlinkMorphTarget(TEXT("Eyelid Angle Lower Outer"), 1.0f),
			FEFBlinkMorphTarget(TEXT("Eyeball Position Depth"), -1.0f)
		};
	}

	TArray<FString> MakePreferredMeshNameTokens()
	{
		return {
			TEXT("Female"),
			TEXT("DazToUnreal"),
			TEXT("Genesis"),
			TEXT("Body"),
			TEXT("Face"),
			TEXT("Head"),
			TEXT("Mesh")
		};
	}
}

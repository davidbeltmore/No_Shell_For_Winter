#include "Calysto/EFCalystoMaterialEditorLibrary.h"

#include "Materials/MaterialExpression.h"

FString UEFCalystoMaterialEditorLibrary::GetMaterialExpressionInputSourcePath(
	UMaterialExpression* Expression,
	const int32 InputIndex)
{
#if WITH_EDITOR
	if (!IsValid(Expression) || InputIndex < 0 || InputIndex >= Expression->CountInputs())
	{
		return FString();
	}
	const FExpressionInput* Input = Expression->GetInput(InputIndex);
	return Input && IsValid(Input->Expression)
		? Input->Expression->GetPathName()
		: FString();
#else
	return FString();
#endif
}

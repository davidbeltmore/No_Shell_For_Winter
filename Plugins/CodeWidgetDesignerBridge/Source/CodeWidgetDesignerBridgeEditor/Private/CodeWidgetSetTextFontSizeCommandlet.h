#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetSetTextFontSizeCommandlet.generated.h"

UCLASS()
class UCodeWidgetSetTextFontSizeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetSetTextFontSizeCommandlet();

	virtual int32 Main(const FString& Params) override;
};

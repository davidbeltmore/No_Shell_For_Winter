#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetValidateWBPCommandlet.generated.h"

UCLASS()
class UCodeWidgetValidateWBPCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetValidateWBPCommandlet();

	virtual int32 Main(const FString& Params) override;
};

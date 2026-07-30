#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetToWBPCommandlet.generated.h"

UCLASS()
class UCodeWidgetToWBPCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetToWBPCommandlet();

	virtual int32 Main(const FString& Params) override;
};

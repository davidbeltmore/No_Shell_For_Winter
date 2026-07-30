#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetPreflightWBPCommandlet.generated.h"

UCLASS()
class UCodeWidgetPreflightWBPCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetPreflightWBPCommandlet();

	virtual int32 Main(const FString& Params) override;
};

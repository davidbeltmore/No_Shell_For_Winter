#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetCleanRebuildCommandlet.generated.h"

UCLASS()
class UCodeWidgetCleanRebuildCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetCleanRebuildCommandlet();

	virtual int32 Main(const FString& Params) override;
};

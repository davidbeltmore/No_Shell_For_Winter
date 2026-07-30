#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetAuditRuntimeCommandlet.generated.h"

UCLASS()
class UCodeWidgetAuditRuntimeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetAuditRuntimeCommandlet();

	virtual int32 Main(const FString& Params) override;
};

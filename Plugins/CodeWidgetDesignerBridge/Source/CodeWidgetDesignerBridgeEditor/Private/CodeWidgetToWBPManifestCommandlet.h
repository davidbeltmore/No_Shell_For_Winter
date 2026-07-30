#pragma once

#include "Commandlets/Commandlet.h"
#include "CodeWidgetToWBPManifestCommandlet.generated.h"

UCLASS()
class UCodeWidgetToWBPManifestCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodeWidgetToWBPManifestCommandlet();

	virtual int32 Main(const FString& Params) override;
};

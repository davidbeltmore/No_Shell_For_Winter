#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EFCalystoDirectorPCGSubsystem.generated.h"

class IEFCalystoDirectorAttemptRuntime;

/** Registers the one project-owned native implementation with the Director request owner. */
UCLASS()
class EFPROCEDURALPCGRUNTIME_API UEFCalystoDirectorPCGSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	TSharedPtr<IEFCalystoDirectorAttemptRuntime> Runtime;
};

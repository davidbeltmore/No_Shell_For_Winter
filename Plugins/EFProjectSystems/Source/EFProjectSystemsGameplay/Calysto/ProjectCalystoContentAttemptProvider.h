#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoContentAttemptProvider.h"

/** EFProjectSystems factory for V7 content attempts.  Its sole public contract
 * is the neutral EFProcedural interface, so the native Calysto runtime keeps no
 * project gameplay module dependency. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoContentAttemptProvider final
	: public IEFCalystoContentAttemptProvider
{
public:
	virtual bool CapturePreTravelSnapshot(const FGuid& RequestId, UWorld* SourceWorld,
		TSharedPtr<IEFCalystoGameplaySnapshot>& OutSnapshot, FString& Error) override;
	virtual bool CanHandleSnapshot(const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot) const override;
	virtual bool PreparePreTravelSnapshot(const FGuid& RequestId,
		const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) override;
	virtual bool DetachPreTravelSnapshot(const FGuid& RequestId,
		const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) override;
	virtual bool ReconstructPreTravelSnapshot(const FEFCalystoAttemptToken& Token, UWorld* DestinationWorld,
		const TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) override;
	virtual bool RecoverDetachedSnapshot(const FEFCalystoAttemptToken& Token, UWorld* DestinationWorld,
		TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, FString& Error) override;
	virtual bool PrepareAttempt(const FEFCalystoContentAttemptProviderRequest& Request,
		FEFCalystoContentAttemptProviderResult& OutResult, FString& Error) override;
	virtual bool BuildMaterializationContext(const FEFCalystoContentAttemptProviderRequest& Request,
		const FEFCalystoContentAttemptProviderResult& Prepared,
		TSharedRef<const FEFCalystoReservedContentManifest> Manifest,
		FEFCalystoContentGameplayContext& OutContext, FString& Error) override;
	virtual bool FinishRequest(const FGuid& RequestId,
		TSharedPtr<IEFCalystoGameplaySnapshot>& Snapshot, bool bAccepted) override;

private:
	/** Concrete type validation relies on capture provenance, never an unchecked
	 * downcast supplied by another content provider. Entries are removed only
	 * after the provider has completed the terminal lease. */
	TSet<const IEFCalystoGameplaySnapshot*> CapturedSnapshots;
};

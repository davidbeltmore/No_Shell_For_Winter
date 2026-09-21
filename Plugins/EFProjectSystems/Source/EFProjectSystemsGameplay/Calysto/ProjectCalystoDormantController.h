#pragma once

#include "CoreMinimal.h"
#include "Calysto/EFCalystoFloorTransaction.h"
#include "UObject/GCObject.h"

class AActor;
class APawn;
class AAIController;
class UActorComponent;

/** Owns the exact authored controller while it is constructed but deliberately unpossessed.
 * The pawn class and AIControllerClass are never replaced. Native possession and AI events begin
 * only after the floor transaction accepts. Callers retain this owner until release is verified. */
class EFPROJECTSYSTEMSGAMEPLAY_API FProjectCalystoDormantController final : public FGCObject
{
public:
	FProjectCalystoDormantController()=default;
	virtual ~FProjectCalystoDormantController() override;
	FProjectCalystoDormantController(const FProjectCalystoDormantController&)=delete;
	FProjectCalystoDormantController& operator=(const FProjectCalystoDormantController&)=delete;
	/** Read-only resident-class checks; no class substitution, load or CDO mutation. */
	static bool PreflightClass(UClass* SelectedClass,TArray<FSoftObjectPath>& Dependencies,FString& Error);
	bool Begin(const FEFCalystoAttemptToken& Token,APawn* ExactPawn,AActor* AttemptOwner,FString& Error);
	bool Verify(const FEFCalystoAttemptToken& Token,const APawn* ExactPawn,FString& Error) const;
	/** Invoke only after acceptance. Possession is native activation of the already-created controller. */
	bool ActivateAccepted(const FEFCalystoAttemptToken& Token,APawn* ExactPawn,FString& Error);
	void BeginRelease(const FEFCalystoAttemptToken& Token);
	bool ObserveRelease(const FEFCalystoAttemptToken& Token);
	/** The caller must already hold independent proof of persistent actor/controller ownership. */
	AAIController* TransferAccepted(const FEFCalystoAttemptToken& Token,const APawn* ExactPawn,FString& Error);
	AAIController* GetController() const { return Controller.Get(); }
	bool IsAccepted() const { return bAccepted; }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FProjectCalystoDormantController"); }
private:
	struct FComponentActivation
	{
		TWeakObjectPtr<UActorComponent> Component;
		bool bTick=false;
	};
	FEFCalystoAttemptToken OwnedToken;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<AActor> Owner;
	TObjectPtr<AAIController> Controller=nullptr;
	TArray<FComponentActivation> ComponentActivation;
	bool bControllerTick=false,bAccepted=false,bReleasing=false,bReleased=false,bOperation=false;
	void Quarantine();
};

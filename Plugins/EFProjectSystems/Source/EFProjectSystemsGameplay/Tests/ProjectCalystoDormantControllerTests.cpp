#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/ProjectCalystoDormantController.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace ProjectCalystoDormantControllerTests
{
	FGuid Id(uint32 N) { return FGuid(0x444F524D,0x414E5454,0,N); }
	FEFCalystoAttemptToken Token(uint32 N=1) { return {Id(100),Id(N)}; }
	struct FWorld
	{
		UWorld* Value=nullptr;
		FWorld()
		{
			if (!GEngine) return;
			const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).RequiresHitProxies(false).ShouldSimulatePhysics(false);
			Value=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
			if (Value) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Value);
		}
		~FWorld()
		{
			if (Value)
			{
				// Native controller/actor teardown may query the context; retire it only after world destruction.
				Value->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(Value);
			}
		}
		APawn* SpawnPawn(AActor* Owner) const
		{
			if (!Value) return nullptr;
			APawn* Pawn=Value->SpawnActorDeferred<APawn>(APawn::StaticClass(),FTransform::Identity,Owner,nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Pawn)
			{
				Pawn->AutoPossessAI=EAutoPossessAI::Disabled; Pawn->AutoPossessPlayer=EAutoReceiveInput::Disabled;
				Pawn->AIControllerClass=AAIController::StaticClass(); Pawn->FinishSpawning(FTransform::Identity);
			}
			return Pawn;
		}
	};
	/** Bind when SpawnActorDeferred publishes its native actor, before controller FinishSpawning. */
	struct FObserver
	{
		UWorld* World;
		FDelegateHandle SpawnHandle,PawnHandle;
		TWeakObjectPtr<AAIController> Controller;
		int32 Created=0,Possessed=0,Unpossessed=0;
		explicit FObserver(UWorld* InWorld) : World(InWorld)
		{
			SpawnHandle=World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
			{
				if (AAIController* AI=Cast<AAIController>(Actor))
				{
					++Created; Controller=AI;
					PawnHandle=AI->GetOnNewPawnNotifier().AddLambda([this](APawn* Pawn)
					{ if (Pawn) ++Possessed; else ++Unpossessed; });
				}
			}));
		}
		~FObserver()
		{
			World->RemoveOnActorSpawnedHandler(SpawnHandle);
			if (Controller.IsValid()) Controller->GetOnNewPawnNotifier().Remove(PawnHandle);
		}
	};
	bool Drain(FProjectCalystoDormantController& Lease,const FEFCalystoAttemptToken& OwnedToken)
	{
		Lease.BeginRelease(OwnedToken);
		for (int32 I=0;I<8;++I) if (Lease.ObserveRelease(OwnedToken)) return true;
		return false;
	}
	struct FLeaseScope
	{
		FEFCalystoAttemptToken OwnedToken;
		FAutomationTestBase& Test;
		FProjectCalystoDormantController Lease;
		explicit FLeaseScope(FAutomationTestBase& InTest) : OwnedToken(Token()),Test(InTest) {}
		~FLeaseScope()
		{
			if (Lease.GetController() && !Drain(Lease,OwnedToken))
				Test.AddError(TEXT("The native controller fixture retained an actor after bounded teardown."));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoDormantRejectedTest,
	"NoShellForWinter.CalystoDungeon.Director.DormantController.ExactClassWithoutPrematurePossession",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoDormantRejectedTest::RunTest(const FString&)
{
	using namespace ProjectCalystoDormantControllerTests;
	FWorld World; if (!World.Value) return false;
	AActor* Owner=World.Value->SpawnActor<AActor>(); APawn* Pawn=World.SpawnPawn(Owner); if (!Owner || !Pawn) return false;
	FObserver Events(World.Value); FLeaseScope Scope(*this); auto& Lease=Scope.Lease; FString Error;
	TArray<FSoftObjectPath> Dependencies;
	TestTrue(TEXT("Resident native AAIController passes exact class preflight"),FProjectCalystoDormantController::PreflightClass(AAIController::StaticClass(),Dependencies,Error));
	TestTrue(TEXT("Preflight reports the exact selected native class dependency"),Dependencies.Contains(FSoftObjectPath(AAIController::StaticClass())));
	TestFalse(TEXT("A non-controller class has no dormant capability"),FProjectCalystoDormantController::PreflightClass(APawn::StaticClass(),Dependencies,Error));
	if (!TestTrue(TEXT("The exact native controller stages"),Lease.Begin(Token(),Pawn,Owner,Error))) { AddError(Error); return false; }
	AAIController* Selected=Lease.GetController();
	TestNotNull(TEXT("Preparation retains its actual controller actor"),Selected);
	if (!Selected) return false;
	TestTrue(TEXT("Prepared controller preserves the authored exact class"),Selected->GetClass()==AAIController::StaticClass());
	TestTrue(TEXT("Preparation does not rewrite the pawn controller class"),Pawn->AIControllerClass.Get()==AAIController::StaticClass());
	TestNull(TEXT("Prepared pawn has no controller binding"),Pawn->GetController());
	TestNull(TEXT("Prepared controller has no pawn binding"),Selected->GetPawn());
	TestEqual(TEXT("Construction emits zero native possession events"),Events.Possessed,0);
	TestEqual(TEXT("Construction emits zero native unpossession events"),Events.Unpossessed,0);
	TestTrue(TEXT("Exact repeated preparation is idempotent"),Lease.Begin(Token(),Pawn,Owner,Error));
	TestTrue(TEXT("Idempotence retains the same controller actor"),Lease.GetController()==Selected);
	TestEqual(TEXT("Preparation creates exactly one selected controller"),Events.Created,1);
	TestFalse(TEXT("A stale token cannot observe the dormant pair"),Lease.Verify(Token(2),Pawn,Error));
	TestFalse(TEXT("A stale token cannot activate the dormant pair"),Lease.ActivateAccepted(Token(2),Pawn,Error));
	Lease.BeginRelease(Token(2));
	TestTrue(TEXT("A stale release leaves the exact attempt intact"),Lease.Verify(Token(),Pawn,Error));
	TestTrue(TEXT("Rejected cleanup releases the actual controller"),Drain(Lease,Token()));
	TestNull(TEXT("Rejected cleanup retains no controller actor"),Lease.GetController());
	TestNull(TEXT("Rejected pawn was never possessed"),Pawn->GetController());
	TestEqual(TEXT("Rejected attempt emits no possession event"),Events.Possessed,0);
	TestEqual(TEXT("Rejected cleanup emits no artificial unpossession event"),Events.Unpossessed,0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoDormantAcceptedTest,
	"NoShellForWinter.CalystoDungeon.Director.DormantController.AcceptedPossessionOnceAndExactCleanup",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoDormantAcceptedTest::RunTest(const FString&)
{
	using namespace ProjectCalystoDormantControllerTests;
	FWorld World; if (!World.Value) return false;
	AActor* Owner=World.Value->SpawnActor<AActor>(); APawn* Pawn=World.SpawnPawn(Owner); if (!Owner || !Pawn) return false;
	FObserver Events(World.Value); FLeaseScope Scope(*this); auto& Lease=Scope.Lease; FString Error;
	if (!TestTrue(TEXT("Accepted fixture stages a native controller"),Lease.Begin(Token(),Pawn,Owner,Error))) { AddError(Error); return false; }
	AAIController* Selected=Lease.GetController();
	TestEqual(TEXT("The accepted fixture remains unpossessed during preparation"),Events.Possessed,0);
	if (!TestTrue(TEXT("Acceptance performs native possession"),Lease.ActivateAccepted(Token(),Pawn,Error))) { AddError(Error); return false; }
	TestTrue(TEXT("The authored controller possesses its exact pawn"),Pawn->GetController()==Selected && Selected->GetPawn()==Pawn);
	TestTrue(TEXT("Accepted state is explicit"),Lease.IsAccepted());
	TestEqual(TEXT("Acceptance emits one native possession event"),Events.Possessed,1);
	TestTrue(TEXT("Repeated exact accepted activation is idempotent"),Lease.ActivateAccepted(Token(),Pawn,Error));
	TestEqual(TEXT("Repeated acceptance creates no second possession event"),Events.Possessed,1);
	TestEqual(TEXT("Acceptance creates no replacement controller"),Events.Created,1);
	Lease.BeginRelease(Token());
	TestFalse(TEXT("Activation cannot report success after release begins"),Lease.ActivateAccepted(Token(),Pawn,Error));
	TestTrue(TEXT("Accepted cleanup removes the retained controller"),Drain(Lease,Token()));
	TestNull(TEXT("Accepted cleanup clears the pawn controller binding"),Pawn->GetController());
	TestTrue(TEXT("Controller cleanup preserves the owned pawn for its separate actor owner"),IsValid(Pawn));
	TestEqual(TEXT("Accepted cleanup emits one native unpossession event"),Events.Unpossessed,1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoDormantRejectedPreflightReleaseTest,
	"NoShellForWinter.CalystoDungeon.Director.DormantController.RejectedPreflightOwnsAndReleasesItsToken",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoDormantRejectedPreflightReleaseTest::RunTest(const FString&)
{
	using namespace ProjectCalystoDormantControllerTests;
	FWorld World; if (!World.Value) return false;
	AActor* Owner=World.Value->SpawnActor<AActor>(); APawn* Pawn=World.SpawnPawn(Owner); if (!Owner || !Pawn) return false;
	Pawn->AIControllerClass=TSubclassOf<AAIController>(APawn::StaticClass());
	FProjectCalystoDormantController Lease; FString Error;
	TestFalse(TEXT("An invalid exact controller class rejects before allocation"),Lease.Begin(Token(),Pawn,Owner,Error));
	TestNull(TEXT("Rejected preflight owns no controller actor"),Lease.GetController());
	Lease.BeginRelease(Token(2));
	TestFalse(TEXT("A stale token cannot release a rejected preflight lease"),Lease.ObserveRelease(Token(2)));
	Lease.BeginRelease(Token());
	TestTrue(TEXT("Rejected preflight releases its owned token without pending callbacks"),Lease.ObserveRelease(Token()));
	TestNull(TEXT("Rejected preflight cleanup still owns no controller actor"),Lease.GetController());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoDormantAuditedNpcProfileTest,
	"NoShellForWinter.CalystoDungeon.Director.DormantController.AuditedNpcProfilePreflight",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoDormantAuditedNpcProfileTest::RunTest(const FString&)
{
	const TCHAR* Path=TEXT("/AscentCombatFramework/Blueprints/AI/Controllers/ACF_NPCController_BP.ACF_NPCController_BP_C");
	UClass* ControllerClass=LoadClass<AAIController>(nullptr,Path);
	TArray<FSoftObjectPath> Dependencies; FString Error;
	if (!TestNotNull(TEXT("The exact installed ACF NPC controller class is resident"),ControllerClass)) return false;
	TestTrue(TEXT("The exact audited NPC BeginPlay profile is admitted"),FProjectCalystoDormantController::PreflightClass(ControllerClass,Dependencies,Error));
	TestTrue(TEXT("Preflight retains the exact audited NPC controller dependency"),Dependencies.Contains(FSoftObjectPath(ControllerClass)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectCalystoDormantTransferTest,
	"NoShellForWinter.CalystoDungeon.Director.DormantController.AcceptedTransferRetainsExactNativePair",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectCalystoDormantTransferTest::RunTest(const FString&)
{
	using namespace ProjectCalystoDormantControllerTests;
	FWorld World; if (!World.Value) return false;
	AActor* Owner=World.Value->SpawnActor<AActor>(); APawn* Pawn=World.SpawnPawn(Owner); if (!Owner || !Pawn) return false;
	FObserver Events(World.Value); FLeaseScope Scope(*this); auto& Lease=Scope.Lease; FString Error;
	if (!Lease.Begin(Token(),Pawn,Owner,Error)) { AddError(Error); return false; }
	TestNull(TEXT("An unaccepted controller cannot transfer"),Lease.TransferAccepted(Token(),Pawn,Error));
	if (!Lease.ActivateAccepted(Token(),Pawn,Error)) { AddError(Error); return false; }
	TestNull(TEXT("A stale accepted transfer cannot steal the pair"),Lease.TransferAccepted(Token(2),Pawn,Error));
	TestNull(TEXT("A null pawn cannot establish persistent ownership"),Lease.TransferAccepted(Token(),nullptr,Error));
	// This fixture supplies the independent strong owner required before asking the lease to relinquish its reference.
	TStrongObjectPtr<AAIController> Persistent(Lease.GetController());
	TestTrue(TEXT("Accepted transfer returns the exact selected controller"),Lease.TransferAccepted(Token(),Pawn,Error)==Persistent.Get());
	TestNull(TEXT("Transferred controller leaves the attempt ledger"),Lease.GetController());
	TestTrue(TEXT("Transfer preserves both directions of the accepted native binding"),Persistent->GetPawn()==Pawn && Pawn->GetController()==Persistent.Get());
	TestTrue(TEXT("Retired attempt cleanup cannot destroy the transferred controller"),Lease.ObserveRelease(Token()));
	TestTrue(TEXT("Transferred controller remains alive under the independent owner"),IsValid(Persistent.Get()));
	Persistent->UnPossess(); TestTrue(TEXT("Persistent fixture owner performs its own controller cleanup"),Persistent->Destroy());
	TestEqual(TEXT("Transfer never repeats native possession"),Events.Possessed,1);
	TestEqual(TEXT("Persistent cleanup unpossesses once"),Events.Unpossessed,1);
	return true;
}

#endif

#include "Calysto/ProjectCalystoDormantController.h"

#include "ACFAIController.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTreeManager.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BrainComponent.h"
#include "Components/ACFAIRoutineComponent.h"
#include "Components/ACFSplineFollowerComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"

namespace ProjectCalystoDormantPrivate
{
	bool HasScript(UClass* Class,FName FunctionName)
	{ const UFunction* Function=Class?Class->FindFunctionByName(FunctionName):nullptr; return Function && !Function->Script.IsEmpty(); }
	const UFunction* DirectFunction(UClass* Class,FName FunctionName)
	{
		const UFunction* Function=Class?Class->FindFunctionByName(FunctionName):nullptr;
		return Function && Function->GetOuterUClass()==Class ? Function : nullptr;
	}
	bool MatchesAuditedBlueprint(const UClass* Class,const TCHAR* ExpectedPath,const FGuid& ExpectedGuid)
	{
		const UBlueprint* Blueprint=Class?Cast<UBlueprint>(Class->ClassGeneratedBy):nullptr;
		return Class && Class->GetPathName()==ExpectedPath && Blueprint && Blueprint->GetBlueprintGuid()==ExpectedGuid;
	}
	bool IsAuditedNpcBeginPlayProfile(UClass* Class,FString& Error)
	{
		// The only Blueprint BeginPlay admitted by the dormant contract is the
		// installed ACF NPC controller pair.  Its package identities are frozen
		// here, while FinishSpawning below still audits all observable side effects.
		static const FGuid NpcBlueprintGuid(0x7EB252BC,0x4F7157B1,0xC52D0690,0xDB902687);
		static const FGuid ParentBlueprintGuid(0x043B1BD3,0x4FF32FAA,0x105CC095,0x2AAD1E75);
		static const TCHAR* NpcPath=TEXT("/AscentCombatFramework/Blueprints/AI/Controllers/ACF_NPCController_BP.ACF_NPCController_BP_C");
		static const TCHAR* ParentPath=TEXT("/AscentCombatFramework/Blueprints/AI/Controllers/ACFAIControllerBP.ACFAIControllerBP_C");
		UClass* Parent=Class?Class->GetSuperClass():nullptr;
		if (!MatchesAuditedBlueprint(Class,NpcPath,NpcBlueprintGuid)
			|| !MatchesAuditedBlueprint(Parent,ParentPath,ParentBlueprintGuid)
			|| !Class->IsChildOf(AACFAIController::StaticClass()))
		{
			Error=FString::Printf(TEXT("Controller class %s has a Blueprint BeginPlay outside the audited ACF NPC profile."),*GetNameSafe(Class));
			return false;
		}
		const UFunction* LeafBeginPlay=DirectFunction(Class,TEXT("ReceiveBeginPlay"));
		const UFunction* ParentBeginPlay=DirectFunction(Parent,TEXT("ReceiveBeginPlay"));
		if (!LeafBeginPlay || LeafBeginPlay->Script.IsEmpty() || !ParentBeginPlay || ParentBeginPlay->Script.IsEmpty())
		{
			Error=TEXT("The audited ACF NPC BeginPlay profile no longer has its exact frozen Blueprint functions.");
			return false;
		}
		return true;
	}
	bool CheckHooks(UClass* Class,FString& Error)
	{
		static const FName Hooks[]={TEXT("UserConstructionScript"),TEXT("ReceiveEndPlay"),TEXT("ReceiveDestroyed"),TEXT("ReceivePossess"),TEXT("ReceiveUnPossess"),TEXT("OnUsingBlackBoard")};
		if (HasScript(Class,TEXT("ReceiveBeginPlay")) && !IsAuditedNpcBeginPlayProfile(Class,Error)) return false;
		for (const FName Hook:Hooks) if (HasScript(Class,Hook))
		{ Error=FString::Printf(TEXT("Controller class %s has executable %s; its native staging contract is unsupported."),*GetNameSafe(Class),*Hook.ToString());return false; }
		return true;
	}
	bool SupportedComponent(const UActorComponent* Component,FString& Error)
	{
		if (!IsValid(Component) || HasScript(Component->GetClass(),TEXT("ReceiveBeginPlay")))
		{ Error=TEXT("A controller component has unsupported BeginPlay behavior.");return false; }
		// This exact native component is supplied by the audited ACF NPC profile. Its
		// BeginPlay merely builds an in-memory schedule; it cannot tick and does not
		// start a task unless a caller explicitly invokes UpdateByTime/StartByGuid.
		// Admit no subclass, and require the observable task state to remain empty so
		// a controller that was activated before transaction acceptance is rejected.
		if (Component->GetClass()==UACFAIRoutineComponent::StaticClass())
		{
			const UACFAIRoutineComponent* Routine=Cast<UACFAIRoutineComponent>(Component);
			if (!Routine || Routine->GetLastStartedTask()!=nullptr
				|| Component->PrimaryComponentTick.bCanEverTick || Component->IsComponentTickEnabled())
			{ Error=TEXT("The audited ACF routine component was active before floor acceptance.");return false; }
			return true;
		}
		// This component is part of the exact ACF NPC profile. Its native BeginPlay
		// only caches its unpossessed owner; it starts no path request and has no tick
		// until StartFollowing. Admit no subclass and reject any pre-existing movement
		// state after construction, so this does not broaden the staging contract.
		if (Component->GetClass()==UACFSplineFollowerComponent::StaticClass())
		{
			const UACFSplineFollowerComponent* Follower=Cast<UACFSplineFollowerComponent>(Component);
			if (!Follower || Follower->IsFollowing() || Follower->IsWaitingForTarget())
			{ Error=TEXT("The audited ACF spline follower entered movement before floor acceptance.");return false; }
			return true;
		}
		// Exact class identities localize the reviewed native compatibility boundary. A different
		// class from the same module is not automatically safe (for example, a group spawner).
		static const TSet<FName> Classes={
			TEXT("/Script/Engine.ActorComponent"),TEXT("/Script/Engine.SceneComponent"),
			TEXT("/Script/AIModule.PathFollowingComponent"),TEXT("/Script/AIModule.CrowdFollowingComponent"),
			TEXT("/Script/AIModule.BehaviorTreeComponent"),TEXT("/Script/AIModule.BlackboardComponent"),
			TEXT("/Script/AIModule.AIPerceptionComponent"),TEXT("/Script/AIFramework.ACFCommandsManagerComponent"),
			TEXT("/Script/AIFramework.ACFCombatBehaviourComponent"),TEXT("/Script/AIFramework.ACFThreatManagerComponent"),
			TEXT("/Script/AscentTargetingSystem.ATSAITargetComponent")};
		if (!Classes.Contains(FName(*Component->GetClass()->GetPathName())))
		{ Error=TEXT("The selected controller contains a component without a supported native staging contract: ")+Component->GetClass()->GetPathName();return false; }
		return true;
	}
	bool Gone(const AActor* Actor)
	{
		if (!Actor) return true;
		const ULevel* Level=Actor->GetLevel(); if (Level && Level->Actors.Contains(Actor)) return false;
		TArray<UActorComponent*> Components;Actor->GetComponents(Components);
		for (const auto* C:Components) if (IsValid(C) && C->IsRegistered()) return false;
		return true;
	}
}

bool FProjectCalystoDormantController::PreflightClass(UClass* Class,TArray<FSoftObjectPath>& Dependencies,FString& Error)
{
	Dependencies.Reset();Error.Reset();
	if (!IsInGameThread() || !IsValid(Class) || !Class->IsChildOf(AAIController::StaticClass())
		|| Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated) || !ProjectCalystoDormantPrivate::CheckHooks(Class,Error))
	{ if (Error.IsEmpty()) Error=TEXT("Dormant controller staging requires a resident concrete native AI controller class.");return false; }
	UClass* NativeClass=Class;
	while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native)) NativeClass=NativeClass->GetSuperClass();
	if (NativeClass!=AAIController::StaticClass() && NativeClass!=AACFAIController::StaticClass())
	{ Error=TEXT("This controller's native possession implementation has not been admitted to the dormant staging contract.");return false; }
	auto* Defaults=Class->GetDefaultObject<AAIController>();
	if (Defaults->bWantsPlayerState) { Error=TEXT("Controllers which spawn a separate PlayerState require an additional tracked staging contract.");return false; }
	TArray<UObject*> DefaultsObjects;Defaults->GetDefaultSubobjects(DefaultsObjects);
	if (DefaultsObjects.Num()>64) { Error=TEXT("The controller exceeds the bounded default-component contract.");return false; }
	for (UObject* Object:DefaultsObjects) if (auto* C=Cast<UActorComponent>(Object))
		if (!ProjectCalystoDormantPrivate::SupportedComponent(C,Error)) return false;
	for (UClass* Current=Class;Current;Current=Current->GetSuperClass())
		if (const auto* BlueprintClass=Cast<UBlueprintGeneratedClass>(Current))
			for (const TObjectPtr<UActorComponent>& Component:BlueprintClass->ComponentTemplates)
				if (!ProjectCalystoDormantPrivate::SupportedComponent(Component.Get(),Error)) return false;
	Dependencies.Add(FSoftObjectPath(Class));
	if (const auto* ACF=Cast<AACFAIController>(Defaults))
	{
		const UBehaviorTree* Tree=ACF->GetBehaviorTree();
		if (!Tree || !Tree->BlackboardAsset || !Tree->RootNode)
		{ Error=TEXT("The selected ACF controller requires its actual resident behavior tree, root and blackboard before staging.");return false; }
		Dependencies.AddUnique(FSoftObjectPath(Tree));Dependencies.AddUnique(FSoftObjectPath(Tree->BlackboardAsset));
	}
	return true;
}

FProjectCalystoDormantController::~FProjectCalystoDormantController()
{
	if (Controller && IsInGameThread())
	{
		BeginRelease(OwnedToken);
		for (int32 Index=0;Index<8 && Controller;++Index) ObserveRelease(OwnedToken);
	}
	ensureMsgf(!Controller,TEXT("The dormant controller owner was destroyed before native release completed."));
}

void FProjectCalystoDormantController::AddReferencedObjects(FReferenceCollector& Collector)
{ Collector.AddReferencedObject(Controller); }

void FProjectCalystoDormantController::Quarantine()
{
	if (!IsValid(Controller)) return;
	Controller->SetActorHiddenInGame(true);Controller->SetActorEnableCollision(false);Controller->SetActorTickEnabled(false);
	TArray<UActorComponent*> Components;Controller->GetComponents(Components);
	for (auto* Component:Components) if (IsValid(Component)) Component->SetComponentTickEnabled(false);
}

bool FProjectCalystoDormantController::Begin(const FEFCalystoAttemptToken& Token,APawn* ExactPawn,AActor* AttemptOwner,FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bOperation || bReleased || bReleasing || !Token.Request.IsValid() || !Token.Attempt.IsValid()
		|| !IsValid(ExactPawn) || ExactPawn->IsActorBeingDestroyed() || !ExactPawn->HasAuthority() || !IsValid(ExactPawn->GetWorld())
		|| !IsValid(AttemptOwner) || AttemptOwner->IsActorBeingDestroyed() || AttemptOwner->GetWorld()!=ExactPawn->GetWorld())
	{ Error=TEXT("Dormant controller preparation requires its exact authoritative owned pawn and attempt.");return false; }
	if (OwnedToken.Request.IsValid() && (!(OwnedToken==Token) || Pawn.Get()!=ExactPawn || Owner.Get()!=AttemptOwner))
	{ Error=TEXT("Dormant controller preparation cannot change its exact owned attempt, pawn or owner.");return false; }
	if (!OwnedToken.Request.IsValid()) { OwnedToken=Token;Pawn=ExactPawn;Owner=AttemptOwner; }
	if (Controller) return Verify(Token,ExactPawn,Error);
	if (ExactPawn->GetController() || ExactPawn->AutoPossessAI!=EAutoPossessAI::Disabled)
	{ Error=TEXT("The exact pawn must remain unpossessed with automatic possession disabled before native staging.");return false; }
	TArray<FSoftObjectPath> Dependencies;
	if (!PreflightClass(ExactPawn->AIControllerClass.Get(),Dependencies,Error)) return false;
	TGuardValue<bool> Operation(bOperation,true);
	UWorld* World=ExactPawn->GetWorld();
	Controller=World->SpawnActorDeferred<AAIController>(ExactPawn->AIControllerClass,ExactPawn->GetActorTransform(),AttemptOwner,nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
	if (!Controller) { Error=TEXT("The exact selected controller could not be allocated.");return false; }
	bControllerTick=Controller->IsActorTickEnabled();
	Quarantine();
	if (bReleasing) { Error=TEXT("Controller staging was cancelled during allocation.");return false; }
	TArray<TWeakObjectPtr<AActor>> UntrackedSpawnedActors;
	int32 UntrackedSpawnCount=0;
	const FDelegateHandle SpawnHandle=World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
		[&UntrackedSpawnedActors,&UntrackedSpawnCount,ThisController=Controller](AActor* Spawned)
		{
			if (IsValid(Spawned) && Spawned!=ThisController)
			{ ++UntrackedSpawnCount; UntrackedSpawnedActors.Add(Spawned); }
		}));
	Controller->FinishSpawning(ExactPawn->GetActorTransform(),false,nullptr,ESpawnActorScaleMethod::OverrideRootScale);
	World->RemoveOnActorSpawnedHandler(SpawnHandle);
	if (UntrackedSpawnCount!=0)
	{
		for (const TWeakObjectPtr<AActor>& Spawned:UntrackedSpawnedActors)
			if (AActor* Actor=Spawned.Get(); IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->Destroy();
		Error=FString::Printf(TEXT("AuditedControllerBeginPlaySpawnedUntrackedActor: %d untracked actor(s) appeared during exact controller construction."),UntrackedSpawnCount);
		BeginRelease(Token); return false;
	}
	if (!IsValid(Controller) || bReleasing || Controller->GetPawn() || ExactPawn->GetController())
	{ Error=TEXT("Controller construction violated its unpossessed staging contract.");BeginRelease(Token);return false; }
	TArray<AActor*> AttachedActors; Controller->GetAttachedActors(AttachedActors,true,true);
	if (!AttachedActors.IsEmpty())
	{
		Error=TEXT("AuditedControllerBeginPlayAttachedUntrackedActor: controller construction attached an untracked actor.");
		BeginRelease(Token); return false;
	}
	TArray<UActorComponent*> Components;Controller->GetComponents(Components);
	if (Components.Num()>64) { Error=TEXT("The generated controller exceeds the component bound.");BeginRelease(Token);return false; }
	for (auto* Component:Components)
	{
		if (!ProjectCalystoDormantPrivate::SupportedComponent(Component,Error)) { BeginRelease(Token);return false; }
		ComponentActivation.Add({Component,Component->PrimaryComponentTick.bStartWithTickEnabled != 0});
	}
	Quarantine();
	if (auto* ACF=Cast<AACFAIController>(Controller))
	{
		UBehaviorTree* Tree=ACF->GetBehaviorTree();UBlackboardComponent* Blackboard=nullptr;
		UBehaviorTreeManager* Manager=UBehaviorTreeManager::GetCurrent(World);UBTCompositeNode* Root=nullptr;uint16 Memory=0;
		if (!Manager || !Manager->LoadTree(*Tree,Root,Memory) || !Root || !Controller->UseBlackboard(Tree->BlackboardAsset,Blackboard) || !Blackboard)
		{ Error=TEXT("The exact native behavior tree/blackboard could not prepare before controller activation.");BeginRelease(Token);return false; }
	}
	return Verify(Token,ExactPawn,Error);
}

bool FProjectCalystoDormantController::Verify(const FEFCalystoAttemptToken& Token,const APawn* ExactPawn,FString& Error) const
{
	Error.Reset();
	if (!(Token==OwnedToken) || bReleasing || bReleased || bAccepted || !IsValid(ExactPawn) || Pawn.Get()!=ExactPawn
		|| !Owner.IsValid() || !IsValid(Controller) || Controller->GetWorld()!=ExactPawn->GetWorld()
		|| Controller->GetOwner()!=Owner.Get() || Controller->GetClass()!=ExactPawn->AIControllerClass.Get()
		|| Controller->GetPawn() || ExactPawn->GetController() || Controller->IsActorTickEnabled()
		|| !Controller->IsHidden() || Controller->GetActorEnableCollision())
	{ Error=TEXT("Dormant controller identity, ownership, exact class or unpossessed quarantine changed.");return false; }
	if (const auto* Brain=Controller->GetBrainComponent();Brain && Brain->IsRunning())
	{ Error=TEXT("A controller began AI logic before floor acceptance.");return false; }
	TArray<UActorComponent*> Components;Controller->GetComponents(Components);
	if (Components.Num()>64) { Error=TEXT("Dormant controller components exceeded their frozen bound.");return false; }
	for (const auto* Component:Components) if (!IsValid(Component) || !Component->IsRegistered() || Component->IsComponentTickEnabled()
		|| !ProjectCalystoDormantPrivate::SupportedComponent(Component,Error))
	{ Error=TEXT("Dormant controller components are unregistered or active before acceptance.");return false; }
	return true;
}

bool FProjectCalystoDormantController::ActivateAccepted(const FEFCalystoAttemptToken& Token,APawn* ExactPawn,FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bOperation || bReleasing || bReleased)
	{ Error=TEXT("A controller which is releasing cannot reactivate.");return false; }
	if (bAccepted && Token==OwnedToken && Pawn.Get()==ExactPawn && IsValid(Controller) && Controller->GetPawn()==ExactPawn) return true;
	if (!Verify(Token,ExactPawn,Error)) return false;
	TGuardValue<bool> Operation(bOperation,true); bAccepted=true;
	// All selected objects and assets already exist. Native possession starts gameplay now,
	// after the transaction boundary; no new controller or alternate pawn is created.
	Controller->Possess(ExactPawn);
	if (bReleasing || !IsValid(Controller) || !IsValid(ExactPawn) || Controller->GetPawn()!=ExactPawn || ExactPawn->GetController()!=Controller)
	{ Error=TEXT("Native accepted possession did not retain its exact pawn/controller binding.");return false; }
	Controller->SetActorTickEnabled(bControllerTick);
	for (const auto& Activation:ComponentActivation)
	{
		if (bReleasing) { Error=TEXT("The accepted controller was released during activation.");return false; }
		if (Activation.Component.IsValid()) Activation.Component->SetComponentTickEnabled(Activation.bTick);
	}
	return true;
}

void FProjectCalystoDormantController::BeginRelease(const FEFCalystoAttemptToken& Token)
{
	if (!IsInGameThread() || !(Token==OwnedToken) || bReleased || bReleasing) return;
	bReleasing=true;Quarantine();
}

bool FProjectCalystoDormantController::ObserveRelease(const FEFCalystoAttemptToken& Token)
{
	if (!IsInGameThread() || bOperation || !(Token==OwnedToken)) return false;
	if (bReleased) return true;
	if (!bReleasing) return false;
	TGuardValue<bool> Operation(bOperation,true);
	if (IsValid(Controller) && !Controller->IsActorBeingDestroyed())
	{
		if (Controller->GetPawn()) Controller->UnPossess();
		if (!Controller->Destroy()) return false;
	}
	if (!ProjectCalystoDormantPrivate::Gone(Controller)) return false;
	Controller=nullptr;Pawn.Reset();Owner.Reset();ComponentActivation.Reset();bReleased=true;return true;
}

AAIController* FProjectCalystoDormantController::TransferAccepted(const FEFCalystoAttemptToken& Token,const APawn* ExactPawn,FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || bOperation || !(Token==OwnedToken) || !bAccepted || bReleasing || bReleased || !IsValid(ExactPawn) || Pawn.Get()!=ExactPawn
		|| !IsValid(Controller) || Controller->GetPawn()!=ExactPawn || ExactPawn->GetController()!=Controller)
	{ Error=TEXT("Only the exact accepted pawn/controller pair can transfer to persistent gameplay.");return nullptr; }
	AAIController* Result=Controller;Controller=nullptr;Pawn.Reset();Owner.Reset();ComponentActivation.Reset();bReleased=true;return Result;
}

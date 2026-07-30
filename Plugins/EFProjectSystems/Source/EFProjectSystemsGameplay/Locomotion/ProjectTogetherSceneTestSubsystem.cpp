#include "Locomotion/ProjectTogetherSceneTestSubsystem.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "BrainComponent.h"
#include "Characters/ProjectTargetingFixComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectTogetherSceneTest, Log, All);

namespace ProjectTogetherSceneTestPrivate
{
	constexpr int32 InputPriority = 8;
	constexpr float MontageBlendInTime = 0.0f;
	constexpr float MontageBlendOutTime = 0.08f;
	const FName OverlaySlotName(TEXT("DefaultSlot"));
	const FName FemaleRoleName(TEXT("Female"));
	const FName MaleRoleName(TEXT("Male"));

	static FString NormalizeToken(const FString& Value)
	{
		FString Normalized;
		Normalized.Reserve(Value.Len());
		for (const TCHAR Character : Value)
		{
			if (FChar::IsAlnum(Character))
			{
				Normalized.AppendChar(FChar::ToLower(Character));
			}
		}

		return Normalized;
	}

	static bool ComponentLooksLikeRole(const USkeletalMeshComponent* Component, const FName ComponentNameHint, const TCHAR* RoleToken)
	{
		if (!Component)
		{
			return false;
		}

		const FString NormalizedRole = NormalizeToken(RoleToken);
		if (!ComponentNameHint.IsNone() && NormalizeToken(ComponentNameHint.ToString()).Contains(NormalizedRole))
		{
			return true;
		}

		if (NormalizeToken(Component->GetName()).Contains(NormalizedRole))
		{
			return true;
		}

		if (const USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset())
		{
			return NormalizeToken(Mesh->GetPathName()).Contains(NormalizedRole);
		}

		return false;
	}

	static int32 ScoreRuntimeMesh(const USkeletalMeshComponent* RuntimeMesh, const FProjectTogetherSceneRoleDefinition& Role)
	{
		if (!RuntimeMesh)
		{
			return INDEX_NONE;
		}

		int32 Score = RuntimeMesh->IsVisible() ? 10 : 0;
		const USkeletalMesh* RuntimeMeshAsset = RuntimeMesh->GetSkeletalMeshAsset();
		if (RuntimeMeshAsset && RuntimeMeshAsset == Role.ReferenceMesh)
		{
			Score += 1000;
		}

		if (RuntimeMeshAsset && Role.ReferenceMesh && RuntimeMeshAsset->GetSkeleton() && RuntimeMeshAsset->GetSkeleton() == Role.ReferenceMesh->GetSkeleton())
		{
			Score += 400;
		}

		if (Role.Animation && RuntimeMeshAsset && RuntimeMeshAsset->GetSkeleton() && Role.Animation->GetSkeleton() == RuntimeMeshAsset->GetSkeleton())
		{
			Score += 250;
		}

		if (NormalizeToken(RuntimeMesh->GetName()).Contains(NormalizeToken(Role.RoleName.ToString())))
		{
			Score += 25;
		}

		return Score;
	}

	static FString CleanBlueprintObjectPath(const FSoftObjectPath& Path)
	{
		FString ObjectPath = Path.ToString();
		int32 QuoteStart = INDEX_NONE;
		int32 QuoteEnd = INDEX_NONE;
		if (ObjectPath.FindChar(TEXT('\''), QuoteStart) && ObjectPath.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
		{
			ObjectPath = ObjectPath.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		}

		return ObjectPath;
	}

	static UClass* LoadBlueprintGeneratedClass(const FSoftObjectPath& BlueprintPath)
	{
		const FString ObjectPath = CleanBlueprintObjectPath(BlueprintPath);
		if (ObjectPath.IsEmpty())
		{
			return nullptr;
		}

		const FString GeneratedClassPath = ObjectPath.EndsWith(TEXT("_C")) ? ObjectPath : ObjectPath + TEXT("_C");
		return StaticLoadClass(AActor::StaticClass(), nullptr, *GeneratedClassPath);
	}
}

UProjectTogetherSceneTestSubsystem::UProjectTogetherSceneTestSubsystem()
{
	SceneBlueprintPath = FSoftObjectPath(TEXT("/Script/Engine.Blueprint'/Game/_Game/Animations/Intimacy/Scenes/BP_IntimacyScene_0001.BP_IntimacyScene_0001'"));
}

void UProjectTogetherSceneTestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TrackedPlayerController = nullptr;
	TrackedPlayerPawn = nullptr;
	TrackedInputComponent = nullptr;
	ActivePlayerMontage = nullptr;
	ActiveEnemyMontage = nullptr;
	PlayerParticipantState.Reset();
	EnemyParticipantState.Reset();
	BoolSceneLockSnapshots.Reset();
	FloatSceneLockSnapshots.Reset();
	LeaderPoseSnapshots.Reset();
	bSceneActive = false;
}

void UProjectTogetherSceneTestSubsystem::Deinitialize()
{
	DetachFromTrackedPlayerController(true);
	Super::Deinitialize();
}

void UProjectTogetherSceneTestSubsystem::Tick(float DeltaTime)
{
	TryResolveRuntimeContext();

	if (bSceneActive)
	{
		if (!PlayerParticipantState.Actor.IsValid() || !EnemyParticipantState.Actor.IsValid())
		{
			UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Participant became invalid; stopping scene."));
			StopTogetherScene();
		}
	}
}

TStatId UProjectTogetherSceneTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProjectTogetherSceneTestSubsystem, STATGROUP_Tickables);
}

bool UProjectTogetherSceneTestSubsystem::IsTickable() const
{
	return false;
}

bool UProjectTogetherSceneTestSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return false;
}

void UProjectTogetherSceneTestSubsystem::HandleTrackedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (OldPawn && OldPawn == TrackedPlayerPawn)
	{
		StopTogetherScene();
	}

	TrackedPlayerPawn = NewPawn;
}

void UProjectTogetherSceneTestSubsystem::TryResolveRuntimeContext()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		DetachFromTrackedPlayerController(true);
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		DetachFromTrackedPlayerController(true);
		return;
	}

	AttachToPlayerController(PlayerController);
}

void UProjectTogetherSceneTestSubsystem::AttachToPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (TrackedPlayerController == PlayerController)
	{
		BindInputToTrackedPlayerController();
		if (TrackedPlayerPawn != PlayerController->GetPawn())
		{
			HandleTrackedPawnChanged(TrackedPlayerPawn, PlayerController->GetPawn());
		}
		return;
	}

	DetachFromTrackedPlayerController(true);
	TrackedPlayerController = PlayerController;
	TrackedPlayerPawn = PlayerController->GetPawn();
	TrackedPlayerController->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	BindInputToTrackedPlayerController();
}

void UProjectTogetherSceneTestSubsystem::DetachFromTrackedPlayerController(const bool bStopActiveScene)
{
	if (bStopActiveScene)
	{
		StopTogetherScene();
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::HandleTrackedPawnChanged);
	}

	UnbindInputFromTrackedPlayerController();
	TrackedPlayerPawn = nullptr;
	TrackedPlayerController = nullptr;
}

void UProjectTogetherSceneTestSubsystem::BindInputToTrackedPlayerController()
{
	// Deprecated test path. Together scenes now run exclusively through ProjectEmote/Y menu.
}

void UProjectTogetherSceneTestSubsystem::UnbindInputFromTrackedPlayerController()
{
	if (!TrackedInputComponent)
	{
		return;
	}

	if (TrackedPlayerController)
	{
		TrackedPlayerController->PopInputComponent(TrackedInputComponent);
	}

	if (TrackedInputComponent->IsRegistered())
	{
		TrackedInputComponent->DestroyComponent();
	}

	TrackedInputComponent = nullptr;
}

void UProjectTogetherSceneTestSubsystem::HandleToggleScenePressed()
{
	if (bSceneActive)
	{
		StopTogetherScene();
		return;
	}

	StartTogetherScene();
}

bool UProjectTogetherSceneTestSubsystem::StartTogetherScene()
{
	if (bSceneActive)
	{
		return false;
	}

	APlayerController* PlayerController = TrackedPlayerController.Get();
	APawn* PlayerPawn = TrackedPlayerPawn.Get();
	if (!PlayerController || !PlayerPawn)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Cannot start: no local player pawn."));
		return false;
	}

	if (const UProjectEmoteComponent* EmoteComponent = PlayerPawn->FindComponentByClass<UProjectEmoteComponent>())
	{
		if (EmoteComponent->IsEmoteActive() || EmoteComponent->IsEmoteTransitionPending())
		{
			UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Cannot start while a Y-menu emote/action is active."));
			return false;
		}
	}

	AActor* TargetActor = nullptr;
	if (!ResolveTargetActor(TargetActor) || !TargetActor || TargetActor == PlayerPawn)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Cannot start: no valid targeted enemy. Select/observe an enemy with T first."));
		return false;
	}

	FProjectTogetherSceneDefinition SceneDefinition;
	if (!LoadSceneDefinition(SceneDefinition))
	{
		return false;
	}

	USkeletalMeshComponent* PlayerMesh = ResolveRuntimeMesh(PlayerPawn, SceneDefinition.Female);
	USkeletalMeshComponent* EnemyMesh = ResolveRuntimeMesh(TargetActor, SceneDefinition.Male);
	if (!ValidateRoleAgainstRuntimeMesh(SceneDefinition.Female, PlayerMesh, TEXT("Player/Female"))
		|| !ValidateRoleAgainstRuntimeMesh(SceneDefinition.Male, EnemyMesh, TEXT("Enemy/Male")))
	{
		return false;
	}

	CacheAndFreezeParticipant(PlayerParticipantState, PlayerPawn, PlayerMesh, PlayerController);
	CacheAndFreezeParticipant(EnemyParticipantState, TargetActor, EnemyMesh, PlayerController);

	const FTransform SceneAnchorTransform = PlayerPawn->GetActorTransform();
	ApplyParticipantTransform(PlayerParticipantState, SceneDefinition.Female, SceneAnchorTransform);
	ApplyParticipantTransform(EnemyParticipantState, SceneDefinition.Male, SceneAnchorTransform);

	ApplyAnimSceneLockForActor(PlayerPawn, PlayerMesh);
	ApplyAnimSceneLockForActor(TargetActor, EnemyMesh);

	ActivePlayerMontage = PlayRoleMontage(SceneDefinition.Female, PlayerMesh, TEXT("Player/Female"));
	ActiveEnemyMontage = PlayRoleMontage(SceneDefinition.Male, EnemyMesh, TEXT("Enemy/Male"));

	if (!ActivePlayerMontage || !ActiveEnemyMontage)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Failed to start one or both role montages; restoring."));
		StopTogetherScene();
		return false;
	}

	bSceneActive = true;
	UE_LOG(
		LogProjectTogetherSceneTest,
		Log,
		TEXT("[TogetherSceneTest] Started %s with Player=%s Enemy=%s FemaleAnim=%s MaleAnim=%s"),
		*SceneBlueprintPath.ToString(),
		*GetNameSafe(PlayerPawn),
		*GetNameSafe(TargetActor),
		*GetNameSafe(SceneDefinition.Female.Animation),
		*GetNameSafe(SceneDefinition.Male.Animation));
	return true;
}

void UProjectTogetherSceneTestSubsystem::StopTogetherScene()
{
	if (!bSceneActive && !ActivePlayerMontage && !ActiveEnemyMontage && !PlayerParticipantState.Actor.IsValid() && !EnemyParticipantState.Actor.IsValid())
	{
		return;
	}

	APlayerController* PlayerController = TrackedPlayerController.Get();
	StopRoleMontage(PlayerParticipantState, ActivePlayerMontage);
	StopRoleMontage(EnemyParticipantState, ActiveEnemyMontage);
	RestoreAnimSceneLock();
	RestoreParticipant(EnemyParticipantState, PlayerController);
	RestoreParticipant(PlayerParticipantState, PlayerController);
	PlayerParticipantState.Reset();
	EnemyParticipantState.Reset();
	bSceneActive = false;

	if (PlayerController)
	{
		PlayerController->FlushPressedKeys();
	}

	UE_LOG(LogProjectTogetherSceneTest, Log, TEXT("[TogetherSceneTest] Stopped and restored participants."));
}

bool UProjectTogetherSceneTestSubsystem::ResolveTargetActor(AActor*& OutTargetActor) const
{
	OutTargetActor = nullptr;
	const APawn* PlayerPawn = TrackedPlayerPawn.Get();
	if (!PlayerPawn)
	{
		return false;
	}

	if (const UProjectTargetingFixComponent* TargetingFixComponent = PlayerPawn->FindComponentByClass<UProjectTargetingFixComponent>())
	{
		OutTargetActor = TargetingFixComponent->GetCurrentTargetActor();
	}

	return IsValid(OutTargetActor);
}

bool UProjectTogetherSceneTestSubsystem::LoadSceneDefinition(FProjectTogetherSceneDefinition& OutDefinition) const
{
	OutDefinition = FProjectTogetherSceneDefinition();
	OutDefinition.Female.RoleName = ProjectTogetherSceneTestPrivate::FemaleRoleName;
	OutDefinition.Male.RoleName = ProjectTogetherSceneTestPrivate::MaleRoleName;

	UObject* BlueprintObject = SceneBlueprintPath.TryLoad();
	UClass* SceneClass = nullptr;
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintObject))
	{
		SceneClass = Blueprint->GeneratedClass;
	}

	if (!SceneClass)
	{
		SceneClass = ProjectTogetherSceneTestPrivate::LoadBlueprintGeneratedClass(SceneBlueprintPath);
	}

	if (!SceneClass || !SceneClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Failed to load generated actor class from %s"), *SceneBlueprintPath.ToString());
		return false;
	}

	const AActor* SceneCDO = Cast<AActor>(SceneClass->GetDefaultObject());
	if (!SceneCDO)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] Blueprint class has no actor CDO: %s"), *GetNameSafe(SceneClass));
		return false;
	}

	TArray<USkeletalMeshComponent*> SceneMeshComponents;
	SceneCDO->GetComponents(SceneMeshComponents);
	for (USkeletalMeshComponent* Component : SceneMeshComponents)
	{
		ExtractRoleFromComponent(Component, Component ? Component->GetFName() : NAME_None, OutDefinition.Female, OutDefinition.Male);
	}

	if ((!OutDefinition.Female.ReferenceMesh || !OutDefinition.Female.Animation || !OutDefinition.Male.ReferenceMesh || !OutDefinition.Male.Animation)
		&& BlueprintObject)
	{
		const UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintObject);
		const UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(SceneClass);
		const USimpleConstructionScript* SimpleConstructionScript = nullptr;
		if (Blueprint && Blueprint->SimpleConstructionScript)
		{
			SimpleConstructionScript = Blueprint->SimpleConstructionScript.Get();
		}
		else if (BlueprintGeneratedClass)
		{
			SimpleConstructionScript = BlueprintGeneratedClass->SimpleConstructionScript.Get();
		}

		if (SimpleConstructionScript)
		{
			for (const USCS_Node* Node : SimpleConstructionScript->GetAllNodes())
			{
				if (!Node)
				{
					continue;
				}

				UActorComponent* ComponentTemplate = nullptr;
				if (UBlueprintGeneratedClass* MutableGeneratedClass = Cast<UBlueprintGeneratedClass>(SceneClass))
				{
					ComponentTemplate = const_cast<USCS_Node*>(Node)->GetActualComponentTemplate(MutableGeneratedClass);
				}

				if (!ComponentTemplate)
				{
					ComponentTemplate = Node->ComponentTemplate;
				}

				ExtractRoleFromComponent(
					Cast<USkeletalMeshComponent>(ComponentTemplate),
					Node->GetVariableName(),
					OutDefinition.Female,
					OutDefinition.Male);
			}
		}
	}

	const bool bHasFemale = OutDefinition.Female.ReferenceMesh && OutDefinition.Female.Animation;
	const bool bHasMale = OutDefinition.Male.ReferenceMesh && OutDefinition.Male.Animation;
	if (!bHasFemale || !bHasMale)
	{
		UE_LOG(
			LogProjectTogetherSceneTest,
			Warning,
			TEXT("[TogetherSceneTest] Blueprint %s did not expose valid Female/Male skeletal mesh components with AnimToPlay. FemaleMesh=%s FemaleAnim=%s MaleMesh=%s MaleAnim=%s"),
			*SceneBlueprintPath.ToString(),
			*GetNameSafe(OutDefinition.Female.ReferenceMesh),
			*GetNameSafe(OutDefinition.Female.Animation),
			*GetNameSafe(OutDefinition.Male.ReferenceMesh),
			*GetNameSafe(OutDefinition.Male.Animation));
		return false;
	}

	UE_LOG(
		LogProjectTogetherSceneTest,
		Log,
		TEXT("[TogetherSceneTest] Blueprint roles: FemaleComponent=%s FemaleMesh=%s FemaleAnim=%s MaleComponent=%s MaleMesh=%s MaleAnim=%s"),
		*OutDefinition.Female.ComponentName.ToString(),
		*GetNameSafe(OutDefinition.Female.ReferenceMesh),
		*GetNameSafe(OutDefinition.Female.Animation),
		*OutDefinition.Male.ComponentName.ToString(),
		*GetNameSafe(OutDefinition.Male.ReferenceMesh),
		*GetNameSafe(OutDefinition.Male.Animation));
	return true;
}

bool UProjectTogetherSceneTestSubsystem::ExtractRoleFromComponent(USkeletalMeshComponent* Component, const FName ComponentNameHint, FProjectTogetherSceneRoleDefinition& InOutFemale, FProjectTogetherSceneRoleDefinition& InOutMale) const
{
	if (!Component)
	{
		return false;
	}

	FProjectTogetherSceneRoleDefinition* TargetRole = nullptr;
	if (ProjectTogetherSceneTestPrivate::ComponentLooksLikeRole(Component, ComponentNameHint, TEXT("Female")))
	{
		TargetRole = &InOutFemale;
	}
	else if (ProjectTogetherSceneTestPrivate::ComponentLooksLikeRole(Component, ComponentNameHint, TEXT("Male")))
	{
		TargetRole = &InOutMale;
	}

	if (!TargetRole)
	{
		return false;
	}

	TargetRole->ComponentName = ComponentNameHint.IsNone() ? Component->GetFName() : ComponentNameHint;
	TargetRole->ReferenceMesh = Component->GetSkeletalMeshAsset();
	TargetRole->Animation = Cast<UAnimSequenceBase>(Component->AnimationData.AnimToPlay);
	TargetRole->RelativeTransform = Component->GetRelativeTransform();
	return TargetRole->ReferenceMesh && TargetRole->Animation;
}

USkeletalMeshComponent* UProjectTogetherSceneTestSubsystem::ResolveRuntimeMesh(AActor* Actor, const FProjectTogetherSceneRoleDefinition& Role) const
{
	if (!Actor)
	{
		return nullptr;
	}

	USkeletalMeshComponent* BestMesh = nullptr;
	int32 BestScore = INDEX_NONE;

	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			const int32 Score = ProjectTogetherSceneTestPrivate::ScoreRuntimeMesh(CharacterMesh, Role) + 100;
			if (Score > BestScore)
			{
				BestScore = Score;
				BestMesh = CharacterMesh;
			}
		}
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Actor->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		const int32 Score = ProjectTogetherSceneTestPrivate::ScoreRuntimeMesh(MeshComponent, Role);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestMesh = MeshComponent;
		}
	}

	return BestMesh;
}

bool UProjectTogetherSceneTestSubsystem::ValidateRoleAgainstRuntimeMesh(const FProjectTogetherSceneRoleDefinition& Role, const USkeletalMeshComponent* RuntimeMesh, const TCHAR* RuntimeLabel) const
{
	if (!RuntimeMesh)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] %s has no compatible runtime skeletal mesh."), RuntimeLabel);
		return false;
	}

	const USkeletalMesh* RuntimeMeshAsset = RuntimeMesh->GetSkeletalMeshAsset();
	if (!Role.ReferenceMesh || !Role.Animation || !RuntimeMeshAsset)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] %s missing mesh or animation data."), RuntimeLabel);
		return false;
	}

	if (!RuntimeMeshAsset->GetSkeleton() || Role.Animation->GetSkeleton() != RuntimeMeshAsset->GetSkeleton())
	{
		UE_LOG(
			LogProjectTogetherSceneTest,
			Warning,
			TEXT("[TogetherSceneTest] %s skeleton mismatch. RuntimeMesh=%s RuntimeSkeleton=%s Anim=%s AnimSkeleton=%s"),
			RuntimeLabel,
			*GetNameSafe(RuntimeMeshAsset),
			*GetNameSafe(RuntimeMeshAsset->GetSkeleton()),
			*GetNameSafe(Role.Animation),
			*GetNameSafe(Role.Animation->GetSkeleton()));
		return false;
	}

	if (Role.ReferenceMesh != RuntimeMeshAsset)
	{
		UE_LOG(
			LogProjectTogetherSceneTest,
			Warning,
			TEXT("[TogetherSceneTest] %s mesh asset differs from blueprint but skeleton matches. BlueprintMesh=%s RuntimeMesh=%s"),
			RuntimeLabel,
			*GetNameSafe(Role.ReferenceMesh),
			*GetNameSafe(RuntimeMeshAsset));
	}

	return true;
}

void UProjectTogetherSceneTestSubsystem::CacheAndFreezeParticipant(FProjectTogetherParticipantState& State, AActor* Actor, USkeletalMeshComponent* Mesh, APlayerController* PlayerController)
{
	State.Reset();
	if (!Actor)
	{
		return;
	}

	State.Actor = Actor;
	State.Character = Cast<ACharacter>(Actor);
	State.Mesh = Mesh;
	State.AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	State.CachedActorTransform = Actor->GetActorTransform();

	if (ACharacter* Character = State.Character.Get())
	{
		Character->StopJumping();
		State.MovementComponent = Character->GetCharacterMovement();
		if (UCharacterMovementComponent* MovementComponent = State.MovementComponent.Get())
		{
			State.bHadMovementComponent = true;
			State.CachedMovementMode = MovementComponent->MovementMode;
			State.CachedCustomMovementMode = MovementComponent->CustomMovementMode;
			MovementComponent->StopMovementImmediately();
			MovementComponent->DisableMovement();
		}

		if (PlayerController && Actor == PlayerController->GetPawn())
		{
			State.bCachedPlayerControllerState = true;
			State.bWasMoveInputIgnored = PlayerController->IsMoveInputIgnored();
			State.bWasLookInputIgnored = PlayerController->IsLookInputIgnored();
			if (!State.bWasMoveInputIgnored)
			{
				PlayerController->SetIgnoreMoveInput(true);
				State.bAppliedMoveInputIgnore = true;
			}
			if (!State.bWasLookInputIgnored)
			{
				PlayerController->SetIgnoreLookInput(true);
				State.bAppliedLookInputIgnore = true;
			}
			Character->DisableInput(PlayerController);
			State.bPawnInputSuspended = true;
		}
	}

	if (AController* Controller = State.Character.IsValid() ? State.Character->GetController() : nullptr)
	{
		Controller->StopMovement();
		if (AAIController* AIController = Cast<AAIController>(Controller))
		{
			if (UBrainComponent* BrainComponent = AIController->GetBrainComponent())
			{
				State.BrainComponent = BrainComponent;
				State.bHadBrainComponent = true;
				State.bBrainWasPaused = BrainComponent->IsPaused();
				if (!State.bBrainWasPaused)
				{
					BrainComponent->PauseLogic(TEXT("ProjectTogetherSceneTest"));
				}
			}
		}
	}
}

void UProjectTogetherSceneTestSubsystem::RestoreParticipant(FProjectTogetherParticipantState& State, APlayerController* PlayerController)
{
	AActor* Actor = State.Actor.Get();
	if (Actor)
	{
		Actor->SetActorTransform(State.CachedActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (ACharacter* Character = State.Character.Get())
	{
		if (State.bPawnInputSuspended && PlayerController && Character == PlayerController->GetPawn())
		{
			Character->EnableInput(PlayerController);
		}

		if (UCharacterMovementComponent* MovementComponent = State.MovementComponent.Get())
		{
			MovementComponent->StopMovementImmediately();
			if (State.bHadMovementComponent)
			{
				MovementComponent->SetMovementMode(State.CachedMovementMode, State.CachedCustomMovementMode);
			}
		}
	}

	if (PlayerController && State.bCachedPlayerControllerState)
	{
		if (State.bAppliedMoveInputIgnore)
		{
			PlayerController->SetIgnoreMoveInput(false);
		}

		if (State.bAppliedLookInputIgnore)
		{
			PlayerController->SetIgnoreLookInput(false);
		}
	}

	if (UBrainComponent* BrainComponent = State.BrainComponent.Get())
	{
		if (State.bHadBrainComponent && !State.bBrainWasPaused)
		{
			BrainComponent->ResumeLogic(TEXT("ProjectTogetherSceneTest"));
		}
	}
}

void UProjectTogetherSceneTestSubsystem::ApplyParticipantTransform(const FProjectTogetherParticipantState& State, const FProjectTogetherSceneRoleDefinition& Role, const FTransform& SceneAnchorTransform) const
{
	AActor* Actor = State.Actor.Get();
	const USkeletalMeshComponent* Mesh = State.Mesh.Get();
	if (!Actor || !Mesh)
	{
		return;
	}

	const FTransform DesiredMeshWorldTransform = Role.RelativeTransform * SceneAnchorTransform;
	const FTransform RuntimeMeshRelativeToActor = Mesh->GetComponentTransform().GetRelativeTransform(Actor->GetActorTransform());
	const FTransform DesiredActorTransform = RuntimeMeshRelativeToActor.Inverse() * DesiredMeshWorldTransform;
	Actor->SetActorTransform(DesiredActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

UAnimMontage* UProjectTogetherSceneTestSubsystem::PlayRoleMontage(const FProjectTogetherSceneRoleDefinition& Role, USkeletalMeshComponent* RuntimeMesh, const TCHAR* RuntimeLabel) const
{
	if (!RuntimeMesh || !Role.Animation)
	{
		return nullptr;
	}

	UAnimInstance* AnimInstance = RuntimeMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] %s has no AnimInstance."), RuntimeLabel);
		return nullptr;
	}

	const FMontageBlendSettings BlendInSettings(ProjectTogetherSceneTestPrivate::MontageBlendInTime);
	const FMontageBlendSettings BlendOutSettings(ProjectTogetherSceneTestPrivate::MontageBlendOutTime);
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(
		Role.Animation,
		ProjectTogetherSceneTestPrivate::OverlaySlotName,
		BlendInSettings,
		BlendOutSettings,
		1.0f,
		MAX_int32,
		-1.0f);
	if (!Montage)
	{
		return nullptr;
	}

	Montage->bEnableAutoBlendOut = false;
	const float PlayResult = AnimInstance->Montage_PlayWithBlendSettings(
		Montage,
		BlendInSettings,
		1.0f,
		EMontagePlayReturnType::MontageLength,
		0.0f,
		true);
	if (PlayResult <= 0.0f)
	{
		UE_LOG(LogProjectTogetherSceneTest, Warning, TEXT("[TogetherSceneTest] %s failed to play %s."), RuntimeLabel, *GetNameSafe(Role.Animation));
		return nullptr;
	}

	return Montage;
}

void UProjectTogetherSceneTestSubsystem::StopRoleMontage(FProjectTogetherParticipantState& State, TObjectPtr<UAnimMontage>& Montage) const
{
	UAnimInstance* AnimInstance = State.Mesh.IsValid() ? State.Mesh->GetAnimInstance() : State.AnimInstance.Get();
	if (AnimInstance)
	{
		if (Montage)
		{
			AnimInstance->Montage_Stop(ProjectTogetherSceneTestPrivate::MontageBlendOutTime, Montage);
		}
		AnimInstance->StopSlotAnimation(ProjectTogetherSceneTestPrivate::MontageBlendOutTime, ProjectTogetherSceneTestPrivate::OverlaySlotName);
	}

	Montage = nullptr;
}

void UProjectTogetherSceneTestSubsystem::ApplyAnimSceneLockForActor(AActor* Actor, USkeletalMeshComponent* SourceMesh)
{
	if (!Actor)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Actor->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
		{
			ApplyAnimInstanceSceneLock(AnimInstance);
		}

		if (UAnimInstance* PostProcessInstance = MeshComponent->GetPostProcessInstance())
		{
			ApplyAnimInstanceSceneLock(PostProcessInstance);
		}
	}

	ApplyVisibleMeshLeaderPoseSceneLock(Actor, SourceMesh);
	if (SourceMesh)
	{
		SourceMesh->RefreshBoneTransforms();
	}
}

void UProjectTogetherSceneTestSubsystem::ApplyAnimInstanceSceneLock(UObject* AnimInstanceObject)
{
	if (!AnimInstanceObject)
	{
		return;
	}

	static const FName BoolPropertyNames[] =
	{
		TEXT("bUpdateAimData"),
		TEXT("bUpdateRotationData"),
		TEXT("bUpdateLeaningData"),
		TEXT("bCanUseAdditive"),
		TEXT("bEnableIK"),
		TEXT("EnableIK"),
		TEXT("bEnableFootIK"),
		TEXT("EnableFootIK"),
		TEXT("bEnableHandIK"),
		TEXT("EnableHandIK")
	};

	for (const FName PropertyName : BoolPropertyNames)
	{
		CacheAndSetBoolSceneLockProperty(AnimInstanceObject, PropertyName, false);
	}

	static const FName FloatPropertyNames[] =
	{
		TEXT("TurnRate"),
		TEXT("LeanAngle"),
		TEXT("YawOffset"),
		TEXT("AimYaw"),
		TEXT("AimPitch")
	};

	for (const FName PropertyName : FloatPropertyNames)
	{
		CacheAndSetFloatSceneLockProperty(AnimInstanceObject, PropertyName, 0.0);
	}
}

void UProjectTogetherSceneTestSubsystem::ApplyVisibleMeshLeaderPoseSceneLock(AActor* Actor, USkeletalMeshComponent* SourceMesh)
{
	if (!Actor || !SourceMesh)
	{
		return;
	}

	const USkeletalMesh* SourceMeshAsset = SourceMesh->GetSkeletalMeshAsset();
	if (!SourceMeshAsset || !SourceMeshAsset->GetSkeleton())
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	Actor->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		FProjectTogetherLeaderPoseSnapshot Snapshot;
		Snapshot.Component = MeshComponent;
		Snapshot.LeaderPoseComponent = MeshComponent->LeaderPoseComponent.Get();
		LeaderPoseSnapshots.Add(Snapshot);
	}

	for (USkeletalMeshComponent* CandidateMeshComponent : MeshComponents)
	{
		if (!CandidateMeshComponent || CandidateMeshComponent == SourceMesh)
		{
			continue;
		}

		const USkeletalMesh* CandidateMeshAsset = CandidateMeshComponent->GetSkeletalMeshAsset();
		if (!CandidateMeshAsset || CandidateMeshAsset->GetSkeleton() != SourceMeshAsset->GetSkeleton())
		{
			continue;
		}

		if (!CandidateMeshComponent->IsVisible())
		{
			continue;
		}

		if (CandidateMeshComponent->LeaderPoseComponent.Get() == SourceMesh)
		{
			continue;
		}

		CandidateMeshComponent->SetLeaderPoseComponent(SourceMesh, true, false);
		CandidateMeshComponent->RefreshBoneTransforms();
	}
}

void UProjectTogetherSceneTestSubsystem::RestoreAnimSceneLock()
{
	for (const FProjectTogetherLeaderPoseSnapshot& Snapshot : LeaderPoseSnapshots)
	{
		if (USkeletalMeshComponent* Component = Snapshot.Component.Get())
		{
			Component->SetLeaderPoseComponent(nullptr, true);
		}
	}

	for (const FProjectTogetherLeaderPoseSnapshot& Snapshot : LeaderPoseSnapshots)
	{
		if (USkeletalMeshComponent* Component = Snapshot.Component.Get())
		{
			Component->SetLeaderPoseComponent(Snapshot.LeaderPoseComponent.Get(), true);
			Component->RefreshBoneTransforms();
		}
	}

	for (const FProjectTogetherBoolSceneLockSnapshot& Snapshot : BoolSceneLockSnapshots)
	{
		UObject* Target = Snapshot.Target.Get();
		if (!Target)
		{
			continue;
		}

		if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Target->GetClass(), Snapshot.PropertyName))
		{
			BoolProperty->SetPropertyValue_InContainer(Target, Snapshot.bValue);
		}
	}

	for (const FProjectTogetherFloatSceneLockSnapshot& Snapshot : FloatSceneLockSnapshots)
	{
		UObject* Target = Snapshot.Target.Get();
		if (!Target)
		{
			continue;
		}

		if (FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(Target->GetClass(), Snapshot.PropertyName))
		{
			FloatProperty->SetPropertyValue_InContainer(Target, static_cast<float>(Snapshot.Value));
		}
		else if (FDoubleProperty* DoubleProperty = FindFProperty<FDoubleProperty>(Target->GetClass(), Snapshot.PropertyName))
		{
			DoubleProperty->SetPropertyValue_InContainer(Target, Snapshot.Value);
		}
	}

	BoolSceneLockSnapshots.Reset();
	FloatSceneLockSnapshots.Reset();
	LeaderPoseSnapshots.Reset();
}

void UProjectTogetherSceneTestSubsystem::CacheAndSetBoolSceneLockProperty(UObject* Target, const FName PropertyName, const bool bNewValue)
{
	if (!IsValid(Target) || PropertyName.IsNone())
	{
		return;
	}

	FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Target->GetClass(), PropertyName);
	if (!BoolProperty)
	{
		return;
	}

	for (const FProjectTogetherBoolSceneLockSnapshot& Snapshot : BoolSceneLockSnapshots)
	{
		if (Snapshot.Target.Get() == Target && Snapshot.PropertyName == PropertyName)
		{
			BoolProperty->SetPropertyValue_InContainer(Target, bNewValue);
			return;
		}
	}

	FProjectTogetherBoolSceneLockSnapshot Snapshot;
	Snapshot.Target = Target;
	Snapshot.PropertyName = PropertyName;
	Snapshot.bValue = BoolProperty->GetPropertyValue_InContainer(Target);
	BoolSceneLockSnapshots.Add(Snapshot);
	BoolProperty->SetPropertyValue_InContainer(Target, bNewValue);
}

void UProjectTogetherSceneTestSubsystem::CacheAndSetFloatSceneLockProperty(UObject* Target, const FName PropertyName, const double NewValue)
{
	if (!IsValid(Target) || PropertyName.IsNone())
	{
		return;
	}

	if (FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(Target->GetClass(), PropertyName))
	{
		for (const FProjectTogetherFloatSceneLockSnapshot& Snapshot : FloatSceneLockSnapshots)
		{
			if (Snapshot.Target.Get() == Target && Snapshot.PropertyName == PropertyName)
			{
				FloatProperty->SetPropertyValue_InContainer(Target, static_cast<float>(NewValue));
				return;
			}
		}

		FProjectTogetherFloatSceneLockSnapshot Snapshot;
		Snapshot.Target = Target;
		Snapshot.PropertyName = PropertyName;
		Snapshot.Value = FloatProperty->GetPropertyValue_InContainer(Target);
		FloatSceneLockSnapshots.Add(Snapshot);
		FloatProperty->SetPropertyValue_InContainer(Target, static_cast<float>(NewValue));
		return;
	}

	if (FDoubleProperty* DoubleProperty = FindFProperty<FDoubleProperty>(Target->GetClass(), PropertyName))
	{
		for (const FProjectTogetherFloatSceneLockSnapshot& Snapshot : FloatSceneLockSnapshots)
		{
			if (Snapshot.Target.Get() == Target && Snapshot.PropertyName == PropertyName)
			{
				DoubleProperty->SetPropertyValue_InContainer(Target, NewValue);
				return;
			}
		}

		FProjectTogetherFloatSceneLockSnapshot Snapshot;
		Snapshot.Target = Target;
		Snapshot.PropertyName = PropertyName;
		Snapshot.Value = DoubleProperty->GetPropertyValue_InContainer(Target);
		FloatSceneLockSnapshots.Add(Snapshot);
		DoubleProperty->SetPropertyValue_InContainer(Target, NewValue);
	}
}

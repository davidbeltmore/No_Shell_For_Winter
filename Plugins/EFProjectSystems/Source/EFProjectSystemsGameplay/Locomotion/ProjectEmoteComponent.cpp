#include "Locomotion/ProjectEmoteComponent.h"

#include "ACFTrainingComponent.h"
#include "ACFTrainingSettings.h"
#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/ProjectCombatAttributeComponent.h"
#include "ContentPolicy/ProjectContentPolicySubsystem.h"
#include "EFProjectAssetPathResolver.h"
#include "Engine/World.h"
#include "EFCharacterCreationSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "BrainComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/ProjectTargetingFixComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/GameInstance.h"
#include "Engine/SCS_Node.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectEmoteMenuDataAsset.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectEmoteComponent, Log, All);

namespace ProjectEmoteComponentPrivate
{
	static constexpr TCHAR ACFUArmorSlotComponentClassPath[] = TEXT("/Script/InventorySystem.ACFArmorSlotComponent");
	static const FName RootActionsNodeId(TEXT("Root.Actions"));
	static const FName RootObjectsNodeId(TEXT("Root.Objects"));
	static const FName RootSocialNodeId(TEXT("Root.Social"));
	static const FName RootSpecialNodeId(TEXT("Root.Special"));
	static const FName RootCancelNodeId(TEXT("Root.Cancel"));
	static const FName ActionsBasicNodeId(TEXT("Actions.Basic"));
	static const FName ActionsEmotesNodeId(TEXT("Actions.Emotes"));
	static const FName ActionsCombatNodeId(TEXT("Actions.Combat"));
	static const FName ActionsPartnerNodeId(TEXT("Actions.Partner"));
	static const FName ActionsObjectsNodeId(TEXT("Actions.Objects"));
	static const FName ActionsSpecialNodeId(TEXT("Actions.Special"));
	static const FName ActionsTrainingNodeId(TEXT("Actions.Training"));
	static const FName PrivateSoloFolderNodeId(TEXT("Intimacy.Solo"));
	static const FName PrivateSoloPresentationNodeId(TEXT("Intimacy.Solo.Private01"));
	static const FName TogetherFolderNodeId(TEXT("Actions.Together"));
	static const FName TogetherScene0001NodeId(TEXT("Actions.Together.0001Scene"));
	static constexpr TCHAR DefaultMenuDataAssetPath[] = TEXT("/Game/_Game/Emote/DA_ProjectEmoteMenu.DA_ProjectEmoteMenu");
	static constexpr TCHAR DefaultTogetherSceneBlueprintPath[] = TEXT("/Script/Engine.Blueprint'/Game/_Game/Animations/Intimacy/Scenes/BP_IntimacyScene_0001.BP_IntimacyScene_0001'");
	static constexpr TCHAR BlueprintSceneSessionPeakCueToken[] = TEXT("MilkySplash");
	static constexpr TCHAR EquipmentIdentityTokens[][16] =
	{
		TEXT("Weapon"),
		TEXT("ItemActor"),
		TEXT("Equipment"),
		TEXT("Sword"),
		TEXT("BowActor"),
		TEXT("Shield"),
		TEXT("Quiver"),
		TEXT("ArrowActor")
	};
	// Project-standard permanently authored RootOffset values for these shipped action nodes.
	// They seed fallback/menu generation only; DA_ProjectEmoteMenu remains manually editable.
	static constexpr float StandardPrivateSoloRootOffsetZ = 10.0f;
	static constexpr float StandardTogetherScene0001RootOffsetZ = -70.0f;

	static bool IsPrivateSoloPresentation(const FProjectEmoteInteractionDefinition& Definition)
	{
		return Definition.InteractionId == PrivateSoloPresentationNodeId
			|| (Definition.LegacyEmoteType == EProjectEmoteType::Sit
				&& Definition.Animation.ToSoftObjectPath().ToString().Contains(
					TEXT("/Animations/Intimacy/Female/"),
					ESearchCase::IgnoreCase));
	}

	static bool IsIntimacySessionPresentation(const FProjectEmoteInteractionDefinition& Definition)
	{
		return Definition.InteractionId == TogetherScene0001NodeId
			|| Definition.InteractionId.ToString().StartsWith(
				TEXT("Actions.Together."),
				ESearchCase::IgnoreCase);
	}

	static bool IsOptionalMaturePresentation(const FProjectEmoteInteractionDefinition& Definition)
	{
		return IsPrivateSoloPresentation(Definition)
			|| IsIntimacySessionPresentation(Definition);
	}

	static FName MakeTrainingNodeId(const FName TrainingId)
	{
		return TrainingId.IsNone()
			? NAME_None
			: FName(*FString::Printf(TEXT("Actions.Training.%s"), *TrainingId.ToString()));
	}

	static bool ContainsEquipmentIdentityToken(const FString& Value)
	{
		for (const TCHAR* Token : EquipmentIdentityTokens)
		{
			if (Value.Contains(Token, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	static bool IsLikelyAttachedEquipmentActor(const AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsA<AController>() || Actor->IsA<ACharacter>())
		{
			return false;
		}

		const FString ActorIdentity = FString::Printf(TEXT("%s %s"), *Actor->GetName(), *Actor->GetClass()->GetPathName());
		if (ContainsEquipmentIdentityToken(ActorIdentity))
		{
			return true;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			FString AssetPath;
			if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
			{
				AssetPath = GetPathNameSafe(SkeletalMeshComponent->GetSkeletalMeshAsset());
			}
			else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
			{
				AssetPath = GetPathNameSafe(StaticMeshComponent->GetStaticMesh());
			}

			if (AssetPath.Contains(TEXT("/Weapons/"), ESearchCase::IgnoreCase)
				|| AssetPath.Contains(TEXT("/Items/"), ESearchCase::IgnoreCase)
				|| AssetPath.Contains(TEXT("/Equipment/"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	static FName ResolveAttributeLeafName(const FGameplayTag& AttributeTag)
	{
		if (!AttributeTag.IsValid())
		{
			return TEXT("Training");
		}

		FString TagString = AttributeTag.ToString();
		FString Left;
		FString Right;
		while (TagString.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			TagString = Right;
			break;
		}

		return TagString.IsEmpty() ? FName(TEXT("Training")) : FName(*TagString);
	}

	static FProjectEmoteRootOffsetSettings MakeStandardRootOffset(const float LocalZ)
	{
		FProjectEmoteRootOffsetSettings RootOffset;
		RootOffset.bApplyRootOffset = true;
		RootOffset.LocalActorOffset = FVector(0.0f, 0.0f, LocalZ);
		return RootOffset;
	}

	struct FSetHudEnabledParams
	{
		bool bEnabled = false;
	};

	static void ApplyPresentationDefaults(
		FProjectEmoteMenuNodeDefinition& Node,
		const FText& Description,
		const FName IconId,
		const FName Attribute = TEXT("None"),
		const int32 RequiredExtraNpcCount = 0)
	{
		if (Node.Description.IsEmpty())
		{
			Node.Description = Description;
		}

		if (Node.VisualIconId.IsNone())
		{
			Node.VisualIconId = IconId;
		}

		if (Node.VisualAttribute.IsNone() || Node.VisualAttribute == TEXT("None"))
		{
			Node.VisualAttribute = Attribute;
		}

		if (Node.RequiredExtraNpcCount <= 0 && RequiredExtraNpcCount > 0)
		{
			Node.RequiredExtraNpcCount = RequiredExtraNpcCount;
		}

		Node.RequiredExtraNpcCount = FMath::Clamp(Node.RequiredExtraNpcCount, 0, 3);
	}

	static void ApplyDefaultMenuVisualMetadata(FProjectEmoteMenuNodeDefinition& Node)
	{
		const FName NodeId = Node.NodeId;
		const FString NodeIdString = NodeId.ToString();

		if (NodeId == RootActionsNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "RootActionsDescription", "Open action categories."), TEXT("Actions"));
			return;
		}

		if (NodeId == RootObjectsNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "RootObjectsDescription", "Interact with inventory."), TEXT("Objects"));
			return;
		}

		if (NodeId == RootSocialNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "RootSocialDescription", "Social interactions and reactions."), TEXT("Social"), TEXT("Charisma"));
			return;
		}

		if (NodeId == RootSpecialNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "RootSpecialDescription", "Unique and situational actions."), TEXT("Special"), TEXT("Cunning"));
			return;
		}

		if (NodeId == RootCancelNodeId || NodeId == TEXT("Root.Cancel"))
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "RootCancelDescription", "Close the interaction menu."), TEXT("Cancel"));
			return;
		}

		if (NodeId == ActionsBasicNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsBasicDescription", "Standard actions and general animations."), TEXT("Basic"));
			return;
		}

		if (NodeId == ActionsEmotesNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsEmotesDescription", "Express yourself through different gestures."), TEXT("Emotes"), TEXT("Charisma"));
			return;
		}

		if (NodeId == ActionsCombatNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsCombatDescription", "Combat actions and battle stances."), TEXT("Combat"), TEXT("Offensive"));
			return;
		}

		if (NodeId == ActionsPartnerNodeId)
		{
			// The migrated DataAsset still carries the retired Allure metadata.
			// Charisma is the authoritative runtime gate and themed visual attribute.
			Node.VisualAttribute = TEXT("Charisma");
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsPartnerDescription", "Actions that involve another character."), TEXT("Partner"), TEXT("Charisma"), 1);
			return;
		}

		if (NodeId == ActionsObjectsNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsObjectsDescription", "Interact with objects and the environment."), TEXT("Objects"));
			return;
		}

		if (NodeId == ActionsSpecialNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsSpecialDescription", "Unique and situational actions."), TEXT("Special"), TEXT("Cunning"));
			return;
		}

		if (NodeId == ActionsTrainingNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "ActionsTrainingDescription", "Train ACF primary attributes."), TEXT("Training"), TEXT("Strength"));
			return;
		}

		if (NodeId == TEXT("Actions.Dance"))
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "DanceDescription", "Move with rhythmic flair."), TEXT("Emotes"), TEXT("Charisma"));
			return;
		}

		if (NodeId == PrivateSoloFolderNodeId || NodeId == PrivateSoloPresentationNodeId)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "PrivatePoseDescription", "Optional private presentation."), TEXT("Private"), TEXT("Charisma"));
			return;
		}

		if (NodeId == TogetherScene0001NodeId)
		{
			// Keep the stable interaction ID and scene asset contract while presenting
			// the product-facing name consistently, including data-asset-authored nodes.
			Node.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "TogetherScene0001NodeLabel", "Intimacy");
			Node.VisualAttribute = TEXT("Charisma");
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "IntimacySceneDescription", "Start an Intimacy session with the selected partner."), TEXT("Partner"), TEXT("Charisma"), 1);
			return;
		}

		if (NodeId == TogetherFolderNodeId)
		{
			Node.VisualAttribute = TEXT("Charisma");
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "PartnerSceneDescription", "Synchronized partner scene."), TEXT("Partner"), TEXT("Charisma"), 1);
			return;
		}

		if (NodeId == TEXT("Objects.LookingBack"))
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "LookingBackDescription", "Look back over your shoulder."), TEXT("Objects"), TEXT("Cunning"));
			return;
		}

		if (Node.BlueprintScene.bUseBlueprintScene && Node.BlueprintScene.bRequireCurrentTarget)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "SceneActionDescription", "Requires a nearby participant."), TEXT("Partner"), TEXT("Charisma"), 1);
			return;
		}

		if (Node.NodeType == EProjectEmoteMenuNodeType::Folder)
		{
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "GenericFolderDescription", "Open this category."), TEXT("Special"));
			return;
		}

		if (Node.NodeType == EProjectEmoteMenuNodeType::Action)
		{
			const FName Attribute = NodeIdString.Contains(TEXT("Faith")) ? FName(TEXT("Faith"))
				: NodeIdString.Contains(TEXT("Combat")) ? FName(TEXT("Offensive"))
				: NodeIdString.Contains(TEXT("Celerity")) ? FName(TEXT("Celerity"))
				: NodeIdString.Contains(TEXT("Charisma")) ? FName(TEXT("Charisma"))
				: FName(TEXT("None"));
			ApplyPresentationDefaults(Node, NSLOCTEXT("ProjectEmoteComponent", "GenericActionDescription", "Play this animation."), TEXT("Default"), Attribute);
			return;
		}

		ApplyPresentationDefaults(Node, FText::GetEmpty(), TEXT("Default"));
	}

	static UClass* ResolveClassByPath(const TCHAR* ClassPath)
	{
		if (ClassPath == nullptr || *ClassPath == TEXT('\0'))
		{
			return nullptr;
		}

		if (UClass* ExistingClass = FindObject<UClass>(nullptr, ClassPath))
		{
			return ExistingClass;
		}

		return LoadObject<UClass>(nullptr, ClassPath);
	}

	static FString NormalizeRoleToken(const FString& Value)
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

	static bool ComponentLooksLikeRole(const USkeletalMeshComponent* Component, const FName ComponentNameHint, const FName RoleName)
	{
		if (!Component || RoleName.IsNone())
		{
			return false;
		}

		const FString NormalizedRole = NormalizeRoleToken(RoleName.ToString());
		if (!ComponentNameHint.IsNone() && NormalizeRoleToken(ComponentNameHint.ToString()).Contains(NormalizedRole))
		{
			return true;
		}

		if (NormalizeRoleToken(Component->GetName()).Contains(NormalizedRole))
		{
			return true;
		}

		if (const USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset())
		{
			return NormalizeRoleToken(Mesh->GetPathName()).Contains(NormalizedRole);
		}

		return false;
	}

	static int32 ScoreRuntimeMeshForRole(const USkeletalMeshComponent* RuntimeMesh, const FProjectEmoteBlueprintSceneRoleDefinition& Role)
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

		if (NormalizeRoleToken(RuntimeMesh->GetName()).Contains(NormalizeRoleToken(Role.RoleName.ToString())))
		{
			Score += 25;
		}

		return Score;
	}

	static bool ContainsSessionPeakCueToken(const FString& Value)
	{
		return Value.Contains(BlueprintSceneSessionPeakCueToken, ESearchCase::IgnoreCase);
	}

	static bool IsBlueprintSceneSessionPeakCueNiagara(const UNiagaraComponent* NiagaraComponent)
	{
		if (!NiagaraComponent)
		{
			return false;
		}

		if (ContainsSessionPeakCueToken(NiagaraComponent->GetName())
			|| ContainsSessionPeakCueToken(NiagaraComponent->GetFName().ToString()))
		{
			return true;
		}

		if (const UNiagaraSystem* NiagaraAsset = NiagaraComponent->GetAsset())
		{
			return ContainsSessionPeakCueToken(NiagaraAsset->GetName())
				|| ContainsSessionPeakCueToken(NiagaraAsset->GetPathName());
		}

		return false;
	}

	static void SetBlueprintSceneSessionPeakCueHidden(UNiagaraComponent* NiagaraComponent)
	{
		if (!NiagaraComponent)
		{
			return;
		}

		NiagaraComponent->DeactivateImmediate();
		NiagaraComponent->SetVisibility(false, true);
		NiagaraComponent->SetHiddenInGame(true, true);
		NiagaraComponent->SetComponentTickEnabled(false);
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

	static void AddComparableTokens(const FString& RawValue, TSet<FString>& OutTokens)
	{
		if (RawValue.IsEmpty())
		{
			return;
		}

		FString Normalized = RawValue;
		Normalized.TrimStartAndEndInline();
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));

		const int32 FirstQuoteIndex = Normalized.Find(TEXT("'"), ESearchCase::CaseSensitive);
		if (FirstQuoteIndex != INDEX_NONE)
		{
			Normalized = Normalized.Mid(FirstQuoteIndex + 1);
			const int32 LastQuoteIndex = Normalized.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastQuoteIndex != INDEX_NONE)
			{
				Normalized = Normalized.Left(LastQuoteIndex);
			}
		}

		const int32 GameIndex = Normalized.Find(TEXT("/Game/"), ESearchCase::IgnoreCase);
		if (GameIndex != INDEX_NONE)
		{
			Normalized = Normalized.Mid(GameIndex);
		}
		else
		{
			const int32 LastSpaceIndex = Normalized.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastSpaceIndex != INDEX_NONE)
			{
				Normalized = Normalized.Mid(LastSpaceIndex + 1);
			}
		}

		Normalized = Normalized.ToLower();
		Normalized.RemoveFromEnd(TEXT("'"));

		if (Normalized.EndsWith(TEXT("_c")))
		{
			const FString WithoutClassSuffix = Normalized.LeftChop(2);
			OutTokens.Add(WithoutClassSuffix);
			Normalized = WithoutClassSuffix;
		}

		OutTokens.Add(Normalized);

		int32 DotIndex = INDEX_NONE;
		if (Normalized.FindLastChar(TEXT('.'), DotIndex))
		{
			const FString PackageName = Normalized.Left(DotIndex);
			FString ObjectName = Normalized.Mid(DotIndex + 1);
			if (ObjectName.EndsWith(TEXT("_c")))
			{
				ObjectName.LeftChopInline(2, EAllowShrinking::No);
			}

			OutTokens.Add(PackageName);
			OutTokens.Add(ObjectName);

			const FString PackageBaseName = FPaths::GetBaseFilename(PackageName).ToLower();
			if (!PackageBaseName.IsEmpty())
			{
				OutTokens.Add(PackageBaseName);
			}
		}

		const FString BaseName = FPaths::GetBaseFilename(Normalized).ToLower();
		if (!BaseName.IsEmpty())
		{
			OutTokens.Add(BaseName);
		}
	}

	static bool TokensMatchRequirement(const TSet<FString>& CandidateTokens, const FProjectEmoteEquipmentRequirement& Requirement)
	{
		bool bHasMatchers = false;

		for (const FSoftObjectPath& RequiredAssetPath : Requirement.AcceptedAssetPaths)
		{
			if (!RequiredAssetPath.IsValid())
			{
				continue;
			}

			bHasMatchers = true;
			TSet<FString> RequiredTokens;
			AddComparableTokens(RequiredAssetPath.ToString(), RequiredTokens);
			for (const FString& RequiredToken : RequiredTokens)
			{
				if (CandidateTokens.Contains(RequiredToken))
				{
					return true;
				}
			}
		}

		for (const FString& RequiredHint : Requirement.AcceptedNameHints)
		{
			if (RequiredHint.IsEmpty())
			{
				continue;
			}

			bHasMatchers = true;
			TSet<FString> RequiredTokens;
			AddComparableTokens(RequiredHint, RequiredTokens);
			for (const FString& CandidateToken : CandidateTokens)
			{
				for (const FString& RequiredToken : RequiredTokens)
				{
					if (!RequiredToken.IsEmpty() && CandidateToken.Contains(RequiredToken))
					{
						return true;
					}
				}
			}
		}

		return !bHasMatchers;
	}

	static bool IsLikelyArmorPropertyName(const FString& PropertyName)
	{
		const FString LowerName = PropertyName.ToLower();
		return LowerName.Contains(TEXT("armor"))
			|| LowerName.Contains(TEXT("item"))
			|| LowerName.Contains(TEXT("equip"))
			|| LowerName.Contains(TEXT("cloth"))
			|| LowerName.Contains(TEXT("skinned"))
			|| LowerName.Contains(TEXT("wear"));
	}
}

UProjectEmoteComponent::UProjectEmoteComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	InitializeDefaultInteractionCatalog();
}

void UProjectEmoteComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeDefaultInteractionCatalog();
	ResolveDependencies();
	BindDamageCancellationSources();
}

void UProjectEmoteComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopEmote(false);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);
	}
	UnbindDamageCancellationSources();
	Super::EndPlay(EndPlayReason);
}

void UProjectEmoteComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshBlueprintSceneEquipmentSuppression();
	UpdateFreeCamera(DeltaTime);
}

bool UProjectEmoteComponent::StartEmote(const EProjectEmoteType EmoteType)
{
	if (EmoteType == EProjectEmoteType::None)
	{
		return false;
	}

	const FProjectEmoteInteractionDefinition* Definition = FindInteractionByLegacyType(EmoteType);
	return Definition != nullptr ? StartInteraction(*Definition) : false;
}

bool UProjectEmoteComponent::StartInteractionById(const FName InteractionId)
{
	const FProjectEmoteInteractionDefinition* Definition = FindInteractionById(InteractionId);
	return Definition != nullptr ? StartInteraction(*Definition) : false;
}

bool UProjectEmoteComponent::StartRuntimeInteractionById(const FName InteractionId)
{
	const FProjectEmoteInteractionDefinition* Definition = FindInteractionById(InteractionId);
	return Definition != nullptr ? StartInteraction(*Definition, true) : false;
}

bool UProjectEmoteComponent::StartMatureDefeatPresentationById(const FName InteractionId)
{
	const FProjectEmoteInteractionDefinition* Definition = FindInteractionById(InteractionId);
	if (!Definition
		|| Definition->InteractionId != ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId)
	{
		return false;
	}

	return StartInteraction(*Definition, true, true);
}

bool UProjectEmoteComponent::IsMatureDefeatPresentationAvailableById(
	const FName InteractionId) const
{
	const FProjectEmoteInteractionDefinition* Definition = FindInteractionById(InteractionId);
	if (!Definition
		|| Definition->InteractionId != ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId
		|| !IsInteractionCurrentlyAvailable(*Definition, true))
	{
		return false;
	}

	if (Definition->BlueprintScene.bUseBlueprintScene)
	{
		return !Definition->BlueprintScene.SceneBlueprint.IsNull()
			&& IsValid(Definition->BlueprintScene.SceneBlueprint.LoadSynchronous());
	}

	return !Definition->Animation.IsNull()
		&& IsValid(Definition->Animation.LoadSynchronous());
}

void UProjectEmoteComponent::StopEmote(const bool bRestoreTargetActor)
{
	const TWeakObjectPtr<AActor> TargetActorToRestoreAfterStop = bRestoreTargetActor
		? TargetingActorToRestore
		: TWeakObjectPtr<AActor>();
	TargetingActorToRestore.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredEmoteStartTimerHandle);
		World->GetTimerManager().ClearTimer(DelayedPostEmoteRecoveryTimerHandle);
		World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);
	}

	const bool bHadActiveState = ActiveInteractionId != NAME_None
		|| bEmoteTransitionPending
		|| ActiveOverlayMontage != nullptr
		|| ActiveTargetOverlayMontage != nullptr
		|| ActiveBlueprintSceneVisualActor != nullptr;
	bEmoteTransitionPending = false;
	StopActiveMontage(LoopBlendOutTime);
	RestoreBlueprintSceneState();
	RestoreRootOffset();
	StopFreeCamera(ResolveOwningPlayerController());
	RestoreMinimalAnimSceneLock();
	RestoreHudSuppression();
	RestorePreEmoteState();
	RestoreLocomotionOverride();

	ActiveInteractionId = NAME_None;
	ActiveInteractionDefinition = FProjectEmoteInteractionDefinition();
	ActivePlaybackMode = EProjectEmotePlaybackMode::Looping;
	ActiveAnimation.Reset();
	ActiveBlueprintSceneDefinition = FProjectEmoteBlueprintSceneDefinition();
	ActiveEmote = EProjectEmoteType::None;
#if WITH_EDITOR
	DebugBlueprintSceneTargetActor.Reset();
#endif
	if (AActor* TargetActor = TargetActorToRestoreAfterStop.Get())
	{
		if (ACharacter* Character = CachedCharacterOwner.Get())
		{
			if (UProjectTargetingFixComponent* TargetingFixComponent = Character->FindComponentByClass<UProjectTargetingFixComponent>())
			{
				TargetingFixComponent->RestoreCurrentTargetActor(TargetActor);
			}
		}
	}

	if (bHadActiveState)
	{
		ScheduleDelayedPostEmoteRecovery();
		PlayFadeFromBlack();
	}
}

void UProjectEmoteComponent::OverrideDelayedPostEmoteRecovery(const float DelaySeconds, const bool bMoveInputIgnored, const bool bLookInputIgnored)
{
	ScheduleDelayedPostEmoteRecovery(DelaySeconds, bMoveInputIgnored, bLookInputIgnored);
}

void UProjectEmoteComponent::GetMenuInteractions(const EProjectEmoteMenuCategory Category, TArray<FProjectEmoteInteractionDefinition>& OutInteractions) const
{
	OutInteractions.Reset();

	switch (Category)
	{
	case EProjectEmoteMenuCategory::Actions:
		CollectActionDescendants(ProjectEmoteComponentPrivate::RootActionsNodeId, OutInteractions);
		break;
	case EProjectEmoteMenuCategory::Objects:
		CollectActionDescendants(ProjectEmoteComponentPrivate::RootObjectsNodeId, OutInteractions);
		break;
	case EProjectEmoteMenuCategory::Root:
	default:
		return;
	}

	OutInteractions.StableSort([](const FProjectEmoteInteractionDefinition& Left, const FProjectEmoteInteractionDefinition& Right)
	{
		return Left.SortOrder == Right.SortOrder
			? Left.InteractionId.LexicalLess(Right.InteractionId)
			: Left.SortOrder < Right.SortOrder;
	});
}

bool UProjectEmoteComponent::FindInteractionDefinition(const FName InteractionId, FProjectEmoteInteractionDefinition& OutDefinition) const
{
	if (const FProjectEmoteInteractionDefinition* Definition = FindInteractionById(InteractionId))
	{
		OutDefinition = *Definition;
		return true;
	}

	return false;
}

void UProjectEmoteComponent::GetRootMenuNodes(TArray<FProjectEmoteMenuNodeDefinition>& OutNodes) const
{
	GetChildMenuNodes(NAME_None, OutNodes);
}

void UProjectEmoteComponent::GetChildMenuNodes(const FName ParentNodeId, TArray<FProjectEmoteMenuNodeDefinition>& OutNodes) const
{
	OutNodes.Reset();

	for (const FProjectEmoteMenuNodeDefinition& Node : CachedMenuNodes)
	{
		if (Node.ParentNodeId != ParentNodeId || !IsMenuNodeCurrentlyAvailable(Node))
		{
			continue;
		}

		OutNodes.Add(Node);
	}

	OutNodes.StableSort([](const FProjectEmoteMenuNodeDefinition& Left, const FProjectEmoteMenuNodeDefinition& Right)
	{
		return Left.SortOrder == Right.SortOrder
			? Left.NodeId.LexicalLess(Right.NodeId)
			: Left.SortOrder < Right.SortOrder;
	});
}

bool UProjectEmoteComponent::FindMenuNodeDefinition(const FName NodeId, FProjectEmoteMenuNodeDefinition& OutNode) const
{
	if (const FProjectEmoteMenuNodeDefinition* Node = FindMenuNodeById(NodeId))
	{
		OutNode = *Node;
		return true;
	}

	return false;
}

bool UProjectEmoteComponent::FindActiveInteractionDefinition(FProjectEmoteInteractionDefinition& OutDefinition) const
{
	if (ActiveInteractionId.IsNone())
	{
		return false;
	}

	OutDefinition = ActiveInteractionDefinition;
	if (ProjectEmoteComponentPrivate::IsOptionalMaturePresentation(OutDefinition))
	{
		// Mature presentation is cosmetic-only. Sanitize old serialized action
		// effects so it cannot grant progression or alter gameplay state.
		OutDefinition.Effects = FProjectEmoteActionEffects();
	}
	return true;
}

bool UProjectEmoteComponent::IsEmoteActive() const
{
	return ActiveInteractionId != NAME_None;
}

bool UProjectEmoteComponent::IsEmoteTransitionPending() const
{
	return bEmoteTransitionPending;
}

bool UProjectEmoteComponent::IsEmotePlaybackStarted() const
{
	return IsEmoteActive() && !bEmoteTransitionPending && (ActiveOverlayMontage != nullptr || ActiveTargetOverlayMontage != nullptr);
}

bool UProjectEmoteComponent::IsActiveInteractionBlueprintScene() const
{
	return ActiveInteractionId != NAME_None && ActiveInteractionDefinition.BlueprintScene.bUseBlueprintScene;
}

AActor* UProjectEmoteComponent::GetActiveBlueprintSceneVisualActor() const
{
	return ActiveBlueprintSceneVisualActor;
}

AActor* UProjectEmoteComponent::GetActiveBlueprintSceneTargetActor() const
{
	return TargetParticipantState.Actor.Get();
}

AActor* UProjectEmoteComponent::GetCurrentInteractionTargetActor() const
{
	if (AActor* ActiveTargetActor = GetActiveBlueprintSceneTargetActor())
	{
		return ActiveTargetActor;
	}
	return ResolveCurrentTargetActor();
}

bool UProjectEmoteComponent::TriggerBlueprintSceneVisualSessionPeakCue()
{
	AActor* VisualActor = ActiveBlueprintSceneVisualActor;
	if (!VisualActor)
	{
		return false;
	}

	bool bTriggered = false;
	TArray<UNiagaraComponent*> NiagaraComponents;
	VisualActor->GetComponents(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		if (!ProjectEmoteComponentPrivate::IsBlueprintSceneSessionPeakCueNiagara(NiagaraComponent))
		{
			continue;
		}

		NiagaraComponent->SetVisibility(true, true);
		NiagaraComponent->SetHiddenInGame(false, true);
		NiagaraComponent->SetComponentTickEnabled(true);
		NiagaraComponent->Activate(true);
		bTriggered = true;
		UE_LOG(
			LogProjectEmoteComponent,
			Log,
			TEXT("[ProjectEmote] Triggered blueprint scene session-peak cue %s asset=%s actor=%s."),
			*NiagaraComponent->GetName(),
			*GetNameSafe(NiagaraComponent->GetAsset()),
			*GetNameSafe(VisualActor));
	}

	return bTriggered;
}

#if WITH_DEV_AUTOMATION_TESTS
void UProjectEmoteComponent::AutomationApplyBlueprintSceneEquipmentSuppressionForTest(AActor* PlayerActor, AActor* PartnerActor)
{
	ApplyBlueprintSceneEquipmentSuppression(PlayerActor, PartnerActor);
}

void UProjectEmoteComponent::AutomationRestoreBlueprintSceneEquipmentSuppressionForTest()
{
	RestoreBlueprintSceneEquipmentSuppression();
}
#endif

int32 UProjectEmoteComponent::AutomationGetSuppressedBlueprintSceneEquipmentCount() const
{
	return BlueprintSceneEquipmentSnapshots.Num();
}

#if WITH_EDITOR
void UProjectEmoteComponent::SetDebugBlueprintSceneTargetActor(AActor* TargetActor)
{
	DebugBlueprintSceneTargetActor = TargetActor;
}
#endif

EProjectEmoteType UProjectEmoteComponent::GetActiveEmote() const
{
	return ActiveEmote;
}

int32 UProjectEmoteComponent::GetActiveEmoteValue() const
{
	return static_cast<int32>(ActiveEmote);
}

FName UProjectEmoteComponent::GetActiveInteractionId() const
{
	return ActiveInteractionId;
}

bool UProjectEmoteComponent::IsCombatLockoutActive(const float LockoutSeconds) const
{
	if (LockoutSeconds <= 0.0f || LastCombatImpactTimeSeconds <= -FLT_MAX * 0.5f)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	return (Now - LastCombatImpactTimeSeconds) <= LockoutSeconds;
}

void UProjectEmoteComponent::SetFreeCameraForwardInput(const float Value)
{
	FreeCameraForwardInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UProjectEmoteComponent::SetFreeCameraRightInput(const float Value)
{
	FreeCameraRightInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UProjectEmoteComponent::SetFreeCameraVerticalInput(const float Value)
{
	FreeCameraVerticalInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UProjectEmoteComponent::SetFreeCameraBoostActive(const bool bActive)
{
	bFreeCameraBoostActive = bActive;
}

void UProjectEmoteComponent::ClearFreeCameraMoveInput()
{
	FreeCameraForwardInput = 0.0f;
	FreeCameraRightInput = 0.0f;
	FreeCameraVerticalInput = 0.0f;
	bFreeCameraBoostActive = false;
}

void UProjectEmoteComponent::RefreshMenuCatalog()
{
	InitializeDefaultInteractionCatalog();
}

void UProjectEmoteComponent::TryLoadDefaultMenuDataAsset()
{
	if (MenuDataAsset)
	{
		return;
	}

	if (UProjectEmoteMenuDataAsset* DefaultMenuDataAsset = LoadObject<UProjectEmoteMenuDataAsset>(nullptr, ProjectEmoteComponentPrivate::DefaultMenuDataAssetPath))
	{
		MenuDataAsset = DefaultMenuDataAsset;
	}
}

void UProjectEmoteComponent::ApplyMenuDataAssetRuntimeSettings()
{
	if (!MenuDataAsset)
	{
		return;
	}

	const FProjectEmoteSceneRuntimeSettings& RuntimeSettings = MenuDataAsset->RuntimeSettings;
	if (!RuntimeSettings.OverlaySlotName.IsNone())
	{
		OverlaySlotName = RuntimeSettings.OverlaySlotName;
	}

	LoopBlendInTime = FMath::Max(0.0f, RuntimeSettings.LoopBlendInTime);
	LoopBlendOutTime = FMath::Max(0.0f, RuntimeSettings.LoopBlendOutTime);
	FadeToBlackDuration = FMath::Max(0.0f, RuntimeSettings.FadeToBlackDuration);
	BlackScreenHoldDuration = FMath::Max(0.0f, RuntimeSettings.BlackScreenHoldDuration);
	FadeFromBlackDuration = FMath::Max(0.0f, RuntimeSettings.FadeFromBlackDuration);
	PostEmoteRecoveryDelaySeconds = FMath::Max(0.0f, RuntimeSettings.PostEmoteRecoveryDelaySeconds);
	CombatMenuLockoutSeconds = FMath::Max(0.0f, RuntimeSettings.CombatMenuLockoutSeconds);
	bSuppressHudDuringEmote = RuntimeSettings.bSuppressHudDuringEmote;
	bApplyPreActionSettle = RuntimeSettings.bApplyPreActionSettle;
	bApplyMinimalAnimSceneLock = RuntimeSettings.bApplyMinimalAnimSceneLock;

	bUseFreeCameraDuringEmote = RuntimeSettings.FreeCamera.bUseFreeCameraDuringEmote;
	FreeCameraBlendTime = FMath::Max(0.0f, RuntimeSettings.FreeCamera.BlendTime);
	FreeCameraMoveSpeed = FMath::Max(1.0f, RuntimeSettings.FreeCamera.MoveSpeed);
	FreeCameraBoostMoveSpeed = FMath::Max(1.0f, RuntimeSettings.FreeCamera.BoostMoveSpeed);
	FreeCameraMouseSensitivity = FMath::Max(0.001f, RuntimeSettings.FreeCamera.MouseSensitivity);
	FreeCameraMinPitch = FMath::Clamp(RuntimeSettings.FreeCamera.MinPitch, -89.0f, 0.0f);
	FreeCameraMaxPitch = FMath::Clamp(RuntimeSettings.FreeCamera.MaxPitch, 0.0f, 89.0f);
}

void UProjectEmoteComponent::InitializeDefaultInteractionCatalog()
{
	TryLoadDefaultMenuDataAsset();
	ApplyMenuDataAssetRuntimeSettings();

	if (MenuDataAsset && !MenuDataAsset->Nodes.IsEmpty())
	{
		bUsingGeneratedDefaultCatalog = false;
	}

	if ((!MenuDataAsset || MenuDataAsset->Nodes.IsEmpty()) && ActionInteractions.IsEmpty() && ObjectInteractions.IsEmpty())
	{
		BuildDefaultInteractionCatalog();
		bUsingGeneratedDefaultCatalog = true;
	}

	RebuildMenuNodeCache();
}

void UProjectEmoteComponent::BuildDefaultInteractionCatalog()
{
	ActionInteractions.Reset();
	ObjectInteractions.Reset();

	FProjectEmoteInteractionDefinition DanceInteraction;
	DanceInteraction.InteractionId = TEXT("Actions.Dance");
	DanceInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "DanceInteractionLabel", "Dance");
	DanceInteraction.MenuCategory = EProjectEmoteMenuCategory::Actions;
	DanceInteraction.PlaybackMode = EProjectEmotePlaybackMode::Looping;
	DanceInteraction.LegacyEmoteType = EProjectEmoteType::Dance;
	DanceInteraction.SortOrder = 0;
	DanceInteraction.Animation = TSoftObjectPtr<UAnimationAsset>(FEFProjectAssetPathResolver::BuildExportedAnimationPath(TEXT("Anim_KA_Idle63_Dance05_Loop")));
	ActionInteractions.Add(DanceInteraction);

	FProjectEmoteInteractionDefinition PrivateSoloInteraction;
	PrivateSoloInteraction.InteractionId = ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId;
	PrivateSoloInteraction.SourceNodeId = ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId;
	PrivateSoloInteraction.ParentNodeId = ProjectEmoteComponentPrivate::PrivateSoloFolderNodeId;
	PrivateSoloInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "PrivateSoloInteractionLabel", "Fap 1");
	PrivateSoloInteraction.MenuCategory = EProjectEmoteMenuCategory::Actions;
	PrivateSoloInteraction.PlaybackMode = EProjectEmotePlaybackMode::Looping;
	PrivateSoloInteraction.LegacyEmoteType = EProjectEmoteType::Sit;
	PrivateSoloInteraction.SortOrder = 0;
	PrivateSoloInteraction.Animation = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Script/Engine.AnimSequence'/Game/_Game/Animations/Intimacy/Female/P_INT_Solo_Private01.P_INT_Solo_Private01'")));
	PrivateSoloInteraction.RootOffset = ProjectEmoteComponentPrivate::MakeStandardRootOffset(ProjectEmoteComponentPrivate::StandardPrivateSoloRootOffsetZ);
	ActionInteractions.Add(PrivateSoloInteraction);

	FProjectEmoteInteractionDefinition TogetherSceneInteraction;
	TogetherSceneInteraction.InteractionId = ProjectEmoteComponentPrivate::TogetherScene0001NodeId;
	TogetherSceneInteraction.SourceNodeId = ProjectEmoteComponentPrivate::TogetherScene0001NodeId;
	TogetherSceneInteraction.ParentNodeId = ProjectEmoteComponentPrivate::TogetherFolderNodeId;
	TogetherSceneInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "TogetherScene0001InteractionLabel", "Intimacy");
	TogetherSceneInteraction.MenuCategory = EProjectEmoteMenuCategory::Actions;
	TogetherSceneInteraction.PlaybackMode = EProjectEmotePlaybackMode::Looping;
	TogetherSceneInteraction.LegacyEmoteType = EProjectEmoteType::None;
	TogetherSceneInteraction.SortOrder = 0;
	TogetherSceneInteraction.BlueprintScene.bUseBlueprintScene = true;
	TogetherSceneInteraction.BlueprintScene.SceneBlueprint = TSoftObjectPtr<UBlueprint>(FSoftObjectPath(ProjectEmoteComponentPrivate::DefaultTogetherSceneBlueprintPath));
	TogetherSceneInteraction.BlueprintScene.PrimaryRoleName = TEXT("Female");
	TogetherSceneInteraction.BlueprintScene.TargetRoleName = TEXT("Male");
	TogetherSceneInteraction.BlueprintScene.bRequireCurrentTarget = true;
	TogetherSceneInteraction.BlueprintScene.bPlaceTargetNearPrimaryOnEnd = true;
	TogetherSceneInteraction.BlueprintScene.TargetEndLocalOffset = FVector(100.0f, 0.0f, 0.0f);
	TogetherSceneInteraction.BlueprintScene.bTargetFacesPrimaryOnEnd = true;
	TogetherSceneInteraction.RootOffset = ProjectEmoteComponentPrivate::MakeStandardRootOffset(ProjectEmoteComponentPrivate::StandardTogetherScene0001RootOffsetZ);
	ActionInteractions.Add(TogetherSceneInteraction);

	FProjectEmoteInteractionDefinition LookingBackInteraction;
	LookingBackInteraction.InteractionId = TEXT("Objects.LookingBack");
	LookingBackInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "LookingBackInteractionLabel", "Looking Back");
	LookingBackInteraction.MenuCategory = EProjectEmoteMenuCategory::Objects;
	LookingBackInteraction.PlaybackMode = EProjectEmotePlaybackMode::PlayOnce;
	LookingBackInteraction.LegacyEmoteType = EProjectEmoteType::LookingBack;
	LookingBackInteraction.SortOrder = 0;
	LookingBackInteraction.Animation = TSoftObjectPtr<UAnimationAsset>(FEFProjectAssetPathResolver::BuildExportedAnimationPath(TEXT("Anim_KA_Idle11_LookingBack")));
	ObjectInteractions.Add(LookingBackInteraction);
}

bool UProjectEmoteComponent::IsGeneratedDefaultInteractionCatalog() const
{
	return ActionInteractions.Num() == 3
		&& ObjectInteractions.Num() == 1
		&& ActionInteractions[0].InteractionId == TEXT("Actions.Dance")
		&& ActionInteractions[1].InteractionId == ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId
		&& ActionInteractions[2].InteractionId == ProjectEmoteComponentPrivate::TogetherScene0001NodeId
		&& ObjectInteractions[0].InteractionId == TEXT("Objects.LookingBack");
}

void UProjectEmoteComponent::RebuildMenuNodeCache()
{
	CachedMenuNodes.Reset();
	CachedMenuNodeIndexById.Reset();

	if (MenuDataAsset && !MenuDataAsset->Nodes.IsEmpty())
	{
		for (const FProjectEmoteMenuNodeDefinition& Node : MenuDataAsset->Nodes)
		{
			AddMenuNode(Node);
		}
	}
	else if (bUsingGeneratedDefaultCatalog && IsGeneratedDefaultInteractionCatalog())
	{
		BuildDefaultMenuNodes();
	}
	else
	{
		BuildMenuNodesFromLegacyInteractions();
	}

	AppendTrainingMenuNodes();

	ActionInteractions.Reset();
	ObjectInteractions.Reset();

	TArray<FProjectEmoteMenuNodeDefinition> SortedNodes = CachedMenuNodes;
	SortedNodes.StableSort([](const FProjectEmoteMenuNodeDefinition& Left, const FProjectEmoteMenuNodeDefinition& Right)
	{
		return Left.SortOrder == Right.SortOrder
			? Left.NodeId.LexicalLess(Right.NodeId)
			: Left.SortOrder < Right.SortOrder;
	});

	for (const FProjectEmoteMenuNodeDefinition& Node : SortedNodes)
	{
		if (Node.NodeType != EProjectEmoteMenuNodeType::Action)
		{
			continue;
		}

		FProjectEmoteInteractionDefinition Interaction;
		if (!ConvertMenuNodeToInteraction(Node, Interaction))
		{
			continue;
		}

		if (Interaction.MenuCategory == EProjectEmoteMenuCategory::Objects)
		{
			ObjectInteractions.Add(Interaction);
		}
		else
		{
			ActionInteractions.Add(Interaction);
		}
	}
}

void UProjectEmoteComponent::BuildDefaultMenuNodes()
{
	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "ActionsFolderLabel", "Actions"),
		0);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootObjectsNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "ObjectsFolderLabel", "Objects"),
		10);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootSocialNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "SocialFolderLabel", "Social"),
		20);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootSpecialNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "SpecialFolderLabel", "Special"),
		30);

	AddCancelMenuNode(
		ProjectEmoteComponentPrivate::RootCancelNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "CancelLabel", "Cancel"),
		100);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsBasicNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "BasicFolderLabel", "Basic"),
		0);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsEmotesNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "EmotesFolderLabel", "Emotes"),
		10);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsCombatNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "CombatFolderLabel", "Combat"),
		20);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsPartnerNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "PartnerFolderLabel", "Partner"),
		30);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsObjectsNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "ActionObjectsFolderLabel", "Objects"),
		40);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsSpecialNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "ActionSpecialFolderLabel", "Special"),
		50);

	FProjectEmoteInteractionDefinition DanceInteraction;
	DanceInteraction.InteractionId = TEXT("Actions.Dance");
	DanceInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "DanceInteractionLabel", "Dance");
	DanceInteraction.MenuCategory = EProjectEmoteMenuCategory::Actions;
	DanceInteraction.PlaybackMode = EProjectEmotePlaybackMode::Looping;
	DanceInteraction.LegacyEmoteType = EProjectEmoteType::Dance;
	DanceInteraction.SortOrder = 0;
	DanceInteraction.Animation = TSoftObjectPtr<UAnimationAsset>(FEFProjectAssetPathResolver::BuildExportedAnimationPath(TEXT("Anim_KA_Idle63_Dance05_Loop")));
	AddActionMenuNodeFromInteraction(DanceInteraction, ProjectEmoteComponentPrivate::ActionsEmotesNodeId);

	FProjectEmoteInteractionDefinition PrivateSoloInteraction;
	PrivateSoloInteraction.InteractionId = ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId;
	PrivateSoloInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "PrivateSoloInteractionLabel", "Fap 1");
	PrivateSoloInteraction.MenuCategory = EProjectEmoteMenuCategory::Actions;
	PrivateSoloInteraction.PlaybackMode = EProjectEmotePlaybackMode::Looping;
	PrivateSoloInteraction.LegacyEmoteType = EProjectEmoteType::Sit;
	PrivateSoloInteraction.SortOrder = 0;
	PrivateSoloInteraction.Animation = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(TEXT("/Script/Engine.AnimSequence'/Game/_Game/Animations/Intimacy/Female/P_INT_Solo_Private01.P_INT_Solo_Private01'")));
	PrivateSoloInteraction.RootOffset = ProjectEmoteComponentPrivate::MakeStandardRootOffset(ProjectEmoteComponentPrivate::StandardPrivateSoloRootOffsetZ);
	AddActionMenuNodeFromInteraction(
		PrivateSoloInteraction,
		ProjectEmoteComponentPrivate::ActionsBasicNodeId,
		ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "PrivateSoloNodeLabel", "Fap 1"),
		0);

	AddBlueprintSceneMenuNode(
		ProjectEmoteComponentPrivate::TogetherScene0001NodeId,
		ProjectEmoteComponentPrivate::ActionsPartnerNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "TogetherScene0001NodeLabel", "Intimacy"),
		FSoftObjectPath(ProjectEmoteComponentPrivate::DefaultTogetherSceneBlueprintPath),
		0,
		ProjectEmoteComponentPrivate::MakeStandardRootOffset(ProjectEmoteComponentPrivate::StandardTogetherScene0001RootOffsetZ));

	FProjectEmoteInteractionDefinition LookingBackInteraction;
	LookingBackInteraction.InteractionId = TEXT("Objects.LookingBack");
	LookingBackInteraction.DisplayName = NSLOCTEXT("ProjectEmoteComponent", "LookingBackInteractionLabel", "Looking Back");
	LookingBackInteraction.MenuCategory = EProjectEmoteMenuCategory::Objects;
	LookingBackInteraction.PlaybackMode = EProjectEmotePlaybackMode::PlayOnce;
	LookingBackInteraction.LegacyEmoteType = EProjectEmoteType::LookingBack;
	LookingBackInteraction.SortOrder = 0;
	LookingBackInteraction.Animation = TSoftObjectPtr<UAnimationAsset>(FEFProjectAssetPathResolver::BuildExportedAnimationPath(TEXT("Anim_KA_Idle11_LookingBack")));
	AddActionMenuNodeFromInteraction(LookingBackInteraction, ProjectEmoteComponentPrivate::RootObjectsNodeId);
}

void UProjectEmoteComponent::BuildMenuNodesFromLegacyInteractions()
{
	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "ActionsFolderLabel", "Actions"),
		0);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootObjectsNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "ObjectsFolderLabel", "Objects"),
		10);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootSocialNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "SocialFolderLabel", "Social"),
		20);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::RootSpecialNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "SpecialFolderLabel", "Special"),
		30);

	AddCancelMenuNode(
		ProjectEmoteComponentPrivate::RootCancelNodeId,
		NAME_None,
		NSLOCTEXT("ProjectEmoteComponent", "CancelLabel", "Cancel"),
		100);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsBasicNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "BasicFolderLabel", "Basic"),
		0);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsEmotesNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "EmotesFolderLabel", "Emotes"),
		10);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsCombatNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "CombatFolderLabel", "Combat"),
		20);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsPartnerNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "PartnerFolderLabel", "Partner"),
		30);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsObjectsNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "ActionObjectsFolderLabel", "Objects"),
		40);

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsSpecialNodeId,
		ProjectEmoteComponentPrivate::RootActionsNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "ActionSpecialFolderLabel", "Special"),
		50);

	for (const FProjectEmoteInteractionDefinition& Interaction : ActionInteractions)
	{
		AddActionMenuNodeFromInteraction(Interaction, ProjectEmoteComponentPrivate::ActionsBasicNodeId);
	}

	for (const FProjectEmoteInteractionDefinition& Interaction : ObjectInteractions)
	{
		AddActionMenuNodeFromInteraction(Interaction, ProjectEmoteComponentPrivate::RootObjectsNodeId);
	}
}

void UProjectEmoteComponent::AppendTrainingMenuNodes()
{
	TArray<FACFTrainingDefinition> TrainingDefinitions;
	if (const UACFTrainingComponent* TrainingComponent = GetOwner() ? GetOwner()->FindComponentByClass<UACFTrainingComponent>() : nullptr)
	{
		TrainingDefinitions = TrainingComponent->GetAvailableTrainingDefinitions();
	}
	else if (const UACFTrainingSettings* TrainingSettings = GetDefault<UACFTrainingSettings>())
	{
		TrainingDefinitions = TrainingSettings->GetTrainingDefinitions();
	}
	else
	{
		TrainingDefinitions = UACFTrainingSettings::MakeDefaultTrainingDefinitions();
	}

	if (TrainingDefinitions.IsEmpty())
	{
		return;
	}

	AddFolderMenuNode(
		ProjectEmoteComponentPrivate::ActionsTrainingNodeId,
		ProjectEmoteComponentPrivate::RootSpecialNodeId,
		NSLOCTEXT("ProjectEmoteComponent", "TrainingFolderLabel", "Training"),
		0);

	int32 SortOffset = 0;
	for (const FACFTrainingDefinition& Definition : TrainingDefinitions)
	{
		if (Definition.TrainingId.IsNone())
		{
			continue;
		}

		const FName NodeId = ProjectEmoteComponentPrivate::MakeTrainingNodeId(Definition.TrainingId);
		if (NodeId.IsNone())
		{
			continue;
		}

		const FName AttributeName = ProjectEmoteComponentPrivate::ResolveAttributeLeafName(Definition.TargetPrimaryAttribute);
		const FText DisplayName = Definition.DisplayName.IsEmpty()
			? FText::FromName(Definition.TrainingId)
			: Definition.DisplayName;

		FProjectEmoteMenuNodeDefinition Node;
		Node.NodeId = NodeId;
		Node.ParentNodeId = ProjectEmoteComponentPrivate::ActionsTrainingNodeId;
		Node.DisplayName = DisplayName;
		Node.Description = Definition.Description.IsEmpty()
			? FText::Format(NSLOCTEXT("ProjectEmoteComponent", "TrainingDefaultDescription", "+{0} {1}"), FText::AsNumber(Definition.SuccessReward), FText::FromName(AttributeName))
			: Definition.Description;
		Node.NodeType = EProjectEmoteMenuNodeType::Action;
		Node.VisualIconId = TEXT("Training");
		Node.VisualAttribute = AttributeName;
		Node.Animation = Definition.TrainingAnimation;
		Node.PlaybackMode = EProjectEmotePlaybackMode::Looping;
		Node.LegacyEmoteType = EProjectEmoteType::None;
		Node.LegacyMenuCategory = EProjectEmoteMenuCategory::Actions;
		Node.SortOrder = SortOffset;
		AddMenuNode(Node);

		SortOffset += 10;
	}
}

void UProjectEmoteComponent::AddMenuNode(const FProjectEmoteMenuNodeDefinition& Node)
{
	if (Node.NodeId.IsNone() || CachedMenuNodeIndexById.Contains(Node.NodeId))
	{
		return;
	}

	FProjectEmoteMenuNodeDefinition ResolvedNode = Node;
	ProjectEmoteComponentPrivate::ApplyDefaultMenuVisualMetadata(ResolvedNode);

	CachedMenuNodeIndexById.Add(ResolvedNode.NodeId, CachedMenuNodes.Num());
	CachedMenuNodes.Add(ResolvedNode);
}

void UProjectEmoteComponent::AddFolderMenuNode(const FName NodeId, const FName ParentNodeId, const FText& DisplayName, const int32 SortOrder)
{
	FProjectEmoteMenuNodeDefinition Node;
	Node.NodeId = NodeId;
	Node.ParentNodeId = ParentNodeId;
	Node.DisplayName = DisplayName;
	Node.NodeType = EProjectEmoteMenuNodeType::Folder;
	Node.SortOrder = SortOrder;
	AddMenuNode(Node);
}

void UProjectEmoteComponent::AddCancelMenuNode(const FName NodeId, const FName ParentNodeId, const FText& DisplayName, const int32 SortOrder)
{
	FProjectEmoteMenuNodeDefinition Node;
	Node.NodeId = NodeId;
	Node.ParentNodeId = ParentNodeId;
	Node.DisplayName = DisplayName;
	Node.NodeType = EProjectEmoteMenuNodeType::Cancel;
	Node.SortOrder = SortOrder;
	AddMenuNode(Node);
}

void UProjectEmoteComponent::AddActionMenuNodeFromInteraction(
	const FProjectEmoteInteractionDefinition& Interaction,
	const FName ParentNodeId,
	const FName OverrideNodeId,
	FText OverrideDisplayName,
	const int32 OverrideSortOrder)
{
	FProjectEmoteMenuNodeDefinition Node;
	Node.NodeId = OverrideNodeId.IsNone() ? Interaction.InteractionId : OverrideNodeId;
	Node.ParentNodeId = ParentNodeId;
	Node.DisplayName = OverrideDisplayName.IsEmpty() ? Interaction.DisplayName : OverrideDisplayName;
	Node.NodeType = EProjectEmoteMenuNodeType::Action;
	Node.Animation = Interaction.Animation;
	Node.PlaybackMode = Interaction.PlaybackMode;
	Node.LegacyEmoteType = Interaction.LegacyEmoteType;
	Node.LegacyMenuCategory = Interaction.MenuCategory;
	Node.SortOrder = OverrideSortOrder == INDEX_NONE ? Interaction.SortOrder : OverrideSortOrder;
	Node.EquipmentRequirements = Interaction.EquipmentRequirements;
	Node.Effects = Interaction.Effects;
	Node.RootOffset = Interaction.RootOffset;
	Node.AdditionalParticipantRootOffsets = Interaction.AdditionalParticipantRootOffsets;
	Node.BlueprintScene = Interaction.BlueprintScene;
	AddMenuNode(Node);
}

void UProjectEmoteComponent::AddBlueprintSceneMenuNode(const FName NodeId, const FName ParentNodeId, const FText& DisplayName, const FSoftObjectPath& BlueprintPath, const int32 SortOrder, const FProjectEmoteRootOffsetSettings& RootOffset)
{
	FProjectEmoteMenuNodeDefinition Node;
	Node.NodeId = NodeId;
	Node.ParentNodeId = ParentNodeId;
	Node.DisplayName = DisplayName;
	Node.NodeType = EProjectEmoteMenuNodeType::Action;
	Node.PlaybackMode = EProjectEmotePlaybackMode::Looping;
	Node.LegacyEmoteType = EProjectEmoteType::None;
	Node.LegacyMenuCategory = EProjectEmoteMenuCategory::Actions;
	Node.SortOrder = SortOrder;
	Node.BlueprintScene.bUseBlueprintScene = true;
	Node.BlueprintScene.SceneBlueprint = TSoftObjectPtr<UBlueprint>(BlueprintPath);
	Node.BlueprintScene.PrimaryRoleName = TEXT("Female");
	Node.BlueprintScene.TargetRoleName = TEXT("Male");
	Node.BlueprintScene.bRequireCurrentTarget = true;
	Node.BlueprintScene.bPlaceTargetNearPrimaryOnEnd = true;
	Node.BlueprintScene.TargetEndLocalOffset = FVector(100.0f, 0.0f, 0.0f);
	Node.BlueprintScene.bTargetFacesPrimaryOnEnd = true;
	Node.RootOffset = RootOffset;
	AddMenuNode(Node);
}

bool UProjectEmoteComponent::ConvertMenuNodeToInteraction(const FProjectEmoteMenuNodeDefinition& Node, FProjectEmoteInteractionDefinition& OutDefinition) const
{
	if (Node.NodeType != EProjectEmoteMenuNodeType::Action || Node.NodeId.IsNone())
	{
		return false;
	}

	OutDefinition = FProjectEmoteInteractionDefinition();
	OutDefinition.InteractionId = Node.NodeId;
	OutDefinition.SourceNodeId = Node.NodeId;
	OutDefinition.ParentNodeId = Node.ParentNodeId;
	OutDefinition.DisplayName = Node.DisplayName;
	OutDefinition.MenuCategory = Node.LegacyMenuCategory;
	OutDefinition.PlaybackMode = Node.PlaybackMode;
	OutDefinition.LegacyEmoteType = Node.LegacyEmoteType;
	OutDefinition.Animation = Node.Animation;
	OutDefinition.EquipmentRequirements = Node.EquipmentRequirements;
	OutDefinition.SortOrder = Node.SortOrder;
	OutDefinition.Effects = Node.Effects;
	OutDefinition.RootOffset = Node.RootOffset;
	OutDefinition.AdditionalParticipantRootOffsets = Node.AdditionalParticipantRootOffsets;
	OutDefinition.BlueprintScene = Node.BlueprintScene;
	return true;
}

const FProjectEmoteMenuNodeDefinition* UProjectEmoteComponent::FindMenuNodeById(const FName NodeId) const
{
	if (const int32* Index = CachedMenuNodeIndexById.Find(NodeId))
	{
		return CachedMenuNodes.IsValidIndex(*Index) ? &CachedMenuNodes[*Index] : nullptr;
	}

	return nullptr;
}

const FProjectEmoteMenuNodeDefinition* UProjectEmoteComponent::FindFirstAvailableActionDescendant(const FName ParentNodeId) const
{
	TSet<FName> VisitedNodeIds;

	TFunction<const FProjectEmoteMenuNodeDefinition*(FName)> FindAvailableAction;
	FindAvailableAction = [this, &VisitedNodeIds, &FindAvailableAction](const FName CurrentParentNodeId) -> const FProjectEmoteMenuNodeDefinition*
	{
		if (VisitedNodeIds.Contains(CurrentParentNodeId))
		{
			return nullptr;
		}
		VisitedNodeIds.Add(CurrentParentNodeId);

		TArray<FProjectEmoteMenuNodeDefinition> Children;
		GetChildMenuNodes(CurrentParentNodeId, Children);
		for (const FProjectEmoteMenuNodeDefinition& Child : Children)
		{
			if (Child.NodeType == EProjectEmoteMenuNodeType::Action)
			{
				return FindMenuNodeById(Child.NodeId);
			}

			if (Child.NodeType == EProjectEmoteMenuNodeType::Folder)
			{
				if (const FProjectEmoteMenuNodeDefinition* Descendant = FindAvailableAction(Child.NodeId))
				{
					return Descendant;
				}
			}
		}

		return nullptr;
	};

	return FindAvailableAction(ParentNodeId);
}

void UProjectEmoteComponent::CollectActionDescendants(const FName ParentNodeId, TArray<FProjectEmoteInteractionDefinition>& OutInteractions) const
{
	TSet<FName> VisitedNodeIds;

	TFunction<void(FName)> Collect;
	Collect = [this, &VisitedNodeIds, &Collect, &OutInteractions](const FName CurrentParentNodeId)
	{
		if (VisitedNodeIds.Contains(CurrentParentNodeId))
		{
			return;
		}
		VisitedNodeIds.Add(CurrentParentNodeId);

		TArray<FProjectEmoteMenuNodeDefinition> Children;
		GetChildMenuNodes(CurrentParentNodeId, Children);
		for (const FProjectEmoteMenuNodeDefinition& Child : Children)
		{
			if (Child.NodeType == EProjectEmoteMenuNodeType::Folder)
			{
				Collect(Child.NodeId);
				continue;
			}

			if (Child.NodeType != EProjectEmoteMenuNodeType::Action)
			{
				continue;
			}

			FProjectEmoteInteractionDefinition Interaction;
			if (ConvertMenuNodeToInteraction(Child, Interaction))
			{
				OutInteractions.Add(Interaction);
			}
		}
	};

	Collect(ParentNodeId);
}

bool UProjectEmoteComponent::IsMenuNodeCurrentlyAvailable(const FProjectEmoteMenuNodeDefinition& Node) const
{
	if (Node.NodeType != EProjectEmoteMenuNodeType::Action)
	{
		return true;
	}

	FProjectEmoteInteractionDefinition Interaction;
	return ConvertMenuNodeToInteraction(Node, Interaction) && IsInteractionCurrentlyAvailable(Interaction);
}

bool UProjectEmoteComponent::StartInteraction(
	const FProjectEmoteInteractionDefinition& Definition,
	const bool bBypassCombatLockout,
	const bool bMatureDefeatPresentation)
{
	if (Definition.InteractionId.IsNone()
		|| (!Definition.BlueprintScene.bUseBlueprintScene && Definition.Animation.IsNull())
		|| !IsInteractionCurrentlyAvailable(Definition, bMatureDefeatPresentation))
	{
		return false;
	}

	if (!bBypassCombatLockout && IsCombatLockoutActive(CombatMenuLockoutSeconds))
	{
		UE_LOG(LogProjectEmoteComponent, Log, TEXT("[ProjectEmote] Interaction %s blocked by recent combat."), *Definition.InteractionId.ToString());
		return false;
	}

	ResolveDependencies();
	BindDamageCancellationSources();
	ACharacter* Character = CachedCharacterOwner.Get();
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!Character || !CharacterMovementComponent || !SkeletalMeshComponent || !PlayerController)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Missing runtime dependencies for %s"), *GetNameSafe(GetOwner()));
		return false;
	}

	FProjectEmoteBlueprintSceneDefinition PreparedBlueprintSceneDefinition;
	AActor* PreparedBlueprintSceneTargetActor = nullptr;
	USkeletalMeshComponent* PreparedBlueprintSceneTargetMesh = nullptr;
	if (Definition.BlueprintScene.bUseBlueprintScene)
	{
		if (!PrepareBlueprintSceneInteraction(Definition, PreparedBlueprintSceneDefinition, PreparedBlueprintSceneTargetActor, PreparedBlueprintSceneTargetMesh))
		{
			return false;
		}
	}

	if (UProjectTargetingFixComponent* TargetingFixComponent = Character->FindComponentByClass<UProjectTargetingFixComponent>())
	{
		TargetingFixComponent->DeactivateCurrentTargetingLock();
	}

	StopEmote();
	TargetingActorToRestore = PreparedBlueprintSceneTargetActor;
	ResolveDependencies();
	Character = CachedCharacterOwner.Get();
	PlayerController = ResolveOwningPlayerController();
	if (!Character || !CharacterMovementComponent || !SkeletalMeshComponent || !PlayerController)
	{
		return false;
	}

	CachedMovementMode = CharacterMovementComponent->MovementMode;
	CachedCustomMovementMode = CharacterMovementComponent->CustomMovementMode;
	bCachedControllerMoveInputIgnored = PlayerController->IsMoveInputIgnored();
	bCachedControllerLookInputIgnored = PlayerController->IsLookInputIgnored();
	bAppliedMoveInputIgnore = false;
	bForcedLookInputEnable = false;
	bAppliedLookInputIgnore = false;
	bPawnInputSuspended = false;
	bPreEmoteStateCached = true;
	bRootOffsetApplied = false;
	bCachedControlRotation = false;
	bAppliedNeutralControlRotation = false;

	SuspendLocomotionOverride();

	Character->StopJumping();
	CharacterMovementComponent->StopMovementImmediately();
	CharacterMovementComponent->DisableMovement();
	if (!bCachedControllerMoveInputIgnored)
	{
		PlayerController->SetIgnoreMoveInput(true);
		bAppliedMoveInputIgnore = true;
	}

	if (!bCachedControllerLookInputIgnored)
	{
		PlayerController->SetIgnoreLookInput(true);
		bAppliedLookInputIgnore = true;
	}

	Character->DisableInput(PlayerController);
	bPawnInputSuspended = true;
	if (Definition.BlueprintScene.bUseBlueprintScene)
	{
		bBlueprintScenePlayerTransformCached = true;
		CachedBlueprintScenePlayerTransform = Character->GetActorTransform();
	}
	ApplyRootOffset(Definition);

	if (Definition.BlueprintScene.bUseBlueprintScene)
	{
		bBlueprintSceneActive = true;
		ActiveBlueprintSceneDefinition = PreparedBlueprintSceneDefinition;
		CacheAndFreezeTargetParticipant(TargetParticipantState, PreparedBlueprintSceneTargetActor, PreparedBlueprintSceneTargetMesh);
		ApplyBlueprintSceneEquipmentSuppression(Character, PreparedBlueprintSceneTargetActor);

		const FTransform SceneAnchorTransform = Character->GetActorTransform();
		ApplyBlueprintSceneParticipantTransform(Character, SkeletalMeshComponent, PreparedBlueprintSceneDefinition.PrimaryRole, SceneAnchorTransform);
		ApplyBlueprintSceneParticipantTransform(PreparedBlueprintSceneTargetActor, PreparedBlueprintSceneTargetMesh, PreparedBlueprintSceneDefinition.TargetRole, SceneAnchorTransform);
		ActiveBlueprintSceneVisualActor = SpawnBlueprintSceneVisualActor(PreparedBlueprintSceneDefinition, SceneAnchorTransform);
	}

	ActiveInteractionId = Definition.InteractionId;
	ActiveInteractionDefinition = Definition;
	if (ProjectEmoteComponentPrivate::IsOptionalMaturePresentation(Definition))
	{
		ActiveInteractionDefinition.Effects = FProjectEmoteActionEffects();
	}
	ActivePlaybackMode = Definition.PlaybackMode;
	ActiveAnimation = Definition.Animation;
	ActiveEmote = Definition.LegacyEmoteType;
	bEmoteTransitionPending = true;
	ApplyHudSuppression(PlayerController);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredEmoteStartTimerHandle);
		World->GetTimerManager().SetTimer(
			DeferredEmoteStartTimerHandle,
			this,
			&ThisClass::HandleDeferredEmotePlaybackStart,
			FMath::Max(0.0f, FadeToBlackDuration + BlackScreenHoldDuration),
			false);
	}

	PlayFadeToBlack();
	StartFreeCamera(PlayerController);
	ApplyPreActionSettle(PlayerController);
	if (Definition.BlueprintScene.bUseBlueprintScene)
	{
		ApplyAnimSceneLockForActor(PreparedBlueprintSceneTargetActor, PreparedBlueprintSceneTargetMesh);
	}
	return true;
}

const FProjectEmoteInteractionDefinition* UProjectEmoteComponent::FindInteractionById(const FName InteractionId) const
{
	const FName ResolvedInteractionId = InteractionId == ProjectEmoteComponentPrivate::PrivateSoloFolderNodeId
		? ProjectEmoteComponentPrivate::PrivateSoloPresentationNodeId
		: (InteractionId == ProjectEmoteComponentPrivate::TogetherFolderNodeId
			? ProjectEmoteComponentPrivate::TogetherScene0001NodeId
			: InteractionId);

	for (const FProjectEmoteInteractionDefinition& Interaction : ActionInteractions)
	{
		if (Interaction.InteractionId == ResolvedInteractionId)
		{
			return &Interaction;
		}
	}

	for (const FProjectEmoteInteractionDefinition& Interaction : ObjectInteractions)
	{
		if (Interaction.InteractionId == ResolvedInteractionId)
		{
			return &Interaction;
		}
	}

	if (const FProjectEmoteMenuNodeDefinition* Node = FindMenuNodeById(ResolvedInteractionId))
	{
		if (Node->NodeType == EProjectEmoteMenuNodeType::Action)
		{
			if (ConvertMenuNodeToInteraction(*Node, InteractionLookupScratch))
			{
				return &InteractionLookupScratch;
			}
		}

		if (Node->NodeType == EProjectEmoteMenuNodeType::Folder)
		{
			if (const FProjectEmoteMenuNodeDefinition* Descendant = FindFirstAvailableActionDescendant(Node->NodeId))
			{
				if (ConvertMenuNodeToInteraction(*Descendant, InteractionLookupScratch))
				{
					return &InteractionLookupScratch;
				}
			}
		}
	}

	return nullptr;
}

const FProjectEmoteInteractionDefinition* UProjectEmoteComponent::FindInteractionByLegacyType(const EProjectEmoteType EmoteType) const
{
	for (const FProjectEmoteInteractionDefinition& Interaction : ActionInteractions)
	{
		if (Interaction.LegacyEmoteType == EmoteType)
		{
			return &Interaction;
		}
	}

	for (const FProjectEmoteInteractionDefinition& Interaction : ObjectInteractions)
	{
		if (Interaction.LegacyEmoteType == EmoteType)
		{
			return &Interaction;
		}
	}

	return nullptr;
}

bool UProjectEmoteComponent::IsInteractionCurrentlyAvailable(
	const FProjectEmoteInteractionDefinition& Definition,
	const bool bMatureDefeatPresentation) const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UProjectContentPolicySubsystem* ContentPolicy = GameInstance
		? GameInstance->GetSubsystem<UProjectContentPolicySubsystem>()
		: nullptr;
	const bool bVoluntaryAdultInteraction =
		(ProjectEmoteComponentPrivate::IsPrivateSoloPresentation(Definition)
			&& !bMatureDefeatPresentation)
		|| ProjectEmoteComponentPrivate::IsIntimacySessionPresentation(Definition);
	if (bVoluntaryAdultInteraction
		&& !UProjectIntimacySubsystem::HasRequiredCharismaForActor(GetOwner()))
	{
		return false;
	}

	if (ProjectEmoteComponentPrivate::IsPrivateSoloPresentation(Definition)
		&& (!ContentPolicy
			|| (bMatureDefeatPresentation
				? !ContentPolicy->IsMatureDefeatAllowed()
				: !ContentPolicy->IsPrivateSoloPresentationAllowed())))
	{
		return false;
	}

	if (ProjectEmoteComponentPrivate::IsIntimacySessionPresentation(Definition)
		&& (!ContentPolicy || !ContentPolicy->IsIntimacyAllowed()))
	{
		return false;
	}

	if (ProjectEmoteComponentPrivate::IsIntimacySessionPresentation(Definition))
	{
		const UWorld* World = GetWorld();
		const UProjectIntimacySubsystem* IntimacySubsystem =
			World ? World->GetSubsystem<UProjectIntimacySubsystem>() : nullptr;
		AActor* PartnerActor = ResolveCurrentTargetActor();
		FText FailureReason;
		if (!IntimacySubsystem
			|| !PartnerActor
			|| !IntimacySubsystem->CanRequestIntimacyWithPartner(PartnerActor, FailureReason))
		{
			return false;
		}
	}

	if (Definition.BlueprintScene.bUseBlueprintScene && Definition.BlueprintScene.bRequireCurrentTarget && !ResolveCurrentTargetActor())
	{
		return false;
	}

	if (Definition.MenuCategory != EProjectEmoteMenuCategory::Objects || Definition.EquipmentRequirements.IsEmpty())
	{
		return true;
	}

	for (const FProjectEmoteEquipmentRequirement& Requirement : Definition.EquipmentRequirements)
	{
		if (!DoesRequirementMatchAnyArmorSlot(Requirement))
		{
			return false;
		}
	}

	return true;
}

AActor* UProjectEmoteComponent::ResolveCurrentTargetActor() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

#if WITH_EDITOR
	if (AActor* DebugTargetActor = DebugBlueprintSceneTargetActor.Get())
	{
		if (IsValid(DebugTargetActor) && DebugTargetActor != OwnerActor)
		{
			return DebugTargetActor;
		}
	}
#endif

	if (const UProjectTargetingFixComponent* TargetingFixComponent = OwnerActor->FindComponentByClass<UProjectTargetingFixComponent>())
	{
		return TargetingFixComponent->GetCurrentTargetActor();
	}

	return nullptr;
}

bool UProjectEmoteComponent::DoesRequirementMatchAnyArmorSlot(const FProjectEmoteEquipmentRequirement& Requirement) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	TInlineComponentArray<UActorComponent*> Components;
	OwnerActor->GetComponents(Components);

	for (const UActorComponent* Component : Components)
	{
		if (!IsArmorSlotComponent(Component))
		{
			continue;
		}

		if (DoesSlotComponentMatchRequirement(Component, Requirement))
		{
			return true;
		}
	}

	return false;
}

bool UProjectEmoteComponent::IsArmorSlotComponent(const UActorComponent* Component) const
{
	if (!IsValid(Component))
	{
		return false;
	}

	if (UClass* ArmorSlotClass = ProjectEmoteComponentPrivate::ResolveClassByPath(ProjectEmoteComponentPrivate::ACFUArmorSlotComponentClassPath))
	{
		return Component->IsA(ArmorSlotClass);
	}

	return Component->GetClass()->GetName().Contains(TEXT("ACFArmorSlotComponent"));
}

bool UProjectEmoteComponent::DoesSlotComponentMatchRequirement(const UActorComponent* ArmorSlotComponent, const FProjectEmoteEquipmentRequirement& Requirement) const
{
	if (!IsValid(ArmorSlotComponent))
	{
		return false;
	}

	if (DoesObjectValueMatchRequirement(ArmorSlotComponent, Requirement))
	{
		return true;
	}

	for (TFieldIterator<FProperty> PropertyIt(ArmorSlotComponent->GetClass(), EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		if (!Property || !ProjectEmoteComponentPrivate::IsLikelyArmorPropertyName(Property->GetName()))
		{
			continue;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			if (UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue_InContainer(ArmorSlotComponent))
			{
				if (DoesObjectValueMatchRequirement(ObjectValue, Requirement))
				{
					return true;
				}
			}
			continue;
		}

		if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			if (const UObject* ClassObjectValue = ClassProperty->GetObjectPropertyValue_InContainer(ArmorSlotComponent))
			{
				if (DoesCandidateValueMatchRequirement(ClassObjectValue->GetPathName(), Requirement)
					|| DoesCandidateValueMatchRequirement(ClassObjectValue->GetName(), Requirement))
				{
					return true;
				}
			}
			continue;
		}

		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObjectValue = SoftObjectProperty->GetPropertyValue_InContainer(ArmorSlotComponent);
			if (DoesCandidateValueMatchRequirement(SoftObjectValue.ToString(), Requirement))
			{
				return true;
			}
			continue;
		}

		if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr SoftClassValue = SoftClassProperty->GetPropertyValue_InContainer(ArmorSlotComponent);
			if (DoesCandidateValueMatchRequirement(SoftClassValue.ToString(), Requirement))
			{
				return true;
			}
			continue;
		}

		if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			if (DoesCandidateValueMatchRequirement(NameProperty->GetPropertyValue_InContainer(ArmorSlotComponent).ToString(), Requirement))
			{
				return true;
			}
			continue;
		}

		if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			if (DoesCandidateValueMatchRequirement(StringProperty->GetPropertyValue_InContainer(ArmorSlotComponent), Requirement))
			{
				return true;
			}
		}
	}

	return false;
}

bool UProjectEmoteComponent::DoesObjectValueMatchRequirement(const UObject* CandidateObject, const FProjectEmoteEquipmentRequirement& Requirement) const
{
	if (!IsValid(CandidateObject))
	{
		return false;
	}

	if (DoesCandidateValueMatchRequirement(CandidateObject->GetPathName(), Requirement)
		|| DoesCandidateValueMatchRequirement(CandidateObject->GetName(), Requirement)
		|| DoesCandidateValueMatchRequirement(CandidateObject->GetClass()->GetPathName(), Requirement)
		|| DoesCandidateValueMatchRequirement(CandidateObject->GetClass()->GetName(), Requirement))
	{
		return true;
	}

	if (const USkeletalMeshComponent* SkeletalMeshComponentValue = Cast<USkeletalMeshComponent>(CandidateObject))
	{
		if (const USkeletalMesh* SkeletalMeshAsset = SkeletalMeshComponentValue->GetSkeletalMeshAsset())
		{
			if (DoesCandidateValueMatchRequirement(SkeletalMeshAsset->GetPathName(), Requirement)
				|| DoesCandidateValueMatchRequirement(SkeletalMeshAsset->GetName(), Requirement))
			{
				return true;
			}
		}
	}

	return false;
}

bool UProjectEmoteComponent::DoesCandidateValueMatchRequirement(const FString& CandidateValue, const FProjectEmoteEquipmentRequirement& Requirement) const
{
	TSet<FString> CandidateTokens;
	ProjectEmoteComponentPrivate::AddComparableTokens(CandidateValue, CandidateTokens);
	return ProjectEmoteComponentPrivate::TokensMatchRequirement(CandidateTokens, Requirement);
}

void UProjectEmoteComponent::ResolveDependencies()
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	CachedCharacterOwner = CharacterOwner;
	CharacterMovementComponent = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
	SkeletalMeshComponent = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
	ActiveAnimInstance = SkeletalMeshComponent ? SkeletalMeshComponent->GetAnimInstance() : nullptr;
}

APlayerController* UProjectEmoteComponent::ResolveOwningPlayerController() const
{
	const ACharacter* CharacterOwner = CachedCharacterOwner.IsValid() ? CachedCharacterOwner.Get() : Cast<ACharacter>(GetOwner());
	return CharacterOwner ? Cast<APlayerController>(CharacterOwner->GetController()) : nullptr;
}

UAnimInstance* UProjectEmoteComponent::ResolveAnimInstance() const
{
	if (ActiveAnimInstance.IsValid())
	{
		return ActiveAnimInstance.Get();
	}

	return SkeletalMeshComponent ? SkeletalMeshComponent->GetAnimInstance() : nullptr;
}

UAnimationAsset* UProjectEmoteComponent::ResolveAnimationForInteraction(const FProjectEmoteInteractionDefinition& Definition)
{
	return LoadAnimationAsset(Definition.Animation);
}

UAnimationAsset* UProjectEmoteComponent::LoadAnimationAsset(const TSoftObjectPtr<UAnimationAsset>& AssetReference)
{
	if (AssetReference.IsNull())
	{
		return nullptr;
	}

	const FSoftObjectPath AssetPath = AssetReference.ToSoftObjectPath();
	if (TObjectPtr<UAnimationAsset>* ExistingAsset = LoadedAnimationAssets.Find(AssetPath))
	{
		return ExistingAsset->Get();
	}

	UAnimationAsset* LoadedAsset = FEFProjectAssetPathResolver::LoadObjectWithLegacyFallback<UAnimationAsset>(AssetPath);
	if (LoadedAsset)
	{
		LoadedAnimationAssets.Add(AssetPath, LoadedAsset);
	}

	return LoadedAsset;
}

void UProjectEmoteComponent::SuspendLocomotionOverride()
{
	bLocomotionStateCached = false;
	bLocomotionWalkWasEnabled = false;
	bLocomotionCrawlWasEnabled = false;
	SuspendedLocomotionOverrideComponent = nullptr;

	UProjectLocomotionOverrideComponent* LocomotionOverride = GetOwner() ? GetOwner()->FindComponentByClass<UProjectLocomotionOverrideComponent>() : nullptr;
	if (!LocomotionOverride)
	{
		return;
	}

	bLocomotionStateCached = true;
	bLocomotionWalkWasEnabled = LocomotionOverride->IsWalkModeEnabled();
	bLocomotionCrawlWasEnabled = LocomotionOverride->IsCrawlModeActive();
	SuspendedLocomotionOverrideComponent = LocomotionOverride;

	if (bLocomotionWalkWasEnabled)
	{
		LocomotionOverride->SetWalkModeEnabled(false);
	}

	if (bLocomotionCrawlWasEnabled)
	{
		LocomotionOverride->SetCrawlModeEnabled(false);
	}
}

void UProjectEmoteComponent::RestoreLocomotionOverride()
{
	if (!bLocomotionStateCached)
	{
		return;
	}

	if (UProjectLocomotionOverrideComponent* LocomotionOverride = SuspendedLocomotionOverrideComponent.Get())
	{
		if (bLocomotionWalkWasEnabled)
		{
			LocomotionOverride->SetWalkModeEnabled(true);
		}

		if (bLocomotionCrawlWasEnabled)
		{
			LocomotionOverride->SetCrawlModeEnabled(true);
		}
	}

	bLocomotionStateCached = false;
	bLocomotionWalkWasEnabled = false;
	bLocomotionCrawlWasEnabled = false;
	SuspendedLocomotionOverrideComponent = nullptr;
}

void UProjectEmoteComponent::BindDamageCancellationSources()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	OwnerActor->OnTakeAnyDamage.AddUniqueDynamic(this, &ThisClass::HandleOwnerAnyDamage);

	UProjectCombatAttributeComponent* CombatAttributeComponent = OwnerActor->FindComponentByClass<UProjectCombatAttributeComponent>();
	if (BoundCombatAttributeComponent == CombatAttributeComponent)
	{
		return;
	}

	UnbindDamageCancellationSources();
	OwnerActor->OnTakeAnyDamage.AddUniqueDynamic(this, &ThisClass::HandleOwnerAnyDamage);

	BoundCombatAttributeComponent = CombatAttributeComponent;
	if (BoundCombatAttributeComponent)
	{
		BoundCombatAttributeComponent->OnDamageApplied.AddUniqueDynamic(this, &ThisClass::HandleProjectDamageApplied);
		BoundCombatAttributeComponent->OnAttributeChanged.AddUniqueDynamic(this, &ThisClass::HandleProjectCombatAttributeChanged);
	}
}

void UProjectEmoteComponent::UnbindDamageCancellationSources()
{
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->OnTakeAnyDamage.RemoveDynamic(this, &ThisClass::HandleOwnerAnyDamage);
	}

	if (BoundCombatAttributeComponent)
	{
		BoundCombatAttributeComponent->OnDamageApplied.RemoveDynamic(this, &ThisClass::HandleProjectDamageApplied);
		BoundCombatAttributeComponent->OnAttributeChanged.RemoveDynamic(this, &ThisClass::HandleProjectCombatAttributeChanged);
		BoundCombatAttributeComponent = nullptr;
	}
}

void UProjectEmoteComponent::RecordCombatImpact()
{
	if (const UWorld* World = GetWorld())
	{
		LastCombatImpactTimeSeconds = World->GetTimeSeconds();
	}
	else
	{
		LastCombatImpactTimeSeconds = 0.0f;
	}
}

void UProjectEmoteComponent::CancelEmoteForDamage(const float AppliedDamage)
{
	if (AppliedDamage <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	RecordCombatImpact();
	if (IsEmoteActive() || IsEmoteTransitionPending())
	{
		StopEmote();
	}
}

void UProjectEmoteComponent::ApplyRootOffset(const FProjectEmoteInteractionDefinition& Definition)
{
	RestoreRootOffset();

	if (!Definition.RootOffset.bApplyRootOffset || Definition.RootOffset.LocalActorOffset.IsNearlyZero())
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	CachedRootOffsetActorTransform = OwnerActor->GetActorTransform();
	const FVector WorldOffset = OwnerActor->GetActorQuat().RotateVector(Definition.RootOffset.LocalActorOffset);
	const FVector TargetLocation = CachedRootOffsetActorTransform.GetLocation() + WorldOffset;
	OwnerActor->SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
	bRootOffsetApplied = true;
}

void UProjectEmoteComponent::RestoreRootOffset()
{
	if (!bRootOffsetApplied)
	{
		return;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->SetActorTransform(CachedRootOffsetActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	CachedRootOffsetActorTransform = FTransform::Identity;
	bRootOffsetApplied = false;
}

void UProjectEmoteComponent::ApplyPreActionSettle(APlayerController* PlayerController)
{
	if (!bApplyPreActionSettle)
	{
		return;
	}

	if (PlayerController && !bCachedControlRotation)
	{
		CachedControlRotation = PlayerController->GetControlRotation();
		bCachedControlRotation = true;
	}

	if (PlayerController)
	{
		const ACharacter* CharacterOwner = CachedCharacterOwner.Get();
		const float NeutralYaw = CharacterOwner ? CharacterOwner->GetActorRotation().Yaw : PlayerController->GetControlRotation().Yaw;
		PlayerController->SetControlRotation(FRotator(0.0f, NeutralYaw, 0.0f));
		bAppliedNeutralControlRotation = true;
	}

	if (bApplyMinimalAnimSceneLock)
	{
		ApplyMinimalAnimSceneLock();
	}

	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->RefreshBoneTransforms();
	}
}

void UProjectEmoteComponent::RestorePreActionSettle(APlayerController* PlayerController)
{
	if (PlayerController && bAppliedNeutralControlRotation && bCachedControlRotation)
	{
		PlayerController->SetControlRotation(CachedControlRotation);
	}

	bAppliedNeutralControlRotation = false;
	bCachedControlRotation = false;
	CachedControlRotation = FRotator::ZeroRotator;
}

void UProjectEmoteComponent::ApplyHudSuppression(APlayerController* PlayerController)
{
	if (!bSuppressHudDuringEmote || bHudSuppressionApplied || !PlayerController)
	{
		return;
	}

	AHUD* HudActor = PlayerController->GetHUD();
	if (!HudActor)
	{
		return;
	}

	SuppressedHudActor = HudActor;
	bHasSavedPlayerHudVisibility = true;
	bWasPlayerHudVisible = HudActor->bShowHUD;
	bAppliedReflectedHudDisable = TrySetReflectedHudEnabled(HudActor, false);
	if (!bAppliedReflectedHudDisable)
	{
		HudActor->bShowHUD = false;
	}

	bHudSuppressionApplied = true;
}

void UProjectEmoteComponent::RestoreHudSuppression()
{
	if (!bHudSuppressionApplied)
	{
		return;
	}

	if (AHUD* HudActor = SuppressedHudActor.Get())
	{
		if (bHasSavedPlayerHudVisibility)
		{
			HudActor->bShowHUD = bWasPlayerHudVisible;
			if (bAppliedReflectedHudDisable)
			{
				TrySetReflectedHudEnabled(HudActor, bWasPlayerHudVisible);
			}
		}
	}

	SuppressedHudActor.Reset();
	bHudSuppressionApplied = false;
	bHasSavedPlayerHudVisibility = false;
	bWasPlayerHudVisible = true;
	bAppliedReflectedHudDisable = false;
}

bool UProjectEmoteComponent::TrySetReflectedHudEnabled(AHUD* HudActor, const bool bEnabled) const
{
	if (!HudActor)
	{
		return false;
	}

	UFunction* Function = HudActor->FindFunction(TEXT("SetHudEnabled"));
	if (!Function)
	{
		return false;
	}

	ProjectEmoteComponentPrivate::FSetHudEnabledParams Params;
	Params.bEnabled = bEnabled;
	HudActor->ProcessEvent(Function, &Params);
	return true;
}

void UProjectEmoteComponent::ApplyAnimSceneLockForActor(AActor* Actor, USkeletalMeshComponent* SourceMeshComponent)
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

	ApplyVisibleMeshLeaderPoseSceneLockForActor(Actor, SourceMeshComponent);
	if (SourceMeshComponent)
	{
		SourceMeshComponent->RefreshBoneTransforms();
	}
}

void UProjectEmoteComponent::ApplyBlueprintSceneEquipmentSuppression(AActor* PlayerActor, AActor* PartnerActor)
{
	RestoreBlueprintSceneEquipmentSuppression();

	const AActor* Participants[] = { PlayerActor, PartnerActor };
	for (const AActor* Participant : Participants)
	{
		if (!IsValid(Participant))
		{
			continue;
		}

		TArray<AActor*> AttachedActors;
		Participant->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			if (!ProjectEmoteComponentPrivate::IsLikelyAttachedEquipmentActor(AttachedActor)
				|| BlueprintSceneEquipmentSnapshots.ContainsByPredicate([AttachedActor](const FProjectEmoteEquipmentVisibilitySnapshot& Snapshot)
				{
					return Snapshot.Actor.Get() == AttachedActor;
				}))
			{
				continue;
			}

			FProjectEmoteEquipmentVisibilitySnapshot Snapshot;
			Snapshot.Actor = AttachedActor;
			Snapshot.bWasHiddenInGame = AttachedActor->IsHidden();
			Snapshot.bWasCollisionEnabled = AttachedActor->GetActorEnableCollision();
			BlueprintSceneEquipmentSnapshots.Add(Snapshot);

			AttachedActor->SetActorHiddenInGame(true);
			AttachedActor->SetActorEnableCollision(false);

			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
			AttachedActor->GetComponents(PrimitiveComponents);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (!PrimitiveComponent)
				{
					continue;
				}

				FProjectEmoteEquipmentPrimitiveVisibilitySnapshot PrimitiveSnapshot;
				PrimitiveSnapshot.Component = PrimitiveComponent;
				PrimitiveSnapshot.bWasVisible = PrimitiveComponent->IsVisible();
				PrimitiveSnapshot.bWasHiddenInGame = PrimitiveComponent->bHiddenInGame;
				BlueprintSceneEquipmentPrimitiveSnapshots.Add(PrimitiveSnapshot);
				PrimitiveComponent->SetVisibility(false, true);
				PrimitiveComponent->SetHiddenInGame(true, true);
			}
		}
	}

	if (!BlueprintSceneEquipmentSnapshots.IsEmpty())
	{
		UE_LOG(
			LogProjectEmoteComponent,
			Log,
			TEXT("[ProjectEmote] Suppressed %d attached equipment actor(s) for blueprint scene playback."),
			BlueprintSceneEquipmentSnapshots.Num());
	}
}

void UProjectEmoteComponent::RefreshBlueprintSceneEquipmentSuppression()
{
	if (BlueprintSceneEquipmentSnapshots.IsEmpty())
	{
		return;
	}

	for (const FProjectEmoteEquipmentVisibilitySnapshot& Snapshot : BlueprintSceneEquipmentSnapshots)
	{
		if (AActor* Actor = Snapshot.Actor.Get())
		{
			Actor->SetActorHiddenInGame(true);
			Actor->SetActorEnableCollision(false);
		}
	}

	for (const FProjectEmoteEquipmentPrimitiveVisibilitySnapshot& Snapshot : BlueprintSceneEquipmentPrimitiveSnapshots)
	{
		if (UPrimitiveComponent* Component = Snapshot.Component.Get())
		{
			Component->SetVisibility(false, true);
			Component->SetHiddenInGame(true, true);
		}
	}
}

void UProjectEmoteComponent::RestoreBlueprintSceneEquipmentSuppression()
{
	for (const FProjectEmoteEquipmentVisibilitySnapshot& Snapshot : BlueprintSceneEquipmentSnapshots)
	{
		if (AActor* Actor = Snapshot.Actor.Get())
		{
			Actor->SetActorHiddenInGame(Snapshot.bWasHiddenInGame);
			Actor->SetActorEnableCollision(Snapshot.bWasCollisionEnabled);
		}
	}
	for (const FProjectEmoteEquipmentPrimitiveVisibilitySnapshot& Snapshot : BlueprintSceneEquipmentPrimitiveSnapshots)
	{
		if (UPrimitiveComponent* Component = Snapshot.Component.Get())
		{
			Component->SetVisibility(Snapshot.bWasVisible, true);
			Component->SetHiddenInGame(Snapshot.bWasHiddenInGame, true);
		}
	}

	BlueprintSceneEquipmentSnapshots.Reset();
	BlueprintSceneEquipmentPrimitiveSnapshots.Reset();
}

void UProjectEmoteComponent::ApplyMinimalAnimSceneLock()
{
	if (bMinimalAnimSceneLockApplied)
	{
		return;
	}

	TArray<UObject*> AnimInstanceObjects;
	if (AActor* OwnerActor = GetOwner())
	{
		TArray<USkeletalMeshComponent*> MeshComponents;
		OwnerActor->GetComponents(MeshComponents);
		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (!MeshComponent)
			{
				continue;
			}

			if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
			{
				AnimInstanceObjects.AddUnique(AnimInstance);
			}

			if (UAnimInstance* PostProcessInstance = MeshComponent->GetPostProcessInstance())
			{
				AnimInstanceObjects.AddUnique(PostProcessInstance);
			}
		}
	}

	if (UObject* PrimaryAnimInstance = ResolveAnimInstance())
	{
		AnimInstanceObjects.AddUnique(PrimaryAnimInstance);
	}

	for (UObject* AnimInstanceObject : AnimInstanceObjects)
	{
		ApplyAnimInstanceSceneLock(AnimInstanceObject);
	}

	ApplyVisibleMeshLeaderPoseSceneLockForActor(GetOwner(), SkeletalMeshComponent);

	bMinimalAnimSceneLockApplied = BoolSceneLockSnapshots.Num() > 0
		|| FloatSceneLockSnapshots.Num() > 0
		|| LeaderPoseSceneLockSnapshots.Num() > 0;
}

void UProjectEmoteComponent::ApplyAnimInstanceSceneLock(UObject* AnimInstanceObject)
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

void UProjectEmoteComponent::ApplyVisibleMeshLeaderPoseSceneLock()
{
	ApplyVisibleMeshLeaderPoseSceneLockForActor(GetOwner(), SkeletalMeshComponent);
}

void UProjectEmoteComponent::ApplyVisibleMeshLeaderPoseSceneLockForActor(AActor* Actor, USkeletalMeshComponent* SourceMeshComponent)
{
	if (!Actor || !SourceMeshComponent)
	{
		return;
	}

	const USkeletalMesh* SourceMeshAsset = SourceMeshComponent->GetSkeletalMeshAsset();
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

		FProjectEmoteLeaderPoseSceneLockSnapshot Snapshot;
		Snapshot.Component = MeshComponent;
		Snapshot.LeaderPoseComponent = MeshComponent->LeaderPoseComponent.Get();
		LeaderPoseSceneLockSnapshots.Add(Snapshot);
	}

	for (USkeletalMeshComponent* CandidateMeshComponent : MeshComponents)
	{
		if (!CandidateMeshComponent || CandidateMeshComponent == SourceMeshComponent)
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

		if (CandidateMeshComponent->LeaderPoseComponent.Get() == SourceMeshComponent)
		{
			continue;
		}

		CandidateMeshComponent->SetLeaderPoseComponent(SourceMeshComponent, true, false);
		CandidateMeshComponent->RefreshBoneTransforms();
	}
}

void UProjectEmoteComponent::RestoreVisibleMeshLeaderPoseSceneLock()
{
	for (const FProjectEmoteLeaderPoseSceneLockSnapshot& Snapshot : LeaderPoseSceneLockSnapshots)
	{
		USkeletalMeshComponent* Component = Snapshot.Component.Get();
		if (!Component)
		{
			continue;
		}

		Component->SetLeaderPoseComponent(nullptr, true);
	}

	for (const FProjectEmoteLeaderPoseSceneLockSnapshot& Snapshot : LeaderPoseSceneLockSnapshots)
	{
		USkeletalMeshComponent* Component = Snapshot.Component.Get();
		if (!Component)
		{
			continue;
		}

		Component->SetLeaderPoseComponent(Snapshot.LeaderPoseComponent.Get(), true);
		Component->RefreshBoneTransforms();
	}
}

void UProjectEmoteComponent::RestoreMinimalAnimSceneLock()
{
	RestoreVisibleMeshLeaderPoseSceneLock();

	for (const FProjectEmoteBoolSceneLockSnapshot& Snapshot : BoolSceneLockSnapshots)
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

	for (const FProjectEmoteFloatSceneLockSnapshot& Snapshot : FloatSceneLockSnapshots)
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
	LeaderPoseSceneLockSnapshots.Reset();
	bMinimalAnimSceneLockApplied = false;
}

void UProjectEmoteComponent::CacheAndSetBoolSceneLockProperty(UObject* Target, const FName PropertyName, const bool bNewValue)
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

	for (const FProjectEmoteBoolSceneLockSnapshot& Snapshot : BoolSceneLockSnapshots)
	{
		if (Snapshot.Target.Get() == Target && Snapshot.PropertyName == PropertyName)
		{
			BoolProperty->SetPropertyValue_InContainer(Target, bNewValue);
			return;
		}
	}

	FProjectEmoteBoolSceneLockSnapshot Snapshot;
	Snapshot.Target = Target;
	Snapshot.PropertyName = PropertyName;
	Snapshot.bValue = BoolProperty->GetPropertyValue_InContainer(Target);
	BoolSceneLockSnapshots.Add(Snapshot);
	BoolProperty->SetPropertyValue_InContainer(Target, bNewValue);
}

void UProjectEmoteComponent::CacheAndSetFloatSceneLockProperty(UObject* Target, const FName PropertyName, const double NewValue)
{
	if (!IsValid(Target) || PropertyName.IsNone())
	{
		return;
	}

	if (FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(Target->GetClass(), PropertyName))
	{
		for (const FProjectEmoteFloatSceneLockSnapshot& Snapshot : FloatSceneLockSnapshots)
		{
			if (Snapshot.Target.Get() == Target && Snapshot.PropertyName == PropertyName)
			{
				FloatProperty->SetPropertyValue_InContainer(Target, static_cast<float>(NewValue));
				return;
			}
		}

		FProjectEmoteFloatSceneLockSnapshot Snapshot;
		Snapshot.Target = Target;
		Snapshot.PropertyName = PropertyName;
		Snapshot.Value = FloatProperty->GetPropertyValue_InContainer(Target);
		FloatSceneLockSnapshots.Add(Snapshot);
		FloatProperty->SetPropertyValue_InContainer(Target, static_cast<float>(NewValue));
		return;
	}

	if (FDoubleProperty* DoubleProperty = FindFProperty<FDoubleProperty>(Target->GetClass(), PropertyName))
	{
		for (const FProjectEmoteFloatSceneLockSnapshot& Snapshot : FloatSceneLockSnapshots)
		{
			if (Snapshot.Target.Get() == Target && Snapshot.PropertyName == PropertyName)
			{
				DoubleProperty->SetPropertyValue_InContainer(Target, NewValue);
				return;
			}
		}

		FProjectEmoteFloatSceneLockSnapshot Snapshot;
		Snapshot.Target = Target;
		Snapshot.PropertyName = PropertyName;
		Snapshot.Value = DoubleProperty->GetPropertyValue_InContainer(Target);
		FloatSceneLockSnapshots.Add(Snapshot);
		DoubleProperty->SetPropertyValue_InContainer(Target, NewValue);
	}
}

bool UProjectEmoteComponent::PrepareBlueprintSceneInteraction(
	const FProjectEmoteInteractionDefinition& Definition,
	FProjectEmoteBlueprintSceneDefinition& OutSceneDefinition,
	AActor*& OutTargetActor,
	USkeletalMeshComponent*& OutTargetMesh)
{
	OutSceneDefinition = FProjectEmoteBlueprintSceneDefinition();
	OutTargetActor = nullptr;
	OutTargetMesh = nullptr;

	if (!Definition.BlueprintScene.bUseBlueprintScene)
	{
		return false;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	OutTargetActor = ResolveCurrentTargetActor();
	if (Definition.BlueprintScene.bRequireCurrentTarget && (!IsValid(OutTargetActor) || OutTargetActor == OwnerActor))
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Blueprint scene %s needs a valid T-selected target."), *Definition.InteractionId.ToString());
		OutTargetActor = nullptr;
		return false;
	}

	if (!LoadBlueprintSceneDefinition(Definition.BlueprintScene, OutSceneDefinition))
	{
		return false;
	}

	ResolveDependencies();
	USkeletalMeshComponent* PlayerMesh = SkeletalMeshComponent;
	OutTargetMesh = ResolveRuntimeMeshForBlueprintSceneRole(OutTargetActor, OutSceneDefinition.TargetRole);
	if (!ValidateBlueprintSceneRoleAgainstRuntimeMesh(OutSceneDefinition.PrimaryRole, PlayerMesh, TEXT("Player/Female"))
		|| !ValidateBlueprintSceneRoleAgainstRuntimeMesh(OutSceneDefinition.TargetRole, OutTargetMesh, TEXT("Target/Male")))
	{
		OutTargetActor = nullptr;
		OutTargetMesh = nullptr;
		return false;
	}

	return true;
}

bool UProjectEmoteComponent::LoadBlueprintSceneDefinition(const FProjectEmoteBlueprintSceneSettings& Settings, FProjectEmoteBlueprintSceneDefinition& OutSceneDefinition) const
{
	OutSceneDefinition = FProjectEmoteBlueprintSceneDefinition();
	OutSceneDefinition.PrimaryRole.RoleName = Settings.PrimaryRoleName.IsNone() ? FName(TEXT("Female")) : Settings.PrimaryRoleName;
	OutSceneDefinition.TargetRole.RoleName = Settings.TargetRoleName.IsNone() ? FName(TEXT("Male")) : Settings.TargetRoleName;

	FSoftObjectPath BlueprintPath = Settings.SceneBlueprint.ToSoftObjectPath();
	if (!BlueprintPath.IsValid())
	{
		BlueprintPath = FSoftObjectPath(ProjectEmoteComponentPrivate::DefaultTogetherSceneBlueprintPath);
	}

	UObject* BlueprintObject = BlueprintPath.TryLoad();
	UClass* SceneClass = nullptr;
	if (const UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintObject))
	{
		SceneClass = Blueprint->GeneratedClass;
	}

	if (!SceneClass)
	{
		SceneClass = ProjectEmoteComponentPrivate::LoadBlueprintGeneratedClass(BlueprintPath);
	}

	if (!SceneClass || !SceneClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Failed to load blueprint scene generated class from %s"), *BlueprintPath.ToString());
		return false;
	}

	const AActor* SceneCDO = Cast<AActor>(SceneClass->GetDefaultObject());
	if (!SceneCDO)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Blueprint scene class has no actor CDO: %s"), *GetNameSafe(SceneClass));
		return false;
	}

	OutSceneDefinition.SceneClass = SceneClass;
	OutSceneDefinition.SceneBlueprintPath = BlueprintPath;

	TArray<USkeletalMeshComponent*> SceneMeshComponents;
	SceneCDO->GetComponents(SceneMeshComponents);
	for (USkeletalMeshComponent* Component : SceneMeshComponents)
	{
		ExtractBlueprintSceneRoleFromComponent(Component, Component ? Component->GetFName() : NAME_None, Settings, OutSceneDefinition);
	}

	if ((!OutSceneDefinition.PrimaryRole.ReferenceMesh || !OutSceneDefinition.PrimaryRole.Animation || !OutSceneDefinition.TargetRole.ReferenceMesh || !OutSceneDefinition.TargetRole.Animation)
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

				ExtractBlueprintSceneRoleFromComponent(
					Cast<USkeletalMeshComponent>(ComponentTemplate),
					Node->GetVariableName(),
					Settings,
					OutSceneDefinition);
			}
		}
	}

	const bool bHasPrimary = OutSceneDefinition.PrimaryRole.ReferenceMesh && OutSceneDefinition.PrimaryRole.Animation;
	const bool bHasTarget = OutSceneDefinition.TargetRole.ReferenceMesh && OutSceneDefinition.TargetRole.Animation;
	if (!bHasPrimary || !bHasTarget)
	{
		UE_LOG(
			LogProjectEmoteComponent,
			Warning,
			TEXT("[ProjectEmote] Blueprint scene %s did not expose valid %s/%s skeletal components with AnimToPlay. PrimaryMesh=%s PrimaryAnim=%s TargetMesh=%s TargetAnim=%s"),
			*BlueprintPath.ToString(),
			*OutSceneDefinition.PrimaryRole.RoleName.ToString(),
			*OutSceneDefinition.TargetRole.RoleName.ToString(),
			*GetNameSafe(OutSceneDefinition.PrimaryRole.ReferenceMesh),
			*GetNameSafe(OutSceneDefinition.PrimaryRole.Animation),
			*GetNameSafe(OutSceneDefinition.TargetRole.ReferenceMesh),
			*GetNameSafe(OutSceneDefinition.TargetRole.Animation));
		return false;
	}

	UE_LOG(
		LogProjectEmoteComponent,
		Log,
		TEXT("[ProjectEmote] Blueprint scene roles: %s=%s/%s %s=%s/%s"),
		*OutSceneDefinition.PrimaryRole.RoleName.ToString(),
		*GetNameSafe(OutSceneDefinition.PrimaryRole.ReferenceMesh),
		*GetNameSafe(OutSceneDefinition.PrimaryRole.Animation),
		*OutSceneDefinition.TargetRole.RoleName.ToString(),
		*GetNameSafe(OutSceneDefinition.TargetRole.ReferenceMesh),
		*GetNameSafe(OutSceneDefinition.TargetRole.Animation));
	return true;
}

bool UProjectEmoteComponent::ExtractBlueprintSceneRoleFromComponent(
	USkeletalMeshComponent* Component,
	const FName ComponentNameHint,
	const FProjectEmoteBlueprintSceneSettings& Settings,
	FProjectEmoteBlueprintSceneDefinition& InOutSceneDefinition) const
{
	if (!Component)
	{
		return false;
	}

	const FName PrimaryRoleName = Settings.PrimaryRoleName.IsNone() ? FName(TEXT("Female")) : Settings.PrimaryRoleName;
	const FName TargetRoleName = Settings.TargetRoleName.IsNone() ? FName(TEXT("Male")) : Settings.TargetRoleName;

	FProjectEmoteBlueprintSceneRoleDefinition* TargetRole = nullptr;
	if (ProjectEmoteComponentPrivate::ComponentLooksLikeRole(Component, ComponentNameHint, PrimaryRoleName))
	{
		TargetRole = &InOutSceneDefinition.PrimaryRole;
		TargetRole->RoleName = PrimaryRoleName;
	}
	else if (ProjectEmoteComponentPrivate::ComponentLooksLikeRole(Component, ComponentNameHint, TargetRoleName))
	{
		TargetRole = &InOutSceneDefinition.TargetRole;
		TargetRole->RoleName = TargetRoleName;
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

USkeletalMeshComponent* UProjectEmoteComponent::ResolveRuntimeMeshForBlueprintSceneRole(AActor* Actor, const FProjectEmoteBlueprintSceneRoleDefinition& Role) const
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
			const int32 Score = ProjectEmoteComponentPrivate::ScoreRuntimeMeshForRole(CharacterMesh, Role) + 100;
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
		const int32 Score = ProjectEmoteComponentPrivate::ScoreRuntimeMeshForRole(MeshComponent, Role);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestMesh = MeshComponent;
		}
	}

	return BestMesh;
}

bool UProjectEmoteComponent::ValidateBlueprintSceneRoleAgainstRuntimeMesh(
	const FProjectEmoteBlueprintSceneRoleDefinition& Role,
	const USkeletalMeshComponent* RuntimeMesh,
	const TCHAR* RuntimeLabel) const
{
	if (!RuntimeMesh)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] %s has no compatible runtime skeletal mesh."), RuntimeLabel);
		return false;
	}

	const USkeletalMesh* RuntimeMeshAsset = RuntimeMesh->GetSkeletalMeshAsset();
	if (!Role.ReferenceMesh || !Role.Animation || !RuntimeMeshAsset)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] %s missing blueprint scene mesh or animation data."), RuntimeLabel);
		return false;
	}

	if (!RuntimeMeshAsset->GetSkeleton() || Role.Animation->GetSkeleton() != RuntimeMeshAsset->GetSkeleton())
	{
		UE_LOG(
			LogProjectEmoteComponent,
			Warning,
			TEXT("[ProjectEmote] %s skeleton mismatch. RuntimeMesh=%s RuntimeSkeleton=%s Anim=%s AnimSkeleton=%s"),
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
			LogProjectEmoteComponent,
			Warning,
			TEXT("[ProjectEmote] %s mesh asset differs from blueprint but skeleton matches. BlueprintMesh=%s RuntimeMesh=%s"),
			RuntimeLabel,
			*GetNameSafe(Role.ReferenceMesh),
			*GetNameSafe(RuntimeMeshAsset));
	}

	return true;
}

void UProjectEmoteComponent::CacheAndFreezeTargetParticipant(FProjectEmoteParticipantSceneState& State, AActor* Actor, USkeletalMeshComponent* Mesh)
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

		if (AController* Controller = Character->GetController())
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
						BrainComponent->PauseLogic(TEXT("ProjectEmoteBlueprintScene"));
					}
				}
			}
		}
	}
}

void UProjectEmoteComponent::RestoreTargetParticipant(FProjectEmoteParticipantSceneState& State, const FTransform* OverrideActorTransform)
{
	if (AActor* Actor = State.Actor.Get())
	{
		Actor->SetActorTransform(OverrideActorTransform ? *OverrideActorTransform : State.CachedActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (UCharacterMovementComponent* MovementComponent = State.MovementComponent.Get())
	{
		MovementComponent->StopMovementImmediately();
		if (State.bHadMovementComponent)
		{
			MovementComponent->SetMovementMode(State.CachedMovementMode, State.CachedCustomMovementMode);
		}
	}

	if (UBrainComponent* BrainComponent = State.BrainComponent.Get())
	{
		if (State.bHadBrainComponent && !State.bBrainWasPaused)
		{
			BrainComponent->ResumeLogic(TEXT("ProjectEmoteBlueprintScene"));
		}
	}

	State.Reset();
}

void UProjectEmoteComponent::ApplyBlueprintSceneParticipantTransform(
	AActor* Actor,
	USkeletalMeshComponent* Mesh,
	const FProjectEmoteBlueprintSceneRoleDefinition& Role,
	const FTransform& SceneAnchorTransform) const
{
	if (!Actor || !Mesh)
	{
		return;
	}

	const FTransform DesiredMeshWorldTransform = Role.RelativeTransform * SceneAnchorTransform;
	const FTransform RuntimeMeshRelativeToActor = Mesh->GetComponentTransform().GetRelativeTransform(Actor->GetActorTransform());
	const FTransform DesiredActorTransform = RuntimeMeshRelativeToActor.Inverse() * DesiredMeshWorldTransform;
	Actor->SetActorTransform(DesiredActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

UAnimMontage* UProjectEmoteComponent::PlayBlueprintSceneRoleMontage(
	const FProjectEmoteBlueprintSceneRoleDefinition& Role,
	USkeletalMeshComponent* RuntimeMesh,
	const TCHAR* RuntimeLabel) const
{
	if (!RuntimeMesh || !Role.Animation)
	{
		return nullptr;
	}

	UAnimInstance* AnimInstance = RuntimeMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] %s has no AnimInstance."), RuntimeLabel);
		return nullptr;
	}

	const FMontageBlendSettings BlendInSettings(LoopBlendInTime);
	const FMontageBlendSettings BlendOutSettings(LoopBlendOutTime);
	const int32 LoopCount = ActivePlaybackMode == EProjectEmotePlaybackMode::Looping ? MAX_int32 : 1;
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(
		Role.Animation,
		OverlaySlotName,
		BlendInSettings,
		BlendOutSettings,
		1.0f,
		LoopCount,
		-1.0f);
	if (!Montage)
	{
		return nullptr;
	}

	Montage->bEnableAutoBlendOut = ActivePlaybackMode == EProjectEmotePlaybackMode::PlayOnce;
	const float PlayResult = AnimInstance->Montage_PlayWithBlendSettings(
		Montage,
		BlendInSettings,
		1.0f,
		EMontagePlayReturnType::MontageLength,
		0.0f,
		true);
	if (PlayResult <= 0.0f)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] %s failed to play %s."), RuntimeLabel, *GetNameSafe(Role.Animation));
		return nullptr;
	}

	return Montage;
}

void UProjectEmoteComponent::StopTargetBlueprintSceneMontage(const float BlendOutTime)
{
	UAnimInstance* AnimInstance = nullptr;
	if (USkeletalMeshComponent* TargetMesh = TargetParticipantState.Mesh.Get())
	{
		AnimInstance = TargetMesh->GetAnimInstance();
	}
	if (!AnimInstance)
	{
		AnimInstance = TargetParticipantState.AnimInstance.Get();
	}

	if (AnimInstance)
	{
		if (ActiveTargetOverlayMontage)
		{
			AnimInstance->Montage_Stop(BlendOutTime, ActiveTargetOverlayMontage);
		}
		AnimInstance->StopSlotAnimation(BlendOutTime, OverlaySlotName);
	}

	ActiveTargetOverlayMontage = nullptr;
}

bool UProjectEmoteComponent::BuildBlueprintSceneTargetEndTransform(FTransform& OutTransform) const
{
	if (!ActiveInteractionDefinition.BlueprintScene.bPlaceTargetNearPrimaryOnEnd)
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	const FTransform PrimaryFinalTransform = bBlueprintScenePlayerTransformCached
		? CachedBlueprintScenePlayerTransform
		: OwnerActor->GetActorTransform();
	const FVector TargetLocation = PrimaryFinalTransform.TransformPosition(ActiveInteractionDefinition.BlueprintScene.TargetEndLocalOffset);
	FRotator TargetRotation = PrimaryFinalTransform.GetRotation().Rotator();
	if (ActiveInteractionDefinition.BlueprintScene.bTargetFacesPrimaryOnEnd)
	{
		TargetRotation = (PrimaryFinalTransform.GetLocation() - TargetLocation).Rotation();
		TargetRotation.Pitch = 0.0f;
		TargetRotation.Roll = 0.0f;
	}

	const FVector TargetScale = TargetParticipantState.Actor.IsValid()
		? TargetParticipantState.CachedActorTransform.GetScale3D()
		: FVector::OneVector;
	OutTransform = FTransform(TargetRotation, TargetLocation, TargetScale);
	return true;
}

AActor* UProjectEmoteComponent::SpawnBlueprintSceneVisualActor(
	const FProjectEmoteBlueprintSceneDefinition& SceneDefinition,
	const FTransform& SceneAnchorTransform)
{
	if (!SceneDefinition.SceneClass || !SceneDefinition.SceneClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Blueprint scene visual actor missing valid class for %s."), *SceneDefinition.SceneBlueprintPath.ToString());
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AActor* VisualActor = World->SpawnActor<AActor>(SceneDefinition.SceneClass, SceneAnchorTransform, SpawnParameters);
	if (!VisualActor)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Failed to spawn blueprint scene visual actor for %s."), *SceneDefinition.SceneBlueprintPath.ToString());
		return nullptr;
	}

#if WITH_EDITOR
	VisualActor->SetActorLabel(TEXT("ProjectEmote_BlueprintSceneVisual"), false);
#endif
	VisualActor->SetActorEnableCollision(false);
	VisualActor->SetActorHiddenInGame(false);
	ConfigureBlueprintSceneVisualActor(VisualActor, SceneDefinition);
	return VisualActor;
}

void UProjectEmoteComponent::ConfigureBlueprintSceneVisualActor(AActor* VisualActor, const FProjectEmoteBlueprintSceneDefinition& SceneDefinition) const
{
	if (!VisualActor)
	{
		return;
	}

	TInlineComponentArray<UActorComponent*> Components;
	VisualActor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		Component->SetComponentTickEnabled(true);

		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
		{
			PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PrimitiveComponent->SetGenerateOverlapEvents(false);
			PrimitiveComponent->SetNotifyRigidBodyCollision(false);
		}

		if (UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(Component))
		{
			if (ProjectEmoteComponentPrivate::IsBlueprintSceneSessionPeakCueNiagara(NiagaraComponent))
			{
				ProjectEmoteComponentPrivate::SetBlueprintSceneSessionPeakCueHidden(NiagaraComponent);
				continue;
			}

			NiagaraComponent->SetVisibility(true, true);
			NiagaraComponent->SetHiddenInGame(false, true);
			NiagaraComponent->DeactivateImmediate();
		}
	}

	PrepareBlueprintSceneVisualRoleMesh(ResolveVisualSceneRoleMesh(VisualActor, SceneDefinition.PrimaryRole));
	PrepareBlueprintSceneVisualRoleMesh(ResolveVisualSceneRoleMesh(VisualActor, SceneDefinition.TargetRole));
}

void UProjectEmoteComponent::PrepareBlueprintSceneVisualRoleMesh(USkeletalMeshComponent* MeshComponent) const
{
	if (!MeshComponent)
	{
		return;
	}

	MeshComponent->SetVisibility(false, false);
	MeshComponent->SetHiddenInGame(true, false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	MeshComponent->bPauseAnims = false;
	MeshComponent->SetComponentTickEnabled(true);
	MeshComponent->PrimaryComponentTick.SetTickFunctionEnable(true);
}

USkeletalMeshComponent* UProjectEmoteComponent::ResolveVisualSceneRoleMesh(AActor* VisualActor, const FProjectEmoteBlueprintSceneRoleDefinition& Role) const
{
	if (!VisualActor)
	{
		return nullptr;
	}

	USkeletalMeshComponent* BestMesh = nullptr;
	int32 BestScore = INDEX_NONE;
	TArray<USkeletalMeshComponent*> MeshComponents;
	VisualActor->GetComponents(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		int32 Score = ProjectEmoteComponentPrivate::ScoreRuntimeMeshForRole(MeshComponent, Role);
		if (!Role.ComponentName.IsNone() && (MeshComponent->GetFName() == Role.ComponentName || MeshComponent->GetName().Contains(Role.ComponentName.ToString())))
		{
			Score += 10000;
		}

		if (ProjectEmoteComponentPrivate::ComponentLooksLikeRole(MeshComponent, MeshComponent->GetFName(), Role.RoleName))
		{
			Score += 2500;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestMesh = MeshComponent;
		}
	}

	return BestMesh;
}

void UProjectEmoteComponent::StartBlueprintSceneVisualPlayback()
{
	AActor* VisualActor = ActiveBlueprintSceneVisualActor;
	if (!VisualActor)
	{
		return;
	}

	PlayBlueprintSceneVisualRoleAnimation(
		ActiveBlueprintSceneDefinition.PrimaryRole,
		ResolveVisualSceneRoleMesh(VisualActor, ActiveBlueprintSceneDefinition.PrimaryRole),
		TEXT("Visual/Female"));
	PlayBlueprintSceneVisualRoleAnimation(
		ActiveBlueprintSceneDefinition.TargetRole,
		ResolveVisualSceneRoleMesh(VisualActor, ActiveBlueprintSceneDefinition.TargetRole),
		TEXT("Visual/Male"));
	ActivateBlueprintSceneVisualNiagara();
}

void UProjectEmoteComponent::PlayBlueprintSceneVisualRoleAnimation(
	const FProjectEmoteBlueprintSceneRoleDefinition& Role,
	USkeletalMeshComponent* VisualMesh,
	const TCHAR* RuntimeLabel) const
{
	if (!VisualMesh || !Role.Animation)
	{
		return;
	}

	PrepareBlueprintSceneVisualRoleMesh(VisualMesh);
	VisualMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	VisualMesh->SetAnimation(Role.Animation);
	VisualMesh->Play(true);
	VisualMesh->RefreshBoneTransforms();
	UE_LOG(LogProjectEmoteComponent, Verbose, TEXT("[ProjectEmote] Started hidden blueprint scene visual role %s with %s."), RuntimeLabel, *GetNameSafe(Role.Animation));
}

void UProjectEmoteComponent::ActivateBlueprintSceneVisualNiagara() const
{
	AActor* VisualActor = ActiveBlueprintSceneVisualActor;
	if (!VisualActor)
	{
		return;
	}

	TArray<UNiagaraComponent*> NiagaraComponents;
	VisualActor->GetComponents(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		if (!NiagaraComponent)
		{
			continue;
		}

		if (ProjectEmoteComponentPrivate::IsBlueprintSceneSessionPeakCueNiagara(NiagaraComponent))
		{
			ProjectEmoteComponentPrivate::SetBlueprintSceneSessionPeakCueHidden(NiagaraComponent);
			continue;
		}

		NiagaraComponent->SetVisibility(true, true);
		NiagaraComponent->SetHiddenInGame(false, true);
		NiagaraComponent->SetComponentTickEnabled(true);
		NiagaraComponent->Activate(true);

		if (NiagaraComponent->GetFName() == TEXT("HeavyBreathing") || NiagaraComponent->GetName().Contains(TEXT("HeavyBreathing")))
		{
			UE_LOG(
				LogProjectEmoteComponent,
				Log,
				TEXT("[ProjectEmote] Activated blueprint scene Niagara %s asset=%s actor=%s."),
				*NiagaraComponent->GetName(),
				*GetNameSafe(NiagaraComponent->GetAsset()),
				*GetNameSafe(VisualActor));
		}
	}
}

void UProjectEmoteComponent::DestroyBlueprintSceneVisualActor()
{
	if (AActor* VisualActor = ActiveBlueprintSceneVisualActor)
	{
		TArray<UNiagaraComponent*> NiagaraComponents;
		VisualActor->GetComponents(NiagaraComponents);
		for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
		{
			if (!NiagaraComponent)
			{
				continue;
			}

			NiagaraComponent->DeactivateImmediate();
			NiagaraComponent->SetVisibility(false, true);
			NiagaraComponent->SetHiddenInGame(true, true);
			NiagaraComponent->SetComponentTickEnabled(false);
			NiagaraComponent->DestroyComponent();
		}

		VisualActor->SetActorHiddenInGame(true);
		VisualActor->SetActorEnableCollision(false);
		VisualActor->Destroy();
	}

	ActiveBlueprintSceneVisualActor = nullptr;
}

void UProjectEmoteComponent::RestoreBlueprintSceneState()
{
	if (!bBlueprintSceneActive
		&& !ActiveTargetOverlayMontage
		&& !TargetParticipantState.Actor.IsValid()
		&& !bBlueprintScenePlayerTransformCached
		&& !ActiveBlueprintSceneVisualActor
		&& BlueprintSceneEquipmentSnapshots.IsEmpty()
		&& BlueprintSceneEquipmentPrimitiveSnapshots.IsEmpty())
	{
		return;
	}

	StopTargetBlueprintSceneMontage(LoopBlendOutTime);
	DestroyBlueprintSceneVisualActor();

	if (bBlueprintScenePlayerTransformCached)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			OwnerActor->SetActorTransform(CachedBlueprintScenePlayerTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	FTransform TargetEndTransform = FTransform::Identity;
	const bool bUseTargetEndTransform = BuildBlueprintSceneTargetEndTransform(TargetEndTransform);
	RestoreTargetParticipant(TargetParticipantState, bUseTargetEndTransform ? &TargetEndTransform : nullptr);
	RestoreBlueprintSceneEquipmentSuppression();

	bBlueprintSceneActive = false;
	bBlueprintScenePlayerTransformCached = false;
	CachedBlueprintScenePlayerTransform = FTransform::Identity;
	ActiveBlueprintSceneDefinition = FProjectEmoteBlueprintSceneDefinition();
}

void UProjectEmoteComponent::StartFreeCamera(APlayerController* PlayerController)
{
	if (!bUseFreeCameraDuringEmote || ActiveFreeCameraActor || !PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float CameraFov = 90.0f;
	if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
	{
		CameraLocation = CameraManager->GetCameraLocation();
		CameraRotation = CameraManager->GetCameraRotation();
		CameraFov = CameraManager->GetFOVAngle();
	}
	else if (AActor* ViewTarget = PlayerController->GetViewTarget())
	{
		CameraLocation = ViewTarget->GetActorLocation();
		CameraRotation = ViewTarget->GetActorRotation();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveFreeCameraActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation, CameraRotation, SpawnParameters);
	if (!ActiveFreeCameraActor)
	{
		return;
	}

	if (UCameraComponent* CameraComponent = ActiveFreeCameraActor->GetCameraComponent())
	{
		CameraComponent->SetFieldOfView(CameraFov);
	}

	SavedViewTarget = PlayerController->GetViewTarget();
	UE_LOG(
		LogProjectEmoteComponent,
		VeryVerbose,
		TEXT("[ProjectEmote] StartFreeCamera: ViewTarget=%s Pawn=%s FreeCamera=%s FOV=%.2f"),
		*GetNameSafe(SavedViewTarget.Get()),
		*GetNameSafe(PlayerController->GetPawn()),
		*GetNameSafe(ActiveFreeCameraActor),
		CameraFov);
	PlayerController->SetViewTargetWithBlend(ActiveFreeCameraActor, FreeCameraBlendTime);
	SetComponentTickEnabled(true);
}

void UProjectEmoteComponent::StopFreeCamera(APlayerController* PlayerController)
{
	ClearFreeCameraMoveInput();
	SetComponentTickEnabled(false);

	if (PlayerController)
	{
		AActor* CurrentViewTarget = PlayerController->GetViewTarget();
		AActor* RestoreTarget = ResolvePostEmoteViewTarget(PlayerController);
		const float CurrentFov = PlayerController->PlayerCameraManager ? PlayerController->PlayerCameraManager->GetFOVAngle() : 0.0f;
		UE_LOG(
			LogProjectEmoteComponent,
			VeryVerbose,
			TEXT("[ProjectEmote] StopFreeCamera: CurrentViewTarget=%s SavedViewTarget=%s Pawn=%s RestoreTarget=%s FreeCamera=%s FOV=%.2f"),
			*GetNameSafe(CurrentViewTarget),
			*GetNameSafe(SavedViewTarget.Get()),
			*GetNameSafe(PlayerController->GetPawn()),
			*GetNameSafe(RestoreTarget),
			*GetNameSafe(ActiveFreeCameraActor),
			CurrentFov);

		if (RestoreTarget && CurrentViewTarget != RestoreTarget)
		{
			const float BlendTime = (ActiveFreeCameraActor && CurrentViewTarget == ActiveFreeCameraActor) ? FreeCameraBlendTime : 0.0f;
			PlayerController->SetViewTargetWithBlend(RestoreTarget, BlendTime);
		}
	}

	if (ACameraActor* CameraToDestroy = ActiveFreeCameraActor)
	{
		CameraToDestroy->SetLifeSpan(FMath::Max(FreeCameraBlendTime + 0.05f, 0.05f));
		ActiveFreeCameraActor = nullptr;
	}

	SavedViewTarget = nullptr;
	ScheduleDeferredViewTargetRestore(PlayerController);
}

AActor* UProjectEmoteComponent::ResolvePostEmoteViewTarget(APlayerController* PlayerController) const
{
	if (PlayerController)
	{
		if (APawn* ControlledPawn = PlayerController->GetPawn())
		{
			if (IsValid(ControlledPawn))
			{
				return ControlledPawn;
			}
		}
	}

	if (ACharacter* CharacterOwner = CachedCharacterOwner.Get())
	{
		if (IsValid(CharacterOwner))
		{
			return CharacterOwner;
		}
	}

	AActor* PreviousViewTarget = SavedViewTarget.Get();
	if (PreviousViewTarget && IsValid(PreviousViewTarget) && PreviousViewTarget != ActiveFreeCameraActor)
	{
		return PreviousViewTarget;
	}

	return nullptr;
}

void UProjectEmoteComponent::ScheduleDeferredViewTargetRestore(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredViewTargetRestoreTimerHandle);
		World->GetTimerManager().SetTimer(
			DeferredViewTargetRestoreTimerHandle,
			this,
			&ThisClass::RestorePostEmoteViewTarget,
			FMath::Max(FreeCameraBlendTime + 0.05f, 0.05f),
			false);
	}
}

void UProjectEmoteComponent::RestorePostEmoteViewTarget()
{
	if (ActiveInteractionId != NAME_None || bEmoteTransitionPending)
	{
		return;
	}

	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	AActor* RestoreTarget = ResolvePostEmoteViewTarget(PlayerController);
	AActor* CurrentViewTarget = PlayerController->GetViewTarget();
	const float CurrentFov = PlayerController->PlayerCameraManager ? PlayerController->PlayerCameraManager->GetFOVAngle() : 0.0f;
	UE_LOG(
		LogProjectEmoteComponent,
		VeryVerbose,
		TEXT("[ProjectEmote] DeferredViewTargetRestore: CurrentViewTarget=%s Pawn=%s RestoreTarget=%s FOV=%.2f"),
		*GetNameSafe(CurrentViewTarget),
		*GetNameSafe(PlayerController->GetPawn()),
		*GetNameSafe(RestoreTarget),
		CurrentFov);

	if (RestoreTarget && CurrentViewTarget != RestoreTarget)
	{
		PlayerController->SetViewTargetWithBlend(RestoreTarget, 0.0f);
	}
}

void UProjectEmoteComponent::UpdateFreeCamera(const float DeltaTime)
{
	if (!ActiveFreeCameraActor || DeltaTime <= 0.0f)
	{
		return;
	}

	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PlayerController->GetInputMouseDelta(MouseX, MouseY);

	FRotator CameraRotation = ActiveFreeCameraActor->GetActorRotation();
	CameraRotation.Yaw += MouseX * FreeCameraMouseSensitivity;
	CameraRotation.Pitch = FMath::Clamp(CameraRotation.Pitch + (MouseY * FreeCameraMouseSensitivity), FreeCameraMinPitch, FreeCameraMaxPitch);
	CameraRotation.Roll = 0.0f;
	ActiveFreeCameraActor->SetActorRotation(CameraRotation);

	const FVector Forward = CameraRotation.Vector();
	const FVector Right = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
	const FVector Up = FVector::UpVector;
	const float CurrentMoveSpeed = bFreeCameraBoostActive ? FreeCameraBoostMoveSpeed : FreeCameraMoveSpeed;
	const FVector MoveDelta = ((Forward * FreeCameraForwardInput) + (Right * FreeCameraRightInput) + (Up * FreeCameraVerticalInput)).GetClampedToMaxSize(1.0f)
		* CurrentMoveSpeed
		* DeltaTime;

	if (!MoveDelta.IsNearlyZero())
	{
		ActiveFreeCameraActor->AddActorWorldOffset(MoveDelta, false);
	}
}

void UProjectEmoteComponent::HandleDeferredEmotePlaybackStart()
{
	bEmoteTransitionPending = false;

	if (ActiveInteractionId.IsNone())
	{
		PlayFadeFromBlack();
		return;
	}

	ResolveDependencies();
	UAnimInstance* AnimInstance = ResolveAnimInstance();
	if (ActiveInteractionDefinition.BlueprintScene.bUseBlueprintScene)
	{
		USkeletalMeshComponent* TargetMesh = TargetParticipantState.Mesh.Get();
		if (!SkeletalMeshComponent || !TargetMesh)
		{
			UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Blueprint scene playback lost one or more participant meshes for %s"), *GetNameSafe(GetOwner()));
			StopEmote();
			return;
		}

		StopActiveMontage(LoopBlendOutTime);
		StopTargetBlueprintSceneMontage(LoopBlendOutTime);
		ActiveOverlayMontage = PlayBlueprintSceneRoleMontage(ActiveBlueprintSceneDefinition.PrimaryRole, SkeletalMeshComponent, TEXT("Player/Female"));
		ActiveTargetOverlayMontage = PlayBlueprintSceneRoleMontage(ActiveBlueprintSceneDefinition.TargetRole, TargetMesh, TEXT("Target/Male"));
		if (!ActiveOverlayMontage || !ActiveTargetOverlayMontage)
		{
			UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Failed to start one or both blueprint scene role montages for %s"), *ActiveInteractionId.ToString());
			StopEmote();
			return;
		}

		ActiveAnimInstance = AnimInstance;
		if (AnimInstance && ActivePlaybackMode == EProjectEmotePlaybackMode::PlayOnce)
		{
			FOnMontageEnded MontageEndedDelegate;
			MontageEndedDelegate.BindUObject(this, &ThisClass::HandleActiveMontageEnded);
			AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ActiveOverlayMontage);
		}

		StartBlueprintSceneVisualPlayback();

		UE_LOG(
			LogProjectEmoteComponent,
			Log,
			TEXT("[ProjectEmote] Started blueprint scene %s with player anim %s and target anim %s."),
			*ActiveInteractionId.ToString(),
			*GetNameSafe(ActiveBlueprintSceneDefinition.PrimaryRole.Animation),
			*GetNameSafe(ActiveBlueprintSceneDefinition.TargetRole.Animation));
		PlayFadeFromBlack();
		return;
	}

	UAnimSequenceBase* SequenceAsset = Cast<UAnimSequenceBase>(LoadAnimationAsset(ActiveAnimation));
	if (!AnimInstance || !SequenceAsset)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Failed to resolve animation sequence for %s"), *GetNameSafe(GetOwner()));
		StopEmote();
		return;
	}

	const FMontageBlendSettings BlendInSettings(LoopBlendInTime);
	const FMontageBlendSettings BlendOutSettings(LoopBlendOutTime);
	const int32 LoopCount = ActivePlaybackMode == EProjectEmotePlaybackMode::Looping ? MAX_int32 : 1;

	StopActiveMontage(LoopBlendOutTime);
	ActiveOverlayMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(
		SequenceAsset,
		OverlaySlotName,
		BlendInSettings,
		BlendOutSettings,
		1.f,
		LoopCount,
		-1.f);
	if (!ActiveOverlayMontage)
	{
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Failed to create dynamic montage for %s"), *GetNameSafe(SequenceAsset));
		StopEmote();
		return;
	}

	ActiveOverlayMontage->bEnableAutoBlendOut = ActivePlaybackMode == EProjectEmotePlaybackMode::PlayOnce;
	ActiveAnimInstance = AnimInstance;

	const float PlayResult = AnimInstance->Montage_PlayWithBlendSettings(
		ActiveOverlayMontage,
		BlendInSettings,
		1.f,
		EMontagePlayReturnType::MontageLength,
		0.f);
	if (PlayResult <= 0.0f)
	{
		ActiveOverlayMontage = nullptr;
		UE_LOG(LogProjectEmoteComponent, Warning, TEXT("[ProjectEmote] Failed to play dynamic montage for %s"), *GetNameSafe(SequenceAsset));
		StopEmote();
		return;
	}

	if (ActivePlaybackMode == EProjectEmotePlaybackMode::PlayOnce)
	{
		FOnMontageEnded MontageEndedDelegate;
		MontageEndedDelegate.BindUObject(this, &ThisClass::HandleActiveMontageEnded);
		AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ActiveOverlayMontage);
	}

	PlayFadeFromBlack();
}

void UProjectEmoteComponent::RestorePreEmoteState()
{
	ResolveDependencies();

	if (ACharacter* CharacterOwner = CachedCharacterOwner.Get())
	{
		CharacterOwner->StopJumping();
	}

	if (CharacterMovementComponent && bPreEmoteStateCached)
	{
		CharacterMovementComponent->StopMovementImmediately();
		CharacterMovementComponent->SetMovementMode(CachedMovementMode, CachedCustomMovementMode);
	}

	if (APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		RestorePreActionSettle(PlayerController);

		if (bPawnInputSuspended)
		{
			if (ACharacter* CharacterOwner = CachedCharacterOwner.Get())
			{
				CharacterOwner->EnableInput(PlayerController);
			}
		}

		if (bAppliedMoveInputIgnore)
		{
			PlayerController->SetIgnoreMoveInput(false);
		}

		if (bAppliedLookInputIgnore)
		{
			PlayerController->SetIgnoreLookInput(false);
		}

		if (bForcedLookInputEnable)
		{
			PlayerController->ResetIgnoreLookInput();
			if (bCachedControllerLookInputIgnored)
			{
				PlayerController->SetIgnoreLookInput(true);
			}
		}
	}

	bAppliedMoveInputIgnore = false;
	bForcedLookInputEnable = false;
	bAppliedLookInputIgnore = false;
	bPawnInputSuspended = false;
	bPreEmoteStateCached = false;
	bCachedControlRotation = false;
	bAppliedNeutralControlRotation = false;
}

void UProjectEmoteComponent::ScheduleDelayedPostEmoteRecovery()
{
	ScheduleDelayedPostEmoteRecovery(PostEmoteRecoveryDelaySeconds, bCachedControllerMoveInputIgnored, bCachedControllerLookInputIgnored);
}

void UProjectEmoteComponent::ScheduleDelayedPostEmoteRecovery(
	const float DelaySeconds,
	const bool bMoveInputIgnored,
	const bool bLookInputIgnored)
{
	DelayedRecoveryMovementMode = CachedMovementMode == MOVE_None ? TEnumAsByte<EMovementMode>(MOVE_Walking) : CachedMovementMode;
	DelayedRecoveryCustomMovementMode = CachedCustomMovementMode;
	bDelayedRecoveryMoveInputIgnored = bMoveInputIgnored;
	bDelayedRecoveryLookInputIgnored = bLookInputIgnored;
	bHasDelayedRecoveryState = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DelayedPostEmoteRecoveryTimerHandle);
		World->GetTimerManager().SetTimer(
			DelayedPostEmoteRecoveryTimerHandle,
			this,
			&ThisClass::HandleDelayedPostEmoteRecovery,
			FMath::Max(DelaySeconds, 0.0f),
			false);
	}
}

void UProjectEmoteComponent::HandleDelayedPostEmoteRecovery()
{
	if (!bHasDelayedRecoveryState || ShouldSkipDelayedPostEmoteRecovery())
	{
		return;
	}

	ACharacter* CharacterOwner = CachedCharacterOwner.IsValid() ? CachedCharacterOwner.Get() : Cast<ACharacter>(GetOwner());
	APlayerController* PlayerController = ResolveOwningPlayerController();
	if (!CharacterOwner || !PlayerController)
	{
		bHasDelayedRecoveryState = false;
		return;
	}

	PlayerController->FlushPressedKeys();

	const UProjectLocomotionOverrideComponent* LocomotionOverride = GetOwner() ? GetOwner()->FindComponentByClass<UProjectLocomotionOverrideComponent>() : nullptr;
	const bool bPreserveMoveIgnoreForCrawl = LocomotionOverride && LocomotionOverride->IsCrawlModeActive();

	if (!bPreserveMoveIgnoreForCrawl)
	{
		PlayerController->ResetIgnoreMoveInput();
		if (bDelayedRecoveryMoveInputIgnored)
		{
			PlayerController->SetIgnoreMoveInput(true);
		}
	}

	PlayerController->ResetIgnoreLookInput();
	if (bDelayedRecoveryLookInputIgnored)
	{
		PlayerController->SetIgnoreLookInput(true);
	}

	if (UCharacterMovementComponent* CharacterMovement = CharacterOwner->GetCharacterMovement())
	{
		if (CharacterMovement->MovementMode == MOVE_None)
		{
			CharacterMovement->SetMovementMode(DelayedRecoveryMovementMode, DelayedRecoveryCustomMovementMode);
		}
	}

	bHasDelayedRecoveryState = false;
}

bool UProjectEmoteComponent::ShouldSkipDelayedPostEmoteRecovery() const
{
	if (ActiveInteractionId != NAME_None || bEmoteTransitionPending)
	{
		return true;
	}

	if (const UWorld* World = GetWorld())
	{
		if (UGameplayStatics::IsGamePaused(World))
		{
			return true;
		}

		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (const UEFCharacterCreationSubsystem* CharacterCreationSubsystem = GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>())
			{
				if (CharacterCreationSubsystem->IsCharacterCreationActive())
				{
					return true;
				}
			}
		}

		if (const UProjectEmoteSubsystem* EmoteSubsystem = World->GetSubsystem<UProjectEmoteSubsystem>())
		{
			if (EmoteSubsystem->IsEmoteMenuOpen())
			{
				return true;
			}
		}
	}

	return false;
}

void UProjectEmoteComponent::StopActiveMontage(const float BlendOutTime)
{
	UAnimInstance* AnimInstance = ResolveAnimInstance();
	if (!AnimInstance)
	{
		ActiveOverlayMontage = nullptr;
		return;
	}

	if (ActiveOverlayMontage)
	{
		FOnMontageEnded EmptyDelegate;
		AnimInstance->Montage_SetEndDelegate(EmptyDelegate, ActiveOverlayMontage);
		AnimInstance->Montage_Stop(BlendOutTime, ActiveOverlayMontage);
	}

	AnimInstance->StopSlotAnimation(BlendOutTime, OverlaySlotName);
	ActiveOverlayMontage = nullptr;
}

void UProjectEmoteComponent::HandleOwnerAnyDamage(AActor* DamagedActor, const float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	CancelEmoteForDamage(Damage);
}

void UProjectEmoteComponent::HandleProjectDamageApplied(AActor* SourceActor, FName DamageType, const float RequestedDamage, const float AppliedDamage, const float RemainingValue, const bool bKilledTarget)
{
	CancelEmoteForDamage(AppliedDamage);
}

void UProjectEmoteComponent::HandleProjectCombatAttributeChanged(const FName AttributeName, const float OldValue, const float NewValue, const float MaxValue)
{
	if (!BoundCombatAttributeComponent || AttributeName != BoundCombatAttributeComponent->HealthAttributeName || NewValue >= OldValue)
	{
		return;
	}

	CancelEmoteForDamage(OldValue - NewValue);
}

void UProjectEmoteComponent::HandleActiveMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveOverlayMontage || ActivePlaybackMode != EProjectEmotePlaybackMode::PlayOnce || ActiveInteractionId == NAME_None)
	{
		return;
	}

	ActiveOverlayMontage = nullptr;
	StopEmote();
}

void UProjectEmoteComponent::PlayFadeToBlack() const
{
	if (const APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			CameraManager->StartCameraFade(0.0f, 1.0f, FadeToBlackDuration, FLinearColor::Black, false, true);
		}
	}
}

void UProjectEmoteComponent::PlayFadeFromBlack() const
{
	if (const APlayerController* PlayerController = ResolveOwningPlayerController())
	{
		if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			CameraManager->StartCameraFade(1.0f, 0.0f, FadeFromBlackDuration, FLinearColor::Black, false, false);
		}
	}
}

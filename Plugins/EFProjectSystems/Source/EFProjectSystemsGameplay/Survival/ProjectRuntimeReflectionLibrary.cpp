#include "Survival/ProjectRuntimeReflectionLibrary.h"

#include "ACFAIController.h"
#if WITH_EDITOR
#include "CharacterBackground/ProjectCharacterBackgroundSubsystem.h"
#endif
#include "Characters/ProjectEnemyTargetInfoComponent.h"
#include "Characters/ProjectTargetingFixComponent.h"
#include "Components/ACFGroupAIComponent.h"
#include "Components/ACFThreatManagerComponent.h"
#include "Debug/ProjectGameplayDebugSubsystem.h"
#include "EFCharacterCreationSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EFProceduralRuntimeSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Intimacy/ProjectIntimacySubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Locomotion/ProjectEmoteComponent.h"
#include "Locomotion/ProjectLocomotionOverrideComponent.h"
#include "Misc/PackageName.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"
#include "InnerDoctrine/ProjectInnerDoctrineMenuSubsystem.h"
#include "InnerDoctrine/ProjectInnerDoctrineWidget.h"
#include "Survival/ProjectSurvivalNeedsSubsystem.h"
#include "TattooShop/ProjectDefaultTattooSkinnedDecalSubsystem.h"
#include "TattooShop/ProjectTattooShopInputSubsystem.h"
#include "UI/ProjectActivityFeedSubsystem.h"
#include "UI/ProjectEmoteSubsystem.h"
#include "UObject/FieldIterator.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/FieldIterator.h"
#include "UObject/UObjectGlobals.h"

namespace ProjectRuntimeReflectionLibraryPrivate
{
	static UFunction* FindCallableFunction(UObject* Target, const FName FunctionName)
	{
		if (!Target || FunctionName.IsNone())
		{
			return nullptr;
		}

		return Target->FindFunction(FunctionName);
	}

	static FProperty* FindSingleInputProperty(UFunction* Function)
	{
		FProperty* Result = nullptr;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}

			if (Result)
			{
				return nullptr;
			}

			Result = Property;
		}

		return Result;
	}

	static FProperty* FindReturnProperty(UFunction* Function)
	{
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return Property;
			}
		}

		return nullptr;
	}

	static int32 CountInputProperties(UFunction* Function)
	{
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				++Count;
			}
		}

		return Count;
	}

	static UWorld* ResolveWorld(UObject* WorldContextObject)
	{
		if (UWorld* World = Cast<UWorld>(WorldContextObject))
		{
			return World;
		}

		return WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	}

	static UClass* TryLoadClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* ExistingClass = FindObject<UClass>(nullptr, *ClassPath))
		{
			return ExistingClass;
		}

		return LoadObject<UClass>(nullptr, *ClassPath);
	}

	static UClass* ResolveActorClassByPath(const FString& ActorClassPath)
	{
		const FString TrimmedPath = ActorClassPath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* DirectClass = TryLoadClass(TrimmedPath))
		{
			return DirectClass->IsChildOf(AActor::StaticClass()) ? DirectClass : nullptr;
		}

		if (UBlueprint* BlueprintAsset = LoadObject<UBlueprint>(nullptr, *TrimmedPath))
		{
			if (UClass* GeneratedClass = BlueprintAsset->GeneratedClass)
			{
				return GeneratedClass->IsChildOf(AActor::StaticClass()) ? GeneratedClass : nullptr;
			}
		}

		if (!TrimmedPath.StartsWith(TEXT("/Game/")))
		{
			return nullptr;
		}

		FString PackagePath;
		FString ObjectName;
		if (!TrimmedPath.Split(TEXT("."), &PackagePath, &ObjectName))
		{
			PackagePath = TrimmedPath;
			ObjectName = FPackageName::GetLongPackageAssetName(TrimmedPath);
		}

		if (ObjectName.IsEmpty())
		{
			return nullptr;
		}

		if (!ObjectName.EndsWith(TEXT("_C")))
		{
			ObjectName += TEXT("_C");
		}

		const FString GeneratedClassPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *ObjectName);
		if (UClass* GeneratedClass = TryLoadClass(GeneratedClassPath))
		{
			return GeneratedClass->IsChildOf(AActor::StaticClass()) ? GeneratedClass : nullptr;
		}

		return nullptr;
	}
}

bool UProjectRuntimeReflectionLibrary::InvokeNoArgFunction(UObject* Target, FName FunctionName)
{
	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	if (Function->NumParms != 0 || Function->ParmsSize != 0)
	{
		return false;
	}

	Target->ProcessEvent(Function, nullptr);
	return true;
}

bool UProjectRuntimeReflectionLibrary::InvokeInt32Function(UObject* Target, FName FunctionName, int32 Int32Value)
{
	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	FProperty* InputProperty = ProjectRuntimeReflectionLibraryPrivate::FindSingleInputProperty(Function);
	if (!InputProperty)
	{
		return false;
	}

	FIntProperty* IntProperty = CastField<FIntProperty>(InputProperty);
	if (!IntProperty)
	{
		return false;
	}

	FStructOnScope Parms(Function);
	IntProperty->SetPropertyValue_InContainer(Parms.GetStructMemory(), Int32Value);
	Target->ProcessEvent(Function, Parms.GetStructMemory());
	return true;
}

bool UProjectRuntimeReflectionLibrary::InvokeObjectArgFunction(UObject* Target, FName FunctionName, UObject* ObjectValue)
{
	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	FProperty* InputProperty = ProjectRuntimeReflectionLibraryPrivate::FindSingleInputProperty(Function);
	FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InputProperty);
	if (!ObjectProperty)
	{
		return false;
	}

	if (ObjectValue && !ObjectValue->IsA(ObjectProperty->PropertyClass))
	{
		return false;
	}

	FStructOnScope Parms(Function);
	ObjectProperty->SetObjectPropertyValue_InContainer(Parms.GetStructMemory(), ObjectValue);
	Target->ProcessEvent(Function, Parms.GetStructMemory());
	return true;
}

bool UProjectRuntimeReflectionLibrary::InvokeBoolReturnFunction(UObject* Target, FName FunctionName, bool& OutReturnValue)
{
	OutReturnValue = false;

	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	FBoolProperty* ReturnProperty = CastField<FBoolProperty>(ProjectRuntimeReflectionLibraryPrivate::FindReturnProperty(Function));
	if (!ReturnProperty || ProjectRuntimeReflectionLibraryPrivate::CountInputProperties(Function) != 0)
	{
		return false;
	}

	FStructOnScope Parms(Function);
	Target->ProcessEvent(Function, Parms.GetStructMemory());
	OutReturnValue = ReturnProperty->GetPropertyValue_InContainer(Parms.GetStructMemory());
	return true;
}

bool UProjectRuntimeReflectionLibrary::InvokeInt32ReturnFunction(UObject* Target, FName FunctionName, int32& OutReturnValue)
{
	OutReturnValue = 0;

	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	FIntProperty* ReturnProperty = CastField<FIntProperty>(ProjectRuntimeReflectionLibraryPrivate::FindReturnProperty(Function));
	if (!ReturnProperty || ProjectRuntimeReflectionLibraryPrivate::CountInputProperties(Function) != 0)
	{
		return false;
	}

	FStructOnScope Parms(Function);
	Target->ProcessEvent(Function, Parms.GetStructMemory());
	OutReturnValue = ReturnProperty->GetPropertyValue_InContainer(Parms.GetStructMemory());
	return true;
}

bool UProjectRuntimeReflectionLibrary::InvokeObjectReturnFunction(
	UObject* Target,
	FName FunctionName,
	TSubclassOf<UObject> ExpectedClass,
	UObject*& OutReturnValue)
{
	OutReturnValue = nullptr;

	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	FObjectPropertyBase* ReturnProperty = CastField<FObjectPropertyBase>(ProjectRuntimeReflectionLibraryPrivate::FindReturnProperty(Function));
	if (!ReturnProperty || ProjectRuntimeReflectionLibraryPrivate::CountInputProperties(Function) != 0)
	{
		return false;
	}

	FStructOnScope Parms(Function);
	Target->ProcessEvent(Function, Parms.GetStructMemory());

	UObject* ReturnObject = ReturnProperty->GetObjectPropertyValue_InContainer(Parms.GetStructMemory());
	if (ReturnObject && ExpectedClass && !ReturnObject->IsA(ExpectedClass))
	{
		return false;
	}

	OutReturnValue = ReturnObject;
	return true;
}

bool UProjectRuntimeReflectionLibrary::InvokeObjectArgObjectReturnFunction(
	UObject* Target,
	FName FunctionName,
	UObject* ObjectValue,
	TSubclassOf<UObject> ExpectedClass,
	UObject*& OutReturnValue)
{
	OutReturnValue = nullptr;

	UFunction* Function = ProjectRuntimeReflectionLibraryPrivate::FindCallableFunction(Target, FunctionName);
	if (!Function)
	{
		return false;
	}

	FObjectPropertyBase* InputProperty = CastField<FObjectPropertyBase>(ProjectRuntimeReflectionLibraryPrivate::FindSingleInputProperty(Function));
	FObjectPropertyBase* ReturnProperty = CastField<FObjectPropertyBase>(ProjectRuntimeReflectionLibraryPrivate::FindReturnProperty(Function));
	if (!InputProperty || !ReturnProperty || ProjectRuntimeReflectionLibraryPrivate::CountInputProperties(Function) != 1)
	{
		return false;
	}

	if (ObjectValue && !ObjectValue->IsA(InputProperty->PropertyClass))
	{
		return false;
	}

	FStructOnScope Parms(Function);
	InputProperty->SetObjectPropertyValue_InContainer(Parms.GetStructMemory(), ObjectValue);
	Target->ProcessEvent(Function, Parms.GetStructMemory());

	UObject* ReturnObject = ReturnProperty->GetObjectPropertyValue_InContainer(Parms.GetStructMemory());
	if (ReturnObject && ExpectedClass && !ReturnObject->IsA(ExpectedClass))
	{
		return false;
	}

	OutReturnValue = ReturnObject;
	return true;
}

TArray<FName> UProjectRuntimeReflectionLibrary::GetAvailableFunctionNames(UObject* Target, FString NameContains)
{
	TArray<FName> FunctionNames;
	if (!Target)
	{
		return FunctionNames;
	}

	const FString NameFilter = NameContains.TrimStartAndEnd().ToLower();
	for (TFieldIterator<UFunction> It(Target->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const UFunction* Function = *It;
		if (!Function)
		{
			continue;
		}

		if (!NameFilter.IsEmpty() && !Function->GetName().ToLower().Contains(NameFilter))
		{
			continue;
		}

		FunctionNames.Add(Function->GetFName());
	}

	FunctionNames.Sort(FNameLexicalLess());
	return FunctionNames;
}

UProjectEmoteSubsystem* UProjectRuntimeReflectionLibrary::GetProjectEmoteSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectEmoteSubsystem>();
	}

	return nullptr;
}

UProjectGameplayDebugSubsystem* UProjectRuntimeReflectionLibrary::GetProjectGameplayDebugSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectGameplayDebugSubsystem>();
	}

	return nullptr;
}

UProjectActivityFeedSubsystem* UProjectRuntimeReflectionLibrary::GetProjectActivityFeedSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectActivityFeedSubsystem>();
	}

	return nullptr;
}

UProjectIntimacySubsystem* UProjectRuntimeReflectionLibrary::GetProjectIntimacySubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectIntimacySubsystem>();
	}

	return nullptr;
}

UProjectInnerDoctrineMenuSubsystem* UProjectRuntimeReflectionLibrary::GetProjectInnerDoctrineMenuSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectInnerDoctrineMenuSubsystem>();
	}

	return nullptr;
}

UProjectSurvivalNeedsSubsystem* UProjectRuntimeReflectionLibrary::GetProjectSurvivalNeedsSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectSurvivalNeedsSubsystem>();
	}

	return nullptr;
}

UEFCharacterCreationSubsystem* UProjectRuntimeReflectionLibrary::GetEFCharacterCreationSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UEFCharacterCreationSubsystem>();
		}
	}

	return nullptr;
}

UProjectTattooShopInputSubsystem* UProjectRuntimeReflectionLibrary::GetProjectTattooShopInputSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectTattooShopInputSubsystem>();
	}

	return nullptr;
}

UProjectDefaultTattooSkinnedDecalSubsystem* UProjectRuntimeReflectionLibrary::GetProjectDefaultTattooSkinnedDecalSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		return World->GetSubsystem<UProjectDefaultTattooSkinnedDecalSubsystem>();
	}

	return nullptr;
}

#if WITH_EDITOR
UProjectCharacterBackgroundSubsystem* UProjectRuntimeReflectionLibrary::GetProjectCharacterBackgroundSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UProjectCharacterBackgroundSubsystem>();
		}
	}

	return nullptr;
}
#endif

UEFProceduralRuntimeSubsystem* UProjectRuntimeReflectionLibrary::GetProceduralRuntimeSubsystem(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>();
		}
	}

	return nullptr;
}

UProjectTargetingFixComponent* UProjectRuntimeReflectionLibrary::GetProjectTargetingFixComponent(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->FindComponentByClass<UProjectTargetingFixComponent>();
			}
		}
	}

	return nullptr;
}

UProjectLocomotionOverrideComponent* UProjectRuntimeReflectionLibrary::GetProjectLocomotionOverrideComponent(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->FindComponentByClass<UProjectLocomotionOverrideComponent>();
			}
		}
	}

	return nullptr;
}

UProjectInnerDoctrineComponent* UProjectRuntimeReflectionLibrary::GetProjectInnerDoctrineComponent(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->FindComponentByClass<UProjectInnerDoctrineComponent>();
			}
		}
	}

	return nullptr;
}

UProjectInnerDoctrineWidget* UProjectRuntimeReflectionLibrary::GetProjectInnerDoctrineWidget(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (UProjectSurvivalNeedsSubsystem* NeedsSubsystem = World->GetSubsystem<UProjectSurvivalNeedsSubsystem>())
		{
			return NeedsSubsystem->GetTrackedInnerDoctrineWidget();
		}
	}

	return nullptr;
}

UProjectEmoteComponent* UProjectRuntimeReflectionLibrary::GetProjectEmoteComponent(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->FindComponentByClass<UProjectEmoteComponent>();
			}
		}
	}

	return nullptr;
}

UProjectEnemyTargetInfoComponent* UProjectRuntimeReflectionLibrary::GetProjectEnemyTargetInfoComponent(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UProjectEnemyTargetInfoComponent>() : nullptr;
}

AActor* UProjectRuntimeReflectionLibrary::GetACFAIControllerTarget(AController* Controller)
{
	if (AACFAIController* ACFController = Cast<AACFAIController>(Controller))
	{
		return ACFController->GetTarget();
	}

	return nullptr;
}

AActor* UProjectRuntimeReflectionLibrary::GetACFAIControllerBlackboardTarget(AController* Controller)
{
	if (AACFAIController* ACFController = Cast<AACFAIController>(Controller))
	{
		return ACFController->GetTargetActorBK();
	}

	return nullptr;
}

UObject* UProjectRuntimeReflectionLibrary::GetACFAIControllerGroup(AController* Controller)
{
	if (AACFAIController* ACFController = Cast<AACFAIController>(Controller))
	{
		return ACFController->GetGroup();
	}

	return nullptr;
}

bool UProjectRuntimeReflectionLibrary::IsACFAIControllerThreateningActor(AController* Controller, AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (AACFAIController* ACFController = Cast<AACFAIController>(Controller))
	{
		if (UACFThreatManagerComponent* ThreatManager = ACFController->GetThreatManager())
		{
			return ThreatManager->IsThreatening(Actor);
		}
	}

	return false;
}

bool UProjectRuntimeReflectionLibrary::SetACFAIControllerTarget(AController* Controller, AActor* Actor)
{
	if (AACFAIController* ACFController = Cast<AACFAIController>(Controller))
	{
		if (Actor)
		{
			if (UACFThreatManagerComponent* ThreatManager = ACFController->GetThreatManager())
			{
				if (!ThreatManager->IsThreatening(Actor))
				{
					const float DefaultThreat = ThreatManager->GetDefaultThreatForActor(Actor);
					ThreatManager->AddThreat(Actor, DefaultThreat > 0.0f ? DefaultThreat : 1.0f);
				}
			}
		}
		ACFController->SetTarget(Actor);
		return true;
	}

	return false;
}

bool UProjectRuntimeReflectionLibrary::ClearACFAIControllerAwarenessOfActor(AController* Controller, AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	AACFAIController* ACFController = Cast<AACFAIController>(Controller);
	if (!ACFController)
	{
		return false;
	}

	bool bTouched = false;
	UACFThreatManagerComponent* ThreatManager = ACFController->GetThreatManager();

	if (ACFController->GetTarget() == Actor || ACFController->GetTargetActorBK() == Actor)
	{
		ACFController->SetTarget(nullptr);
		bTouched = true;

		if (ThreatManager)
		{
			if (AActor* NextTarget = ThreatManager->GetActorWithHigherThreat())
			{
				if (NextTarget != Actor)
				{
					ACFController->SetTarget(NextTarget);
				}
				else
				{
					ThreatManager->RemoveThreatening(Actor);
					ACFController->ResetToDefaultState();
				}
			}
			else
			{
				ACFController->ResetToDefaultState();
			}
		}
		else
		{
			ACFController->ResetToDefaultState();
		}
	}

	if (ThreatManager && ThreatManager->IsThreatening(Actor))
	{
		ThreatManager->RemoveThreatening(Actor);
		bTouched = true;
	}

	return bTouched;
}

AActor* UProjectRuntimeReflectionLibrary::SpawnActorByClassPath(
	UObject* WorldContextObject,
	const FString& ActorClassPath,
	FVector Location,
	FRotator Rotation)
{
	UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject);
	UClass* ActorClass = ProjectRuntimeReflectionLibraryPrivate::ResolveActorClassByPath(ActorClassPath);
	if (!World || !ActorClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.bNoFail = false;
	return World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParameters);
}

bool UProjectRuntimeReflectionLibrary::IsProceduralDungeonRuntimeReady(UObject* WorldContextObject)
{
	if (UWorld* World = ProjectRuntimeReflectionLibraryPrivate::ResolveWorld(WorldContextObject))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UEFProceduralRuntimeSubsystem* RuntimeSubsystem = GameInstance->GetSubsystem<UEFProceduralRuntimeSubsystem>())
			{
				return RuntimeSubsystem->IsDungeonRuntimeReady(World);
			}
		}
	}

	return false;
}

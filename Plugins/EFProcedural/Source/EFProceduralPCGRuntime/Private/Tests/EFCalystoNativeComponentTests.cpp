#if WITH_DEV_AUTOMATION_TESTS

#include "Calysto/EFCalystoNativeAdapter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "PCGComponent.h"

namespace EFCalystoNativeComponentTestsPrivate
{
	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.RequiresHitProxies(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
				true, ERHIFeatureLevel::Num, &Initialization);
		}
		~FTestWorld() { if (World) { World->DestroyWorld(false); World->MarkObjectsPendingKill(); } }
	};

	static UPCGComponent* Add(AActor* Actor, FName Name, bool bEditorOnly)
	{
		UPCGComponent* Component = NewObject<UPCGComponent>(Actor, Name, RF_Transient);
		Component->bIsEditorOnly = bEditorOnly;
		Component->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		Actor->AddInstanceComponent(Component);
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEFCalystoNativeRuntimeComponentSelectionTest,
	"NoShellForWinter.CalystoDungeon.Director.Native.RuntimeComponentSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEFCalystoNativeRuntimeComponentSelectionTest::RunTest(const FString&)
{
	using namespace EFCalystoNativeComponentTestsPrivate;
	FTestWorld Fixture;
	if (!TestNotNull(TEXT("Isolated component fixture world exists"), Fixture.World)) return false;
	AActor* Actor = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Fixture actor exists"), Actor)) return false;
	const auto DefaultTrigger = GetDefault<UPCGComponent>()->GenerationTrigger;
	const bool bDefaultActivated = GetDefault<UPCGComponent>()->bActivated;
	FString Error;
	TestNull(TEXT("Missing runtime component fails closed"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	TestTrue(TEXT("Missing-component diagnostic includes actual count"), Error.Contains(TEXT("components=0 runtime=0")));
	// Names intentionally contradict roles: selection uses the engine's editor-only flag.
	UPCGComponent* Auxiliary = Add(Actor, TEXT("PCG_Runtime_Label"), true);
	TestNull(TEXT("An editor-only auxiliary is never selected"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	UPCGComponent* Runtime = Add(Actor, TEXT("PCG_Editor_Label"), false);
	TestEqual(TEXT("PIE auxiliary does not make the sole runtime PCG ambiguous"),
		FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error), Runtime);
	TestTrue(TEXT("Read-only selection preserves auxiliary activation"), Auxiliary->bActivated);
	TestEqual(TEXT("Read-only selection preserves auxiliary trigger"), Auxiliary->GenerationTrigger, EPCGComponentGenerationTrigger::GenerateOnDemand);
	Runtime->bGenerated = true;
	TestNull(TEXT("Previously generated runtime work cannot enter a new attempt"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	TestTrue(TEXT("Failure includes runtime state and both components"), Error.Contains(TEXT("components=2 runtime=1 editorOnly=1"))
		&& Error.Contains(TEXT("generated=1")) && Error.Contains(TEXT("generating=0 cleaning=0")));
	Runtime->bGenerated = false;
	Auxiliary->bGenerated = true;
	TestNull(TEXT("An auxiliary with generated work fails preflight"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	Auxiliary->bGenerated = false;
	Auxiliary->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;
	TestNull(TEXT("A retained auxiliary cannot automatically generate during runtime"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	Auxiliary->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
	Runtime->bActivated = false;
	TestNull(TEXT("An inactive runtime component cannot schedule the request"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	Runtime->bActivated = true;
	UPCGComponent* Duplicate = Add(Actor, TEXT("SecondRuntime"), false);
	TestNull(TEXT("Two runtime components remain ambiguous"), FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error));
	TestTrue(TEXT("Ambiguity reports the actual runtime count"), Error.Contains(TEXT("runtime=2")));
	Duplicate->DestroyComponent();
	Auxiliary->DestroyComponent();
	TestEqual(TEXT("Cooked shape with no editor-only auxiliary uses the same runtime selection"),
		FEFCalystoNativeAdapter::FindIdleRuntimeComponent(Actor, Error), Runtime);
	TestFalse(TEXT("Selection never starts native generation"), Runtime->IsGenerating() || Runtime->bGenerated);
	TestEqual(TEXT("Class default trigger is unchanged"), GetDefault<UPCGComponent>()->GenerationTrigger, DefaultTrigger);
	TestEqual(TEXT("Class default activation is unchanged"), bool(GetDefault<UPCGComponent>()->bActivated), bDefaultActivated);
	return true;
}
#endif

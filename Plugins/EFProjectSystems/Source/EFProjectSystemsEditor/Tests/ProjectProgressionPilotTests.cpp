#include "Progression/ProjectDoctrineAttributeSet.h"
#include "Progression/ProjectProgressionBridge.h"
#include "ACFGASStatisticsComponent.h"
#include "ACFPrimaryAttributeSet.h"
#include "ACFAttributeSet.h"
#include "ACFStatisticsSet.h"
#include "GameplayEffect.h"
#include "ACFGASTypes.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace ProgressionPilot
{
struct FFixture
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 AActor* Owner = nullptr;
 UAbilitySystemComponent* ASC = nullptr;
 UACFGASStatisticsComponent* Stats = nullptr;
 UCurveFloat* Curve = NewObject<UCurveFloat>();
 FFixture()
 {
  // UE 5.8 CreateWorld already calls InitializeNewWorld.
  Owner = World->SpawnActor<AActor>();
  ASC = NewObject<UAbilitySystemComponent>(Owner);
  Stats = NewObject<UACFGASStatisticsComponent>(Owner);
  Owner->AddInstanceComponent(ASC);
  Owner->AddInstanceComponent(Stats);
  ASC->RegisterComponent();
  Stats->RegisterComponent();
  ASC->InitAbilityActorInfo(Owner, Owner);
  ASC->AddAttributeSetSubobject(NewObject<UACFPrimaryAttributeSet>(Owner));
  ASC->AddAttributeSetSubobject(NewObject<UProjectDoctrineAttributeSet>(Owner));
  Stats->SetLevelingType(ELevelingType::EAssignPerksManually);
  Curve->FloatCurve.AddKey(1, 100);
  Curve->FloatCurve.AddKey(100, 100);
  Stats->SetExpForNextLevelCurve(Curve);
  Stats->ForceSetLevel(1);
  Stats->InitializeAttributeSet();
 }
 ~FFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionPurchasePilot,
 "NoShellForWinter.ProgressionPilot.NativePurchase", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProgressionPurchasePilot::RunTest(const FString&)
{
 using namespace ProgressionPilot;
 FFixture F;
 auto W = UProjectDoctrineAttributeSet::GetWillpowerAttribute();
 TestFalse(TEXT("No points cannot buy"), FProjectProgressionBridge::Purchase(F.Stats, W));
 TestTrue(TEXT("Native XP awards first point"), FProjectProgressionBridge::GrantExperience(F.Stats, 100));
 TestEqual(TEXT("Native level"), F.Stats->GetCurrentLevel(), 2);
 TestEqual(TEXT("Native perks"), F.Stats->GetAvailablePerks(), 1);
 TestTrue(TEXT("Willpower uses native purchase"), FProjectProgressionBridge::Purchase(F.Stats, W));
 TestEqual(TEXT("Willpower base"), F.ASC->GetNumericAttributeBase(W), 1.f);
 TestEqual(TEXT("Same wallet consumed"), F.Stats->GetAvailablePerks(), 0);
 F.Stats->SetAvailablePerks(20);
 for (auto A : FProjectProgressionBridge::GetPurchasableAttributes())
  TestTrue(*FString::Printf(TEXT("Purchase %s"), *A.GetName()), FProjectProgressionBridge::Purchase(F.Stats, A));
 TestEqual(TEXT("Twelve attributes share one wallet"), F.Stats->GetAvailablePerks(), 8);
 for (int32 Invalid : {0, -1, MAX_int32})
  TestFalse(TEXT("Invalid amounts rejected"), FProjectProgressionBridge::Purchase(F.Stats, W, Invalid));
 TestFalse(TEXT("Invalid attribute rejected"), FProjectProgressionBridge::Purchase(F.Stats, FGameplayAttribute()));
 F.ASC->SetNumericAttributeBase(W, 100.f);
 TestFalse(TEXT("Cap does not spend"), FProjectProgressionBridge::Purchase(F.Stats, W));
 TestEqual(TEXT("No points lost on rejection"), F.Stats->GetAvailablePerks(), 8);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionXPPilot,
 "NoShellForWinter.ProgressionPilot.ExperienceGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProgressionXPPilot::RunTest(const FString&)
{
 ProgressionPilot::FFixture F;
 TestFalse(TEXT("Negative XP rejected"), FProjectProgressionBridge::GrantExperience(F.Stats, -1));
 TestTrue(TEXT("Large reward stops at configured cap"), FProjectProgressionBridge::GrantExperience(F.Stats, 100000, 10));
 TestEqual(TEXT("Cap respected"), F.Stats->GetCurrentLevel(), 10);
 TestEqual(TEXT("Exactly nine points"), F.Stats->GetAvailablePerks(), 9);
 TestEqual(TEXT("No overflow bank"), F.Stats->GetCurrentExp(), 0);
 TestFalse(TEXT("At cap reward rejected"), FProjectProgressionBridge::GrantExperience(F.Stats, 1, 10));
 F.Stats->ForceSetLevel(1);
 F.Stats->SetExpForNextLevelCurve(nullptr);
 TestFalse(TEXT("Missing curve fails closed"), FProjectProgressionBridge::GrantExperience(F.Stats, 1));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionSavePilot,
 "NoShellForWinter.ProgressionPilot.NativeSaveRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProgressionSavePilot::RunTest(const FString&)
{
 ProgressionPilot::FFixture Before;
 // Inject a transient equivalent of an extended serialization table. No config or player saves touched.
 UDataTable* Table = NewObject<UDataTable>();
 Table->RowStruct = FAttributeSerializeKeys::StaticStruct();
 const auto Attributes = FProjectProgressionBridge::GetPurchasableAttributes();
 for (auto A : Attributes)
 {
  FAttributeSerializeKeys Row;
  Row.Attribute = A;
  Table->AddRow(FName(*A.GetName()), Row);
 }
 FObjectPropertyBase* Mapping = FindFProperty<FObjectPropertyBase>(UACFGASStatisticsComponent::StaticClass(), TEXT("allAttributes"));
 if (!TestNotNull(TEXT("Native serialization table contract"), Mapping)) return false;
 Mapping->SetObjectPropertyValue_InContainer(Before.Stats, Table);
 FProjectProgressionBridge::GrantExperience(Before.Stats, 1234);
 for (auto A : Attributes) FProjectProgressionBridge::Purchase(Before.Stats, A);
 Before.Stats->OnComponentSaved();
 TArray<uint8> Bytes;
 FMemoryWriter Writer(Bytes);
 FObjectAndNameAsStringProxyArchive Save(Writer, false);
 Save.ArIsSaveGame = true;
 Before.Stats->Serialize(Save);
 ProgressionPilot::FFixture After;
 Mapping->SetObjectPropertyValue_InContainer(After.Stats, Table);
 FMemoryReader Reader(Bytes);
 FObjectAndNameAsStringProxyArchive Load(Reader, true);
 Load.ArIsSaveGame = true;
 After.Stats->Serialize(Load);
 After.Stats->OnComponentLoaded();
 TestEqual(TEXT("Native level restored"), After.Stats->GetCurrentLevel(), Before.Stats->GetCurrentLevel());
 TestEqual(TEXT("Native XP restored"), After.Stats->GetCurrentExp(), Before.Stats->GetCurrentExp());
 TestEqual(TEXT("Native wallet restored"), After.Stats->GetAvailablePerks(), Before.Stats->GetAvailablePerks());
 for (auto A : Attributes)
  TestEqual(*FString::Printf(TEXT("Restored %s"), *A.GetName()), After.ASC->GetNumericAttributeBase(A), Before.ASC->GetNumericAttributeBase(A));
 After.Stats->OnComponentLoaded();
 TestEqual(TEXT("Repeated load never stacks"), After.ASC->GetNumericAttributeBase(UProjectDoctrineAttributeSet::GetWillpowerAttribute()), 1.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProgressionCurvePilot,
 "NoShellForWinter.ProgressionPilot.CurveAdapterParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProgressionCurvePilot::RunTest(const FString&)
{
 ProgressionPilot::FFixture F;
 F.ASC->AddAttributeSetSubobject(NewObject<UACFAttributeSet>(F.Owner));
 F.ASC->AddAttributeSetSubobject(NewObject<UACFStatisticsSet>(F.Owner));
 AddExpectedError(TEXT("FScalableFloat has no CurveTable or DataRegistry"), EAutomationExpectedErrorFlags::Contains, 0);
 for (const FString Suffix : {FString(TEXT("AttDependencies_GE")), FString(TEXT("StatInit_GE"))})
 {
  UClass* Native = LoadClass<UGameplayEffect>(nullptr, *FString::Printf(TEXT("/AscentCombatFramework/GASRuntime/GameplayEffects/ACF_%s.ACF_%s_C"), *Suffix, *Suffix));
  UClass* Adapter = LoadClass<UGameplayEffect>(nullptr, *FString::Printf(TEXT("/Game/_Game/Data/Progression/GE_Project%s.GE_Project%s_C"), *Suffix, *Suffix));
  if (!TestNotNull(TEXT("Native effect"), Native) || !TestNotNull(TEXT("Project adapter"), Adapter)) return false;
  for (int32 Rank : {1, 10, 50, 100})
  {
   for (auto Attribute : FProjectProgressionBridge::GetPurchasableAttributes())
    F.ASC->SetNumericAttributeBase(Attribute, float(Rank));
   auto A = F.ASC->MakeOutgoingSpec(Native, float(Rank), F.ASC->MakeEffectContext());
   auto B = F.ASC->MakeOutgoingSpec(Adapter, float(Rank), F.ASC->MakeEffectContext());
   if (!TestTrue(TEXT("Valid specs"), A.IsValid() && B.IsValid())) return false;
   A.Data->CaptureAttributeDataFromTarget(F.ASC);
   B.Data->CaptureAttributeDataFromTarget(F.ASC);
   A.Data->CalculateModifierMagnitudes();
   B.Data->CalculateModifierMagnitudes();
   const int32 Count = A.Data->Def->Modifiers.Num();
   TestEqual(TEXT("Modifier count preserved"), B.Data->Def->Modifiers.Num(), Count);
   for (int32 Index = 0; Index < FMath::Min(Count, B.Data->Def->Modifiers.Num()); ++Index)
    TestEqual(*FString::Printf(TEXT("%s rank %d modifier %d"), *Suffix, Rank, Index), B.Data->GetModifierMagnitude(Index), A.Data->GetModifierMagnitude(Index));
  }
 }
 return true;
}
#endif

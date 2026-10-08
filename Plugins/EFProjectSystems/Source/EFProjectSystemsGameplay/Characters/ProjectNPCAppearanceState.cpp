#include "Characters/ProjectNPCAppearanceState.h"
#include "Characters/ProjectEnemyVisualVariationSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Crc.h"
#include "Async/Async.h"

AProjectNPCAppearanceState::AProjectNPCAppearanceState() { PrimaryActorTick.bCanEverTick = false; }
AProjectNPCAppearanceState* AProjectNPCAppearanceState::Ensure(UWorld* World)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client) return nullptr;
	for (TActorIterator<AProjectNPCAppearanceState> It(World); It; ++It) return *It;
	FActorSpawnParameters Params; Params.Name = TEXT("ProjectNPCAppearanceState");
	return World->SpawnActor<AProjectNPCAppearanceState>(StaticClass(), FTransform::Identity, Params);
}
void AProjectNPCAppearanceState::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Travel = GetGameInstance()->GetSubsystem<UProjectNPCAppearanceTravel>()) Records = Travel->Records;
}
void AProjectNPCAppearanceState::Publish()
{
	if (GetGameInstance())
		if (auto* Travel = GetGameInstance()->GetSubsystem<UProjectNPCAppearanceTravel>()) Travel->Records = Records;
}
void AProjectNPCAppearanceState::OnLoaded_Implementation()
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [Weak = TWeakObjectPtr<AProjectNPCAppearanceState>(this)] { if (Weak.IsValid()) Weak->OnLoaded_Implementation(); });
		return;
	}
	Publish();
	if (auto* Variation = GetWorld()->GetSubsystem<UProjectEnemyVisualVariationSubsystem>()) Variation->RequestAppearanceRestore();
}
void AProjectNPCAppearanceState::EndPlay(EEndPlayReason::Type Reason)
{
	Publish();
	Super::EndPlay(Reason);
}
FString AProjectNPCAppearanceState::IdentityFor(APawn* Pawn)
{
	if (!Pawn) return FString();
	const FString Prefix = TEXT("EF.AppearanceId.");
	for (FName Tag : Pawn->Tags)
		if (Tag.ToString().StartsWith(Prefix)) return Tag.ToString();
	if (Pawn->HasAnyFlags(RF_WasLoaded))
	{
		FString Path = Pawn->GetPathName();
		if (Pawn->GetWorld() && !Pawn->GetWorld()->StreamingLevelsPrefix.IsEmpty()) Path.ReplaceInline(*Pawn->GetWorld()->StreamingLevelsPrefix, TEXT(""));
		return TEXT("Placed:") + Path;
	}
	const FString Id = Prefix + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Pawn->Tags.Add(FName(*Id));
	return Id;
}
int32 AProjectNPCAppearanceState::SeedFor(const FString& Identity)
{
	return static_cast<int32>(FCrc::StrCrc32(*Identity));
}

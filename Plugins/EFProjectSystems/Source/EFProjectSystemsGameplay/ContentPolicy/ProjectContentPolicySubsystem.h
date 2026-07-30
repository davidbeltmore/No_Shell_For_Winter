#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "ContentPolicy/ProjectContentPolicyTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ProjectContentPolicySubsystem.generated.h"

class IProjectOptionalMatureContentProvider;
class UProjectInnerDoctrineComponent;
enum class EProjectDoctrineAttribute : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FProjectContentPolicyChangedSignature,
	FProjectContentPolicySnapshot,
	Policy);

/**
 * Single runtime authority for optional mature content.
 *
 * Optional intimacy/private presentations unlock at Charisma 10. Mature
 * defeat remains governed by its own authoritative 10% outcome. The
 * command-line -StreamerSafe switch always wins and cannot be disabled.
 */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectContentPolicySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	FProjectContentPolicySnapshot GetPolicySnapshot() const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	FProjectContentPolicySnapshot GetPolicySnapshotForActor(AActor* Participant) const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsStreamerSafeForced() const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsMatureContentUnlocked() const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsMatureContentUnlockedForActor(AActor* Participant) const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsIntimacyAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsMatureDefeatAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsPrivateSoloPresentationAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	bool IsMatureFeatureAllowed(EProjectOptionalMatureFeature Feature) const;

	UFUNCTION(BlueprintPure, Category = "Project|Content Policy")
	static bool IsStreamerSafeCommandLineActive();

	/** Registers a project-owned C++ provider. The subsystem keeps only a weak reference. */
	bool RegisterMatureContentProvider(UObject* ProviderObject);
	void UnregisterMatureContentProvider(UObject* ProviderObject);

	bool IsMatureFeaturePresentationAvailable(EProjectOptionalMatureFeature Feature) const;
	FGuid TryBeginMaturePresentation(FProjectMaturePresentationRequest Request);
	bool CancelMaturePresentation(const FGuid& RequestId);
	void NotifyMaturePresentationEnded(const FGuid& RequestId, UObject* ProviderObject);

	UPROPERTY(BlueprintAssignable, Category = "Project|Content Policy")
	FProjectContentPolicyChangedSignature OnContentPolicyChanged;

private:
	struct FActiveMaturePresentation
	{
		TWeakObjectPtr<UObject> Provider;
		TWeakObjectPtr<AActor> PrimaryParticipant;
		TWeakObjectPtr<AActor> SecondaryParticipant;
		EProjectOptionalMatureFeature Feature = EProjectOptionalMatureFeature::IntimacySession;
	};

	UFUNCTION()
	void HandleSocialParticipantChanged(AActor* Participant);

	UFUNCTION()
	void HandleSocialParticipantUnregistered(AActor* Participant);

	UFUNCTION()
	void HandleSocialIntimacyConsentChanged(AActor* GrantingParticipant, AActor* OtherParticipant, bool bConsented);

	UFUNCTION()
	void HandleDoctrineAttributeLevelChanged(
		EProjectDoctrineAttribute Attribute,
		int32 OldLevel,
		int32 NewLevel,
		int32 NextLevelCost);

	void RefreshTrackedDoctrineComponent();
	void RefreshPolicyState();
	bool HandlePolicyTicker(float DeltaTime);
	UProjectInnerDoctrineComponent* ResolveDoctrineComponent(AActor* Participant) const;
	UProjectInnerDoctrineComponent* ResolveLocalPlayerDoctrineComponent() const;
	FProjectContentPolicySnapshot BuildPolicySnapshot(
		const UProjectInnerDoctrineComponent* DoctrineComponent) const;
	bool IsMatureFeatureAllowedForActor(
		EProjectOptionalMatureFeature Feature,
		AActor* Participant) const;
	void CancelPresentationsNoLongerAllowed();
	void CancelPresentationsWithInvalidParticipants();
	void CancelAllPresentations();
	void PruneProviders();
	bool AreParticipantsEligible(const FProjectMaturePresentationRequest& Request) const;
	static bool ArePoliciesEquivalent(
		const FProjectContentPolicySnapshot& Left,
		const FProjectContentPolicySnapshot& Right);

private:
	bool bStreamerSafeForced = false;
	bool bHasCachedPolicy = false;
	FProjectContentPolicySnapshot CachedPolicy;
	FTSTicker::FDelegateHandle PolicyTickerHandle;

	UPROPERTY(Transient)
	TObjectPtr<UProjectInnerDoctrineComponent> TrackedDoctrineComponent;

	TArray<TWeakObjectPtr<UObject>> MatureContentProviders;
	TMap<FGuid, FActiveMaturePresentation> ActiveMaturePresentations;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CharacterBackground/ProjectCharacterBackgroundTypes.h"
#include "ProjectCharacterBackgroundComponent.generated.h"

class UProjectInnerDoctrineComponent;

UCLASS(ClassGroup = (CharacterBackground), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectCharacterBackgroundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProjectCharacterBackgroundComponent();

	UFUNCTION(BlueprintCallable, Category = "Character Background")
	bool SetBackstory(FName BackstoryID);

	UFUNCTION(BlueprintCallable, Category = "Character Background")
	bool SetProfession(FName ProfessionID);

	UFUNCTION(BlueprintCallable, Category = "Character Background")
	void ClearBackground();

	UFUNCTION(BlueprintPure, Category = "Character Background")
	bool IsSelectionValid() const;

	UFUNCTION(BlueprintPure, Category = "Character Background")
	FName GetSelectedBackstoryID() const { return SelectedBackstoryID; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	FName GetSelectedProfessionID() const { return SelectedProfessionID; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	FProjectCharacterBackstoryData GetSelectedBackstoryData() const { return SelectedBackstoryData; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	FProjectCharacterProfessionData GetSelectedProfessionData() const { return SelectedProfessionData; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	TArray<FProjectDoctrineStartingLevelModifier> GetFinalStartingLevelModifiers() const { return FinalStartingLevelModifiers; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	TArray<FProjectDxpGainModifier> GetFinalGainModifiers() const { return FinalGainModifiers; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	TArray<FProjectCharacterBackstoryData> GetAvailableBackstories() const;

	UFUNCTION(BlueprintPure, Category = "Character Background")
	TArray<FProjectCharacterProfessionData> GetAvailableProfessions() const;

	UFUNCTION(BlueprintCallable, Category = "Character Background")
	void SetProfileRevision(int32 InProfileRevision);

	UFUNCTION(BlueprintPure, Category = "Character Background")
	int32 GetProfileRevision() const { return ProfileRevision; }

	UFUNCTION(BlueprintPure, Category = "Character Background")
	int32 GetAppliedProfileRevision() const { return AppliedProfileRevision; }

	UFUNCTION(BlueprintCallable, Category = "Character Background")
	bool ApplyBackgroundToInnerDoctrine();

	UFUNCTION(BlueprintCallable, Category = "Character Background")
	FProjectCharacterBackgroundSummary BuildSummary() const;

	bool ResolveBackstory(FName BackstoryID, FProjectCharacterBackstoryData& OutData) const;
	bool ResolveProfession(FName ProfessionID, FProjectCharacterProfessionData& OutData) const;

private:
	void RebuildFinalModifiers();
	UProjectInnerDoctrineComponent* ResolveInnerDoctrineComponent() const;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Background", meta = (AllowPrivateAccess = "true"))
	FName SelectedBackstoryID = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Background", meta = (AllowPrivateAccess = "true"))
	FName SelectedProfessionID = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Background", meta = (AllowPrivateAccess = "true"))
	FProjectCharacterBackstoryData SelectedBackstoryData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Background", meta = (AllowPrivateAccess = "true"))
	FProjectCharacterProfessionData SelectedProfessionData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Background", meta = (AllowPrivateAccess = "true"))
	TArray<FProjectDoctrineStartingLevelModifier> FinalStartingLevelModifiers;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Character Background", meta = (AllowPrivateAccess = "true"))
	TArray<FProjectDxpGainModifier> FinalGainModifiers;

	UPROPERTY(Transient)
	int32 ProfileRevision = 0;

	UPROPERTY(Transient)
	int32 AppliedProfileRevision = INDEX_NONE;
};

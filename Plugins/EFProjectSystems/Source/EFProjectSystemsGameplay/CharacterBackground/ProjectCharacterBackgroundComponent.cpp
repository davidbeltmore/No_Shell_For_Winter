#include "CharacterBackground/ProjectCharacterBackgroundComponent.h"

#include "CharacterBackground/ProjectCharacterBackgroundSettings.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "InnerDoctrine/ProjectInnerDoctrineComponent.h"

#define LOCTEXT_NAMESPACE "ProjectCharacterBackgroundComponent"

DEFINE_LOG_CATEGORY_STATIC(LogProjectCharacterBackground, Log, All);

namespace ProjectCharacterBackgroundComponentPrivate
{
	FProjectDoctrineStartingLevelModifier StartingLevel(const TCHAR* AttributeID, const int32 Delta)
	{
		FProjectDoctrineStartingLevelModifier Modifier;
		Modifier.AttributeID = FName(AttributeID);
		Modifier.StartingLevelDelta = Delta;
		return Modifier;
	}

	FProjectDxpGainModifier GainModifier(const TCHAR* AttributeID, const float Multiplier)
	{
		FProjectDxpGainModifier Modifier;
		Modifier.AttributeID = FName(AttributeID);
		Modifier.GainMultiplier = Multiplier;
		return Modifier;
	}

	FProjectCharacterBackstoryData MakeBackstory(
		const TCHAR* ID,
		const FText& DisplayName,
		const FText& Description,
		const TArray<FText>& Advantages,
		const TArray<FText>& Disadvantages,
		const TArray<FProjectDoctrineStartingLevelModifier>& StartingLevels)
	{
		FProjectCharacterBackstoryData Data;
		Data.BackstoryID = FName(ID);
		Data.DisplayName = DisplayName;
		Data.Description = Description;
		Data.Advantages = Advantages;
		Data.Disadvantages = Disadvantages;
		Data.StartingLevels = StartingLevels;
		return Data;
	}

	FProjectCharacterProfessionData MakeProfession(
		const TCHAR* ID,
		const FText& DisplayName,
		const FText& Description,
		const TArray<FText>& Advantages,
		const TArray<FText>& Disadvantages,
		const TArray<FProjectDxpGainModifier>& GainModifiers)
	{
		FProjectCharacterProfessionData Data;
		Data.ProfessionID = FName(ID);
		Data.DisplayName = DisplayName;
		Data.Description = Description;
		Data.Advantages = Advantages;
		Data.Disadvantages = Disadvantages;
		Data.GainModifiers = GainModifiers;
		return Data;
	}

	TArray<FProjectCharacterBackstoryData> BuildFallbackBackstories()
	{
		return {
			MakeBackstory(
				TEXT("Orphan"),
				LOCTEXT("BackstoryOrphanName", "Orphan"),
				LOCTEXT("BackstoryOrphanDescription", "A solitary childhood sharpened instinct, timing, and the talent to survive without protection."),
				{
					LOCTEXT("BackstoryOrphanAdvantageCunning", "Cunning starts at level 3."),
					LOCTEXT("BackstoryOrphanAdvantageCelerity", "Celerity starts at level 2."),
					LOCTEXT("BackstoryOrphanAdvantageFlavor", "Better early mobility and opportunistic damage.")
				},
				{
					LOCTEXT("BackstoryOrphanDisadvantageWillpower", "No starting bonus to Willpower."),
					LOCTEXT("BackstoryOrphanDisadvantageFaith", "No starting bonus to Faith.")
				},
				{
					StartingLevel(TEXT("Cunning"), 3),
					StartingLevel(TEXT("Celerity"), 2)
				}),
			MakeBackstory(
				TEXT("TragicPast"),
				LOCTEXT("BackstoryTragicPastName", "Tragic Past"),
				LOCTEXT("BackstoryTragicPastDescription", "Old pain became a dark endurance, leaving violence and suffering close to the surface."),
				{
					LOCTEXT("BackstoryTragicPastAdvantageDefensive", "Defensive starts at level 3."),
					LOCTEXT("BackstoryTragicPastAdvantageOffensive", "Offensive starts at level 2."),
					LOCTEXT("BackstoryTragicPastAdvantageFlavor", "Better early damage resistance and violent scaling.")
				},
				{
					LOCTEXT("BackstoryTragicPastDisadvantageFaith", "No starting bonus to Faith."),
					LOCTEXT("BackstoryTragicPastDisadvantageCharisma", "No starting bonus to Charisma.")
				},
				{
					StartingLevel(TEXT("Defensive"), 3),
					StartingLevel(TEXT("Offensive"), 2)
				}),
			MakeBackstory(
				TEXT("Survivor"),
				LOCTEXT("BackstorySurvivorName", "Survivor"),
				LOCTEXT("BackstorySurvivorDescription", "You learned to stay standing when everything else collapsed, turning hardship into resolve."),
				{
					LOCTEXT("BackstorySurvivorAdvantageWillpower", "Willpower starts at level 3."),
					LOCTEXT("BackstorySurvivorAdvantageDefensive", "Defensive starts at level 2."),
					LOCTEXT("BackstorySurvivorAdvantageFlavor", "Better early sustain and survivability.")
				},
				{
					LOCTEXT("BackstorySurvivorDisadvantageCharisma", "No starting bonus to Charisma."),
					LOCTEXT("BackstorySurvivorDisadvantageCunning", "No starting bonus to Cunning.")
				},
				{
					StartingLevel(TEXT("Willpower"), 3),
					StartingLevel(TEXT("Defensive"), 2)
				})
		};
	}

	TArray<FProjectCharacterProfessionData> BuildFallbackProfessions()
	{
		return {
			MakeProfession(
				TEXT("Unemployed"),
				LOCTEXT("ProfessionUnemployedName", "Unemployed"),
				LOCTEXT("ProfessionUnemployedDescription", "No specialized training, no strict path. Every instinct grows a little faster."),
				{
					LOCTEXT("ProfessionUnemployedAdvantageAll", "+5% DXP gain to all Inner Doctrine attributes.")
				},
				{
					LOCTEXT("ProfessionUnemployedDisadvantageFocus", "No focused attribute scaling.")
				},
				{
					GainModifier(TEXT("Willpower"), 1.05f),
					GainModifier(TEXT("Offensive"), 1.05f),
					GainModifier(TEXT("Defensive"), 1.05f),
					GainModifier(TEXT("Faith"), 1.05f),
					GainModifier(TEXT("Cunning"), 1.05f),
					GainModifier(TEXT("Celerity"), 1.05f),
					GainModifier(TEXT("Charisma"), 1.05f)
				}),
			MakeProfession(
				TEXT("PoliceOfficer"),
				LOCTEXT("ProfessionPoliceOfficerName", "Police Officer"),
				LOCTEXT("ProfessionPoliceOfficerDescription", "Discipline, resistance, and control define the way this profile gains experience."),
				{
					LOCTEXT("ProfessionPoliceOfficerAdvantageWillpower", "Willpower DXP gain +20%."),
					LOCTEXT("ProfessionPoliceOfficerAdvantageCunning", "Cunning DXP gain +10%.")
				},
				{
					LOCTEXT("ProfessionPoliceOfficerDisadvantageOffensive", "Offensive DXP gain -10%.")
				},
				{
					GainModifier(TEXT("Willpower"), 1.20f),
					GainModifier(TEXT("Cunning"), 1.10f),
					GainModifier(TEXT("Offensive"), 0.90f)
				}),
			MakeProfession(
				TEXT("Criminal"),
				LOCTEXT("ProfessionCriminalName", "Criminal"),
				LOCTEXT("ProfessionCriminalDescription", "Aggression, opportunity, and manipulation push the profile toward sharper predatory gains."),
				{
					LOCTEXT("ProfessionCriminalAdvantageOffensive", "Offensive DXP gain +20%."),
					LOCTEXT("ProfessionCriminalAdvantageCunning", "Cunning DXP gain +15%.")
				},
				{
					LOCTEXT("ProfessionCriminalDisadvantageFaith", "Faith DXP gain -10%.")
				},
				{
					GainModifier(TEXT("Offensive"), 1.20f),
					GainModifier(TEXT("Cunning"), 1.15f),
					GainModifier(TEXT("Faith"), 0.90f)
				}),
			MakeProfession(
				TEXT("Doctor"),
				LOCTEXT("ProfessionDoctorName", "Doctor"),
				LOCTEXT("ProfessionDoctorDescription", "Clinical control and hard knowledge of the body make pain and resolve more efficient."),
				{
					LOCTEXT("ProfessionDoctorAdvantageDefensive", "Defensive DXP gain +20%."),
					LOCTEXT("ProfessionDoctorAdvantageWillpower", "Willpower DXP gain +10%.")
				},
				{
					LOCTEXT("ProfessionDoctorDisadvantageOffensive", "Offensive DXP gain -10%.")
				},
				{
					GainModifier(TEXT("Defensive"), 1.20f),
					GainModifier(TEXT("Willpower"), 1.10f),
					GainModifier(TEXT("Offensive"), 0.90f)
				}),
			MakeProfession(
				TEXT("Engineer"),
				LOCTEXT("ProfessionEngineerName", "Engineer"),
				LOCTEXT("ProfessionEngineerDescription", "Precision, rhythm, and technical execution turn movement and analysis into reliable growth."),
				{
					LOCTEXT("ProfessionEngineerAdvantageCelerity", "Celerity DXP gain +20%."),
					LOCTEXT("ProfessionEngineerAdvantageCunning", "Cunning DXP gain +10%.")
				},
				{
					LOCTEXT("ProfessionEngineerDisadvantageCharisma", "Charisma DXP gain -10%.")
				},
				{
					GainModifier(TEXT("Celerity"), 1.20f),
					GainModifier(TEXT("Cunning"), 1.10f),
					GainModifier(TEXT("Charisma"), 0.90f)
				})
		};
	}

	template <typename RowType>
	TArray<RowType> LoadRowsFromDataTable(const TSoftObjectPtr<UDataTable>& TablePtr)
	{
		TArray<RowType> Result;
		if (TablePtr.IsNull())
		{
			return Result;
		}

		const FSoftObjectPath TablePath = TablePtr.ToSoftObjectPath();
		const FString LongPackageName = TablePath.GetLongPackageName();
		if (!LongPackageName.IsEmpty() && !FPackageName::DoesPackageExist(LongPackageName))
		{
			return Result;
		}

		const UDataTable* DataTable = TablePtr.LoadSynchronous();
		if (!DataTable)
		{
			return Result;
		}

		TArray<RowType*> Rows;
		DataTable->GetAllRows<RowType>(TEXT("ProjectCharacterBackground"), Rows);
		Result.Reserve(Rows.Num());
		for (const RowType* Row : Rows)
		{
			if (Row)
			{
				Result.Add(*Row);
			}
		}

		return Result;
	}

	FText FormatMultiplier(const float Multiplier)
	{
		return FText::FromString(FString::Printf(TEXT("x%.2f"), Multiplier));
	}
}

UProjectCharacterBackgroundComponent::UProjectCharacterBackgroundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UProjectCharacterBackgroundComponent::SetBackstory(const FName BackstoryID)
{
	FProjectCharacterBackstoryData ResolvedData;
	if (!ResolveBackstory(BackstoryID, ResolvedData))
	{
		UE_LOG(LogProjectCharacterBackground, Warning, TEXT("[CharacterBackground] Unknown BackstoryID '%s'."), *BackstoryID.ToString());
		return false;
	}

	SelectedBackstoryID = ResolvedData.BackstoryID.IsNone() ? BackstoryID : ResolvedData.BackstoryID;
	SelectedBackstoryData = ResolvedData;
	SelectedBackstoryData.BackstoryID = SelectedBackstoryID;
	RebuildFinalModifiers();
	return true;
}

bool UProjectCharacterBackgroundComponent::SetProfession(const FName ProfessionID)
{
	FProjectCharacterProfessionData ResolvedData;
	if (!ResolveProfession(ProfessionID, ResolvedData))
	{
		UE_LOG(LogProjectCharacterBackground, Warning, TEXT("[CharacterBackground] Unknown ProfessionID '%s'."), *ProfessionID.ToString());
		return false;
	}

	SelectedProfessionID = ResolvedData.ProfessionID.IsNone() ? ProfessionID : ResolvedData.ProfessionID;
	SelectedProfessionData = ResolvedData;
	SelectedProfessionData.ProfessionID = SelectedProfessionID;
	RebuildFinalModifiers();
	return true;
}

void UProjectCharacterBackgroundComponent::ClearBackground()
{
	SelectedBackstoryID = NAME_None;
	SelectedProfessionID = NAME_None;
	SelectedBackstoryData = FProjectCharacterBackstoryData();
	SelectedProfessionData = FProjectCharacterProfessionData();
	FinalStartingLevelModifiers.Reset();
	FinalGainModifiers.Reset();
	ProfileRevision = 0;
	AppliedProfileRevision = INDEX_NONE;
}

bool UProjectCharacterBackgroundComponent::IsSelectionValid() const
{
	return !SelectedBackstoryID.IsNone()
		&& !SelectedProfessionID.IsNone()
		&& !SelectedBackstoryData.BackstoryID.IsNone()
		&& !SelectedProfessionData.ProfessionID.IsNone();
}

TArray<FProjectCharacterBackstoryData> UProjectCharacterBackgroundComponent::GetAvailableBackstories() const
{
	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	TArray<FProjectCharacterBackstoryData> Rows = Settings
		? ProjectCharacterBackgroundComponentPrivate::LoadRowsFromDataTable<FProjectCharacterBackstoryData>(Settings->BackstoryDataTable)
		: TArray<FProjectCharacterBackstoryData>();

	if (Rows.IsEmpty())
	{
		Rows = ProjectCharacterBackgroundComponentPrivate::BuildFallbackBackstories();
	}

	for (FProjectCharacterBackstoryData& Row : Rows)
	{
		if (Row.BackstoryID.IsNone())
		{
			Row.BackstoryID = FName(*Row.DisplayName.ToString().Replace(TEXT(" "), TEXT("")));
		}
	}
	return Rows;
}

TArray<FProjectCharacterProfessionData> UProjectCharacterBackgroundComponent::GetAvailableProfessions() const
{
	const UProjectCharacterBackgroundSettings* Settings = UProjectCharacterBackgroundSettings::Get();
	TArray<FProjectCharacterProfessionData> Rows = Settings
		? ProjectCharacterBackgroundComponentPrivate::LoadRowsFromDataTable<FProjectCharacterProfessionData>(Settings->ProfessionDataTable)
		: TArray<FProjectCharacterProfessionData>();

	if (Rows.IsEmpty())
	{
		Rows = ProjectCharacterBackgroundComponentPrivate::BuildFallbackProfessions();
	}

	for (FProjectCharacterProfessionData& Row : Rows)
	{
		if (Row.ProfessionID.IsNone())
		{
			Row.ProfessionID = FName(*Row.DisplayName.ToString().Replace(TEXT(" "), TEXT("")));
		}
	}
	return Rows;
}

void UProjectCharacterBackgroundComponent::SetProfileRevision(const int32 InProfileRevision)
{
	ProfileRevision = FMath::Max(0, InProfileRevision);
}

bool UProjectCharacterBackgroundComponent::ApplyBackgroundToInnerDoctrine()
{
	if (!IsSelectionValid())
	{
		UE_LOG(LogProjectCharacterBackground, Warning, TEXT("[CharacterBackground] Cannot apply an incomplete background selection."));
		return false;
	}

	if (AppliedProfileRevision == ProfileRevision)
	{
		return false;
	}

	UProjectInnerDoctrineComponent* DoctrineComponent = ResolveInnerDoctrineComponent();
	if (!DoctrineComponent)
	{
		UE_LOG(LogProjectCharacterBackground, Warning, TEXT("[CharacterBackground] No Inner Doctrine component found on %s."), *GetNameSafe(GetOwner()));
		return false;
	}

	TMap<EProjectDoctrineAttribute, float> Multipliers;
	for (const FProjectDxpGainModifier& Modifier : FinalGainModifiers)
	{
		EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
		if (!ProjectCharacterBackground::TryResolveDoctrineAttribute(Modifier.AttributeID, Attribute))
		{
			UE_LOG(LogProjectCharacterBackground, Warning, TEXT("[CharacterBackground] Invalid profession AttributeID '%s'. Skipping gain modifier."), *Modifier.AttributeID.ToString());
			continue;
		}

		Multipliers.Add(Attribute, FMath::Max(0.0f, Modifier.GainMultiplier));
	}

	DoctrineComponent->SetDxpGainMultipliers(Multipliers);

	for (const FProjectDoctrineStartingLevelModifier& Modifier : FinalStartingLevelModifiers)
	{
		EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
		if (!ProjectCharacterBackground::TryResolveDoctrineAttribute(Modifier.AttributeID, Attribute))
		{
			UE_LOG(LogProjectCharacterBackground, Warning, TEXT("[CharacterBackground] Invalid backstory AttributeID '%s'. Skipping starting level modifier."), *Modifier.AttributeID.ToString());
			continue;
		}

		DoctrineComponent->ApplyFreeAttributeLevels(Attribute, Modifier.StartingLevelDelta);
	}

	AppliedProfileRevision = ProfileRevision;
	return true;
}

FProjectCharacterBackgroundSummary UProjectCharacterBackgroundComponent::BuildSummary() const
{
	FProjectCharacterBackgroundSummary Summary;
	Summary.bValid = IsSelectionValid();
	Summary.BackstoryName = SelectedBackstoryData.DisplayName;
	Summary.ProfessionName = SelectedProfessionData.DisplayName;

	for (const FProjectDoctrineStartingLevelModifier& Modifier : FinalStartingLevelModifiers)
	{
		EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
		const FText AttributeName = ProjectCharacterBackground::TryResolveDoctrineAttribute(Modifier.AttributeID, Attribute)
			? ProjectCharacterBackground::GetDoctrineAttributeDisplayText(Attribute)
			: FText::FromName(Modifier.AttributeID);

		Summary.StartingLevelLines.Add(FText::Format(
			LOCTEXT("StartingLevelSummaryLine", "{0} +{1} starting levels"),
			AttributeName,
			FText::AsNumber(Modifier.StartingLevelDelta)));
	}

	for (const FProjectDxpGainModifier& Modifier : FinalGainModifiers)
	{
		EProjectDoctrineAttribute Attribute = EProjectDoctrineAttribute::Willpower;
		const FText AttributeName = ProjectCharacterBackground::TryResolveDoctrineAttribute(Modifier.AttributeID, Attribute)
			? ProjectCharacterBackground::GetDoctrineAttributeDisplayText(Attribute)
			: FText::FromName(Modifier.AttributeID);

		Summary.GainModifierLines.Add(FText::Format(
			LOCTEXT("GainModifierSummaryLine", "{0} DXP gain {1}"),
			AttributeName,
			ProjectCharacterBackgroundComponentPrivate::FormatMultiplier(Modifier.GainMultiplier)));
	}

	return Summary;
}

bool UProjectCharacterBackgroundComponent::ResolveBackstory(const FName BackstoryID, FProjectCharacterBackstoryData& OutData) const
{
	for (FProjectCharacterBackstoryData Candidate : GetAvailableBackstories())
	{
		if (Candidate.BackstoryID.IsNone())
		{
			Candidate.BackstoryID = FName(*Candidate.DisplayName.ToString().Replace(TEXT(" "), TEXT("")));
		}

		if (Candidate.BackstoryID == BackstoryID || Candidate.DisplayName.ToString().Equals(BackstoryID.ToString(), ESearchCase::IgnoreCase))
		{
			OutData = Candidate;
			return true;
		}
	}

	return false;
}

bool UProjectCharacterBackgroundComponent::ResolveProfession(const FName ProfessionID, FProjectCharacterProfessionData& OutData) const
{
	for (FProjectCharacterProfessionData Candidate : GetAvailableProfessions())
	{
		if (Candidate.ProfessionID.IsNone())
		{
			Candidate.ProfessionID = FName(*Candidate.DisplayName.ToString().Replace(TEXT(" "), TEXT("")));
		}

		if (Candidate.ProfessionID == ProfessionID || Candidate.DisplayName.ToString().Equals(ProfessionID.ToString(), ESearchCase::IgnoreCase))
		{
			OutData = Candidate;
			return true;
		}
	}

	return false;
}

void UProjectCharacterBackgroundComponent::RebuildFinalModifiers()
{
	FinalStartingLevelModifiers = SelectedBackstoryID.IsNone()
		? TArray<FProjectDoctrineStartingLevelModifier>()
		: SelectedBackstoryData.StartingLevels;
	FinalGainModifiers = SelectedProfessionID.IsNone()
		? TArray<FProjectDxpGainModifier>()
		: SelectedProfessionData.GainModifiers;
}

UProjectInnerDoctrineComponent* UProjectCharacterBackgroundComponent::ResolveInnerDoctrineComponent() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->FindComponentByClass<UProjectInnerDoctrineComponent>() : nullptr;
}

#undef LOCTEXT_NAMESPACE

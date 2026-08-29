#include "Companions/ProjectCompanionRevivalMenuWidget.h"

#define LOCTEXT_NAMESPACE "ProjectCompanionRevivalMenu"

namespace ProjectCompanionRevivalMenuPrivate
{
	FText GradeText(const EProjectCompanionDifficultyGrade Grade)
	{
		switch (Grade)
		{
		case EProjectCompanionDifficultyGrade::Common: return LOCTEXT("Common", "Common");
		case EProjectCompanionDifficultyGrade::Uncommon: return LOCTEXT("Uncommon", "Uncommon");
		case EProjectCompanionDifficultyGrade::Rare: return LOCTEXT("Rare", "Rare");
		case EProjectCompanionDifficultyGrade::Epic: return LOCTEXT("Epic", "Epic");
		case EProjectCompanionDifficultyGrade::Winter: return LOCTEXT("Winter", "Winter");
		default: return LOCTEXT("Unknown", "Unknown");
		}
	}
}

bool UProjectCompanionRevivalMenuWidget::ConfigureCandidates(
	const TArray<FProjectCompanionRevivalCandidate>& InCandidates)
{
	CandidateByOptionId.Reset();
	TArray<FProjectEmoteMenuOption> Options;
	Options.Reserve(InCandidates.Num() + 1);

	for (const FProjectCompanionRevivalCandidate& Candidate : InCandidates)
	{
		if (!Candidate.StableCompanionId.IsValid())
		{
			CandidateByOptionId.Reset();
			return false;
		}

		const FName OptionId(*FString::Printf(
			TEXT("Revive_%s"),
			*Candidate.StableCompanionId.ToString(EGuidFormats::Digits)));
		if (CandidateByOptionId.Contains(OptionId))
		{
			CandidateByOptionId.Reset();
			return false;
		}

		CandidateByOptionId.Add(OptionId, Candidate.StableCompanionId);
		FProjectEmoteMenuOption& Option = Options.AddDefaulted_GetRef();
		Option.OptionId = OptionId;
		Option.Label = FText::Format(
			LOCTEXT("CandidateLabel", "{0} {1}"),
			FText::FromName(Candidate.Gender),
			FText::FromName(Candidate.Archetype));
		Option.Description = FText::Format(
			LOCTEXT("CandidateDescription", "{0} - logical level {1}{2}"),
			ProjectCompanionRevivalMenuPrivate::GradeText(Candidate.DifficultyGrade),
			FText::AsNumber(Candidate.ResolvedLevel),
			Candidate.bDeathPendingAdvance
				? LOCTEXT("PendingSuffix", " - death pending this floor")
				: FText::GetEmpty());
		Option.NodeType = EProjectEmoteMenuNodeType::Action;
		Option.VisualIconId = TEXT("Social");
	}

	if (CandidateByOptionId.IsEmpty())
	{
		return false;
	}

	FProjectEmoteMenuOption& Cancel = Options.AddDefaulted_GetRef();
	Cancel.OptionId = GetCancelOptionId();
	Cancel.Label = LOCTEXT("Cancel", "Cancel");
	Cancel.Description = LOCTEXT("CancelDescription", "Close without consuming Winter's Recall.");
	Cancel.NodeType = EProjectEmoteMenuNodeType::Cancel;
	Cancel.VisualIconId = TEXT("Cancel");

	SetMenuContent(
		LOCTEXT("Title", "Winter's Recall"),
		LOCTEXT("Hint", "Choose one companion. The item is consumed only after a verified revival."),
		Options,
		EProjectEmoteMenuVisualMode::Category);
	return true;
}

bool UProjectCompanionRevivalMenuWidget::TryResolveCandidate(
	const FName OptionId,
	FGuid& OutStableCompanionId) const
{
	OutStableCompanionId.Invalidate();
	if (const FGuid* Found = CandidateByOptionId.Find(OptionId))
	{
		OutStableCompanionId = *Found;
		return true;
	}
	return false;
}

FName UProjectCompanionRevivalMenuWidget::GetCancelOptionId()
{
	return TEXT("Revival.Cancel");
}

#undef LOCTEXT_NAMESPACE


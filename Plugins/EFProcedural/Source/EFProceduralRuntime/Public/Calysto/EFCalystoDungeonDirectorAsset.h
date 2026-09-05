#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Calysto/EFCalystoDirectorTypes.h"
#include "Calysto/EFCalystoDirectorProbability.h"
#include "EFCalystoDungeonDirectorAsset.generated.h"

#if WITH_EDITOR
class FDataValidationContext;
enum class EDataValidationResult : uint8;
#endif

struct EFPROCEDURALRUNTIME_API FEFCalystoValidationIssue
{
	FString Field;
	FString Message;
};

struct EFPROCEDURALRUNTIME_API FEFCalystoResolvedContentGroup
{
	FEFCalystoContentGroup Content;
	/** Remains the Style-wide category capacity even when a Theme replaces its local category. */
	int32 FloorMaximum = 0;
};

/** Immutable authored-value snapshot. Resource loading and gameplay verification are separate gates. */
class EFPROCEDURALRUNTIME_API FEFCalystoCompiledDirector final
{
public:
	bool IsValid() const { return bValid; }
	const FEFCalystoDungeonRules& GetDungeon() const { return Dungeon; }
	const FEFCalystoDirectorAdvanced& GetAdvanced() const { return Advanced; }
	const TArray<FEFCalystoStyle>& GetStyles() const { return Styles; }
	const TArray<FEFCalystoTheme>& GetThemes() const { return Themes; }
	const TArray<FSoftObjectPath>& GetVisualDependencies() const { return VisualDependencies; }
	const FEFCalystoStyle* FindStyle(const FGuid& Id) const;
	const FEFCalystoTheme* FindTheme(const FGuid& Id) const;
	bool SelectStyle(const FEFCalystoRandomKey& Key, TConstArrayView<FGuid> CoolingDownIds,
		FGuid& OutStyleId, FString& OutError) const;
	bool ResolveMaterials(const FGuid& StyleId, const FGuid& ThemeId,
		FEFCalystoSurfaceMaterials& OutMaterials, FString& OutError) const;
	bool ResolveContent(const FGuid& StyleId, const FGuid& ThemeId,
		TArray<FEFCalystoResolvedContentGroup>& OutContent, FString& OutError) const;
	bool ResolveDecals(const FGuid& StyleId, const FGuid& ThemeId,
		FEFCalystoDecals& OutDecals, FString& OutError) const;

private:
	friend class UEFCalystoDungeonDirectorAsset;
	bool bValid = false;
	FEFCalystoDungeonRules Dungeon;
	FEFCalystoDirectorAdvanced Advanced;
	TArray<FEFCalystoStyle> Styles;
	TArray<FEFCalystoTheme> Themes;
	TArray<FSoftObjectPath> VisualDependencies;
};

/** Stable, unversioned master asset. No legacy policy, compiler, or runtime dependency. */
UCLASS(BlueprintType, meta = (DisplayName = "Calysto Dungeon Director"))
class EFPROCEDURALRUNTIME_API UEFCalystoDungeonDirectorAsset final : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	static constexpr int32 CurrentSchemaVersion = 7;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon", meta = (ShowOnlyInnerProperties))
	FEFCalystoDungeonRules Dungeon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon Styles", meta = (TitleProperty = "Selection.DisplayName", DisplayName = "Dungeon Styles"))
	TArray<FEFCalystoStyle> Styles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Themes", meta = (TitleProperty = "Selection.DisplayName", DisplayName = "Room Themes"))
	TArray<FEFCalystoTheme> RoomThemes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Advanced", meta = (ShowOnlyInnerProperties))
	FEFCalystoDirectorAdvanced Advanced;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	bool Compile(FEFCalystoCompiledDirector& OutConfiguration, TArray<FEFCalystoValidationIssue>& OutIssues) const;
	UFUNCTION(BlueprintPure, Category = "Calysto|Authoring", meta = (ToolTip = "Checks authored data only. This never reports Gameplay Verified."))
	bool ValidateAuthoring(TArray<FString>& OutErrors) const;
	/** Direct array return preserves field errors in Python, including failed validation. */
	UFUNCTION(BlueprintPure, Category = "Calysto|Authoring", meta = (ToolTip = "Returns exact authored field errors. An empty result does not verify gameplay."))
	TArray<FString> GetAuthoringErrors() const;
	int32 GetSchemaVersion() const { return SchemaVersion; }

#if WITH_EDITOR
	/** Called by the editor/importer, never runtime loading or a probability draw. */
	void EnsureEditorIdentities();
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& Event) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	UPROPERTY()
	int32 SchemaVersion = CurrentSchemaVersion;
};

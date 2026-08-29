#pragma once

// clang-format off
#include "Calysto/EFCalystoDungeonTypesV4.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFCalystoDungeonDirectorPolicyV4.generated.h"
// clang-format on

/**
 * V4 authoring authority. It contains no global gameplay profile: every
 * probability, catalog and gameplay limit belongs to a Style or Theme.
 * SafetyCeilings are inert technical guards and never participate in
 * probability resolution.
 */
UCLASS(BlueprintType,
       meta = (DisplayName = "Calysto Dungeon Director Policy V4"))
class EFPROCEDURALRUNTIME_API UEFCalystoDungeonDirectorPolicyV4
    : public UPrimaryDataAsset {
  GENERATED_BODY()

public:
  UEFCalystoDungeonDirectorPolicyV4();

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity",
            meta = (DisplayName = "Schema Version",
                    ToolTip = "V4 data contract version. Informational and read-only."))
  int32 SchemaVersion = 4;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity",
            meta = (DisplayName = "Generator Version",
                    ToolTip = "Version included in deterministic seeds and hashes."))
  int32 GeneratorVersion = 4;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "01 Identity",
            meta = (DisplayName = "Policy ID",
                    ToolTip = "Stable identity of this generation authority."))
  FName PolicyId = TEXT("CalystoDungeonDirectorV4");

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
            Category = "02 Technical Safety",
            meta = (DisplayName = "Safety Ceilings",
                    ToolTip = "Technical guards that protect Calysto. They are not a global profile and do not alter probabilities."))
  FEFCalystoSafetyCeilingsV4 SafetyCeilings;

  /** Only edge lengths already certified by the Calysto size matrix may
   * resolve. */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "02 Technical Safety",
            meta = (DisplayName = "Validated Dungeon Sizes",
                    ToolTip = "Only these edge lengths may resolve. Initially 26 through 30; each smaller size must pass its validation matrix."))
  TArray<int32> ValidatedDungeonSizes;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "03 Styles",
            meta = (TitleProperty = "Style", DisplayName = "Styles",
                    ToolTip = "Complete Standard, Compact, and Branching profiles. Their selection probabilities must total 1.0, and none is a global layer."))
  TArray<FEFCalystoStyleProfileV4> Styles;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "04 Themes",
            meta = (TitleProperty = "Theme", DisplayName = "Themes",
                    ToolTip = "Complete Default, Forge, and Shrine profiles. Their selection probabilities must total 1.0, and none is a fallback."))
  TArray<FEFCalystoThemeProfileV4> Themes;

  /** Fail-closed schema, probability, catalog, cross-profile and Calysto-safety
   * validation. */
  bool Validate(FString &OutError) const;

  UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V4",
            meta = (DisplayName = "Validate V4 Policy"))
  bool ValidatePolicy() const;

  /** Canonical, order-independent SHA-256. Invalid policies return an empty
   * string. */
  UFUNCTION(BlueprintPure, Category = "EF|Calysto Dungeon|V4",
            meta = (DisplayName = "Get V4 Policy Hash"))
  FString GetPolicyHash() const;

  FString BuildCanonicalString() const;

  const FEFCalystoStyleProfileV4 *FindStyle(EEFCalystoStyleV4 Style) const;
  const FEFCalystoThemeProfileV4 *FindTheme(EEFCalystoThemeV4 Theme) const;
  static const FEFCalystoCategoryProfileV4 *
  FindCategory(const TArray<FEFCalystoCategoryProfileV4> &Categories,
               EEFCalystoContentCategoryV4 Category);

  /** Recomputes every visible Nothing field without normalizing authored tier
   * values. */
  void SynchronizeDerivedTierNothing();

  virtual void PostLoad() override;

#if WITH_EDITOR
  virtual void
  PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif
};

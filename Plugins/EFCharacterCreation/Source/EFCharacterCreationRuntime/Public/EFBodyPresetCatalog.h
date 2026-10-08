#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EFCharacterCreationTypes.h"
#include "EFBodyPresetCatalog.generated.h"

USTRUCT(BlueprintType)
struct EFCHARACTERCREATIONRUNTIME_API FEFBodyMorphProfileEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName MorphName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Minimum = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Maximum = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DefaultValue = 0.0f;
	/** NPC generation uses only explicitly enabled dimensions, never expression/corrective auto-discovery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRandomizeNPC = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float NPCMinimum = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float NPCMaximum = 0.35f;
};

USTRUCT(BlueprintType)
struct EFCHARACTERCREATIONRUNTIME_API FEFBodyPresetDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PresetId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECharacterCreationGender Gender = ECharacterCreationGender::NotApplicable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAllowNPC = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float NPCWeight = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FEFBodyMorphProfileEntry> MorphProfile;
};

/** One authored catalog; array order is UI/default order. No body meshes are loaded to enumerate it. */
UCLASS(BlueprintType)
class EFCHARACTERCREATIONRUNTIME_API UEFBodyPresetCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SchemaVersion = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(TitleProperty="PresetId")) TArray<FEFBodyPresetDefinition> Bodies;
	static const UEFBodyPresetCatalog* Get();
	const FEFBodyPresetDefinition* Find(FName Id) const;
	const FEFBodyPresetDefinition* FindMesh(const FSoftObjectPath& Path) const;
	const FEFBodyPresetDefinition* DefaultForGender(ECharacterCreationGender Gender) const;
	const FEFBodyPresetDefinition* ChooseNPC(ECharacterCreationGender Gender, FRandomStream& Random) const;
	bool ValidateDefinition(FString& Error) const;
};

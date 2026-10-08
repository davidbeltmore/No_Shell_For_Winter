#include "EFBodyPresetCatalog.h"
#include "EFCharacterCreationSettings.h"
#include "UObject/StrongObjectPtr.h"

const UEFBodyPresetCatalog* UEFBodyPresetCatalog::Get()
{
	static TStrongObjectPtr<UEFBodyPresetCatalog> Cached;
	const auto& Reference = UEFCharacterCreationSettings::Get()->BodyPresetCatalog;
	if (Reference.IsNull()) return nullptr;
	if (!Cached.IsValid() || FSoftObjectPath(Cached.Get()) != Reference.ToSoftObjectPath())
		Cached.Reset(Reference.LoadSynchronous());
	return Cached.Get();
}

const FEFBodyPresetDefinition* UEFBodyPresetCatalog::Find(FName Id) const
{
	if (Id.IsNone()) return nullptr;
	return Bodies.FindByPredicate([Id](const auto& B) { return B.bEnabled && B.PresetId == Id && !B.Mesh.IsNull(); });
}
const FEFBodyPresetDefinition* UEFBodyPresetCatalog::FindMesh(const FSoftObjectPath& Path) const
{
	return Bodies.FindByPredicate([&Path](const auto& B) { return B.bEnabled && !B.Mesh.IsNull() && B.Mesh.ToSoftObjectPath() == Path; });
}
const FEFBodyPresetDefinition* UEFBodyPresetCatalog::DefaultForGender(ECharacterCreationGender Gender) const
{
	return Bodies.FindByPredicate([Gender](const auto& B) { return B.bEnabled && B.Gender == Gender && !B.Mesh.IsNull(); });
}
const FEFBodyPresetDefinition* UEFBodyPresetCatalog::ChooseNPC(ECharacterCreationGender Gender, FRandomStream& Random) const
{
	double Total = 0;
	for (const auto& B : Bodies)
		if (B.bEnabled && B.bAllowNPC && B.Gender == Gender && !B.Mesh.IsNull() && FMath::IsFinite(B.NPCWeight) && B.NPCWeight > 0) Total += B.NPCWeight;
	if (Total <= 0) return nullptr;
	double Roll = Random.FRand() * Total;
	const FEFBodyPresetDefinition* Last = nullptr;
	for (const auto& B : Bodies)
	{
		if (!B.bEnabled || !B.bAllowNPC || B.Gender != Gender || B.Mesh.IsNull() || !FMath::IsFinite(B.NPCWeight) || B.NPCWeight <= 0) continue;
		Last = &B;
		Roll -= B.NPCWeight;
		if (Roll < 0) return &B;
	}
	return Last;
}
bool UEFBodyPresetCatalog::ValidateDefinition(FString& Error) const
{
	TSet<FName> Ids;
	TSet<FSoftObjectPath> Meshes;
	for (const auto& B : Bodies)
	{
		if (B.PresetId.IsNone() || Ids.Contains(B.PresetId) || B.Mesh.IsNull() || Meshes.Contains(B.Mesh.ToSoftObjectPath()) ||
			(B.Gender != ECharacterCreationGender::Male && B.Gender != ECharacterCreationGender::Female) ||
			!FMath::IsFinite(B.NPCWeight) || B.NPCWeight < 0)
		{ Error = FString::Printf(TEXT("Invalid/duplicate body definition: %s"), *B.PresetId.ToString()); return false; }
		Ids.Add(B.PresetId); Meshes.Add(B.Mesh.ToSoftObjectPath());
		TSet<FName> Morphs;
		for (const auto& M : B.MorphProfile)
		{
			if (M.MorphName.IsNone() || Morphs.Contains(M.MorphName) || !FMath::IsFinite(M.Minimum) || !FMath::IsFinite(M.Maximum) ||
				!FMath::IsFinite(M.DefaultValue) || M.Minimum > M.Maximum || M.DefaultValue < M.Minimum || M.DefaultValue > M.Maximum ||
				!FMath::IsFinite(M.NPCMinimum) || !FMath::IsFinite(M.NPCMaximum) ||
				(M.bRandomizeNPC && (M.NPCMinimum < M.Minimum || M.NPCMaximum > M.Maximum || M.NPCMinimum > M.NPCMaximum)))
			{ Error = FString::Printf(TEXT("Invalid morph profile: %s/%s"), *B.PresetId.ToString(), *M.MorphName.ToString()); return false; }
			Morphs.Add(M.MorphName);
		}
	}
	Error.Reset(); return Bodies.Num() > 0;
}

#include "Storage/ProjectChestContent.h"
#include "Engine/DataTable.h"
#include "ACFItemSystemFunctionLibrary.h"
#include "Items/ACFItem.h"
#include "Misc/Crc.h"

bool FProjectChestSelector::Generate(const FProjectChestContentSettings& S, const FGuid& Identity, int32 Seed, TArray<FBaseItem>& Out, FString& Error)
{
    Out.Reset(); Error.Reset();
    auto Fail = [&](const FString& Message) { Error = Message; return false; };
    const int32 Min = S.bUseSelectionRange ? S.MinSelections : S.FixedSelections;
    const int32 Max = S.bUseSelectionRange ? S.MaxSelections : S.FixedSelections;
    if (!Identity.IsValid()) return Fail(TEXT("Stable Identity must be a persistent nonzero GUID."));
    if (S.ContentType.IsNone()) return Fail(TEXT("Content Type must be set."));
    if (Min < 0 || Max < Min || Max > 1024) return Fail(TEXT("Selections require 0 <= minimum <= maximum <= 1024."));
    if (S.MinUnitsPerStack < 1 || S.MaxUnitsPerStack < S.MinUnitsPerStack || S.MaxUnitsPerStack > 255) return Fail(TEXT("Units Per Stack require 1 <= minimum <= maximum <= 255."));
    if (!S.Catalog || S.Catalog->GetRowStruct() != FItemGenerationSlot::StaticStruct()) return Fail(TEXT("Catalog must be an ACF ItemGenerationSlot table."));
    if (S.Categories.IsEmpty()) return Fail(TEXT("Categories must contain at least one eligible category."));
    auto Tiers = S.ItemRarities;
    Tiers.Sort([](const auto& A, const auto& B) { return A.Rarity.ToString() < B.Rarity.ToString(); });
    double Total = 0;
    TSet<FGameplayTag> SeenTiers;
    for (const auto& Tier : Tiers)
    {
        if (!Tier.Rarity.IsValid() || SeenTiers.Contains(Tier.Rarity) || !FMath::IsFinite(Tier.Percentage) || Tier.Percentage < 0 || Tier.Percentage > 100)
            return Fail(TEXT("Item Rarities contain an invalid tag, duplicate tier or percentage outside 0..100."));
        SeenTiers.Add(Tier.Rarity); Total += Tier.Percentage;
    }
    if (!FMath::IsNearlyEqual(Total, 100.0, 0.000001)) return Fail(TEXT("Item Rarity percentages must sum to exactly 100."));
    for (const auto Row : S.AllowedRows)
        if (!S.Catalog->GetRowMap().Contains(Row)) return Fail(FString::Printf(TEXT("Allowed Rows: missing catalog row %s."), *Row.ToString()));
    for (const auto Row : S.ExcludedRows)
        if (!S.Catalog->GetRowMap().Contains(Row)) return Fail(FString::Printf(TEXT("Excluded Rows: missing catalog row %s."), *Row.ToString()));
    TArray<TArray<UClass*>> Pools; Pools.SetNum(Tiers.Num());
    TMap<FString, FGameplayTag> ClassTiers;
    auto Names = S.Catalog->GetRowNames(); Names.Sort(FNameLexicalLess());
    for (const FName Name : Names)
    {
        const auto* Row = S.Catalog->FindRow<FItemGenerationSlot>(Name, TEXT("Chest"));
        if ((!S.AllowedRows.IsEmpty() && !S.AllowedRows.Contains(Name)) || S.ExcludedRows.Contains(Name)
            || !Row->Category.MatchesAny(S.Categories) || Row->Category.MatchesAny(S.ExcludedCategories)) continue;
        const int32 Tier = Tiers.IndexOfByPredicate([&](const auto& T) { return T.Rarity == Row->Rarity && T.Percentage > 0; });
        if (Tier == INDEX_NONE) continue;
        auto* Class = Row->ItemClass.LoadSynchronous();
        FItemDescriptor Descriptor;
        if (!Class || !UACFItemSystemFunctionLibrary::GetItemData(Class, Descriptor)) return Fail(FString::Printf(TEXT("Catalog row %s has no valid item class."), *Name.ToString()));
        if (S.MaxUnitsPerStack > Descriptor.MaxInventoryStack) return Fail(FString::Printf(TEXT("Units Per Stack exceed %s native stack limit %d."), *Name.ToString(), Descriptor.MaxInventoryStack));
        const FString Path = Class->GetPathName();
        if (const auto* Prior = ClassTiers.Find(Path); Prior && *Prior != Row->Rarity) return Fail(TEXT("An eligible item class occurs in conflicting rarity tiers."));
        ClassTiers.Add(Path, Row->Rarity); Pools[Tier].AddUnique(Class);
    }
    for (int32 I = 0; I < Tiers.Num(); ++I)
    {
        Pools[I].Sort([](const UClass& A, const UClass& B) { return A.GetPathName() < B.GetPathName(); });
        if (Tiers[I].Percentage > 0 && (Pools[I].IsEmpty() || (!S.bAllowDuplicates && Pools[I].Num() < Max)))
            return Fail(FString::Printf(TEXT("Item Rarities.%s cannot support every requested selection count%s."), *Tiers[I].Rarity.ToString(), S.bAllowDuplicates ? TEXT(": empty eligible tier") : TEXT(" without duplicate classes")));
    }
    FRandomStream Random(FCrc::StrCrc32(*Identity.ToString(EGuidFormats::Digits), static_cast<uint32>(Seed)));
    const int32 Count = Random.RandRange(Min, Max);
    for (int32 I = 0; I < Count; ++I)
    {
        const double Roll = Random.GetFraction() * Total;
        double Boundary = 0; int32 TierIndex = INDEX_NONE;
        for (int32 T = 0; T < Tiers.Num(); ++T) { Boundary += Tiers[T].Percentage; if (Roll < Boundary) { TierIndex = T; break; } }
        if (TierIndex == INDEX_NONE) return Fail(TEXT("Rarity selection exceeded the validated distribution."));
        auto& Pool = Pools[TierIndex];
        const int32 Pick = Random.RandRange(0, Pool.Num() - 1);
        Out.Add(FBaseItem(Pool[Pick], Random.RandRange(S.MinUnitsPerStack, S.MaxUnitsPerStack)));
        if (!S.bAllowDuplicates) Pool.RemoveAt(Pick);
    }
    return true;
}

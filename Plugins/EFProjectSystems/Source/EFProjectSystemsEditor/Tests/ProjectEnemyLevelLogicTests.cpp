#include "Characters/ProjectEnemyLevelLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ProjectEnemyLevelSettings.h"
#include "Misc/AutomationTest.h"

namespace ProjectEnemyLevelLogicTestsPrivate
{
	static FProjectEnemyMapLevelRule MakeMapRule(const TCHAR* Pattern, const int32 WorldTier)
	{
		FProjectEnemyMapLevelRule Rule;
		Rule.MapNamePattern = Pattern;
		Rule.WorldTier = WorldTier;
		return Rule;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyLevelLogicMapTierTest,
	"NoShellForWinter.Enemies.Leveling.MapTierResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyLevelLogicMapTierTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyLevelSettings* Settings = GetDefault<UProjectEnemyLevelSettings>();
	if (!TestNotNull(TEXT("Enemy level settings should exist"), Settings))
	{
		return false;
	}

	int32 ResolvedTier = 0;
	FProjectEnemyLevelLogic::ResolveWorldTierForMapName(TEXT("DungeonGeneration"), *Settings, ResolvedTier);
	TestEqual(TEXT("DungeonGeneration should resolve to tier 1"), ResolvedTier, 1);

	UProjectEnemyLevelSettings* TempSettings = DuplicateObject<UProjectEnemyLevelSettings>(Settings, GetTransientPackage());
	TempSettings->MapLevelRules.Add(ProjectEnemyLevelLogicTestsPrivate::MakeMapRule(TEXT("BossRoom"), 7));

	FProjectEnemyLevelLogic::ResolveWorldTierForMapName(TEXT("Dungeon_BossRoom_A"), *TempSettings, ResolvedTier);
	TestEqual(TEXT("A matching custom rule should override the default tier"), ResolvedTier, 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectEnemyLevelLogicDistributionTest,
	"NoShellForWinter.Enemies.Leveling.LevelDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectEnemyLevelLogicDistributionTest::RunTest(const FString& Parameters)
{
	const UProjectEnemyLevelSettings* Settings = GetDefault<UProjectEnemyLevelSettings>();
	if (!TestNotNull(TEXT("Enemy level settings should exist"), Settings))
	{
		return false;
	}

	const FProjectEnemyLevelContext TierOneContext = FProjectEnemyLevelLogic::BuildLevelContext(1, *Settings);
	TestEqual(TEXT("Tier one min level should be 1"), TierOneContext.MinLevel, 1);
	TestEqual(TEXT("Tier one max level should be 5"), TierOneContext.MaxLevel, 5);

	const FProjectEnemyLevelContext TierTwoContext = FProjectEnemyLevelLogic::BuildLevelContext(2, *Settings);
	TestEqual(TEXT("Tier two min level should be 2"), TierTwoContext.MinLevel, 2);
	TestEqual(TEXT("Tier two max level should be 6"), TierTwoContext.MaxLevel, 6);

	FRandomStream RandomStream(1337);
	TMap<int32, int32> LevelCounts;

	constexpr int32 RollCount = 10000;
	for (int32 RollIndex = 0; RollIndex < RollCount; ++RollIndex)
	{
		FProjectEnemyLevelRollResult RollResult;
		if (!FProjectEnemyLevelLogic::RollEnemyLevel(TierOneContext, *Settings, RandomStream, RollResult))
		{
			AddError(FString::Printf(TEXT("Level roll failed at iteration %d."), RollIndex));
			return false;
		}

		LevelCounts.FindOrAdd(RollResult.AssignedLevel)++;
	}

	TestTrue(TEXT("Level 1 should be more common than level 2"), LevelCounts.FindRef(1) > LevelCounts.FindRef(2));
	TestTrue(TEXT("Level 2 should be more common than level 3"), LevelCounts.FindRef(2) > LevelCounts.FindRef(3));
	TestTrue(TEXT("Level 3 should be more common than level 4"), LevelCounts.FindRef(3) > LevelCounts.FindRef(4));
	TestTrue(TEXT("Level 4 should be more common than level 5"), LevelCounts.FindRef(4) > LevelCounts.FindRef(5));

	return true;
}

#endif

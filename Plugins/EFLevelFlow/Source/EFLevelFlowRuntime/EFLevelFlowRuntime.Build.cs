using UnrealBuildTool;

public class EFLevelFlowRuntime : ModuleRules
{
	public EFLevelFlowRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"UMG",
			"AscentCombatFramework",
			"AscentSaveSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIModule",
			"AIFramework",
			"Slate",
			"SlateCore",
			"EFProceduralRuntime",
			"EFCharacterCreationRuntime"
		});
	}
}

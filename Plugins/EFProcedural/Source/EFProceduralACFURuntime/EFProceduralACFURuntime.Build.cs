using UnrealBuildTool;

public class EFProceduralACFURuntime : ModuleRules
{
	public EFProceduralACFURuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EFProceduralRuntime",
			"AscentCombatFramework",
			"Json"
		});
	}
}

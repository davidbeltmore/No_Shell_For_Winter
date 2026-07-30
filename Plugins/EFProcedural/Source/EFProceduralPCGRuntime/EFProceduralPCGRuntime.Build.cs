using UnrealBuildTool;

public class EFProceduralPCGRuntime : ModuleRules
{
	public EFProceduralPCGRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EFProceduralRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
			"PCG",
			"EFProceduralACFURuntime"
		});
	}
}

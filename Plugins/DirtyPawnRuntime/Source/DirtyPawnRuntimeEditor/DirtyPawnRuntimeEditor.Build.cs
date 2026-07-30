using UnrealBuildTool;

public class DirtyPawnRuntimeEditor : ModuleRules
{
	public DirtyPawnRuntimeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DirtyPawnRuntime",
			"Kismet",
			"KismetCompiler"
		});
	}
}

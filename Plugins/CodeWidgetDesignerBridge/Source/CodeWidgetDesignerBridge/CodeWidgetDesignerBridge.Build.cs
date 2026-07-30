using UnrealBuildTool;

public class CodeWidgetDesignerBridge : ModuleRules
{
	public CodeWidgetDesignerBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG"
		});
	}
}

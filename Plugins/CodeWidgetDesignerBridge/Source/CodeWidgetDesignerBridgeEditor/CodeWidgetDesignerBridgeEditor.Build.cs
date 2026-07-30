using UnrealBuildTool;

public class CodeWidgetDesignerBridgeEditor : ModuleRules
{
	public CodeWidgetDesignerBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"CodeWidgetDesignerBridge"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"Kismet",
			"KismetCompiler",
			"Slate",
			"SlateCore",
			"UMGEditor",
			"UnrealEd"
		});
	}
}

using UnrealBuildTool;

public class EFCharacterCreationDazBridgeEditor : ModuleRules
{
	public EFCharacterCreationDazBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"EFCharacterCreationRuntime",
			"Core",
			"CoreUObject",
			"Engine",
			"Projects"
		});

		// DazToUnreal is enabled by the project descriptor and receipt, but this
		// bridge does not include or link any of its C++ symbols.  Keeping a
		// ModuleRules dependency here prevents a cold project build against the
		// installed/precompiled Daz plugin (whose rules are intentionally not
		// available to the project UBT rules assembly).

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimGraph",
			"AssetRegistry",
			"BlueprintGraph",
			"ControlRig",
			"ControlRigDeveloper",
			"Json",
			"UnrealEd"
		});
	}
}

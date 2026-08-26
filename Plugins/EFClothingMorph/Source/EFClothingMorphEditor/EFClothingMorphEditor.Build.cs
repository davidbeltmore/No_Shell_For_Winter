using UnrealBuildTool;

public class EFClothingMorphEditor : ModuleRules
{
	public EFClothingMorphEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EFClothingMorphRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"GeometryFramework",
			"GeometryScriptingCore",
			"GeometryCore",
			"DynamicMesh",
			"GeometryAlgorithms",
			"SkeletalMeshDescription",
			"AnimationCore",
			"MeshDescription",
			"MeshConversion",
			"ComputeFramework",
			"OptimusCore",
			"RenderCore"
		});
	}
}

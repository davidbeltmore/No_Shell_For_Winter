using System.IO;
using UnrealBuildTool;

public class EFProjectSystemsGameplay : ModuleRules
{
	public EFProjectSystemsGameplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory,
			Path.Combine(ModuleDirectory, "..", "EFProjectSystemsCore")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ImageWrapper",
			"InputCore",
			"UMG",
			"Slate",
			"SlateCore",
			"CodeWidgetDesignerBridge",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"DeveloperSettings",
			"AssetRegistry",
			"DirtyPawnRuntime",
			"AIModule",
			"NavigationSystem",
			"Niagara",
			"RenderCore",
			"RHI",
			"SkinnedDecalComponent",
			"EnhancedInput",
			"PhysicsCore",
			"EFProjectSystemsCore",
			"EFProjectSystemsUI",
			"EFCharacterCreationRuntime",
			"EFLevelFlowRuntime",
			"EFProceduralRuntime",
			"AdvancedRPGSystem",
			"ACFTrainingSystem",
			"AscentCoreInterfaces",
			"AscentCombatFramework",
			"AscentTeams",
			"AscentGASRuntime",
			"AscentTargetingSystem",
			"CinematicCameraManager",
			"AIFramework",
			"InventorySystem",
			"StatusEffectSystem",
			"AscentDialogueSystem",
			"ActionsSystem",
			"CharacterController",
			"CollisionsManager",
			"AscentSaveSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Json"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"BlueprintGraph"
			});
		}
	}
}

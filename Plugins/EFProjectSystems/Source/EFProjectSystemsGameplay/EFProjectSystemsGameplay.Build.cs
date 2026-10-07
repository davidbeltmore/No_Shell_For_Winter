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
			"DaySequence",
			"ImageWrapper",
			"InputCore",
			"UMG",
			"CommonUI",
			"AscentUINavigationSystem",
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
			"EFProceduralPCGRuntime",
			"AdvancedRPGSystem",
			"ACFTrainingSystem",
			"AscentCoreInterfaces",
			"AscentCombatFramework",
			"AscentTeams",
			"AscentGASRuntime",
			"AscentSkillTree",
			"AscentTargetingSystem",
			"CinematicCameraManager",
			"AIFramework",
			"InventorySystem",
			"CraftingSystem",
			"StatusEffectSystem",
			"AscentDialogueSystem",
			"ActionsSystem",
			"CharacterController",
			"CollisionsManager",
			"AscentSaveSystem",
			"AscentQuestSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
				"ApplicationCore",
				"EFEquipmentRuntime",
				"NetCore",
				"Json",
				"AGSGraphRuntime"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Comdlg32.lib");
		}

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"Kismet",
				"BlueprintGraph"
			});
		}
	}
}

using UnrealBuildTool;

public class EFEquipmentRuntime : ModuleRules
{
    public EFEquipmentRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "GameplayTags", "InventorySystem",
            "UMG", "DeveloperSettings", "AscentSaveSystem", "CraftingSystem", "CommonUI", "AscentUINavigationSystem"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "NetCore", "Slate", "SlateCore", "Json", "ActionsSystem", "InputCore", "AscentCoreInterfaces", "EnhancedInput" });
    }
}

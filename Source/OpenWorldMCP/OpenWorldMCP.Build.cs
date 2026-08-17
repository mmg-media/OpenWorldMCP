using UnrealBuildTool;

public class OpenWorldMCP : ModuleRules
{
	public OpenWorldMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ToolsetRegistry",
				"ModelContextProtocol"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"EditorScriptingUtilities",
				"Landscape",
				"LandscapeEditor",
				"Foliage",
				"Projects",
				"AssetRegistry",
				"AssetTools"
			}
		);
	}
}
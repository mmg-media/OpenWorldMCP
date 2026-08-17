using UnrealBuildTool;
using System.IO;

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
				"EditorFramework",
				"EditorScriptingUtilities",
				"Landscape",
				"LandscapeEditor",
				"Foliage",
				"Projects",
				"AssetRegistry",
				"AssetTools",
				"PythonScriptPlugin",
				"Slate",
				"SlateCore",
				"InputCore",
				"LevelEditor",
				"Json",
				"JsonUtilities"
			}
		);

		// Fab integration: talks to fab.com via the signed-in Epic account's EOS auth token, reusing
		// the login the editor/launcher already holds. EOSSDK provides the SDK headers and the
		// WITH_EOS_SDK=1 define; EOSShared provides IEOSSDKManager. Some engine installs ship without
		// the Fab plugin entirely, so all of this is conditional on it existing.
		bool bFabPluginPresent = File.Exists(
			Path.Combine(EngineDirectory, "Plugins", "Fab", "Fab.uplugin"));
		PrivateDefinitions.Add("WITH_OPENWORLD_FAB=" + (bFabPluginPresent ? "1" : "0"));
		if (bFabPluginPresent)
		{
			bRequiresPlatformSDK = true;
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"EOSSDK",
					"EOSShared",
					"Fab",
					"FileUtilities",
					"HTTP",
					"Json",
					"JsonUtilities"
				}
			);
		}
	}
}
// Copyright Perforce Software, Inc. All Rights Reserved.

using UnrealBuildTool;

public class P4Search : ModuleRules
{
	public P4Search(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"ImageWrapper",
				"AssetTools",
				"ApplicationCore",
				"RenderCore",
				"ContentBrowser",
				"GraphEditor",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"ToolMenus",
				"HTTP",
				"Json",
				"UMG", // FWidgetRenderer — renders Blueprint event-graph thumbnails offscreen
				"DeveloperSettings",
				"Settings",
				"GLTFCore",
				"MeshDescription",
				"StaticMeshDescription",
				"MaterialEditor", // UMaterialEditingLibrary — builds the runtime preview's texture-sample material graph
				// ... add private dependencies that you statically link with here ...
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}

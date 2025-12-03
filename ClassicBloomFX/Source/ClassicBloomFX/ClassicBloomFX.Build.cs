// Licensed under the MIT License. See LICENSE file in the project root.

using UnrealBuildTool;

public class ClassicBloomFX : ModuleRules
{
	public ClassicBloomFX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"Renderer",
				"RHI",
				"Projects"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore"
			}
		);

		// Access to private Renderer headers for FViewInfo
		PrivateIncludePaths.AddRange(
			new string[]
			{
				System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private")
			}
		);
	}
}

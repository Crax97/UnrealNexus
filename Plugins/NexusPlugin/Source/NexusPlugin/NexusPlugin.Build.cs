// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class NexusPlugin : ModuleRules
{

	private bool LoadNexusLibrary() {
		
		var librariesPath = Path.Combine(ModuleDirectory, "libs");
		if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win64)) {
			PublicAdditionalLibraries.Add(Path.Combine(librariesPath, "corto.lib"));
			return true;
		} else if (Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac)
		{			
			PublicAdditionalLibraries.Add(Path.Combine(librariesPath, "libcorto.a"));
			return true;
		}
		return false;
	}

	public NexusPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (!LoadNexusLibrary())
        {
			Console.WriteLine("Nexus is not supported.");
        }
		PublicIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory, "nexus", "nxszip"),
				Path.Combine(ModuleDirectory, "nexus", "common"),
				Path.Combine(ModuleDirectory, "vcglib"),
				Path.Combine(ModuleDirectory, "vcglib", "vcg"),
				Path.Combine(ModuleDirectory, "vcglib", "eigenlib"),
				Path.Combine(ModuleDirectory, "corto", "include"),
			}
		);
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", "CoreUObject", "Projects", "Engine",
			"RHI", "RenderCore" });
			
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}

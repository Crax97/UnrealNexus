// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class NexusPluginEditor : ModuleRules
{

	private bool LoadNexusLibrary() {
		if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win64)) {
			string LibrariesPath = Path.Combine(ModuleDirectory, "libs");
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libcorto.lib"));

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
			return true;
		}
		return false;
	}

	public NexusPluginEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (!LoadNexusLibrary())
        {
			Console.WriteLine("Nexus is not supported.");
        }
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", "CoreUObject", "Projects", "Engine",
			"RHI", "RenderCore",
			"NexusPlugin" });
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
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

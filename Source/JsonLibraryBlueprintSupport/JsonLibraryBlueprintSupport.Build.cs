// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class JsonLibraryBlueprintSupport : ModuleRules
	{
		public JsonLibraryBlueprintSupport(ReadOnlyTargetRules Target) : base(Target)
		{
			int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
			PublicDefinitions.Add("UE_VERSION=" + EngineVersion.ToString());

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{ 
					"Core", 
					"CoreUObject", 
					"Engine",
					"UnrealEd",
					"Slate",
					"SlateCore",
					"KismetWidgets",
					"KismetCompiler",
					"BlueprintGraph",
					"JsonLibrary"
				}
			);
		}
	}
}

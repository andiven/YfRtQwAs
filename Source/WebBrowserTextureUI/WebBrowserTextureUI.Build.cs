// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class WebBrowserTextureUI : ModuleRules
{
	public WebBrowserTextureUI(ReadOnlyTargetRules Target) : base(Target)
	{
		int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
		PublicDefinitions.Add("UE_VERSION=" + EngineVersion.ToString());

        if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.IOS || Target.bBuildEditor == true)
		{
			// needed for external texture support
			if (EngineVersion >= 501)
				PublicIncludePathModuleNames.AddRange(
					new string[]
					{
						"MediaUtils",
					}
				);
			else
            {
				PublicIncludePaths.AddRange(
					new string[]
					{
						"Runtime/MediaUtils/Public",
					}
				);
            }

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"RHI",
					"InputCore",
					"Slate",
					"SlateCore",
					"Serialization",
					"MediaUtils",
					"RenderCore",
					"Engine"
				}
			);

			if (EngineVersion < 424)
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UtilityShaders"
					}
				);
		}
		else
		{
			PrecompileForTargets = ModuleRules.PrecompileTargetsType.None;
		}
	}
}

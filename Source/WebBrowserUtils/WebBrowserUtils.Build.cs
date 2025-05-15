// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;

public class WebBrowserUtils : ModuleRules
{
	public WebBrowserUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
		PublicDefinitions.Add("UE_VERSION=" + EngineVersion.ToString());
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"Projects"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEFUI",
				"TracerWebAcceleratedPaint"
				);
		}
	}
}

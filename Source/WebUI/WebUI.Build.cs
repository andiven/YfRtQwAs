// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class WebUI : ModuleRules
	{
		public WebUI(ReadOnlyTargetRules Target) : base(Target)
		{
			int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
			PublicDefinitions.Add("UE_VERSION=" + EngineVersion.ToString());

			if (EngineVersion == 503)
				CppStandard = CppStandardVersion.Cpp17;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
                    "InputCore",
                    "RenderCore",
                    "RHI",
                    "Slate",
					"SlateCore",
					"UMG",
					"JsonLibrary"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HTTP"
				}
			);

            if (Target.bBuildEditor == true)
            {
            	PrivateIncludePathModuleNames.AddRange(
                    new string[]
                    {
                        "UnrealEd",
                    }
                );
                
                if (EngineVersion >= 500)
                	PrivateDependencyModuleNames.AddRange(
	                    new string[]
	                    {
							"EditorFramework",
	                    }
	                );
                
                PrivateDependencyModuleNames.AddRange(
                    new string[]
                    {
                        "UnrealEd",
                    }
                );
            }

			if (Target.Type != TargetType.Server)
			{
				if (Target.Platform == UnrealTargetPlatform.Win64)
					PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"WebBrowserUtils"
						}
					);

				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"WebBrowserUI"
					}
				);

				// only needed on mobile, but we also need to be able to cook the asset so we must include it in editor builds
				if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.IOS || Target.bBuildEditor == true)
				{
					PrivateIncludePathModuleNames.AddRange(
						new string[]
						{
							"WebBrowserTextureUI"
						}
					);

					PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"WebBrowserTextureUI"
						}
					);
				}
			}
		}
	}
}

// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class WebBrowserUI : ModuleRules
{
	public WebBrowserUI(ReadOnlyTargetRules Target) : base(Target)
	{
		int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
		PublicDefinitions.Add("UE_VERSION=" + EngineVersion.ToString());

		if (EngineVersion == 503)
			CppStandard = CppStandardVersion.Cpp17;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ApplicationCore",
				"RHI",
				"RenderCore",
				"InputCore",
				"ImageWriteQueue",
				"Serialization",
				"HTTP"
			}
		);

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
				"Slate",
				"SlateCore"
            }
        );

        if (Target.Platform == UnrealTargetPlatform.Android
		 || Target.Platform == UnrealTargetPlatform.IOS
		 || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			// We need these on mobile for external texture support
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Launch",
					"WebBrowserTextureUI"
				}
			);

			// We need this one on Android for URL decoding
			PrivateDependencyModuleNames.Add("HTTP");
		}

		if (Target.Type != TargetType.Program && Target.Platform == UnrealTargetPlatform.Win64)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"D3D12RHI",
					"D3D11RHI"
				}
			);

			if (EngineVersion < 427)
				PrivateIncludePaths.AddRange(
                    new string[]
					{
						Path.Combine(EngineDirectory, "Source/Runtime/Windows/D3D11RHI/Private"),
                        Path.Combine(EngineDirectory, "Source/Runtime/Windows/D3D11RHI/Private/Windows"),
						Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private"),
						Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private/Windows")
					}
				);

			PublicIncludePaths.AddRange(
				new string[]
				{
					Path.Combine(EngineDirectory, "Source/Runtime/Windows/D3D11RHI/Public"),
					Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Public"),
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12", "DX11");
		}

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
#if UE_4_24_OR_LATER
			PublicSystemLibraries.Add("libjnigraphics");
#endif

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "WebBrowserUI_UPL.xml"));
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("WebBrowserUtils");
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEFUI",
				"TracerWebAcceleratedPaint"
				);
			
			if (Target.Type != TargetType.Server)
			{
				RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", Target.Platform.ToString(), "TracerWebHelper.exe"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac
			 ||  Target.Platform == UnrealTargetPlatform.Linux)
		{
			PrivateDependencyModuleNames.Add("CEF3Utils");
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEF3"
				);

			if (Target.Type != TargetType.Server)
			{
				string ProcessName = EngineVersion >= 500 ? "EpicWebHelper" : "UnrealCEFSubProcess";
				if (Target.Platform == UnrealTargetPlatform.Mac && EngineVersion < 504)
				{
					// Add contents of *.app directory as runtime dependencies
					foreach (string FilePath in Directory.EnumerateFiles(Target.RelativeEnginePath + "/Binaries/Mac/" + ProcessName + ".app", "*", SearchOption.AllDirectories))
					{
						RuntimeDependencies.Add(FilePath);
					}
				}
				else if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux)
				{
					RuntimeDependencies.Add("$(EngineDir)/Binaries/" + Target.Platform.ToString() + "/" + ProcessName);
				}
				else
				{
					RuntimeDependencies.Add("$(EngineDir)/Binaries/" + Target.Platform.ToString() + "/" + ProcessName + ".exe");
				}
			}
		}
	}
}

// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserModule.h"
#include "WebInterfaceBrowserLog.h"
#include "WebInterfaceBrowserSingleton.h"
#include "Misc/App.h"
#include "Misc/CoreMisc.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#if WITH_CEF3
#if PLATFORM_WINDOWS
#include "WebBrowserUtils.h"
#else
#include "CEF3Utils.h"
#endif
#if PLATFORM_MAC
#		include "include/wrapper/cef_library_loader.h"
#  if UE_VERSION < 503
#		define CEF3_BIN_DIR TEXT("Binaries/ThirdParty/CEF3")
#  if UE_VERSION >= 503
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework.framework")
#  elif UE_VERSION >= 501
#     if PLATFORM_MAC_ARM64
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework arm64.framework")
#     else
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework x86.framework")
#     endif
#  else
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework.framework")
#  endif
#		define CEF3_FRAMEWORK_EXE CEF3_FRAMEWORK_DIR TEXT("/Chromium Embedded Framework")
#  endif
#endif
#endif

DEFINE_LOG_CATEGORY(LogWebInterfaceBrowser);

static FWebInterfaceBrowserSingleton* WebBrowserSingleton = nullptr;

FWebInterfaceBrowserInitSettings::FWebInterfaceBrowserInitSettings()
	: ProductVersion(FString::Printf(TEXT("%s/%s UnrealEngine/%s Chrome/%s"), FApp::GetProjectName(), FApp::GetBuildVersion(), *FEngineVersion::Current().ToString(EVersionComponent::Patch),
#if PLATFORM_WINDOWS
		TEXT("124.0.6367.207")
#elif UE_VERSION >= 501
		TEXT("90.0.4430.212")
#elif UE_VERSION >= 500
		TEXT("84.0.4147.38")
#else
		TEXT("59.0.3071.15")
#endif
	))
{
}

class FWebInterfaceBrowserModule : public IWebInterfaceBrowserModule
{
private:
	// IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	virtual bool IsWebModuleAvailable() const override;
	virtual IWebInterfaceBrowserSingleton* GetSingleton() override;
	virtual bool CustomInitialize(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings) override;

private:
#if WITH_CEF3
	bool bLoadedCEFModule = false;
#if PLATFORM_MAC
	// Dynamically load the CEF framework library.
	CefScopedLibraryLoader *CEFLibraryLoader = nullptr;
#endif
#endif
};

IMPLEMENT_MODULE( FWebInterfaceBrowserModule, WebBrowserUI );

void FWebInterfaceBrowserModule::StartupModule()
{
#if WITH_CEF3
	if (!IsRunningCommandlet())
	{
#if PLATFORM_WINDOWS
#if UE_EDITOR
		checkf(WebBrowserUtils::CheckCEF3Modules(), TEXT("WebUI PLUGIN NOT IN /Engine/Plugins/Marketplace DIRECTORY!"));
		checkf(WebBrowserUtils::CheckCEF3Helper(), TEXT("TracerWebHelper.exe NOT IN /Binaries/Win64 DIRECTORY!"));
#endif

		WebBrowserUtils::BackupCEF3Logfile(FPaths::ProjectLogDir());
#elif UE_VERSION >= 501
		CEF3Utils::BackupCEF3Logfile(FPaths::ProjectLogDir());
#endif
	}
#if PLATFORM_WINDOWS
	bLoadedCEFModule = WebBrowserUtils::LoadCEF3Modules(true);
	if (bLoadedCEFModule)
	{
		WebBrowserUtils::RegisterLicenseForProject();
		if (WebBrowserUtils::CanSupportAcceleratedPaint())
		{
			const bool bHasKey = WebBrowserUtils::HasKey();
			if (bHasKey && !WebBrowserUtils::VerifyLicenseForProject())
			{
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("Invalid WebUI License Key! You must obtain a valid license or disable \"Accelerated Paint\" in the Project Settings under the \"Plugins > WebUI\" section."), TEXT("Error"));
			}
	#if !UE_EDITOR
			else if (!bHasKey)
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("Missing WebUI License Key! You must obtain a license or disable \"Accelerated Paint\" in the Project Settings under the \"Plugins > WebUI\" section."), TEXT("Error"));
	#endif
		}
	}
#else
	bLoadedCEFModule = CEF3Utils::LoadCEF3Modules(true);
#endif
#if PLATFORM_MAC
	// Dynamically load the CEF framework library into this dylibs memory space.
	// CEF now loads function pointers at runtime so we need this to be dylib specific.
	CEFLibraryLoader = new CefScopedLibraryLoader();
	
#if UE_VERSION >= 503
	if (!CEFLibraryLoader->LoadInMain(TCHAR_TO_ANSI(*CEF3Utils::GetCEF3ModulePath())))
	{
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("Chromium loader initialization failed"));
	}
#else
	FString CefFrameworkPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_FRAMEWORK_EXE));
	CefFrameworkPath = FPaths::ConvertRelativePathToFull(CefFrameworkPath);
	
	bool bLoaderInitialized = false;
	if (!CEFLibraryLoader->LoadInMain(TCHAR_TO_ANSI(*CefFrameworkPath)))
	{
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("Chromium loader initialization failed"));
	}
#endif
#endif // PLATFORM_MAC
#endif
}

void FWebInterfaceBrowserModule::ShutdownModule()
{
	if (WebBrowserSingleton != nullptr)
	{
		delete WebBrowserSingleton;
		WebBrowserSingleton = nullptr;
	}

#if WITH_CEF3
#if PLATFORM_WINDOWS
	WebBrowserUtils::UnloadCEF3Modules();
#else
	CEF3Utils::UnloadCEF3Modules();
#endif
#if PLATFORM_MAC
	delete CEFLibraryLoader;
	CEFLibraryLoader = nullptr;
#endif // PLATFORM_MAC
#endif
}

bool FWebInterfaceBrowserModule::CustomInitialize(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings)
{
	if (WebBrowserSingleton == nullptr)
	{
		WebBrowserSingleton = new FWebInterfaceBrowserSingleton(WebBrowserInitSettings);
		return true;
	}
	return false;
}

IWebInterfaceBrowserSingleton* FWebInterfaceBrowserModule::GetSingleton()
{
	if (WebBrowserSingleton == nullptr)
	{
		WebBrowserSingleton = new FWebInterfaceBrowserSingleton(FWebInterfaceBrowserInitSettings());
	}
	return WebBrowserSingleton;
}


bool FWebInterfaceBrowserModule::IsWebModuleAvailable() const
{
#if WITH_CEF3
	return bLoadedCEFModule;
#else
	return true;
#endif
}

// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserSingleton.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Internationalization/Culture.h"
#include "Misc/App.h"
#include "WebInterfaceBrowserModule.h"
#include "Misc/EngineVersion.h"
#include "Framework/Application/SlateApplication.h"
#include "IWebInterfaceBrowserCookieManager.h"
#include "WebInterfaceBrowserLog.h"

#if PLATFORM_WINDOWS
#include "WebBrowserUtils.h"
#if UE_VERSION < 504
#include "Windows/WindowsHWrapper.h"
#endif
#endif

#if WITH_CEF3
#include "Misc/ScopeLock.h"
#include "Async/Async.h"
#include "HAL/PlatformApplicationMisc.h"
#include "CEF/CEFInterfaceBrowserApp.h"
#include "CEF/CEFInterfaceBrowserHandler.h"
#include "CEF/CEFWebInterfaceBrowserWindow.h"
#include "CEF/CEFInterfaceSchemeHandler.h"
#include "CEF/CEFInterfaceResourceContextHandler.h"
#include "CEF/CEFInterfaceBrowserClosureTask.h"
#	if PLATFORM_WINDOWS
#		include "Windows/AllowWindowsPlatformTypes.h"
#	endif
#	pragma push_macro("OVERRIDE")
#		undef OVERRIDE // cef headers provide their own OVERRIDE macro
THIRD_PARTY_INCLUDES_START
#if PLATFORM_APPLE
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#endif
#		include "include/cef_app.h"
#		include "include/cef_version.h"
#if PLATFORM_APPLE
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
THIRD_PARTY_INCLUDES_END
#	pragma pop_macro("OVERRIDE")
#	if PLATFORM_WINDOWS
#		include "Windows/HideWindowsPlatformTypes.h"
#	endif
#endif

#if BUILD_EMBEDDED_APP
#	include "Native/NativeWebInterfaceBrowserProxy.h"
#endif

#if PLATFORM_ANDROID && USE_ANDROID_JNI
#	include "Android/AndroidWebInterfaceBrowserWindow.h"
#	include <Android/AndroidInterfaceCookieManager.h>
#elif PLATFORM_IOS
#	include <IOS/IOSPlatformWebInterfaceBrowser.h>
#	include <IOS/IOSInterfaceCookieManager.h>
#endif

// Define some platform-dependent file locations
#if WITH_CEF3
#	if PLATFORM_WINDOWS && PLATFORM_64BITS
#	define CEF3_BIN_DIR TEXT("Plugins/Marketplace/WebUI/Binaries")
#	define CEF3_BIN_DIR_THIRD_PARTY CEF3_BIN_DIR TEXT("/ThirdParty/CEF3")
#   else
#	define CEF3_BIN_DIR TEXT("Binaries/ThirdParty/CEF3")
#   endif
#	if PLATFORM_WINDOWS && PLATFORM_64BITS
#		define CEF3_RESOURCES_DIR CEF3_BIN_DIR_THIRD_PARTY TEXT("/Win64/Resources")
#		define CEF3_SUBPROCES_EXE CEF3_BIN_DIR TEXT("/Win64/TracerWebHelper.exe")
#	elif PLATFORM_WINDOWS && PLATFORM_32BITS
#		define CEF3_RESOURCES_DIR CEF3_BIN_DIR TEXT("/Win32/Resources")
#   if UE_VERSION >= 500
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Win32/EpicWebHelper.exe")
#   else
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Win32/UnrealCEFSubProcess.exe")
#   endif
#	elif PLATFORM_MAC
#     if UE_VERSION >= 503
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework.framework")
#     elif UE_VERSION >= 501
#     if PLATFORM_MAC_ARM64
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework arm64.framework")
#     else
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework x86.framework")
#     endif
#     else
#		define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR TEXT("/Mac/Chromium Embedded Framework.framework")
#     endif
#		define CEF3_RESOURCES_DIR CEF3_FRAMEWORK_DIR TEXT("/Resources")
#     if UE_VERSION >= 504
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Mac/EpicWebHelper")
#     elif UE_VERSION >= 500
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Mac/EpicWebHelper.app/Contents/MacOS/EpicWebHelper")
#     else
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Mac/UnrealCEFSubProcess.app/Contents/MacOS/UnrealCEFSubProcess")
#     endif
#	elif PLATFORM_LINUX // @todo Linux
#		define CEF3_RESOURCES_DIR CEF3_BIN_DIR TEXT("/Linux/Resources")
#     if UE_VERSION >= 500
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Linux/EpicWebHelper")
#     else
#		define CEF3_SUBPROCES_EXE TEXT("Binaries/Linux/UnrealCEFSubProcess")
#     endif
#	endif
	// Caching is enabled by default.
#	ifndef CEF3_DEFAULT_CACHE
#		define CEF3_DEFAULT_CACHE 1
#	endif
#endif

#if UE_VERSION < 425
namespace {

	/**
	 * Helper function to set the current thread name, visible by the debugger.
	 * @param ThreadName	Name to set
	 */
	void SetCurrentThreadName(char* ThreadName)
	{
#if PLATFORM_MAC
		pthread_setname_np(ThreadName);
#elif PLATFORM_LINUX
		pthread_setname_np(pthread_self(), ThreadName);
#elif PLATFORM_WINDOWS && !PLATFORM_SEH_EXCEPTIONS_DISABLED
		/**
		 * Code setting the thread name for use in the debugger.
		 * Copied implementation from WindowsRunnableThread as it is private.
		 *
		 * http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
		 */
		const uint32 MS_VC_EXCEPTION=0x406D1388;

		struct THREADNAME_INFO
		{
			uint32 dwType;		// Must be 0x1000.
			LPCSTR szName;		// Pointer to name (in user addr space).
			uint32 dwThreadID;	// Thread ID (-1=caller thread).
			uint32 dwFlags;		// Reserved for future use, must be zero.
		};

		THREADNAME_INFO ThreadNameInfo = {0x1000, ThreadName, (uint32)-1, 0};

		__try
		{
			RaiseException( MS_VC_EXCEPTION, 0, sizeof(ThreadNameInfo)/sizeof(ULONG_PTR), (ULONG_PTR*)&ThreadNameInfo );
		}
		__except( EXCEPTION_EXECUTE_HANDLER )
		CA_SUPPRESS(6322)
		{
		}
#endif
	}
}
#endif

FString FWebInterfaceBrowserSingleton::ApplicationCacheDir() const
{
#if PLATFORM_MAC
	// OSX wants caches in a separate location from other app data
	static TCHAR Result[MAC_MAX_PATH] = TEXT("");
	if (!Result[0])
	{
		SCOPED_AUTORELEASE_POOL;
		NSString *CacheBaseDir = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) objectAtIndex: 0];
		NSString* BundleID = [[NSBundle mainBundle] bundleIdentifier];
		if(!BundleID)
		{
			BundleID = [[NSProcessInfo processInfo] processName];
		}
		check(BundleID);

		NSString* AppCacheDir = [CacheBaseDir stringByAppendingPathComponent: BundleID];
		FPlatformString::CFStringToTCHAR((CFStringRef)AppCacheDir, Result);
	}
	return FString(Result);
#else
	// Other platforms use the application data directory
	return FPaths::ProjectSavedDir();
#endif
}


class FWebInterfaceBrowserWindowFactory
	: public IWebInterfaceBrowserWindowFactory
{
public:

	virtual ~FWebInterfaceBrowserWindowFactory()
	{ }

	virtual TSharedPtr<IWebInterfaceBrowserWindow> Create(
		TSharedPtr<FCEFWebInterfaceBrowserWindow>& BrowserWindowParent,
		TSharedPtr<FWebInterfaceBrowserWindowInfo>& BrowserWindowInfo) override
	{
		return IWebInterfaceBrowserModule::Get().GetSingleton()->CreateBrowserWindow(
			BrowserWindowParent,
			BrowserWindowInfo);
	}

	virtual TSharedPtr<IWebInterfaceBrowserWindow> Create(
		void* OSWindowHandle,
		FString InitialURL,
		bool bUseTransparency,
		bool bThumbMouseButtonNavigation,
		bool bInterceptLoadRequests = false,
		TOptional<FString> ContentsToLoad = TOptional<FString>(),
		bool ShowErrorMessage = true,
		FColor BackgroundColor = FColor(255, 255, 255, 255)) override
	{
		FCreateInterfaceBrowserWindowSettings Settings;
		Settings.OSWindowHandle = OSWindowHandle;
		Settings.InitialURL = MoveTemp(InitialURL);
		Settings.bUseTransparency = bUseTransparency;
		Settings.bThumbMouseButtonNavigation = bThumbMouseButtonNavigation;
		Settings.ContentsToLoad = MoveTemp(ContentsToLoad);
		Settings.bShowErrorMessage = ShowErrorMessage;
		Settings.BackgroundColor = BackgroundColor;
		Settings.bInterceptLoadRequests = bInterceptLoadRequests;

		return IWebInterfaceBrowserModule::Get().GetSingleton()->CreateBrowserWindow(Settings);
	}
};

class FNoWebInterfaceBrowserWindowFactory
	: public IWebInterfaceBrowserWindowFactory
{
public:

	virtual ~FNoWebInterfaceBrowserWindowFactory()
	{ }

	virtual TSharedPtr<IWebInterfaceBrowserWindow> Create(
		TSharedPtr<FCEFWebInterfaceBrowserWindow>& BrowserWindowParent,
		TSharedPtr<FWebInterfaceBrowserWindowInfo>& BrowserWindowInfo) override
	{
		return nullptr;
	}

	virtual TSharedPtr<IWebInterfaceBrowserWindow> Create(
		void* OSWindowHandle,
		FString InitialURL,
		bool bUseTransparency,
		bool bThumbMouseButtonNavigation,
		bool bInterceptLoadRequests = false,
		TOptional<FString> ContentsToLoad = TOptional<FString>(),
		bool ShowErrorMessage = true,
		FColor BackgroundColor = FColor(255, 255, 255, 255)) override
	{
		return nullptr;
	}
};

#if WITH_CEF3
#if PLATFORM_MAC || PLATFORM_LINUX
class FInterfacePosixSignalPreserver
{
public:
	FInterfacePosixSignalPreserver()
	{
		struct sigaction Sigact;
		for (uint32 i = 0; i < UE_ARRAY_COUNT(PreserveSignals); ++i)
		{
			FMemory::Memset(&Sigact, 0, sizeof(Sigact));
			if (sigaction(PreserveSignals[i], nullptr, &Sigact) != 0)
			{
				UE_LOG(LogWebInterfaceBrowser, Warning, TEXT("Failed to backup signal handler for %i."), PreserveSignals[i]);
			}
			OriginalSignalHandlers[i] = Sigact;
		}
	}

	~FInterfacePosixSignalPreserver()
	{
		for (uint32 i = 0; i < UE_ARRAY_COUNT(PreserveSignals); ++i)
		{
			if(sigaction(PreserveSignals[i], &OriginalSignalHandlers[i], nullptr) != 0)
			{
				UE_LOG(LogWebInterfaceBrowser, Warning, TEXT("Failed to restore signal handler for %i."), PreserveSignals[i]);
			}
		}
	}

private:
	// Backup the list of signals that CEF/Chromium overrides, derived from SetupSignalHandlers() in
	//  https://chromium.googlesource.com/chromium/src.git/+/2fc330d0b93d4bfd7bd04b9fdd3102e529901f91/services/service_manager/embedder/main.cc
	const int PreserveSignals[13] = {SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT,
		SIGFPE, SIGSEGV, SIGALRM, SIGTERM, SIGCHLD, SIGBUS, SIGTRAP, SIGPIPE};

	struct sigaction OriginalSignalHandlers[UE_ARRAY_COUNT(PreserveSignals)];
};

#endif // PLATFORM_MAC || PLATFORM_LINUX
#endif // WITH_CEF3


FWebInterfaceBrowserSingleton::FWebInterfaceBrowserSingleton(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings)
#if WITH_CEF3
	: WebBrowserWindowFactory(MakeShareable(new FWebInterfaceBrowserWindowFactory()))
#else
	: WebBrowserWindowFactory(MakeShareable(new FNoWebInterfaceBrowserWindowFactory()))
#endif
	, bDevToolsShortcutEnabled(UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG)
	, bJSBindingsToLoweringEnabled(true)
	, bAppIsFocused(false)
#if WITH_CEF3
	, bCEFInitialized(false)
#endif
	, DefaultMaterial(nullptr)
	, DefaultTranslucentMaterial(nullptr)
{
#if WITH_CEF3

	// Only enable CEF if we have CEF3, we are not running a commandlet without rendering (e.g. cooking assets) and it has not been explicitly disabled
	// Disallow CEF if we never plan on rendering, ie, with CanEverRender. This includes servers
	bAllowCEF = (!IsRunningCommandlet() || (IsAllowCommandletRendering() && FParse::Param(FCommandLine::Get(), TEXT("AllowCommandletCEF")))) &&
				FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("nocef"));
	if (bAllowCEF)
	{
		// The FWebBrowserSingleton must be initialized on the game thread
		check(IsInGameThread());

		// Provide CEF with command-line arguments.
#if PLATFORM_WINDOWS
		CefMainArgs MainArgs(hInstance);
#else
		CefMainArgs MainArgs;

		// Enable high-DPI support early in CEF startup. For this to work it also depends
		//  on FPlatformApplicationMisc::SetHighDPIMode() being called already which should happen by default
		CefEnableHighDPISupport();
#endif

		bool bVerboseLogging = FParse::Param(FCommandLine::Get(), TEXT("cefverbose")) || FParse::Param(FCommandLine::Get(), TEXT("debuglog"));
		// CEFBrowserApp implements application-level callbacks.
		CEFBrowserApp = new FCEFInterfaceBrowserApp;

		// Specify CEF global settings here.
		CefSettings Settings;
		Settings.no_sandbox = true;
		Settings.command_line_args_disabled = true;
		Settings.external_message_pump = true;
		//@todo change to threaded version instead of using external_message_pump & OnScheduleMessagePumpWork
		Settings.multi_threaded_message_loop = false;
		//Set the default background for browsers to be opaque black, this is used for windowed (not OSR) browsers
		//  setting it black here prevents the white flash on load
		Settings.background_color = CefColorSetARGB(255, 0, 0, 0);

#if UE_VERSION >= 501
#if PLATFORM_LINUX
		Settings.windowless_rendering_enabled = true;
#endif
#endif

		FString CefLogFile(FPaths::Combine(*FPaths::ProjectLogDir(), TEXT("cef3.log")));
		CefLogFile = FPaths::ConvertRelativePathToFull(CefLogFile);
		CefString(&Settings.log_file) = TCHAR_TO_WCHAR(*CefLogFile);
		Settings.log_severity = bVerboseLogging ? LOGSEVERITY_VERBOSE : LOGSEVERITY_WARNING;

		uint16 DebugPort;
		if(FParse::Value(FCommandLine::Get(), TEXT("cefdebug="), DebugPort))
		{
			Settings.remote_debugging_port = DebugPort;
		}

		// Specify locale from our settings
		FString LocaleCode = GetCurrentLocaleCode();
		CefString(&Settings.locale) = TCHAR_TO_WCHAR(*LocaleCode);

		// Append engine version to the user agent string.
#if PLATFORM_WINDOWS || UE_VERSION >= 501
		CefString(&Settings.user_agent_product) = TCHAR_TO_WCHAR(*WebBrowserInitSettings.ProductVersion);
#else
		CefString(&Settings.product_version) = TCHAR_TO_WCHAR(*WebBrowserInitSettings.ProductVersion);
#endif

#if CEF3_DEFAULT_CACHE
		// Enable on disk cache
		FString CachePath(FPaths::Combine(ApplicationCacheDir(), TEXT("webcache")));
		CachePath = FPaths::ConvertRelativePathToFull(GenerateWebCacheFolderName(CachePath));
		CefString(&Settings.cache_path) = TCHAR_TO_WCHAR(*CachePath);
#endif

		// Specify path to resources
		FString ResourcesPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_RESOURCES_DIR));
		ResourcesPath = FPaths::ConvertRelativePathToFull(ResourcesPath);
#if PLATFORM_WINDOWS
		ResourcesPath = WebBrowserUtils::FixupPluginPath(ResourcesPath);
#endif

		if (!FPaths::DirectoryExists(ResourcesPath))
		{
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("Chromium Resources information not found at: %s."), *ResourcesPath);
		}
		CefString(&Settings.resources_dir_path) = TCHAR_TO_WCHAR(*ResourcesPath);

#if !PLATFORM_MAC
		// On Mac Chromium ignores custom locales dir. Files need to be stored in Resources folder in the app bundle
		FString LocalesPath(FPaths::Combine(*ResourcesPath, TEXT("locales")));
		LocalesPath = FPaths::ConvertRelativePathToFull(LocalesPath);
		if (!FPaths::DirectoryExists(LocalesPath))
		{
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("Chromium Locales information not found at: %s."), *LocalesPath);
		}
		CefString(&Settings.locales_dir_path) = TCHAR_TO_WCHAR(*LocalesPath);
#else
		// LocaleCode may contain region, which for some languages may make CEF unable to find the locale pak files
		// In that case use the language name for CEF locale
		FString LocalePakPath = ResourcesPath + TEXT("/") + LocaleCode.Replace(TEXT("-"), TEXT("_")) + TEXT(".lproj/locale.pak");
		if (!FPaths::FileExists(LocalePakPath))
		{
			FCultureRef Culture = FInternationalization::Get().GetCurrentCulture();
			LocaleCode = Culture->GetTwoLetterISOLanguageName();
			LocalePakPath = ResourcesPath + TEXT("/") + LocaleCode + TEXT(".lproj/locale.pak");
			if (FPaths::FileExists(LocalePakPath))
			{
				CefString(&Settings.locale) = TCHAR_TO_WCHAR(*LocaleCode);
			}
		}

		// Let CEF know where we have put the framework bundle as it is non-default
		FString CefFrameworkPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_FRAMEWORK_DIR));
		CefFrameworkPath = FPaths::ConvertRelativePathToFull(CefFrameworkPath);
#if PLATFORM_WINDOWS
		CefFrameworkPath = WebBrowserUtils::FixupPluginPath(CefFrameworkPath);
#endif
		CefString(&Settings.framework_dir_path) = TCHAR_TO_WCHAR(*CefFrameworkPath);
		CefString(&Settings.main_bundle_path) = TCHAR_TO_WCHAR(*CefFrameworkPath);
#endif

		// Specify path to sub process exe
		FString SubProcessPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_SUBPROCES_EXE));
		SubProcessPath = FPaths::ConvertRelativePathToFull(SubProcessPath);
#if PLATFORM_WINDOWS
		SubProcessPath = WebBrowserUtils::FixupPluginPath(SubProcessPath);
#endif

		if (!IPlatformFile::GetPlatformPhysical().FileExists(*SubProcessPath))
		{
#if PLATFORM_WINDOWS
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("TracerWebHelper.exe not found, check that this program has been built and is placed in: %s."), *SubProcessPath);
#elif UE_VERSION >= 500
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("EpicWebHelper.exe not found, check that this program has been built and is placed in: %s."), *SubProcessPath);
#else
			UE_LOG(LogWebInterfaceBrowser, Error, TEXT("UnrealCEFSubProcess.exe not found, check that this program has been built and is placed in: %s."), *SubProcessPath);
#endif
		}
		CefString(&Settings.browser_subprocess_path) = TCHAR_TO_WCHAR(*SubProcessPath);

#if PLATFORM_MAC || PLATFORM_LINUX
		// this class automatically preserves the sigaction handlers we have set
		FInterfacePosixSignalPreserver PosixSignalPreserver;
#endif

		// Initialize CEF.
		bCEFInitialized = CefInitialize(MainArgs, Settings, CEFBrowserApp.get(), nullptr);
		check(bCEFInitialized);

		// Set the thread name back to GameThread.
#if UE_VERSION >= 425
		FPlatformProcess::SetThreadName(*FName(NAME_GameThread).GetPlainNameString());
#else
		SetCurrentThreadName(TCHAR_TO_ANSI(*(FName(NAME_GameThread).GetPlainNameString())));
#endif

		DefaultCookieManager = FCefWebInterfaceCookieManagerFactory::Create(CefCookieManager::GetGlobalManager(nullptr));
	}
#elif PLATFORM_IOS && !BUILD_EMBEDDED_APP
	DefaultCookieManager = MakeShareable(new FIOSInterfaceCookieManager());
#elif PLATFORM_ANDROID
	DefaultCookieManager = MakeShareable(new FAndroidInterfaceCookieManager());
#endif
}

#if WITH_CEF3
void FWebInterfaceBrowserSingleton::WaitForTaskQueueFlush()
{
	// Keep pumping messages until we see the one below clear the queue
	bTaskFinished = false;
	CefPostTask(TID_UI, new FCEFInterfaceBrowserClosureTask(nullptr, [=
#if UE_VERSION >= 504
		, this
#endif
		]()
		{
			bTaskFinished = true;
		}));

	const double StartWaitAppTime = FPlatformTime::Seconds();
	while (!bTaskFinished)
	{
		FPlatformProcess::Sleep(0.01);
		// CEF needs the windows message pump run to be able to finish closing a browser, so run it manually here
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().PumpMessages();
		}
		CefDoMessageLoopWork();
		// Wait at most 1 second for tasks to clear, in case CEF crashes/hangs during process lifetime
		if (FPlatformTime::Seconds() - StartWaitAppTime > 1.0f)
		{
			break; // don't spin forever
		}
	}
}
#endif


FWebInterfaceBrowserSingleton::~FWebInterfaceBrowserSingleton()
{
#if WITH_CEF3
	if (!bCEFInitialized)
		return; // CEF failed to init so don't crash trying to shut it down

	if (bAllowCEF)
	{
		{
			FScopeLock Lock(&WindowInterfacesCS);
			// Force all existing browsers to close in case any haven't been deleted
			for (int32 Index = 0; Index < WindowInterfaces.Num(); ++Index)
			{
				auto BrowserWindow = WindowInterfaces[Index].Pin();
				if (BrowserWindow.IsValid() && BrowserWindow->IsValid())
				{
					// Call CloseBrowser directly on the Host object as FWebInterfaceBrowserWindow::CloseBrowser is delayed
					BrowserWindow->InternalCefBrowser->GetHost()->CloseBrowser(true);
				}
			}
			// Clear this before CefShutdown() below
			WindowInterfaces.Reset();
		}

		// Remove references to the scheme handler factories
		CefClearSchemeHandlerFactories();
		for (const TPair<FString, CefRefPtr<CefRequestContext>>& RequestContextPair : RequestContexts)
		{
			RequestContextPair.Value->ClearSchemeHandlerFactories();
		}
		// Clear this before CefShutdown() below
		RequestContexts.Reset();

		// make sure any handler before load delegates are unbound
		for (const TPair <FString,CefRefPtr<FCEFInterfaceResourceContextHandler>>& HandlerPair : RequestResourceHandlers)
		{
			HandlerPair.Value->OnBeforeLoad().Unbind();
		}
		// Clear this before CefShutdown() below
		RequestResourceHandlers.Reset();
		// CefRefPtr takes care of delete
		CEFBrowserApp = nullptr;

		WaitForTaskQueueFlush();

		// Shut down CEF.
		CefShutdown();
	}
	bCEFInitialized = false;
#elif PLATFORM_IOS || (PLATFORM_ANDROID && USE_ANDROID_JNI)
	{
		FScopeLock Lock(&WindowInterfacesCS);
		// Clear this before CefShutdown() below
		WindowInterfaces.Reset();
	}
#endif
}

TSharedRef<IWebInterfaceBrowserWindowFactory> FWebInterfaceBrowserSingleton::GetWebBrowserWindowFactory() const
{
	return WebBrowserWindowFactory;
}

TSharedPtr<IWebInterfaceBrowserWindow> FWebInterfaceBrowserSingleton::CreateBrowserWindow(
	TSharedPtr<FCEFWebInterfaceBrowserWindow>& BrowserWindowParent,
	TSharedPtr<FWebInterfaceBrowserWindowInfo>& BrowserWindowInfo
	)
{
#if WITH_CEF3
	if (bAllowCEF)
	{
		TOptional<FString> ContentsToLoad;

		bool bShowErrorMessage = BrowserWindowParent->IsShowingErrorMessages();
		bool bThumbMouseButtonNavigation = BrowserWindowParent->IsThumbMouseButtonNavigationEnabled();
		bool bUseTransparency = BrowserWindowParent->UseTransparency();
		bool bUsingAcceleratedPaint = BrowserWindowParent->UsingAcceleratedPaint();
		bool bUseNativeCursors = BrowserWindowParent->UseNativeCursors();
		FString InitialURL = WCHAR_TO_TCHAR(BrowserWindowInfo->Browser->GetMainFrame()->GetURL().ToWString().c_str());
		TSharedPtr<FCEFWebInterfaceBrowserWindow> NewBrowserWindow(new FCEFWebInterfaceBrowserWindow(BrowserWindowInfo->Browser, BrowserWindowInfo->Handler, InitialURL, ContentsToLoad, bShowErrorMessage, bThumbMouseButtonNavigation, bUseTransparency, bUseNativeCursors, bJSBindingsToLoweringEnabled, bUsingAcceleratedPaint));
		BrowserWindowInfo->Handler->SetBrowserWindow(NewBrowserWindow);
		{
			FScopeLock Lock(&WindowInterfacesCS);
			WindowInterfaces.Add(NewBrowserWindow);
		}
		NewBrowserWindow->GetCefBrowser()->GetHost()->SetWindowlessFrameRate(BrowserWindowParent->GetCefBrowser()->GetHost()->GetWindowlessFrameRate());
		return NewBrowserWindow;
	}
#endif
	return nullptr;
}

TSharedPtr<IWebInterfaceBrowserWindow> FWebInterfaceBrowserSingleton::CreateBrowserWindow(const FCreateInterfaceBrowserWindowSettings& WindowSettings)
{
	bool bBrowserEnabled = true;
	GConfig->GetBool(TEXT("Browser"), TEXT("bEnabled"), bBrowserEnabled, GEngineIni);
	if (!bBrowserEnabled || !FApp::CanEverRender())
	{
		return nullptr;
	}

#if WITH_CEF3
	if (bAllowCEF)
	{
		// Information used when creating the native window.
		CefWindowInfo WindowInfo;

		// Specify CEF browser settings here.
		CefBrowserSettings BrowserSettings;

		// The color to paint before a document is loaded
		// if using a windowed(native) browser window AND bUseTransparency is true then the background actually uses Settings.background_color from above
		// if using a OSR window and bUseTransparency is true then you get a transparency channel in your BGRA OnPaint
		// if bUseTransparency is false then you get the background color defined by your RGB setting here
		BrowserSettings.background_color = CefColorSetARGB(WindowSettings.bUseTransparency ? 0 : WindowSettings.BackgroundColor.A, WindowSettings.BackgroundColor.R, WindowSettings.BackgroundColor.G, WindowSettings.BackgroundColor.B);

#if !PLATFORM_WINDOWS
		// Disable plugins
		BrowserSettings.plugins = STATE_DISABLED;
#endif


#if PLATFORM_WINDOWS
		// Create the widget as a child window on windows when passing in a parent window
		if (WindowSettings.OSWindowHandle != nullptr)
		{
			RECT ClientRect = { 0, 0, 0, 0 };
			if (!GetClientRect((HWND)WindowSettings.OSWindowHandle, &ClientRect))
			{
				UE_LOG(LogWebInterfaceBrowser, Error, TEXT("Failed to get client rect"));
			}

			CefRect WindowRect;
			WindowRect.x = ClientRect.left;
			WindowRect.y = ClientRect.top;
			WindowRect.width  = ClientRect.right  - ClientRect.left;
			WindowRect.height = ClientRect.bottom - ClientRect.top;

			WindowInfo.SetAsChild((CefWindowHandle)WindowSettings.OSWindowHandle, WindowRect);
		}
		else
#endif
		{
			// Use off screen rendering so we can integrate with our windows
			WindowInfo.SetAsWindowless(kNullWindowHandle);
			WindowInfo.shared_texture_enabled = ( WindowSettings.bAcceleratedPaint && FCEFWebInterfaceBrowserWindow::CanSupportAcceleratedPaint() ) ? 1 : 0;
			BrowserSettings.windowless_frame_rate = WindowSettings.BrowserFrameRate;
		}
		
		TArray<FString> AuthorizationHeaderAllowListURLS;
		GConfig->GetArray(TEXT("Browser"), TEXT("AuthorizationHeaderAllowListURLS"), AuthorizationHeaderAllowListURLS, GEngineIni);

		// WebBrowserHandler implements browser-level callbacks.
		CefRefPtr<FCEFInterfaceBrowserHandler> NewHandler(new FCEFInterfaceBrowserHandler(WindowSettings.bUseTransparency, WindowSettings.bInterceptLoadRequests ,WindowSettings.AltRetryDomains, AuthorizationHeaderAllowListURLS));

		CefRefPtr<CefRequestContext> RequestContext = nullptr;
		if (WindowSettings.Context.IsSet())
		{
			const FInterfaceBrowserContextSettings Context = WindowSettings.Context.GetValue();
			const CefRefPtr<CefRequestContext>* ExistingRequestContext = RequestContexts.Find(Context.Id);

			if (ExistingRequestContext == nullptr)
			{
				CefRequestContextSettings RequestContextSettings;
				CefString(&RequestContextSettings.accept_language_list) = Context.AcceptLanguageList.IsEmpty() ? TCHAR_TO_WCHAR(*GetCurrentLocaleCode()) : TCHAR_TO_WCHAR(*Context.AcceptLanguageList);
				CefString(&RequestContextSettings.cache_path) = TCHAR_TO_WCHAR(*GenerateWebCacheFolderName(Context.CookieStorageLocation));
				RequestContextSettings.persist_session_cookies = Context.bPersistSessionCookies;
#if !PLATFORM_WINDOWS
				RequestContextSettings.ignore_certificate_errors = Context.bIgnoreCertificateErrors;
#endif

				CefRefPtr<FCEFInterfaceResourceContextHandler> ResourceContextHandler = new FCEFInterfaceResourceContextHandler(this);
				ResourceContextHandler->OnBeforeLoad() = Context.OnBeforeContextResourceLoad;
				RequestResourceHandlers.Add(Context.Id, ResourceContextHandler);

				//Create a new one
				RequestContext = CefRequestContext::CreateContext(RequestContextSettings, ResourceContextHandler);
				RequestContexts.Add(Context.Id, RequestContext);
			}
			else
			{
				RequestContext = *ExistingRequestContext;
			}
			SchemeHandlerFactories.RegisterFactoriesWith(RequestContext);
			UE_LOG(LogWebInterfaceBrowser, Log, TEXT("Creating browser for ContextId=%s."), *WindowSettings.Context.GetValue().Id);
		}
		if (RequestContext == nullptr)
		{
			// As of CEF drop 4430 the CreateBrowserSync call requires a non-null request context, so fall back to the default one if needed
			RequestContext = CefRequestContext::GetGlobalContext();
		}

		// Create the CEF browser window.
		CefRefPtr<CefBrowser> Browser = CefBrowserHost::CreateBrowserSync(WindowInfo, NewHandler.get(), TCHAR_TO_WCHAR(*WindowSettings.InitialURL), BrowserSettings, nullptr, RequestContext);
		if (Browser.get())
		{
			// Create new window
			TSharedPtr<FCEFWebInterfaceBrowserWindow> NewBrowserWindow = MakeShareable(new FCEFWebInterfaceBrowserWindow(
				Browser,
				NewHandler,
				WindowSettings.InitialURL,
				WindowSettings.ContentsToLoad,
				WindowSettings.bShowErrorMessage,
				WindowSettings.bThumbMouseButtonNavigation,
				WindowSettings.bUseTransparency,
				WindowSettings.bUseNativeCursors,
				bJSBindingsToLoweringEnabled,
				WindowInfo.shared_texture_enabled == 1 ? true : false));
			NewHandler->SetBrowserWindow(NewBrowserWindow);
			{
				FScopeLock Lock(&WindowInterfacesCS);
				WindowInterfaces.Add(NewBrowserWindow);
			}

			return NewBrowserWindow;
		}
	}
#elif PLATFORM_ANDROID && USE_ANDROID_JNI
	// Create new window
	TSharedPtr<FAndroidWebInterfaceBrowserWindow> NewBrowserWindow = MakeShareable(new FAndroidWebInterfaceBrowserWindow(
		WindowSettings.InitialURL,
		WindowSettings.ContentsToLoad,
		WindowSettings.bShowErrorMessage,
		WindowSettings.bThumbMouseButtonNavigation,
		WindowSettings.bUseTransparency,
		bJSBindingsToLoweringEnabled));

	{
		FScopeLock Lock(&WindowInterfacesCS);
		WindowInterfaces.Add(NewBrowserWindow);
	}
	return NewBrowserWindow;
#elif PLATFORM_IOS
	// Create new window
	TSharedPtr<FWebInterfaceBrowserWindow> NewBrowserWindow = MakeShareable(new FWebInterfaceBrowserWindow(
		WindowSettings.InitialURL, 
		WindowSettings.ContentsToLoad, 
		WindowSettings.bShowErrorMessage, 
		WindowSettings.bThumbMouseButtonNavigation, 
		WindowSettings.bUseTransparency,
		bJSBindingsToLoweringEnabled));

	{
		FScopeLock Lock(&WindowInterfacesCS);
		WindowInterfaces.Add(NewBrowserWindow);
	}
	return NewBrowserWindow;
#endif
	return nullptr;
}

#if BUILD_EMBEDDED_APP
TSharedPtr<IWebInterfaceBrowserWindow> FWebInterfaceBrowserSingleton::CreateNativeBrowserProxy()
{
	TSharedPtr<FNativeWebInterfaceBrowserProxy> NewBrowserWindow = MakeShareable(new FNativeWebInterfaceBrowserProxy(
		bJSBindingsToLoweringEnabled
	));
	NewBrowserWindow->Initialize();
	return NewBrowserWindow;
}
#endif //BUILD_EMBEDDED_APP

bool FWebInterfaceBrowserSingleton::Tick(float DeltaTime)
{
    QUICK_SCOPE_CYCLE_COUNTER(STAT_FWebInterfaceBrowserSingleton_Tick);

#if WITH_CEF3
	if (bAllowCEF)
	{
		{
			FScopeLock Lock(&WindowInterfacesCS);
			bool bIsSlateAwake = FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsSlateAsleep();
			// Remove any windows that have been deleted and check whether it's currently visible
			for (int32 Index = WindowInterfaces.Num() - 1; Index >= 0; --Index)
			{
				if (!WindowInterfaces[Index].IsValid())
				{
					WindowInterfaces.RemoveAt(Index);
				}
				else if (bIsSlateAwake) // only check for Tick activity if Slate is currently ticking
				{
					TSharedPtr<FCEFWebInterfaceBrowserWindow> BrowserWindow = WindowInterfaces[Index].Pin();
					if(BrowserWindow.IsValid())
					{
						// Test if we've ticked recently. If not assume the browser window has become hidden.
						BrowserWindow->CheckTickActivity();
					}
				}
			}
		}
		
	if (CEFBrowserApp != nullptr)
	{
		bool bForceMessageLoop = false;
		GConfig->GetBool(TEXT("Browser"), TEXT("bForceMessageLoop"), bForceMessageLoop, GEngineIni);

		// Get the configured minimum hertz and make sure the value is within a reasonable range
		static const int MaxFrameRateClamp = 60;
		int32 MinMessageLoopHz = 1;
		GConfig->GetInt(TEXT("Browser"), TEXT("MinMessageLoopHertz"), MinMessageLoopHz, GEngineIni);
		MinMessageLoopHz = FMath::Clamp(MinMessageLoopHz, 1, 60);

		// Get the configured forced maximum hertz and make sure the value is within a reasonable range
		int32 MaxForcedMessageLoopHz = 15;
		GConfig->GetInt(TEXT("Browser"), TEXT("MaxForcedMessageLoopHertz"), MaxForcedMessageLoopHz, GEngineIni);
		MaxForcedMessageLoopHz = FMath::Clamp(MaxForcedMessageLoopHz, MinMessageLoopHz, 60);

		// @todo: Hack: We rely on OnScheduleMessagePumpWork() which tells us to drive the CEF message pump, 
		//  there appear to be some edge cases where we might not be getting a signal from it so for the time being 
		//  we force a minimum rates here and let it run at a configurable maximum rate when we have any WindowInterfaces.

		// Convert to seconds which we'll use to compare against the time we accumulated since last pump / left till next pump
		float MinMessageLoopSeconds = 1.0f / MinMessageLoopHz;
		float MaxForcedMessageLoopSeconds = 1.0f / MaxForcedMessageLoopHz;

		static float SecondsSinceLastPump = 0;
		static float SecondsSinceLastAppFocusCheck = MaxForcedMessageLoopSeconds;
		static float SecondsToNextForcedPump = MaxForcedMessageLoopSeconds;

		// Accumulate time since last pump by adding DeltaTime which gives us the amount of time that has passed since last tick in seconds
		SecondsSinceLastPump += DeltaTime;
		SecondsSinceLastAppFocusCheck += DeltaTime;
		// Time left till next pump
		SecondsToNextForcedPump -= DeltaTime;

		bool bWantForce = bForceMessageLoop;								  // True if we wish to force message pump
		bool bCanForce = SecondsToNextForcedPump <= 0;                        // But can we?
		bool bMustForce = SecondsSinceLastPump >= MinMessageLoopSeconds;      // Absolutely must force (Min frequency rate hit)
		if (SecondsSinceLastAppFocusCheck > MinMessageLoopSeconds && WindowInterfaces.Num() > 0)
		{
			SecondsSinceLastAppFocusCheck = 0;
			// only check app being foreground at the min message loop rate (1hz) and if we have a browser window to save CPU
			bAppIsFocused = FPlatformApplicationMisc::IsThisApplicationForeground(); 
		}
		// NOTE - bAppIsFocused could be stale if WindowInterfaces.Num() == 0
		bool bAppIsFocusedAndWebWindows = WindowInterfaces.Num() > 0 && bAppIsFocused;

		// if we won't force AND are the foreground OS app AND we have windows created see if any are visible (not minimized) right now
		if (bWantForce == false && bMustForce  == false && bAppIsFocusedAndWebWindows == true )
		{
			for (int32 Index = 0; Index < WindowInterfaces.Num(); Index++)
			{
				if (WindowInterfaces[Index].IsValid())
				{
					TSharedPtr<FCEFWebInterfaceBrowserWindow> BrowserWindow = WindowInterfaces[Index].Pin();
					if (BrowserWindow->GetParentWindow().IsValid())
					{
						TSharedPtr<SWindow> BrowserParentWindow = BrowserWindow->GetParentWindow();
						if (!BrowserParentWindow->IsWindowMinimized())
						{
							bWantForce = true;
						}
					}
				}
			}
		}

		// tick the CEF app to determine when to run CefDoMessageLoopWork
		if (CEFBrowserApp->TickMessagePump(DeltaTime, (bWantForce && bCanForce) || bMustForce))
		{
			SecondsSinceLastPump = 0;
			SecondsToNextForcedPump = MaxForcedMessageLoopSeconds;
		}
	}

		// Update video buffering for any windows that need it
		for (int32 Index = 0; Index < WindowInterfaces.Num(); Index++)
		{
			if (WindowInterfaces[Index].IsValid())
			{
				TSharedPtr<FCEFWebInterfaceBrowserWindow> BrowserWindow = WindowInterfaces[Index].Pin();
				if (BrowserWindow.IsValid())
				{
					BrowserWindow->UpdateVideoBuffering();
				}
			}
		}
	}

#elif PLATFORM_IOS || (PLATFORM_ANDROID && USE_ANDROID_JNI)
	FScopeLock Lock(&WindowInterfacesCS);
	bool bIsSlateAwake = FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsSlateAsleep();
	// Remove any windows that have been deleted and check whether it's currently visible
	for (int32 Index = WindowInterfaces.Num() - 1; Index >= 0; --Index)
	{
		if (!WindowInterfaces[Index].IsValid())
		{
			WindowInterfaces.RemoveAt(Index);
		}
		else if (bIsSlateAwake) // only check for Tick activity if Slate is currently ticking
		{
			TSharedPtr<IWebInterfaceBrowserWindow> BrowserWindow = WindowInterfaces[Index].Pin();
			if (BrowserWindow.IsValid())
			{
				// Test if we've ticked recently. If not assume the browser window has become hidden.
				BrowserWindow->CheckTickActivity();
			}
		}
	}

#endif
	return true;
}

FString FWebInterfaceBrowserSingleton::GetCurrentLocaleCode()
{
	FCultureRef Culture = FInternationalization::Get().GetCurrentCulture();
	FString LocaleCode = Culture->GetTwoLetterISOLanguageName();
	FString Country = Culture->GetRegion();
	if (!Country.IsEmpty())
	{
		LocaleCode = LocaleCode + TEXT("-") + Country;
	}
	return LocaleCode;
}

TSharedPtr<IWebInterfaceBrowserCookieManager> FWebInterfaceBrowserSingleton::GetCookieManager(TOptional<FString> ContextId) const
{
	if (ContextId.IsSet())
	{
#if WITH_CEF3
		if (bAllowCEF)
		{
			const CefRefPtr<CefRequestContext>* ExistingContext = RequestContexts.Find(ContextId.GetValue());

			if (ExistingContext && ExistingContext->get())
			{
				// Cache these cookie managers?
				return FCefWebInterfaceCookieManagerFactory::Create((*ExistingContext)->GetCookieManager(nullptr));
			}
			else
			{
				UE_LOG(LogWebInterfaceBrowser, Log, TEXT("No cookie manager for ContextId=%s.  Using default cookie manager"), *ContextId.GetValue());
			}
		}
#endif
	}
	// No ContextId or cookie manager instance associated with it.  Use default
	return DefaultCookieManager;
}

#if WITH_CEF3
bool FWebInterfaceBrowserSingleton::URLRequestAllowsCredentials(const FString& URL)
{
	FScopeLock Lock(&WindowInterfacesCS);
	// The FCEFResourceContextHandler::OnBeforeResourceLoad call doesn't get the browser/frame associated with the load
	// (because bugs) so just look at each browser and see if it thinks it knows about this URL
	for (int32 Index = WindowInterfaces.Num() - 1; Index >= 0; --Index)
	{
		TSharedPtr<FCEFWebInterfaceBrowserWindow> BrowserWindow = WindowInterfaces[Index].Pin();
		if (BrowserWindow.IsValid() && BrowserWindow->URLRequestAllowsCredentials(URL))
		{
			return true;
		}
	}

	return false;
}

FString FWebInterfaceBrowserSingleton::GenerateWebCacheFolderName(const FString& InputPath)
{
	if (InputPath.IsEmpty())
		return InputPath;

	// append the version of this CEF build to our requested cache folder path
	// this means each new CEF build gets its own cache folder, making downgrading safe
	return InputPath + "_" + MAKE_STRING(CHROME_VERSION_BUILD);
}
#endif

void FWebInterfaceBrowserSingleton::ClearOldCacheFolders(const FString &CachePathRoot, const FString &CachePrefix)
{
#if WITH_CEF3
	// only CEF3 currently has version dependant cache folders that may need cleanup
	struct FDirectoryVisitor : public IPlatformFile::FDirectoryVisitor
	{
		const FString CachePrefix;
		const FString CurrentCachePath;

		FDirectoryVisitor(const FString &InCachePrefix, const FString &InCurrentCachePath)
			: CachePrefix(InCachePrefix),
			CurrentCachePath(InCurrentCachePath)
		{
		}

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			static const FString CachePrefixSearch = "/" + CachePrefix;
			if (bIsDirectory)
			{
				FString DirName(FilenameOrDirectory);
				if (DirName.Contains(CachePrefixSearch) && DirName.Equals(CurrentCachePath)==false)
				{
					UE_LOG(LogWebInterfaceBrowser, Log, TEXT("Old Cache folder found=%s, deleting"), *DirName);
					// BUGBUG - enable this deletion once we are happy with the new CEF version rollout
					// Also consider adding code to preserve the previous versions folder for a while?
					/*Async<void>(EAsyncExecution::ThreadPool, [DirName]()
						{
							IPlatformFile::GetPlatformPhysical().DeleteDirectoryRecursively(*DirName);
						});*/

				}
			}
			
			return true;
		}
	};

	// Enumerate the contents of the current directory
	FDirectoryVisitor Visitor(CachePrefix, GenerateWebCacheFolderName(FPaths::Combine(CachePathRoot, CachePrefix)));
	IPlatformFile::GetPlatformPhysical().IterateDirectory(*CachePathRoot, Visitor);


#endif
}

bool FWebInterfaceBrowserSingleton::RegisterContext(const FInterfaceBrowserContextSettings& Settings)
{
#if WITH_CEF3
	if (bAllowCEF)
	{
		const CefRefPtr<CefRequestContext>* ExistingContext = RequestContexts.Find(Settings.Id);

		if (ExistingContext != nullptr)
		{
			// You can't register the same context twice and
			// you can't update the settings for a context that already exists
			return false;
		}

		CefRequestContextSettings RequestContextSettings;
		CefString(&RequestContextSettings.accept_language_list) = Settings.AcceptLanguageList.IsEmpty() ? TCHAR_TO_WCHAR(*GetCurrentLocaleCode()) : TCHAR_TO_WCHAR(*Settings.AcceptLanguageList);
		CefString(&RequestContextSettings.cache_path) = TCHAR_TO_WCHAR(*GenerateWebCacheFolderName(Settings.CookieStorageLocation));
		RequestContextSettings.persist_session_cookies = Settings.bPersistSessionCookies;
#if !PLATFORM_WINDOWS
		RequestContextSettings.ignore_certificate_errors = Settings.bIgnoreCertificateErrors;
#endif

		//Create a new one
		CefRefPtr<FCEFInterfaceResourceContextHandler> ResourceContextHandler = new FCEFInterfaceResourceContextHandler(this);
		ResourceContextHandler->OnBeforeLoad() = Settings.OnBeforeContextResourceLoad;
		RequestResourceHandlers.Add(Settings.Id, ResourceContextHandler);
		CefRefPtr<CefRequestContext> RequestContext = CefRequestContext::CreateContext(RequestContextSettings, ResourceContextHandler);
		RequestContexts.Add(Settings.Id, RequestContext);
		SchemeHandlerFactories.RegisterFactoriesWith(RequestContext);
		UE_LOG(LogWebInterfaceBrowser, Log, TEXT("Registering ContextId=%s."), *Settings.Id);
		return true;
	}
#endif
	return false;
}

bool FWebInterfaceBrowserSingleton::UnregisterContext(const FString& ContextId)
{
#if WITH_CEF3
	bool bFoundContext = false;
	if (bAllowCEF)
	{
		UE_LOG(LogWebInterfaceBrowser, Log, TEXT("Unregistering ContextId=%s."), *ContextId);

		WaitForTaskQueueFlush();

		CefRefPtr<CefRequestContext> Context;
		if (RequestContexts.RemoveAndCopyValue(ContextId, Context))
		{
			bFoundContext = true;
			Context->ClearSchemeHandlerFactories();
		}

		CefRefPtr<FCEFInterfaceResourceContextHandler> ResourceHandler;
		if (RequestResourceHandlers.RemoveAndCopyValue(ContextId, ResourceHandler))
		{
			ResourceHandler->OnBeforeLoad().Unbind();
		}
	}
	return bFoundContext;
#else
	return false;
#endif
}

bool FWebInterfaceBrowserSingleton::RegisterSchemeHandlerFactory(FString Scheme, FString Domain, IWebInterfaceBrowserSchemeHandlerFactory* WebBrowserSchemeHandlerFactory)
{
#if WITH_CEF3
	if (bAllowCEF)
	{
		SchemeHandlerFactories.AddSchemeHandlerFactory(MoveTemp(Scheme), MoveTemp(Domain), WebBrowserSchemeHandlerFactory);
		return true;
	}
#endif
	return false;
}

bool FWebInterfaceBrowserSingleton::UnregisterSchemeHandlerFactory(IWebInterfaceBrowserSchemeHandlerFactory* WebBrowserSchemeHandlerFactory)
{
#if WITH_CEF3
	if (bAllowCEF)
	{
		SchemeHandlerFactories.RemoveSchemeHandlerFactory(WebBrowserSchemeHandlerFactory);
		return true;
	}
#endif
	return false;
}

// Cleanup macros to avoid having them leak outside this source file
#undef CEF3_BIN_DIR
#undef CEF3_FRAMEWORK_DIR
#undef CEF3_RESOURCES_DIR
#undef CEF3_SUBPROCES_EXE

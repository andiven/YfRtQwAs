// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IWebInterfaceBrowserSingleton.h"

#if WITH_CEF3
#if PLATFORM_WINDOWS
#if UE_VERSION < 504
	#include "Windows/WindowsHWrapper.h"
#endif
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include "Windows/AllowWindowsPlatformAtomics.h"
#endif
#pragma push_macro("OVERRIDE")
#undef OVERRIDE // cef headers provide their own OVERRIDE macro
THIRD_PARTY_INCLUDES_START
#if PLATFORM_APPLE
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#endif
#include "include/internal/cef_ptr.h"
#include "include/cef_request_context.h"
#if PLATFORM_APPLE
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("OVERRIDE")
#if PLATFORM_WINDOWS
	#include "Windows/HideWindowsPlatformAtomics.h"
	#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "CEF/CEFInterfaceSchemeHandler.h"
#include "CEF/CEFInterfaceResourceContextHandler.h"
class CefListValue;
class FCEFInterfaceBrowserApp;
class FCEFWebInterfaceBrowserWindow;
#endif

class IWebInterfaceBrowserCookieManager;
class IWebInterfaceBrowserWindow;
struct FWebInterfaceBrowserWindowInfo;
struct FWebInterfaceBrowserInitSettings;
class UMaterialInterface;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

/**
 * Implementation of singleton class that takes care of general web browser tasks
 */
class FWebInterfaceBrowserSingleton
	: public IWebInterfaceBrowserSingleton
#if UE_VERSION >= 500
	, public FTSTickerObjectBase
#else
	, public FTickerObjectBase
#endif
{
public:

	/** Constructor. */
	FWebInterfaceBrowserSingleton(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings);

	/** Virtual destructor. */
	virtual ~FWebInterfaceBrowserSingleton();

	/**
	* Gets the Current Locale Code in the format CEF expects
	*
	* @return Locale code as either "xx" or "xx-YY"
	*/
	static FString GetCurrentLocaleCode();

	virtual FString ApplicationCacheDir() const override;

public:

	// IWebInterfaceBrowserSingleton Interface

	virtual TSharedRef<IWebInterfaceBrowserWindowFactory> GetWebBrowserWindowFactory() const override;

	TSharedPtr<IWebInterfaceBrowserWindow> CreateBrowserWindow(
		TSharedPtr<FCEFWebInterfaceBrowserWindow>& BrowserWindowParent,
		TSharedPtr<FWebInterfaceBrowserWindowInfo>& BrowserWindowInfo) override;

	TSharedPtr<IWebInterfaceBrowserWindow> CreateBrowserWindow(const FCreateInterfaceBrowserWindowSettings& Settings) override;

#if	BUILD_EMBEDDED_APP
	TSharedPtr<IWebInterfaceBrowserWindow> CreateNativeBrowserProxy() override;
#endif

	virtual TSharedPtr<IWebInterfaceBrowserCookieManager> GetCookieManager() const override
	{
		return DefaultCookieManager;
	}

	virtual TSharedPtr<IWebInterfaceBrowserCookieManager> GetCookieManager(TOptional<FString> ContextId) const override;

	virtual bool RegisterContext(const FInterfaceBrowserContextSettings& Settings) override;

	virtual bool UnregisterContext(const FString& ContextId) override;

	virtual bool RegisterSchemeHandlerFactory(FString Scheme, FString Domain, IWebInterfaceBrowserSchemeHandlerFactory* WebBrowserSchemeHandlerFactory) override;

	virtual bool UnregisterSchemeHandlerFactory(IWebInterfaceBrowserSchemeHandlerFactory* WebBrowserSchemeHandlerFactory) override;

	virtual bool IsDevToolsShortcutEnabled() override
	{
		return bDevToolsShortcutEnabled;
	}

	virtual void SetDevToolsShortcutEnabled(bool Value) override
	{
		bDevToolsShortcutEnabled = Value;
	}

	virtual void SetJSBindingToLoweringEnabled(bool bEnabled) override
	{
		bJSBindingsToLoweringEnabled = bEnabled;
	}

	virtual void ClearOldCacheFolders(const FString& CachePathRoot, const FString& CachePrefix) override;

	/** Set a reference to UWebBrowser's default material*/
	virtual void SetDefaultMaterial(UMaterialInterface* InDefaultMaterial) override
	{
		DefaultMaterial = InDefaultMaterial;
	}

	/** Set a reference to UWebBrowser's translucent material*/
	virtual void SetDefaultTranslucentMaterial(UMaterialInterface* InDefaultMaterial) override
	{
		DefaultTranslucentMaterial = InDefaultMaterial;
	}

	/** Get a reference to UWebBrowser's default material*/
	virtual UMaterialInterface* GetDefaultMaterial() override
	{
		return DefaultMaterial;
	}

	/** Get a reference to UWebBrowser's translucent material*/
	virtual UMaterialInterface* GetDefaultTranslucentMaterial() override
	{
		return DefaultTranslucentMaterial;
	}

public:

	// FTSTickerObjectBase Interface

	virtual bool Tick(float DeltaTime) override;

#if WITH_CEF3
	/** Return true if this URL will support adding an Authorization header to it */
	bool URLRequestAllowsCredentials(const FString& URL);
#endif
private:

	TSharedPtr<IWebInterfaceBrowserCookieManager> DefaultCookieManager;

#if WITH_CEF3
	/** When new render processes are created, send all permanent variable bindings to them. */
	void HandleRenderProcessCreated(CefRefPtr<CefListValue> ExtraInfo);
	/** Helper function to generate the CEF build unique name for the cache_path */
	FString GenerateWebCacheFolderName(const FString &InputPath);
	/** Helper function that blocks until the CEF task queue has processed a posted task, flushing the queue */
	void WaitForTaskQueueFlush();

	/** Pointer to the CEF App implementation */
	CefRefPtr<FCEFInterfaceBrowserApp>			CEFBrowserApp;

	TMap<FString, CefRefPtr<CefRequestContext>> RequestContexts;
	TMap<FString, CefRefPtr<FCEFInterfaceResourceContextHandler>> RequestResourceHandlers;
	FCefInterfaceSchemeHandlerFactories SchemeHandlerFactories;
	bool bAllowCEF;
	bool bTaskFinished;
#endif

	/** List of currently existing browser windows */
#if WITH_CEF3
	TArray<TWeakPtr<FCEFWebInterfaceBrowserWindow>>	WindowInterfaces;
#elif PLATFORM_IOS || (PLATFORM_ANDROID && USE_ANDROID_JNI)
	TArray<TWeakPtr<IWebInterfaceBrowserWindow>>	WindowInterfaces;
#endif

	/** Critical section for thread safe modification of WindowInterfaces array. */
	FCriticalSection WindowInterfacesCS;

	TSharedRef<IWebInterfaceBrowserWindowFactory> WebBrowserWindowFactory;

	bool bDevToolsShortcutEnabled;

	bool bJSBindingsToLoweringEnabled;

	bool bAppIsFocused;

#if WITH_CEF3
	/** Did CEF successfully initialize itself */
	bool bCEFInitialized;
#endif

	/** Reference to UWebBrowser's default material*/
	UMaterialInterface* DefaultMaterial;

	/** Reference to UWebBrowser's translucent material*/
	UMaterialInterface* DefaultTranslucentMaterial;

};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_CEF3

class CefCookieManager;

class FCefWebInterfaceCookieManagerFactory
{
public:
	static TSharedRef<IWebInterfaceBrowserCookieManager> Create(
		const CefRefPtr<CefCookieManager>& CookieManager);
};

#endif

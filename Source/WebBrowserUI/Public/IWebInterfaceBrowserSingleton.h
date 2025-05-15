// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Rendering/SlateRenderer.h"
#include "IWebInterfaceBrowserResourceLoader.h"

class FCEFWebInterfaceBrowserWindow;
class IWebInterfaceBrowserCookieManager;
class IWebInterfaceBrowserWindow;
class IWebInterfaceBrowserSchemeHandlerFactory;
class UMaterialInterface;
struct FWebInterfaceBrowserWindowInfo;

class IWebInterfaceBrowserWindowFactory
{
public:

	virtual TSharedPtr<IWebInterfaceBrowserWindow> Create(
		TSharedPtr<FCEFWebInterfaceBrowserWindow>& BrowserWindowParent,
		TSharedPtr<FWebInterfaceBrowserWindowInfo>& BrowserWindowInfo) = 0;

	virtual TSharedPtr<IWebInterfaceBrowserWindow> Create(
		void* OSWindowHandle,
		FString InitialURL,
		bool bUseTransparency,
		bool bThumbMouseButtonNavigation,
		bool InterceptLoadRequests = false,
		TOptional<FString> ContentsToLoad = TOptional<FString>(),
		bool ShowErrorMessage = true,
		FColor BackgroundColor = FColor(255, 255, 255, 255)) = 0;
};

struct WEBBROWSERUI_API FInterfaceBrowserContextSettings
{
	FInterfaceBrowserContextSettings(const FString& InId)
		: Id(InId)
		, AcceptLanguageList()
		, CookieStorageLocation()
		, bPersistSessionCookies(false)
		, bIgnoreCertificateErrors(false)
		, bEnableNetSecurityExpiration(true)
	{ }

	FString Id;
	FString AcceptLanguageList;
	FString CookieStorageLocation;
	bool bPersistSessionCookies;
	bool bIgnoreCertificateErrors;
	bool bEnableNetSecurityExpiration;
	FOnBeforeInterfaceContextResourceLoadDelegate OnBeforeContextResourceLoad;
};


struct WEBBROWSERUI_API FCreateInterfaceBrowserWindowSettings
{

	FCreateInterfaceBrowserWindowSettings()
		: OSWindowHandle(nullptr)
		, InitialURL()
		, bAcceleratedPaint(false)
		, bUseTransparency(false)
		, bInterceptLoadRequests(false)
		, bUseNativeCursors(false)
		, bThumbMouseButtonNavigation(false)
		, ContentsToLoad()
		, bShowErrorMessage(true)
		, BackgroundColor(FColor(255, 255, 255, 255))
		, BrowserFrameRate(60)
		, Context()
		, AltRetryDomains()
	{ }

	void* OSWindowHandle;
	FString InitialURL;
	bool bAcceleratedPaint;
	bool bUseTransparency;
	bool bInterceptLoadRequests;
	bool bUseNativeCursors;
	bool bThumbMouseButtonNavigation;
	TOptional<FString> ContentsToLoad;
	bool bShowErrorMessage;
	FColor BackgroundColor;
	int BrowserFrameRate;
	TOptional<FInterfaceBrowserContextSettings> Context;
	TArray<FString> AltRetryDomains;
};

/**
 * A singleton class that takes care of general web browser tasks
 */
class WEBBROWSERUI_API IWebInterfaceBrowserSingleton
{
public:
	/**
	 * Virtual Destructor
	 */
	virtual ~IWebInterfaceBrowserSingleton() {};

	/** @return A factory object that can be used to construct additional WebBrowserWindows on demand. */
	virtual TSharedRef<IWebInterfaceBrowserWindowFactory> GetWebBrowserWindowFactory() const = 0;


	/**
	 * Create a new web browser window
	 *
	 * @param BrowserWindowParent The parent browser window
	 * @param BrowserWindowInfo Info for setting up the new browser window
	 * @return New Web Browser Window Interface (may be null if not supported)
	 */
	virtual TSharedPtr<IWebInterfaceBrowserWindow> CreateBrowserWindow(
		TSharedPtr<FCEFWebInterfaceBrowserWindow>& BrowserWindowParent,
		TSharedPtr<FWebInterfaceBrowserWindowInfo>& BrowserWindowInfo
		) = 0;

	/**
	 * Create a new web browser window
	 *
	 * @param Settings Struct containing browser window settings
	 * @return New Web Browser Window Interface (may be null if not supported)
	 */
	virtual TSharedPtr<IWebInterfaceBrowserWindow> CreateBrowserWindow(const FCreateInterfaceBrowserWindowSettings& Settings) = 0;

#if	BUILD_EMBEDDED_APP
	virtual TSharedPtr<IWebInterfaceBrowserWindow> CreateNativeBrowserProxy() = 0;
#endif

	virtual TSharedPtr<class IWebInterfaceBrowserCookieManager> GetCookieManager() const = 0;

	virtual TSharedPtr<class IWebInterfaceBrowserCookieManager> GetCookieManager(TOptional<FString> ContextId) const = 0;

	virtual bool RegisterContext(const FInterfaceBrowserContextSettings& Settings) = 0;

	virtual bool UnregisterContext(const FString& ContextId) = 0;

	// @return the application cache dir where the cookies are stored
	virtual FString ApplicationCacheDir() const = 0;
	/**
	 * Registers a custom scheme handler factory, for a given scheme and domain. The domain is ignored if the scheme is not a browser built in scheme
	 * and all requests will go through this factory.
	 * @param Scheme                            The scheme name to handle.
	 * @param Domain                            The domain name to handle.
	 * @param WebBrowserSchemeHandlerFactory    The factory implementation for creating request handlers for this scheme.
	 */
	virtual bool RegisterSchemeHandlerFactory(FString Scheme, FString Domain, IWebInterfaceBrowserSchemeHandlerFactory* WebBrowserSchemeHandlerFactory) = 0;

	/**
	 * Unregister a custom scheme handler factory. The factory may still be used by existing open browser windows, but will no longer be provided for new ones.
	 * @param WebBrowserSchemeHandlerFactory    The factory implementation to remove.
	 */
	virtual bool UnregisterSchemeHandlerFactory(IWebInterfaceBrowserSchemeHandlerFactory* WebBrowserSchemeHandlerFactory) = 0;

	/**
	 * Enable or disable CTRL/CMD-SHIFT-I shortcut to show the Chromium Dev tools window.
	 * The value defaults to true on debug builds, otherwise false.
	 *
	 * The relevant handlers for spawning new browser windows have to be set up correctly in addition to this flag being true before anything is shown.
	 *
	 * @param Value a boolean value to enable or disable the keyboard shortcut.
	 */
	virtual void SetDevToolsShortcutEnabled(bool Value) = 0;


	/**
	 * Returns whether the CTRL/CMD-SHIFT-I shortcut to show the Chromium Dev tools window is enabled.
	 *
	 * The relevant handlers for spawning new browser windows have to be set up correctly in addition to this flag being true before anything is shown.
	 *
	 * @return a boolean value indicating whether the keyboard shortcut is enabled or not.
	 */
	virtual bool IsDevToolsShortcutEnabled() = 0;


	/**
	 * Enable or disable to-lowering of JavaScript object member bindings.
	 *
	 * Due to how JavaScript to UObject bridges require the use of FNames for building up the JS API objects, it is possible for case-sensitivity issues
	 * to develop if an FName has been previously created with differing case to your function or property names. To-lowering the member names allows
	 * a guaranteed casing for the web page's JS to reference.
	 *
	 * Default behavior is enabled, so that all JS side objects have only lowercase members.
	 *
	 * @param bEnabled a boolean value to enable or disable the to-lowering.
	 */
	virtual void SetJSBindingToLoweringEnabled(bool bEnabled) = 0;


	/**
	 * Delete old/unused web cache paths. Some Web implementations (i.e CEF) use version specific cache folders, this
	 * call lets you schedule a deletion of any now unused folders. Calling this may resulting in async disk I/O.
	 *
	 * @param CachePathRoot the base path used for saving the webcache folder
	 * @param CachePrefix the filename prefix we use for the cache folder (i.e "webcache")
	 */
	virtual void ClearOldCacheFolders(const FString &CachePathRoot, const FString &CachePrefix) = 0;


	/** Set a reference to UWebBrowser's default material*/
	virtual void SetDefaultMaterial(UMaterialInterface* InDefaultMaterial) = 0;
	/** Set a reference to UWebBrowser's translucent material*/
	virtual void SetDefaultTranslucentMaterial(UMaterialInterface* InDefaultMaterial) = 0;

	/** Get a reference to UWebBrowser's default material*/
	virtual UMaterialInterface* GetDefaultMaterial() = 0;
	/** Get a reference to UWebBrowser's transparent material*/
	virtual UMaterialInterface* GetDefaultTranslucentMaterial() = 0;
};

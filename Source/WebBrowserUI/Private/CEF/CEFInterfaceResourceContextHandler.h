// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3

#include "IWebInterfaceBrowserResourceLoader.h"

#include "CEFInterfaceLibCefIncludes.h"


FString _ResourceTypeToString(const CefRequest::ResourceType& Type);

class FWebInterfaceBrowserSingleton;



/**
 * Implements CEF Request handling for when a browser window is still being constructed
 */
class FCEFInterfaceResourceContextHandler :
	  public CefRequestContextHandler
	, public CefResourceRequestHandler
{
public:

	/** Default constructor. */
	FCEFInterfaceResourceContextHandler(FWebInterfaceBrowserSingleton *InOwningSingleton);

public:

	// CefResourceRequestHandler Interface
	virtual CefResourceRequestHandler::ReturnValue OnBeforeResourceLoad(
		CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefRequest> Request,
#if PLATFORM_WINDOWS
		CefRefPtr<CefCallback> Callback
#else
		CefRefPtr<CefRequestCallback> Callback
#endif
	) override;

	// CefRequestContextHandler Interface
	virtual CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		bool is_navigation,
		bool is_download,
		const CefString& request_initiator,
		bool& disable_default_handling) override;


public:
	FOnBeforeInterfaceContextResourceLoadDelegate& OnBeforeLoad()
	{
		return BeforeResourceLoadDelegate;
	}

private:

	/** Delegate for handling resource load requests */
	FOnBeforeInterfaceContextResourceLoadDelegate BeforeResourceLoadDelegate;

	/** Singleton that owns this context handler, so we can lookup browser objects from it */
	FWebInterfaceBrowserSingleton* OwningSingleton;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(FCEFInterfaceResourceContextHandler);
};

#endif

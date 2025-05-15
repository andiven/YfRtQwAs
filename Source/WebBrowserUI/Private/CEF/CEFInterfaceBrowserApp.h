// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"

#if WITH_CEF3

#include "CEFInterfaceLibCefIncludes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCEFInterfaceBrowser, Log, All);

/**
 * Implements CEF App and other Process level interfaces
 */
class FCEFInterfaceBrowserApp : public CefApp,
	public CefBrowserProcessHandler
{
public:

	/**
	 * Default Constructor
	 */
	FCEFInterfaceBrowserApp();

	/** Used to pump the CEF message loop whenever OnScheduleMessagePumpWork is triggered */
	bool TickMessagePump(float DeltaTime, bool bForce);

private:
	// CefApp methods.
	virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
	virtual void OnBeforeCommandLineProcessing(const CefString& ProcessType, CefRefPtr< CefCommandLine > CommandLine) override;
	// CefBrowserProcessHandler methods:
	virtual void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> CommandLine) override;
	virtual void OnScheduleMessagePumpWork(int64 delay_ms) override;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(FCEFInterfaceBrowserApp);

	// Lock for access MessagePumpCountdown
	FCriticalSection MessagePumpCountdownCS;
	// Countdown in milliseconds until CefDoMessageLoopWork is called.  Updated by OnScheduleMessagePumpWork
	int64 MessagePumpCountdown;
};
#endif

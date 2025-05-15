// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3
#include "CEFInterfaceLibCefIncludes.h"

// Helper for posting a closure as a task
class FCEFInterfaceBrowserClosureTask
	: public CefTask
{
public:
	FCEFInterfaceBrowserClosureTask(CefRefPtr<CefBaseRefCounted> InHandle, TFunction<void ()> InClosure)
		: Handle(InHandle)
		, Closure(InClosure)
	{ }

	virtual void Execute() override
	{
		Closure();
	}

private:
	CefRefPtr<CefBaseRefCounted> Handle; // Used so the handler will not go out of scope before the closure is executed.
	TFunction<void ()> Closure;
	IMPLEMENT_REFCOUNTING(FCEFInterfaceBrowserClosureTask);
};


#endif /* WITH_CEF3 */

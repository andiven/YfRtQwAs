// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_IOS
#include "IWebInterfaceBrowserCookieManager.h"

/**
 * Implementation of interface for dealing with a Web Browser cookies for iOS.
 */
class FIOSInterfaceCookieManager
	: public IWebInterfaceBrowserCookieManager
	, public TSharedFromThis<FIOSInterfaceCookieManager>
{
public:

	// IWebInterfaceBrowserCookieManager interface

	virtual void SetCookie(const FString& URL, const FCookie& Cookie, TFunction<void(bool)> Completed = nullptr) override;
	virtual void DeleteCookies(const FString& URL = TEXT(""), const FString& CookieName = TEXT(""), TFunction<void(int)> Completed = nullptr) override;

	// FIOSInterfaceCookieManager

	FIOSInterfaceCookieManager();
	virtual ~FIOSInterfaceCookieManager();
};
#endif

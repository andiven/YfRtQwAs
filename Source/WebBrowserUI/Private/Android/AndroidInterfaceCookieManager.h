// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_ANDROID
#include "IWebInterfaceBrowserCookieManager.h"

/**
 * Implementation of interface for dealing with a Web Browser cookies for iOS.
 */
class FAndroidInterfaceCookieManager
	: public IWebInterfaceBrowserCookieManager
	, public TSharedFromThis<FAndroidInterfaceCookieManager>
{
public:

	// IWebInterfaceBrowserCookieManager interface

	virtual void SetCookie(const FString& URL, const FCookie& Cookie, TFunction<void(bool)> Completed = nullptr) override;
	virtual void DeleteCookies(const FString& URL = TEXT(""), const FString& CookieName = TEXT(""), TFunction<void(int)> Completed = nullptr) override;

	// FAndroidInterfaceCookieManager

	FAndroidInterfaceCookieManager();
	virtual ~FAndroidInterfaceCookieManager();
};
#endif

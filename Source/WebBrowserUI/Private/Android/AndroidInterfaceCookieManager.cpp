// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "AndroidInterfaceCookieManager.h"

#if USE_ANDROID_JNI

#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"

#include <jni.h>
#include "Async/TaskGraphInterfaces.h"


FAndroidInterfaceCookieManager::FAndroidInterfaceCookieManager()
{
}

FAndroidInterfaceCookieManager::~FAndroidInterfaceCookieManager()
{
}

void FAndroidInterfaceCookieManager::SetCookie(const FString& URL, const FCookie& Cookie, TFunction<void(bool)> Completed)
{
	bool bResult = false;

	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (Env)
	{
		static jmethodID SetCookieFunc = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_CookieManager_SetCookie", "(Ljava/lang/String;Ljava/lang/String;)Z", false);
		if (SetCookieFunc != nullptr)
		{
			FString CookieData = Cookie.Name + FString(TEXT("=")) + Cookie.Value;
			if (Cookie.bHasExpires)
			{
				CookieData += FString(TEXT(";expires")) + Cookie.Expires.ToHttpDate() + FString(TEXT(";"));
			}

			jstring jUrl = Env->NewStringUTF(TCHAR_TO_UTF8(*URL));
			jstring jCookieData = Env->NewStringUTF(TCHAR_TO_UTF8(*CookieData));
			bResult = FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, SetCookieFunc, jUrl, jCookieData);
			Env->DeleteLocalRef(jCookieData);
			Env->DeleteLocalRef(jUrl);
		}
	}

	// send result
	if (Completed)
	{
		Completed(bResult);
	}
}

void FAndroidInterfaceCookieManager::DeleteCookies(const FString& URL, const FString& CookieName, TFunction<void(int)> Completed)
{
	bool bResult = false;

	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (Env)
	{
		static jmethodID RemoveCookiesFunc = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_CookieManager_RemoveCookies", "(Ljava/lang/String;)Z", false);
		if (RemoveCookiesFunc != nullptr)
		{
			jstring jUrl = Env->NewStringUTF(TCHAR_TO_UTF8(*URL));
			bResult = FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, RemoveCookiesFunc, jUrl);
			Env->DeleteLocalRef(jUrl);
		}
	}

	// send result
	if (Completed)
	{
		Completed(bResult);
	}
}

#else

FAndroidInterfaceCookieManager::FAndroidInterfaceCookieManager()
{
}

FAndroidInterfaceCookieManager::~FAndroidInterfaceCookieManager()
{
}

void FAndroidInterfaceCookieManager::SetCookie(const FString& URL, const FCookie& Cookie, TFunction<void(bool)> Completed)
{
	bool bResult = false;

	// send result
	if (Completed)
	{
		Completed(bResult);
	}
}

void FAndroidInterfaceCookieManager::DeleteCookies(const FString& URL, const FString& CookieName, TFunction<void(int)> Completed)
{
	bool bResult = false;

	// send result
	if (Completed)
	{
		Completed(bResult);
	}
}

#endif

// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "AndroidWebInterfaceBrowserDialog.h"

#if USE_ANDROID_JNI

#include "Android/AndroidApplication.h"
#include "Android/AndroidJava.h"

#include <jni.h>

namespace
{
	FText GetFText(jstring InString)
	{
		if (InString == nullptr)
		{
			return FText::GetEmpty();
		}

		JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();
		FString Temp = FJavaHelper::FStringFromParam(JEnv, InString);
		FText Retval = FText::FromString(Temp);
		return Retval;
	}
}

FAndroidWebInterfaceBrowserDialog::FAndroidWebInterfaceBrowserDialog(jstring InMessageText, jstring InDefaultPrompt, jobject InCallback)
	: Type(EWebInterfaceBrowserDialogType::Prompt)
	, MessageText(GetFText(InMessageText))
	, DefaultPrompt(GetFText(InDefaultPrompt))
	, Callback(InCallback)
{
}

FAndroidWebInterfaceBrowserDialog::FAndroidWebInterfaceBrowserDialog(EWebInterfaceBrowserDialogType InDialogType, jstring InMessageText, jobject InCallback)
	: Type(InDialogType)
	, MessageText(GetFText(InMessageText))
	, DefaultPrompt()
	, Callback(InCallback)
{
}

void FAndroidWebInterfaceBrowserDialog::Continue(bool Success, const FText& UserResponse)
{
	check(Callback != nullptr);
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();
	const char* MethodName = Success?"confirm":"cancel";
	const char* MethodSignature = (Success && Type==EWebInterfaceBrowserDialogType::Prompt)?"(Ljava/lang/String;)V":"()V";
	auto Class = NewScopedJavaObject(JEnv, JEnv->GetObjectClass(Callback));
	jmethodID MethodId = JEnv->GetMethodID(*Class, MethodName, MethodSignature);

	if (Success && Type==EWebInterfaceBrowserDialogType::Prompt)
	{
		auto JUserResponse = FJavaClassObject::GetJString(UserResponse.ToString());
		JEnv->CallVoidMethod(Callback, MethodId, *JUserResponse);
	}
	else
	{
		JEnv->CallVoidMethod(Callback, MethodId, nullptr);
	}
}

#endif // USE_ANDROID_JNI

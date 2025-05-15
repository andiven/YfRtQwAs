// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if USE_ANDROID_JNI

#include "IWebInterfaceBrowserDialog.h"

#include <jni.h>

class SAndroidWebInterfaceBrowserWidget;

class FAndroidWebInterfaceBrowserDialog
	: public IWebInterfaceBrowserDialog
{
public:
	virtual ~FAndroidWebInterfaceBrowserDialog()
	{}

	// IWebInterfaceBrowserDialog interface:

	virtual EWebInterfaceBrowserDialogType GetType() override
	{
		return Type;
	}

	virtual const FText& GetMessageText() override
	{
		return MessageText;
	}

	virtual const FText& GetDefaultPrompt() override
	{
		return DefaultPrompt;
	}

	virtual bool IsReload() override
	{
		check(Type == EWebInterfaceBrowserDialogType::Unload);
		return false; // The android webkit browser does not provide this infomation
	}

	virtual void Continue(bool Success = true, const FText& UserResponse = FText::GetEmpty()) override;

private:

	EWebInterfaceBrowserDialogType Type;
	FText MessageText;
	FText DefaultPrompt;

	jobject Callback; // Either a reference to a JsResult or a JsPromptResult object depending on Type

	// Create a dialog from OnJSPrompt arguments
	FAndroidWebInterfaceBrowserDialog(jstring InMessageText, jstring InDefaultPrompt, jobject InCallback);

	// Create a dialog from OnJSAlert|Confirm|BeforeUnload arguments
	FAndroidWebInterfaceBrowserDialog(EWebInterfaceBrowserDialogType InDialogType, jstring InMessageText, jobject InCallback);

	friend class FAndroidWebInterfaceBrowserWindow;
	friend class SAndroidWebInterfaceBrowserWidget;
};

typedef FAndroidWebInterfaceBrowserDialog FWebInterfaceBrowserDialog;

#endif // USE_ANDROID_JNI
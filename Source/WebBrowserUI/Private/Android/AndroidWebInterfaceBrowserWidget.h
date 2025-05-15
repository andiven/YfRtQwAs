// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if USE_ANDROID_JNI

#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "AndroidWebInterfaceBrowserWindow.h"
#include "AndroidWebInterfaceBrowserDialog.h"
#include "Android/AndroidJava.h"
#include "RHI.h"
#include "RHIResources.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "AndroidJavaWebInterfaceBrowser.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WebInterfaceBrowserTexture.h"

#include <jni.h>

class UMaterialExpressionTextureSample;
class FWebInterfaceBrowserTextureSamplePool;

class SAndroidWebInterfaceBrowserWidget : public SViewport
{
	SLATE_BEGIN_ARGS(SAndroidWebInterfaceBrowserWidget)
		: _InitialURL("about:blank")
		, _UseTransparency(false)
	{ }

		SLATE_ARGUMENT(FString, InitialURL);
		SLATE_ARGUMENT(bool, UseTransparency);
		SLATE_ARGUMENT(TSharedPtr<FAndroidWebInterfaceBrowserWindow>, WebBrowserWindow);

	SLATE_END_ARGS()

public:
	virtual ~SAndroidWebInterfaceBrowserWidget();

	void Construct(const FArguments& Args);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float) const override;

	void ExecuteJavascript(const FString& Script);
	void LoadURL(const FString& NewURL);
	void LoadString(const FString& Content, const FString& BaseUrl);

	void StopLoad();
	void Reload();
	void Close();
	void GoBack();
	void GoForward();
	bool CanGoBack();
	bool CanGoForward();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	void SendTouchDown(FVector2D Position);
	void SendTouchUp(FVector2D Position);
	void SendTouchMove(FVector2D Position);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;

private:

	// InterceptedResponseHeaders class method ids
	jclass InterceptedResponseHeadersClass;
	jfieldID InterceptedResponseHeaders_StatusCode;
	jfieldID InterceptedResponseHeaders_MimeType;
	jfieldID InterceptedResponseHeaders_Encoding;
	jfieldID InterceptedResponseHeaders_ReasonPhrase;
	jfieldID InterceptedResponseHeaders_Headers;

public:

	// WebViewClient callbacks

	jbyteArray HandleShouldInterceptRequest(jstring JUrl, jobject JResponse);

	bool HandleShouldOverrideUrlLoading(jstring JUrl);
	bool HandleJsDialog(EWebInterfaceBrowserDialogType Type, jstring JUrl, jstring MessageText, jobject ResultCallback)
	{
		TSharedPtr<IWebInterfaceBrowserDialog> Dialog(new FWebInterfaceBrowserDialog(Type, MessageText, ResultCallback));
		return HandleJsDialog(Dialog);
	}

	bool HandleJsPrompt(jstring JUrl, jstring MessageText, jstring DefaultPrompt, jobject ResultCallback)
	{
		TSharedPtr<IWebInterfaceBrowserDialog> Dialog(new FWebInterfaceBrowserDialog(MessageText, DefaultPrompt, ResultCallback));
		return HandleJsDialog(Dialog);
	}

	void HandleReceivedTitle(jstring JTitle);
	void HandlePageLoad(jstring JUrl, bool bIsLoading, int InHistorySize, int InHistoryPosition);
	void HandleReceivedError(jint ErrorCode, jstring JMessage, jstring JUrl);

	// Helper to get the native widget pointer from a Java callback.
	// Jobj can either be a WebInterfaceViewControl, a WebInterfaceViewControl.ViewClient or WebInterfaceViewControl.ChromeClient instance
	static TSharedPtr<SAndroidWebInterfaceBrowserWidget> GetWidgetPtr(JNIEnv* JEnv, jobject Jobj);

	//set the native control's visibility
	void SetWebBrowserVisibility(bool InIsVisible);
protected:
	static FCriticalSection WebControlsCS;
	static TMap<int64, TWeakPtr<SAndroidWebInterfaceBrowserWidget>> AllWebControls;

	bool HandleJsDialog(TSharedPtr<IWebInterfaceBrowserDialog>& Dialog);
	int HistorySize;
	int HistoryPosition;

	TWeakPtr<FAndroidWebInterfaceBrowserWindow> WebBrowserWindowPtr;
private:
	FVector2D ConvertMouseEventToLocal(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Enable 3D appearance for Android. */
	bool IsAndroid3DBrowser;

	/** Should use bitmap rendering for 3D. */
	bool bShouldUseBitmapRender;

	/** Mouse captured */
	bool bMouseCapture;

	/** The Java side WebInterfaceBrowser interface. */
	TSharedPtr<FJavaAndroidWebInterfaceBrowser, ESPMode::ThreadSafe> JavaWebBrowser;

	/** The external texture to render the WebInterfaceBrowser output. */
	UWebInterfaceBrowserTexture* WebBrowserTexture;

	/** The material for the external texture. */
	UMaterialInstanceDynamic* WebBrowserMaterial;

	/** The Slate brush that renders the material. */
	TSharedPtr<FSlateBrush> WebBrowserBrush;

	/** The sample queue. */
	TSharedPtr<FWebInterfaceBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSamplesQueue;

	/** Texture sample object pool. */
	FWebInterfaceBrowserTextureSamplePool* TextureSamplePool;
};

#endif // USE_ANDROID_JNI
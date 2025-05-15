// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "AndroidWebInterfaceBrowserWindow.h"

#if USE_ANDROID_JNI

#include "AndroidWebInterfaceBrowserDialog.h"
#include "AndroidWebInterfaceBrowserWidget.h"
#include "Android/AndroidApplication.h"
#include "Android/AndroidWindow.h"
#include "Android/AndroidJava.h"

#include <jni.h>


namespace {

	static const FString JSGetSourceCommand = TEXT("GetSource");
	static const FString JSMessageGetSourceScript =
		TEXT("document.location = '") + FMobileInterfaceJSScripting::JSMessageTag + JSGetSourceCommand +
		TEXT("/' + encodeURIComponent(document.documentElement.innerHTML);");

}

FAndroidWebInterfaceBrowserWindow::FAndroidWebInterfaceBrowserWindow(FString InUrl, TOptional<FString> InContentsToLoad, bool InShowErrorMessage, bool InThumbMouseButtonNavigation, bool InUseTransparency, bool bInJSBindingToLoweringEnabled)
	: CurrentUrl(MoveTemp(InUrl))
	, ContentsToLoad(MoveTemp(InContentsToLoad))
	, bUseTransparency(InUseTransparency)
	, DocumentState(EWebInterfaceBrowserDocumentState::NoDocument)
	, ErrorCode(0)
	, Scripting(new FMobileInterfaceJSScripting(bInJSBindingToLoweringEnabled))
	, AndroidWindowSize(FIntPoint(1024, 768))
	, bIsDisabled(false)
	, bIsVisible(true)
	, bTickedLastFrame(true)
{
}

FAndroidWebInterfaceBrowserWindow::~FAndroidWebInterfaceBrowserWindow()
{
	CloseBrowser(true, false);
}

void FAndroidWebInterfaceBrowserWindow::LoadURL(FString NewURL)
{
	BrowserWidget->LoadURL(NewURL);
}

void FAndroidWebInterfaceBrowserWindow::LoadString(FString Contents, FString DummyURL)
{
	BrowserWidget->LoadString(Contents, DummyURL);
}

TSharedRef<SWidget> FAndroidWebInterfaceBrowserWindow::CreateWidget()
{
	TSharedRef<SAndroidWebInterfaceBrowserWidget> BrowserWidgetRef =
		SNew(SAndroidWebInterfaceBrowserWidget)
		.UseTransparency(bUseTransparency)
		.InitialURL(CurrentUrl)
		.WebBrowserWindow(SharedThis(this));

	BrowserWidget = BrowserWidgetRef;

	Scripting->SetWindow(SharedThis(this));
		
	return BrowserWidgetRef;
}

void FAndroidWebInterfaceBrowserWindow::SetViewportSize(FIntPoint WindowSize, FIntPoint WindowPos)
{
	AndroidWindowSize = WindowSize;
}

FIntPoint FAndroidWebInterfaceBrowserWindow::GetViewportSize() const
{
	return AndroidWindowSize;
}

FSlateShaderResource* FAndroidWebInterfaceBrowserWindow::GetTexture(bool bIsPopup /*= false*/)
{
	return nullptr;
}

bool FAndroidWebInterfaceBrowserWindow::IsValid() const
{
	return BrowserWidget.IsValid();
}

bool FAndroidWebInterfaceBrowserWindow::IsInitialized() const
{
	return true;
}

bool FAndroidWebInterfaceBrowserWindow::IsClosing() const
{
	return false;
}

EWebInterfaceBrowserDocumentState FAndroidWebInterfaceBrowserWindow::GetDocumentLoadingState() const
{
	return DocumentState;
}

FString FAndroidWebInterfaceBrowserWindow::GetTitle() const
{
	return Title;
}

FString FAndroidWebInterfaceBrowserWindow::GetUrl() const
{
	return CurrentUrl;
}

bool FAndroidWebInterfaceBrowserWindow::OnKeyDown(const FKeyEvent& InKeyEvent)
{
//	return BrowserWidget->OnKeyDown(FGeometry(), InKeyEvent).IsEventHandled();
	return false;
}

bool FAndroidWebInterfaceBrowserWindow::OnKeyUp(const FKeyEvent& InKeyEvent)
{
//	return BrowserWidget->OnKeyUp(FGeometry(), InKeyEvent).IsEventHandled();
	return false;
}

bool FAndroidWebInterfaceBrowserWindow::OnKeyChar(const FCharacterEvent& InCharacterEvent)
{
//	return BrowserWidget->OnKeyChar(FGeometry(), InCharacterEvent).IsEventHandled();
	return false;
}

FVector2D FAndroidWebInterfaceBrowserWindow::ConvertMouseEventToLocal(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	FGeometry MouseGeometry = MyGeometry;

	float DPIScale = MouseGeometry.Scale;
	FVector2D LocalPos = MouseGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()) * DPIScale;

	return LocalPos;
}

FReply FAndroidWebInterfaceBrowserWindow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	FReply Reply = FReply::Unhandled();
	/*
	FKey Button = MouseEvent.GetEffectingButton();
	bool bSupportedButton = (Button == EKeys::LeftMouseButton); // || Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton);

	if (bSupportedButton)
	{
		Reply = FReply::Handled();
		BrowserWidget->SendTouchDown(ConvertMouseEventToLocal(MyGeometry, MouseEvent, bIsPopup));
	}
	*/
	return Reply;
}

FReply FAndroidWebInterfaceBrowserWindow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	FReply Reply = FReply::Unhandled();
	/*
	FKey Button = MouseEvent.GetEffectingButton();
	bool bSupportedButton = (Button == EKeys::LeftMouseButton); // || Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton);

	if (bSupportedButton)
	{
		Reply = FReply::Handled();
		BrowserWidget->SendTouchUp(ConvertMouseEventToLocal(MyGeometry, MouseEvent, bIsPopup));
	}
	*/
	return Reply;
}

FReply FAndroidWebInterfaceBrowserWindow::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FAndroidWebInterfaceBrowserWindow::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	FReply Reply = FReply::Unhandled();
	/*
	FKey Button = MouseEvent.GetEffectingButton();
	bool bSupportedButton = (Button == EKeys::LeftMouseButton); // || Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton);

	if (bSupportedButton)
	{
		Reply = FReply::Handled();
		BrowserWidget->SendTouchMove(ConvertMouseEventToLocal(MyGeometry, MouseEvent, bIsPopup));
	}
	*/
	return Reply;
}

void FAndroidWebInterfaceBrowserWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
}

void FAndroidWebInterfaceBrowserWindow::SetSupportsMouseWheel(bool bValue)
{
}

bool FAndroidWebInterfaceBrowserWindow::GetSupportsMouseWheel() const
{
	return false;
}

FReply FAndroidWebInterfaceBrowserWindow::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FAndroidWebInterfaceBrowserWindow::OnTouchGesture(const FGeometry& MyGeometry, const FPointerEvent& GestureEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

void FAndroidWebInterfaceBrowserWindow::OnFocus(bool SetFocus, bool bIsPopup)
{
}

void FAndroidWebInterfaceBrowserWindow::OnCaptureLost()
{
}

bool FAndroidWebInterfaceBrowserWindow::CanGoBack() const
{
	return BrowserWidget->CanGoBack();
}

void FAndroidWebInterfaceBrowserWindow::GoBack()
{
	BrowserWidget->GoBack();
}

bool FAndroidWebInterfaceBrowserWindow::CanGoForward() const
{
	return BrowserWidget->CanGoForward();
}

void FAndroidWebInterfaceBrowserWindow::GoForward()
{
	BrowserWidget->GoForward();
}

bool FAndroidWebInterfaceBrowserWindow::IsLoading() const
{
	return DocumentState != EWebInterfaceBrowserDocumentState::Loading;
}

void FAndroidWebInterfaceBrowserWindow::Reload()
{
	BrowserWidget->Reload();
}

void FAndroidWebInterfaceBrowserWindow::StopLoad()
{
	BrowserWidget->StopLoad();
}

void FAndroidWebInterfaceBrowserWindow::GetSource(TFunction<void (const FString&)> Callback) const
{
	//@todo: decide what to do about multiple pending requests
	GetPageSourceCallback.Emplace(Callback);

	// Ugly hack: Work around the fact that ExecuteJavascript is non-const.
	const_cast<FAndroidWebInterfaceBrowserWindow*>(this)->ExecuteJavascript(JSMessageGetSourceScript);
}

int FAndroidWebInterfaceBrowserWindow::GetLoadError()
{
	return ErrorCode;
}

void FAndroidWebInterfaceBrowserWindow::NotifyDocumentError(const FString& InCurrentUrl, int InErrorCode)
{
	if(!CurrentUrl.Equals(InCurrentUrl, ESearchCase::CaseSensitive))
	{
		CurrentUrl = InCurrentUrl;
		UrlChangedEvent.Broadcast(CurrentUrl);
	}

	ErrorCode = InErrorCode;
	DocumentState = EWebInterfaceBrowserDocumentState::Error;
	DocumentStateChangedEvent.Broadcast(DocumentState);
}

void FAndroidWebInterfaceBrowserWindow::NotifyDocumentLoadingStateChange(const FString& InCurrentUrl, bool IsLoading)
{
	// Ignore a load completed notification if there was an error.
	// For load started, reset any errors from previous page load.
	if (IsLoading || DocumentState != EWebInterfaceBrowserDocumentState::Error)
	{
		if(!CurrentUrl.Equals(InCurrentUrl, ESearchCase::CaseSensitive))
		{
			CurrentUrl = InCurrentUrl;
			UrlChangedEvent.Broadcast(CurrentUrl);
		}

		if(!IsLoading && !InCurrentUrl.StartsWith("javascript:"))
		{
			Scripting->PageLoaded(SharedThis(this));
		}
		ErrorCode = 0;
		DocumentState = IsLoading
			? EWebInterfaceBrowserDocumentState::Loading
			: EWebInterfaceBrowserDocumentState::Completed;
		DocumentStateChangedEvent.Broadcast(DocumentState);
	}

}

void FAndroidWebInterfaceBrowserWindow::SetIsDisabled(bool bValue)
{
	bIsDisabled = bValue;
}

TSharedPtr<SWindow> FAndroidWebInterfaceBrowserWindow::GetParentWindow() const
{
	return ParentWindow;
}

void FAndroidWebInterfaceBrowserWindow::SetParentWindow(TSharedPtr<SWindow> Window)
{
	ParentWindow = Window;
}

void FAndroidWebInterfaceBrowserWindow::ExecuteJavascript(const FString& Script)
{
	BrowserWidget->ExecuteJavascript(Script);
}

void FAndroidWebInterfaceBrowserWindow::CloseBrowser(bool bForce, bool bBlockTillClosed /* ignored */)
{
	BrowserWidget->Close();
}

bool FAndroidWebInterfaceBrowserWindow::OnJsMessageReceived(const FString& Command, const TArray<FString>& Params, const FString& Origin)
{
	if( Command.Equals(JSGetSourceCommand, ESearchCase::CaseSensitive) && GetPageSourceCallback.IsSet() && Params.Num() == 1)
	{
		GetPageSourceCallback.GetValue()(Params[0]);
		GetPageSourceCallback.Reset();
		return true;
	}
	return Scripting->OnJsMessageReceived(Command, Params, Origin);
}

void FAndroidWebInterfaceBrowserWindow::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent /*= true*/)
{
	Scripting->BindUObject(SharedThis(this), Name, Object, bIsPermanent);
}

void FAndroidWebInterfaceBrowserWindow::UnbindUObject(const FString& Name, UObject* Object /*= nullptr*/, bool bIsPermanent /*= true*/)
{
	Scripting->UnbindUObject(SharedThis(this), Name, Object, bIsPermanent);
}

void FAndroidWebInterfaceBrowserWindow::CheckTickActivity()
{
	if (bIsVisible != bTickedLastFrame)
	{
		bIsVisible = bTickedLastFrame;
		BrowserWidget->SetWebBrowserVisibility(bIsVisible);
	}

	bTickedLastFrame = false;
}

void FAndroidWebInterfaceBrowserWindow::SetTickLastFrame()
{
	bTickedLastFrame = !bIsDisabled;
}

bool FAndroidWebInterfaceBrowserWindow::IsVisible()
{
	return bIsVisible;
}

#endif // USE_ANDROID_JNI

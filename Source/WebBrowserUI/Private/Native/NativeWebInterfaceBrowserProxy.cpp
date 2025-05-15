// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "NativeWebInterfaceBrowserProxy.h"
#include "NativeInterfaceJSScripting.h"
#include "Misc/EmbeddedCommunication.h"


FNativeWebInterfaceBrowserProxy::FNativeWebInterfaceBrowserProxy(bool bInJSBindingToLoweringEnabled)
	: bJSBindingToLoweringEnabled(bInJSBindingToLoweringEnabled)
{

}

void FNativeWebInterfaceBrowserProxy::Initialize()
{
	Scripting = MakeShareable(new FNativeInterfaceJSScripting(bJSBindingToLoweringEnabled, SharedThis(this)));
	FEmbeddedDelegates::GetNativeToEmbeddedParamsDelegateForSubsystem(TEXT("browserProxy")).AddRaw(this, &FNativeWebInterfaceBrowserProxy::HandleEmbeddedCommunication);
}

FNativeWebInterfaceBrowserProxy::~FNativeWebInterfaceBrowserProxy()
{
	FEmbeddedDelegates::GetNativeToEmbeddedParamsDelegateForSubsystem(TEXT("browserProxy")).RemoveAll(this);
}

bool FNativeWebInterfaceBrowserProxy::OnJsMessageReceived(const FString& Message)
{
	return Scripting->OnJsMessageReceived(Message);
}

void FNativeWebInterfaceBrowserProxy::HandleEmbeddedCommunication(const FEmbeddedCallParamsHelper& Params)
{
	FString Error;
	if (Params.Command == "handlejs")
	{
		FString Message = Params.Parameters.FindRef(TEXT("script"));
		if (!Message.IsEmpty())
		{
			if (!OnJsMessageReceived(Message))
			{
				Error = TEXT("Command failed");
			}
		}
	}
	else if (Params.Command == "pageload")
	{
		Scripting->PageLoaded();
	}

	Params.OnCompleteDelegate(FEmbeddedCommunicationMap(), Error);
}

void FNativeWebInterfaceBrowserProxy::LoadURL(FString NewURL)
{
}

void FNativeWebInterfaceBrowserProxy::LoadString(FString Contents, FString DummyURL)
{
}

void FNativeWebInterfaceBrowserProxy::SetViewportSize(FIntPoint WindowSize, FIntPoint WindowPos)
{
}

FIntPoint FNativeWebInterfaceBrowserProxy::GetViewportSize() const
{
	return FIntPoint(ForceInitToZero);
}

FSlateShaderResource* FNativeWebInterfaceBrowserProxy::GetTexture(bool bIsPopup /*= false*/)
{
	return nullptr;
}

bool FNativeWebInterfaceBrowserProxy::IsValid() const
{
	return false;
}

bool FNativeWebInterfaceBrowserProxy::IsInitialized() const
{
	return true;
}

bool FNativeWebInterfaceBrowserProxy::IsClosing() const
{
	return false;
}

EWebInterfaceBrowserDocumentState FNativeWebInterfaceBrowserProxy::GetDocumentLoadingState() const
{
	return EWebInterfaceBrowserDocumentState::Loading;
}

FString FNativeWebInterfaceBrowserProxy::GetTitle() const
{
	return TEXT("");
}

FString FNativeWebInterfaceBrowserProxy::GetUrl() const
{
	return TEXT("");
}

void FNativeWebInterfaceBrowserProxy::GetSource(TFunction<void(const FString&)> Callback) const
{
	Callback(FString());
}

void FNativeWebInterfaceBrowserProxy::SetSupportsMouseWheel(bool bValue)
{

}

bool FNativeWebInterfaceBrowserProxy::GetSupportsMouseWheel() const
{
	return false;
}

bool FNativeWebInterfaceBrowserProxy::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	return false;
}

bool FNativeWebInterfaceBrowserProxy::OnKeyUp(const FKeyEvent& InKeyEvent)
{
	return false;
}

bool FNativeWebInterfaceBrowserProxy::OnKeyChar(const FCharacterEvent& InCharacterEvent)
{
	return false;
}

FReply FNativeWebInterfaceBrowserProxy::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FNativeWebInterfaceBrowserProxy::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FNativeWebInterfaceBrowserProxy::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

FReply FNativeWebInterfaceBrowserProxy::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}

void FNativeWebInterfaceBrowserProxy::OnMouseLeave(const FPointerEvent& MouseEvent)
{
}

FReply FNativeWebInterfaceBrowserProxy::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup)
{
	return FReply::Unhandled();
}


void FNativeWebInterfaceBrowserProxy::OnFocus(bool SetFocus, bool bIsPopup)
{
}

void FNativeWebInterfaceBrowserProxy::OnCaptureLost()
{
}

bool FNativeWebInterfaceBrowserProxy::CanGoBack() const
{
	return false;
}

void FNativeWebInterfaceBrowserProxy::GoBack()
{
}

bool FNativeWebInterfaceBrowserProxy::CanGoForward() const
{
	return false;
}

void FNativeWebInterfaceBrowserProxy::GoForward()
{
}

bool FNativeWebInterfaceBrowserProxy::IsLoading() const
{
	return false;
}

void FNativeWebInterfaceBrowserProxy::Reload()
{
}

void FNativeWebInterfaceBrowserProxy::StopLoad()
{
}

void FNativeWebInterfaceBrowserProxy::ExecuteJavascript(const FString& Script)
{
	FEmbeddedCallParamsHelper CallHelper;
	CallHelper.Command = TEXT("execjs");
	CallHelper.Parameters = { { TEXT("script"), Script } };

	FEmbeddedDelegates::GetEmbeddedToNativeParamsDelegateForSubsystem(TEXT("webview")).Broadcast(CallHelper);
}

void FNativeWebInterfaceBrowserProxy::CloseBrowser(bool bForce, bool bBlockTillClosed /* ignored */)
{
}

void FNativeWebInterfaceBrowserProxy::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent /*= true*/)
{
	Scripting->BindUObject(Name, Object, bIsPermanent);
}

void FNativeWebInterfaceBrowserProxy::UnbindUObject(const FString& Name, UObject* Object /*= nullptr*/, bool bIsPermanent /*= true*/)
{
	Scripting->UnbindUObject(Name, Object, bIsPermanent);
}

int FNativeWebInterfaceBrowserProxy::GetLoadError()
{
	return 0;
}

void FNativeWebInterfaceBrowserProxy::SetIsDisabled(bool bValue)
{
}

TSharedPtr<SWindow> FNativeWebInterfaceBrowserProxy::GetParentWindow() const
{
	return nullptr;
}

void FNativeWebInterfaceBrowserProxy::SetParentWindow(TSharedPtr<SWindow> Window)
{
}

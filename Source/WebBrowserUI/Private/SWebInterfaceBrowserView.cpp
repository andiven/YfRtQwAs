// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "SWebInterfaceBrowserView.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Containers/Ticker.h"
#include "WebInterfaceBrowserModule.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "IWebInterfaceBrowserDialog.h"
#include "IWebInterfaceBrowserWindow.h"
#include "WebInterfaceBrowserViewport.h"
#include "IWebInterfaceBrowserAdapter.h"

#if PLATFORM_ANDROID && USE_ANDROID_JNI
#	include "Android/AndroidWebInterfaceBrowserWindow.h"
#elif PLATFORM_IOS
#	include "IOS/IOSPlatformWebInterfaceBrowser.h"
#elif WITH_CEF3
#	include "CEF/CEFWebInterfaceBrowserWindow.h"
#else
#	define DUMMY_WEB_BROWSER 1
#endif

#define LOCTEXT_NAMESPACE "WebInterfaceBrowser"

SWebInterfaceBrowserView::SWebInterfaceBrowserView()
{
}

SWebInterfaceBrowserView::~SWebInterfaceBrowserView()
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->OnDocumentStateChanged().RemoveAll(this);
		BrowserWindow->OnNeedsRedraw().RemoveAll(this);
		BrowserWindow->OnTitleChanged().RemoveAll(this);
		BrowserWindow->OnUrlChanged().RemoveAll(this);
		BrowserWindow->OnToolTip().RemoveAll(this);
		BrowserWindow->OnShowPopup().RemoveAll(this);
		BrowserWindow->OnDismissPopup().RemoveAll(this);

#if WITH_CEF3
		BrowserWindow->OnMouseLost().Unbind();
#endif

		BrowserWindow->OnShowDialog().Unbind();
		BrowserWindow->OnDismissAllDialogs().Unbind();
		BrowserWindow->OnCreateWindow().Unbind();
		BrowserWindow->OnCloseWindow().Unbind();
		BrowserWindow->OnSuppressContextMenu().Unbind();
		BrowserWindow->OnDragWindow().Unbind();
		BrowserWindow->OnConsoleLog().Unbind();
		BrowserWindow->OnConsoleMessage().Unbind();

		if (BrowserWindow->OnBeforeBrowse().IsBoundToObject(this))
		{
			BrowserWindow->OnBeforeBrowse().Unbind();
		}

		if (BrowserWindow->OnLoadUrl().IsBoundToObject(this))
		{
			BrowserWindow->OnLoadUrl().Unbind();
		}

		if (BrowserWindow->OnBeforePopup().IsBoundToObject(this))
		{
			BrowserWindow->OnBeforePopup().Unbind();
		}
	}

	TSharedPtr<SWindow> SlateParentWindow = SlateParentWindowPtr.Pin();
	if (SlateParentWindow.IsValid())
	{
		SlateParentWindow->GetOnWindowDeactivatedEvent().RemoveAll(this);
	}

	if (SlateParentWindow.IsValid())
	{
		SlateParentWindow->GetOnWindowActivatedEvent().RemoveAll(this);
	}
}

void SWebInterfaceBrowserView::Construct(const FArguments& InArgs, const TSharedPtr<IWebInterfaceBrowserWindow>& InWebBrowserWindow)
{
	OnLoadCompleted = InArgs._OnLoadCompleted;
	OnLoadError = InArgs._OnLoadError;
	OnLoadStarted = InArgs._OnLoadStarted;
	OnTitleChanged = InArgs._OnTitleChanged;
	OnUrlChanged = InArgs._OnUrlChanged;
	OnBeforeNavigation = InArgs._OnBeforeNavigation;
	OnLoadUrl = InArgs._OnLoadUrl;
#if WITH_CEF3
	OnMouseLost = InArgs._OnMouseLost;
#endif
	OnShowDialog = InArgs._OnShowDialog;
	OnDismissAllDialogs = InArgs._OnDismissAllDialogs;
	OnBeforePopup = InArgs._OnBeforePopup;
	OnCreateWindow = InArgs._OnCreateWindow;
	OnCloseWindow = InArgs._OnCloseWindow;
	AddressBarUrl = FText::FromString(InArgs._InitialURL);
	PopupMenuMethod = InArgs._PopupMenuMethod;
	OnUnhandledKeyDown = InArgs._OnUnhandledKeyDown;
	OnUnhandledKeyUp = InArgs._OnUnhandledKeyUp;
	OnUnhandledKeyChar = InArgs._OnUnhandledKeyChar;
	OnConsoleLog = InArgs._OnConsoleLog;
	OnConsoleMessage = InArgs._OnConsoleMessage;

	BrowserWindow = InWebBrowserWindow;
	if(!BrowserWindow.IsValid())
	{

		static bool AllowCEF = !FParse::Param(FCommandLine::Get(), TEXT("nocef"));
		bool bBrowserEnabled = true;
		GConfig->GetBool(TEXT("Browser"), TEXT("bEnabled"), bBrowserEnabled, GEngineIni);
		if (AllowCEF && bBrowserEnabled)
		{
			FCreateInterfaceBrowserWindowSettings Settings;
			Settings.InitialURL = InArgs._InitialURL;
			Settings.bUseTransparency = InArgs._SupportsTransparency;
			Settings.bInterceptLoadRequests = InArgs._InterceptLoadRequests;
			Settings.bThumbMouseButtonNavigation = InArgs._SupportsThumbMouseButtonNavigation;
			Settings.ContentsToLoad = InArgs._ContentsToLoad;
			Settings.bShowErrorMessage = InArgs._ShowErrorMessage;
			Settings.BackgroundColor = InArgs._BackgroundColor;
			Settings.BrowserFrameRate = InArgs._BrowserFrameRate;
			Settings.Context = InArgs._ContextSettings;
			Settings.AltRetryDomains = InArgs._AltRetryDomains;

			// IWebInterfaceBrowserModule::Get() was already callled in WebUIModule.cpp so we don't need to force the load again here
			if (IWebInterfaceBrowserModule::IsAvailable() && IWebInterfaceBrowserModule::Get().IsWebModuleAvailable())
			{
				BrowserWindow = IWebInterfaceBrowserModule::Get().GetSingleton()->CreateBrowserWindow(Settings);
			}
		}
	}

	SlateParentWindowPtr = InArgs._ParentWindow;

	if (BrowserWindow.IsValid())
	{
#ifndef DUMMY_WEB_BROWSER
		// The inner widget creation is handled by the WebBrowserWindow implementation.
		const auto& BrowserWidgetRef = static_cast<FWebInterfaceBrowserWindow*>(BrowserWindow.Get())->CreateWidget();
		ChildSlot
		[
			BrowserWidgetRef
		];
		BrowserWidget = BrowserWidgetRef;
#endif

		if(OnCreateWindow.IsBound())
		{
			BrowserWindow->OnCreateWindow().BindSP(this, &SWebInterfaceBrowserView::HandleCreateWindow);
		}

		if(OnCloseWindow.IsBound())
		{
			BrowserWindow->OnCloseWindow().BindSP(this, &SWebInterfaceBrowserView::HandleCloseWindow);
		}

		BrowserWindow->OnDocumentStateChanged().AddSP(this, &SWebInterfaceBrowserView::HandleBrowserWindowDocumentStateChanged);
		BrowserWindow->OnNeedsRedraw().AddSP(this, &SWebInterfaceBrowserView::HandleBrowserWindowNeedsRedraw);
		BrowserWindow->OnTitleChanged().AddSP(this, &SWebInterfaceBrowserView::HandleTitleChanged);
		BrowserWindow->OnUrlChanged().AddSP(this, &SWebInterfaceBrowserView::HandleUrlChanged);
		BrowserWindow->OnToolTip().AddSP(this, &SWebInterfaceBrowserView::HandleToolTip);
		OnCreateToolTip = InArgs._OnCreateToolTip;

		if (!BrowserWindow->OnBeforeBrowse().IsBound())
		{
			BrowserWindow->OnBeforeBrowse().BindSP(this, &SWebInterfaceBrowserView::HandleBeforeNavigation);
		}
		else
		{
			check(!OnBeforeNavigation.IsBound());
		}

		if (!BrowserWindow->OnLoadUrl().IsBound())
		{
			BrowserWindow->OnLoadUrl().BindSP(this, &SWebInterfaceBrowserView::HandleLoadUrl);
		}
		else
		{
			check(!OnLoadUrl.IsBound());
		}

		if (!BrowserWindow->OnBeforePopup().IsBound())
		{
			BrowserWindow->OnBeforePopup().BindSP(this, &SWebInterfaceBrowserView::HandleBeforePopup);
		}
		else
		{
			check(!OnBeforePopup.IsBound());
		}

		if (!BrowserWindow->OnUnhandledKeyDown().IsBound())
		{
			BrowserWindow->OnUnhandledKeyDown().BindSP(this, &SWebInterfaceBrowserView::UnhandledKeyDown);
		}

		if (!BrowserWindow->OnUnhandledKeyUp().IsBound())
		{
			BrowserWindow->OnUnhandledKeyUp().BindSP(this, &SWebInterfaceBrowserView::UnhandledKeyUp);
		}

		if (!BrowserWindow->OnUnhandledKeyChar().IsBound())
		{
			BrowserWindow->OnUnhandledKeyChar().BindSP(this, &SWebInterfaceBrowserView::UnhandledKeyChar);
		}
		
#if WITH_CEF3
		BrowserWindow->OnMouseLost().BindSP(this, &SWebInterfaceBrowserView::HandleMouseLost);
#endif

		BrowserWindow->OnShowDialog().BindSP(this, &SWebInterfaceBrowserView::HandleShowDialog);
		BrowserWindow->OnDismissAllDialogs().BindSP(this, &SWebInterfaceBrowserView::HandleDismissAllDialogs);
		BrowserWindow->OnShowPopup().AddSP(this, &SWebInterfaceBrowserView::HandleShowPopup);
		BrowserWindow->OnDismissPopup().AddSP(this, &SWebInterfaceBrowserView::HandleDismissPopup);

		BrowserWindow->OnSuppressContextMenu().BindSP(this, &SWebInterfaceBrowserView::HandleSuppressContextMenu);


		OnSuppressContextMenu = InArgs._OnSuppressContextMenu;

		BrowserWindow->OnDragWindow().BindSP(this, &SWebInterfaceBrowserView::HandleDrag);
		OnDragWindow = InArgs._OnDragWindow;

		if (!BrowserWindow->OnConsoleLog().IsBound())
		{
			BrowserWindow->OnConsoleLog().BindSP(this, &SWebInterfaceBrowserView::HandleConsoleLog);
		}

		if (!BrowserWindow->OnConsoleMessage().IsBound())
		{
			BrowserWindow->OnConsoleMessage().BindSP(this, &SWebInterfaceBrowserView::HandleConsoleMessage);
		}

		BrowserViewport = MakeShareable(new FWebInterfaceBrowserViewport(BrowserWindow));
#if WITH_CEF3
		BrowserWidget->SetViewportInterface(BrowserViewport.ToSharedRef());
#endif
		// If we could not obtain the parent window during widget construction, we'll defer and keep trying.
		SetupParentWindowHandlers();
	}
	else
	{
		OnLoadError.ExecuteIfBound();
	}
}

int32 SWebInterfaceBrowserView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!SlateParentWindowPtr.IsValid())
	{
		SWebInterfaceBrowserView* MutableThis = const_cast<SWebInterfaceBrowserView*>(this);
		MutableThis->SetupParentWindowHandlers();
	}

	int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	
	// Cache a reference to our parent window, if we didn't already reference it.
	if (!SlateParentWindowPtr.IsValid())
	{
		SWindow* ParentWindow = OutDrawElements.GetPaintWindow();

		TSharedRef<SWindow> SlateParentWindowRef = StaticCastSharedRef<SWindow>(ParentWindow->AsShared());
		SlateParentWindowPtr = SlateParentWindowRef;
		if (BrowserWindow.IsValid())
		{
			BrowserWindow->SetParentWindow(SlateParentWindowRef);
		}
	}

	return Layer;
}

void SWebInterfaceBrowserView::HandleWindowDeactivated()
{
	if (BrowserViewport.IsValid())
	{
		BrowserViewport->OnFocusLost(FFocusEvent());
	}
}

FReply SWebInterfaceBrowserView::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	FReply Reply = FReply::Handled();
	if (InFocusEvent.GetCause() != EFocusCause::Cleared)
	{
		if (BrowserWidget.IsValid())
		{
			Reply.SetUserFocus(BrowserWidget.ToSharedRef(), InFocusEvent.GetCause());
		}
		else
		{
			Reply.SetUserFocus(this->AsShared());
		}
		if (BrowserWindow.IsValid())
		{
			BrowserWindow->OnFocus(true, false);
		}
	}
	return Reply;
}

void SWebInterfaceBrowserView::HandleWindowActivated()
{
	if (BrowserViewport.IsValid())
	{
		if (HasAnyUserFocusOrFocusedDescendants())
		{
			BrowserViewport->OnFocusReceived(FFocusEvent());
		}	
	}
}

void SWebInterfaceBrowserView::LoadURL(FString NewURL)
{
	AddressBarUrl = FText::FromString(NewURL);
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->LoadURL(NewURL);
	}
}

void SWebInterfaceBrowserView::LoadString(FString Contents, FString DummyURL)
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->LoadString(Contents, DummyURL);
	}
}

void SWebInterfaceBrowserView::Reload()
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->Reload();
	}
}

void SWebInterfaceBrowserView::StopLoad()
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->StopLoad();
	}
}

FText SWebInterfaceBrowserView::GetTitleText() const
{
	if (BrowserWindow.IsValid())
	{
		return FText::FromString(BrowserWindow->GetTitle());
	}
	return LOCTEXT("InvalidWindow", "Browser Window is not valid/supported");
}

FString SWebInterfaceBrowserView::GetUrl() const
{
	if (BrowserWindow.IsValid())
	{
		return BrowserWindow->GetUrl();
	}

	return FString();
}

FText SWebInterfaceBrowserView::GetAddressBarUrlText() const
{
	if(BrowserWindow.IsValid())
	{
		return AddressBarUrl;
	}
	return FText::GetEmpty();
}

bool SWebInterfaceBrowserView::IsLoaded() const
{
	if (BrowserWindow.IsValid())
	{
		return (BrowserWindow->GetDocumentLoadingState() == EWebInterfaceBrowserDocumentState::Completed);
	}

	return false;
}

bool SWebInterfaceBrowserView::IsLoading() const
{
	if (BrowserWindow.IsValid())
	{
		return (BrowserWindow->GetDocumentLoadingState() == EWebInterfaceBrowserDocumentState::Loading);
	}

	return false;
}

bool SWebInterfaceBrowserView::CanGoBack() const
{
	if (BrowserWindow.IsValid())
	{
		return BrowserWindow->CanGoBack();
	}
	return false;
}

void SWebInterfaceBrowserView::GoBack()
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->GoBack();
	}
}

bool SWebInterfaceBrowserView::CanGoForward() const
{
	if (BrowserWindow.IsValid())
	{
		return BrowserWindow->CanGoForward();
	}
	return false;
}

void SWebInterfaceBrowserView::GoForward()
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->GoForward();
	}
}

bool SWebInterfaceBrowserView::IsInitialized() const
{
	return BrowserWindow.IsValid() &&  BrowserWindow->IsInitialized();
}

void SWebInterfaceBrowserView::SetupParentWindowHandlers()
{
	if (!SlateParentWindowPtr.IsValid())
	{
		SlateParentWindowPtr = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));

		TSharedPtr<SWindow> SlateParentWindow = SlateParentWindowPtr.Pin();
		if (SlateParentWindow.IsValid() && BrowserWindow.IsValid())
		{
			if (!SlateParentWindow->GetOnWindowDeactivatedEvent().IsBoundToObject(this))
			{
				SlateParentWindow->GetOnWindowDeactivatedEvent().AddSP(this, &SWebInterfaceBrowserView::HandleWindowDeactivated);
			}

			if (!SlateParentWindow->GetOnWindowActivatedEvent().IsBoundToObject(this))
			{
				SlateParentWindow->GetOnWindowActivatedEvent().AddSP(this, &SWebInterfaceBrowserView::HandleWindowActivated);
			}

			BrowserWindow->SetParentWindow(SlateParentWindow);
		}
	}
}

void SWebInterfaceBrowserView::HandleBrowserWindowDocumentStateChanged(EWebInterfaceBrowserDocumentState NewState)
{
	switch (NewState)
	{
	case EWebInterfaceBrowserDocumentState::Completed:
		{
			if (BrowserWindow.IsValid())
			{
				for (auto Adapter : Adapters)
				{
					Adapter->ConnectTo(BrowserWindow.ToSharedRef());
				}
			}

			OnLoadCompleted.ExecuteIfBound();
		}
		break;

	case EWebInterfaceBrowserDocumentState::Error:
		OnLoadError.ExecuteIfBound();
		break;

	case EWebInterfaceBrowserDocumentState::Loading:
		OnLoadStarted.ExecuteIfBound();
		break;
	}
}

void SWebInterfaceBrowserView::HandleBrowserWindowNeedsRedraw()
{
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsSlateAsleep())
	{
		// Tell slate that the widget needs to wake up for one frame to get redrawn
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime) { return EActiveTimerReturnType::Stop; }));
	}
}

void SWebInterfaceBrowserView::HandleTitleChanged( FString NewTitle )
{
	const FText NewTitleText = FText::FromString(NewTitle);
	OnTitleChanged.ExecuteIfBound(NewTitleText);
}

void SWebInterfaceBrowserView::HandleUrlChanged( FString NewUrl )
{
	AddressBarUrl = FText::FromString(NewUrl);
	OnUrlChanged.ExecuteIfBound(AddressBarUrl);
}

void SWebInterfaceBrowserView::CloseBrowser()
{
	BrowserWindow->CloseBrowser(true /*force*/, true /*block until closed*/);
}

void SWebInterfaceBrowserView::HandleToolTip(FString ToolTipText)
{
	if(ToolTipText.IsEmpty())
	{
		FSlateApplication::Get().CloseToolTip();
		SetToolTip(nullptr);
	}
	else if (OnCreateToolTip.IsBound())
	{
		SetToolTip(OnCreateToolTip.Execute(FText::FromString(ToolTipText)));
		FSlateApplication::Get().UpdateToolTip(true);
	}
	else
	{
		SetToolTipText(FText::FromString(ToolTipText));
		FSlateApplication::Get().UpdateToolTip(true);
	}
}

bool SWebInterfaceBrowserView::HandleBeforeNavigation(const FString& Url, const FWebNavigationRequest& Request)
{
	if(OnBeforeNavigation.IsBound())
	{
		return OnBeforeNavigation.Execute(Url, Request);
	}
	return false;
}

bool SWebInterfaceBrowserView::HandleLoadUrl(const FString& Method, const FString& Url, FString& OutResponse)
{
	if(OnLoadUrl.IsBound())
	{
		return OnLoadUrl.Execute(Method, Url, OutResponse);
	}
	return false;
}

void SWebInterfaceBrowserView::HandleMouseLost()
{
#if WITH_CEF3
	if(OnMouseLost.IsBound())
	{
		OnMouseLost.Execute();
	}
#endif
}

EWebInterfaceBrowserDialogEventResponse SWebInterfaceBrowserView::HandleShowDialog(const TWeakPtr<IWebInterfaceBrowserDialog>& DialogParams)
{
	if(OnShowDialog.IsBound())
	{
		return OnShowDialog.Execute(DialogParams);
	}
	return EWebInterfaceBrowserDialogEventResponse::Unhandled;
}

void SWebInterfaceBrowserView::HandleDismissAllDialogs()
{
	OnDismissAllDialogs.ExecuteIfBound();
}


bool SWebInterfaceBrowserView::HandleBeforePopup(FString URL, FString Target)
{
	if (OnBeforePopup.IsBound())
	{
		return OnBeforePopup.Execute(URL, Target);
	}

	return false;
}

void SWebInterfaceBrowserView::ExecuteJavascript(const FString& ScriptText)
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->ExecuteJavascript(ScriptText);
	}
}

void SWebInterfaceBrowserView::GetSource(TFunction<void (const FString&)> Callback) const
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->GetSource(Callback);
	}
}


bool SWebInterfaceBrowserView::HandleCreateWindow(const TWeakPtr<IWebInterfaceBrowserWindow>& NewBrowserWindow, const TWeakPtr<IWebInterfaceBrowserPopupFeatures>& PopupFeatures)
{
	if(OnCreateWindow.IsBound())
	{
		return OnCreateWindow.Execute(NewBrowserWindow, PopupFeatures);
	}
	return false;
}

bool SWebInterfaceBrowserView::HandleCloseWindow(const TWeakPtr<IWebInterfaceBrowserWindow>& NewBrowserWindow)
{
	if(OnCloseWindow.IsBound())
	{
		return OnCloseWindow.Execute(NewBrowserWindow);
	}
	return false;
}

void SWebInterfaceBrowserView::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->BindUObject(Name, Object, bIsPermanent);
	}
}

void SWebInterfaceBrowserView::UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->UnbindUObject(Name, Object, bIsPermanent);
	}
}

void SWebInterfaceBrowserView::BindAdapter(const TSharedRef<IWebInterfaceBrowserAdapter>& Adapter)
{
	Adapters.Add(Adapter);
	if (BrowserWindow.IsValid())
	{
		Adapter->ConnectTo(BrowserWindow.ToSharedRef());
	}
}

void SWebInterfaceBrowserView::UnbindAdapter(const TSharedRef<IWebInterfaceBrowserAdapter>& Adapter)
{
	Adapters.Remove(Adapter);
	if (BrowserWindow.IsValid())
	{
		Adapter->DisconnectFrom(BrowserWindow.ToSharedRef());
	}
}

void SWebInterfaceBrowserView::BindInputMethodSystem(ITextInputMethodSystem* TextInputMethodSystem)
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->BindInputMethodSystem(TextInputMethodSystem);
	}
}

void SWebInterfaceBrowserView::UnbindInputMethodSystem()
{
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->UnbindInputMethodSystem();
	}
}

void SWebInterfaceBrowserView::HandleShowPopup(const FIntRect& PopupSize)
{
	check(!PopupMenuPtr.IsValid())

	TSharedPtr<SViewport> MenuContent;
	SAssignNew(MenuContent, SViewport)
				.ViewportSize(PopupSize.Size())
				.EnableGammaCorrection(false)
				.EnableBlending(false)
				.IgnoreTextureAlpha(true)
#if WITH_CEF3
				.RenderTransform(this, &SWebInterfaceBrowserView::GetPopupRenderTransform)
#endif
				.Visibility(EVisibility::Visible);
	MenuViewport = MakeShareable(new FWebInterfaceBrowserViewport(BrowserWindow, true));
	MenuContent->SetViewportInterface(MenuViewport.ToSharedRef());
	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetUnchecked(SharedThis(this), WidgetPath);
	if (WidgetPath.IsValid())
	{
		TSharedRef< SWidget > MenuContentRef = MenuContent.ToSharedRef();
		const FGeometry& BrowserGeometry = WidgetPath.Widgets.Last().Geometry;
		const FVector2D NewPosition = BrowserGeometry.LocalToAbsolute(PopupSize.Min);


		// Open the pop-up. The popup method will be queried from the widget path passed in.
		TSharedPtr<IMenu> NewMenu = FSlateApplication::Get().PushMenu(SharedThis(this), WidgetPath, MenuContentRef, NewPosition, FPopupTransitionEffect( FPopupTransitionEffect::ComboButton ), false);
		NewMenu->GetOnMenuDismissed().AddSP(this, &SWebInterfaceBrowserView::HandleMenuDismissed);
		PopupMenuPtr = NewMenu;
	}

}

TOptional <FSlateRenderTransform> SWebInterfaceBrowserView::GetPopupRenderTransform() const
{
	if (BrowserWindow.IsValid())
	{
#if !defined(DUMMY_WEB_BROWSER) && WITH_CEF3
		TOptional<FSlateRenderTransform> LocalRenderTransform = FSlateRenderTransform();
		if (static_cast<FWebInterfaceBrowserWindow*>(BrowserWindow.Get())->UsingAcceleratedPaint())
		{
			// the accelerated renderer for CEF generates inverted textures (compared to the slate co-ord system), so flip it here
			LocalRenderTransform = FSlateRenderTransform(Concatenate(FScale2D(1, -1), FVector2D(0, PopupMenuPtr.Pin()->GetContent()->GetDesiredSize().Y)));
		}
		return LocalRenderTransform;
#else
		return FSlateRenderTransform();
#endif
	}
	else
	{
		return FSlateRenderTransform();
	}
}

void SWebInterfaceBrowserView::HandleMenuDismissed(TSharedRef<IMenu>)
{
	PopupMenuPtr.Reset();
}

void SWebInterfaceBrowserView::HandleDismissPopup()
{
	if (PopupMenuPtr.IsValid())
	{
		PopupMenuPtr.Pin()->Dismiss();
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

bool SWebInterfaceBrowserView::HandleSuppressContextMenu()
{
	if (OnSuppressContextMenu.IsBound())
	{
		return OnSuppressContextMenu.Execute();
	}

	return false;
}

bool SWebInterfaceBrowserView::HandleDrag(const FPointerEvent& MouseEvent)
{
	if (OnDragWindow.IsBound())
	{
		return OnDragWindow.Execute(MouseEvent);
	}
	return false;
}

bool SWebInterfaceBrowserView::UnhandledKeyDown(const FKeyEvent& KeyEvent)
{
	if (OnUnhandledKeyDown.IsBound())
	{
		return OnUnhandledKeyDown.Execute(KeyEvent);
	}
	return false;
}

bool SWebInterfaceBrowserView::UnhandledKeyUp(const FKeyEvent& KeyEvent)
{
	if (OnUnhandledKeyUp.IsBound())
	{
		return OnUnhandledKeyUp.Execute(KeyEvent);
	}
	return false;
}

bool SWebInterfaceBrowserView::UnhandledKeyChar(const FCharacterEvent& CharacterEvent)
{
	if (OnUnhandledKeyChar.IsBound())
	{
		return OnUnhandledKeyChar.Execute(CharacterEvent);
	}
	return false;
}


void SWebInterfaceBrowserView::SetParentWindow(TSharedPtr<SWindow> Window)
{
	SetupParentWindowHandlers();
	if (BrowserWindow.IsValid())
	{
		BrowserWindow->SetParentWindow(Window);
	}
}

void SWebInterfaceBrowserView::SetBrowserKeyboardFocus()
{
	BrowserWindow->OnFocus(HasAnyUserFocusOrFocusedDescendants(), false);
}

void SWebInterfaceBrowserView::HandleConsoleLog(const FString& Text, FColor Color)
{
	OnConsoleLog.ExecuteIfBound(Text, Color);
}

void SWebInterfaceBrowserView::HandleConsoleMessage(const FString& Message, const FString& Source, int32 Line, EWebInterfaceBrowserConsoleLogSeverity Severity)
{
	OnConsoleMessage.ExecuteIfBound(Message, Source, Line, Severity);
}

#undef LOCTEXT_NAMESPACE

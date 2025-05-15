// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#if UE_VERSION >= 424
#include "Framework/Application/SlateUser.h"
#endif

#ifndef UE_SERVER
#define UE_SERVER 0
#endif

#if !UE_SERVER
#include "SWebInterfaceBrowserView.h"

class ITextInputMethodSystem;
class SWebInterfaceBrowserView;
class IWebInterfaceBrowserAdapter;
class IWebInterfaceBrowserDialog;
class IWebInterfaceBrowserWindow;
class IWebInterfaceBrowserPopupFeatures;
enum class EWebInterfaceBrowserDialogEventResponse;
struct FWebNavigationRequest;

class WEBUI_API SWebInterface : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams( bool, FOnBeforeBrowse, const FString& /*Url*/, const FWebNavigationRequest& /*Request*/ );
	DECLARE_DELEGATE_RetVal_ThreeParams( bool, FOnLoadUrl, const FString& /*Method*/, const FString& /*Url*/, FString& /* Response */ );
	DECLARE_DELEGATE_RetVal_OneParam( EWebInterfaceBrowserDialogEventResponse, FOnShowDialog, const TWeakPtr<IWebInterfaceBrowserDialog>& );

	DECLARE_DELEGATE_RetVal( bool, FOnSuppressContextMenu );
	DECLARE_DELEGATE_TwoParams( FOnConsoleEvent, const FString& /*Text*/, FColor /*Color*/ );

	SLATE_BEGIN_ARGS( SWebInterface )
		: _FrameRate( 60 )
		, _InitialURL( "https://tracerinteractive.com" )
		, _BackgroundColor( 255, 255, 255, 255 )
		, _AcceleratedPaint( false )
		, _NativeCursors( false )
		, _EnableMouseTransparency( false )
		, _EnableVirtualPointerTransparency( false )
		, _TransparencyDelay( 0.1f )
		, _TransparencyThreshold( 0.333f )
		, _TransparencyTick( 0.0f )
		, _TransparencyDrag( true )
		, _ViewportSize( FVector2D::ZeroVector )
	{
		_Visibility = EVisibility::SelfHitTestInvisible;
	}
		SLATE_ARGUMENT( TSharedPtr<SWindow>, ParentWindow )
		SLATE_ARGUMENT( int32, FrameRate )
		SLATE_ARGUMENT( FString, InitialURL )
		SLATE_ARGUMENT( TOptional<FString>, ContentsToLoad )
		SLATE_ARGUMENT( FColor, BackgroundColor )
		SLATE_ARGUMENT( bool, AcceleratedPaint )
		SLATE_ARGUMENT( bool, NativeCursors )
		SLATE_ARGUMENT( bool, EnableMouseTransparency )
		SLATE_ARGUMENT( bool, EnableVirtualPointerTransparency )
		SLATE_ARGUMENT( float, TransparencyDelay )
		SLATE_ARGUMENT( float, TransparencyThreshold )
		SLATE_ARGUMENT( float, TransparencyTick )
		SLATE_ARGUMENT( bool,  TransparencyDrag )
		SLATE_ARGUMENT( TOptional<EPopupMethod>, PopupMenuMethod )

		SLATE_ATTRIBUTE( FVector2D, ViewportSize );

		SLATE_EVENT( FSimpleDelegate, OnLoadCompleted )
		SLATE_EVENT( FSimpleDelegate, OnLoadError )
		SLATE_EVENT( FSimpleDelegate, OnLoadStarted )
		SLATE_EVENT( FOnTextChanged, OnTitleChanged )
		SLATE_EVENT( FOnTextChanged, OnUrlChanged )
		SLATE_EVENT( FOnBeforePopupDelegate, OnBeforePopup )
		SLATE_EVENT( FOnCreateWindowDelegate, OnCreateWindow )
		SLATE_EVENT( FOnCloseWindowDelegate, OnCloseWindow )
		SLATE_EVENT( FOnBeforeBrowse, OnBeforeNavigation )
		SLATE_EVENT( FOnLoadUrl, OnLoadUrl )
		SLATE_EVENT( FOnShowDialog, OnShowDialog )
		SLATE_EVENT( FSimpleDelegate, OnDismissAllDialogs )
		SLATE_EVENT( FOnSuppressContextMenu, OnSuppressContextMenu )
		SLATE_EVENT( FOnConsoleEvent, OnConsoleEvent );
	SLATE_END_ARGS()

	SWebInterface();
	~SWebInterface();

	void Construct( const FArguments& InArgs );

	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:

	FLinearColor LastMousePixel;
	float        LastMouseTime;
	float        LastMouseTick;

	EVisibility GetViewportVisibility() const;

	bool HandleBeforePopup( FString URL, FString Frame );
	bool HandleSuppressContextMenu();

	bool HandleCreateWindow( const TWeakPtr<IWebInterfaceBrowserWindow>& NewBrowserWindow, const TWeakPtr<IWebInterfaceBrowserPopupFeatures>& PopupFeatures );
	bool HandleCloseWindow( const TWeakPtr<IWebInterfaceBrowserWindow>& BrowserWindowPtr );

	void HandleMouseLost();

	void HandleConsoleLog( const FString& Text, FColor Color );
	void HandleConsoleMessage( const FString& Message, const FString& Source, int32 Line, EWebInterfaceBrowserConsoleLogSeverity Severity );

protected:

	static bool bPAK;

	TSharedPtr<SWebInterfaceBrowserView>   BrowserView;
	TSharedPtr<IWebInterfaceBrowserWindow> BrowserWindow;

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	TMap<TWeakPtr<IWebInterfaceBrowserWindow>, TWeakPtr<SWindow>> BrowserWindowWidgets;
#endif
	
	bool bMouseTransparency;
	bool bVirtualPointerTransparency;

	bool bTransparencyForceVisible;

	float TransparencyDelay;
	float TransparencyThreshold;
	float TransparencyTick;
	bool  TransparencyDrag;


	FSimpleDelegate OnLoadCompleted;
	FSimpleDelegate OnLoadError;
	FSimpleDelegate OnLoadStarted;

	FOnTextChanged OnTitleChanged;
	FOnTextChanged OnUrlChanged;

	FOnBeforePopupDelegate  OnBeforePopup;
	FOnCreateWindowDelegate OnCreateWindow;
	FOnCloseWindowDelegate  OnCloseWindow;

	FOnBeforeBrowse OnBeforeNavigation;
	FOnLoadUrl      OnLoadUrl;

	FOnShowDialog   OnShowDialog;
	FSimpleDelegate OnDismissAllDialogs;

	FOnConsoleEvent OnConsoleEvent;

public:

	bool HasMouseTransparency() const;
	bool HasVirtualPointerTransparency() const;

	float GetTransparencyDelay() const;
	float GetTransparencyThreshold() const;
	float GetTransparencyTick() const;
	bool  GetTransparencyDrag() const;

	bool IsClickable() const;

	bool HasMouseCapture() const;
#if UE_VERSION >= 424
	bool HasMouseCapture( FSlateUser& User ) const;
#else
	bool HasMouseCapture( FSlateUser* User ) const;
#endif

	void ReleaseMouseCapture();
#if UE_VERSION >= 424
	void ReleaseMouseCapture( FSlateUser& User );
#else
	void ReleaseMouseCapture( FSlateUser* User );
#endif

	int32 GetTextureWidth() const;
	int32 GetTextureHeight() const;

	FColor ReadTexturePixel( int32 X, int32 Y ) const;
	TArray<FColor> ReadTexturePixels( int32 X, int32 Y, int32 Width, int32 Height ) const;

	static bool CanSupportAcceleratedPaint();
	bool IsUsingAcceleratedPaint() const;

	bool RequiresAcceleratedPaintInterop() const;
	bool HasAcceleratedPaintInterop() const;
	
	void LoadURL( FString NewURL );
	void LoadString( FString Contents, FString DummyURL );

	void Reload();
	void StopLoad();

	FString GetUrl() const;

	bool IsLoaded() const;
	bool IsLoading() const;

	void ExecuteJavascript( const FString& ScriptText );

	void BindUObject( const FString& Name, UObject* Object, bool bIsPermanent = true );
	void UnbindUObject( const FString& Name, UObject* Object, bool bIsPermanent = true );

	void BindAdapter( const TSharedRef<IWebInterfaceBrowserAdapter>& Adapter );
	void UnbindAdapter( const TSharedRef<IWebInterfaceBrowserAdapter>& Adapter );

	void BindInputMethodSystem( ITextInputMethodSystem* TextInputMethodSystem );
	void UnbindInputMethodSystem();

	void SetParentWindow( TSharedPtr<SWindow> Window );
};
#endif

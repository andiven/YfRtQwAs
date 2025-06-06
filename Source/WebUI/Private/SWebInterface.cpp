// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "SWebInterface.h"
#if !UE_SERVER
#include "SWebInterfaceBrowser.h"
#include "WebInterfaceBrowserModule.h"
#include "WebInterfaceBrowserSchemeHandler.h"
#include "IWebInterfaceBrowserPopupFeatures.h"
#include "IWebInterfaceBrowserSingleton.h"
#include "IWebInterfaceBrowserWindow.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Widgets/Layout/SBorder.h"

#if PLATFORM_WINDOWS
#include "WebBrowserUtils.h"
#endif

SWebInterface::SWebInterface()
{
	bMouseTransparency          = false;
	bVirtualPointerTransparency = false;

	TransparencyDelay     = 0.0f;
	TransparencyThreshold = 0.333f;
	TransparencyTick      = 0.0f;
	TransparencyDrag      = true;

	bTransparencyForceVisible = false;

	LastMousePixel = FLinearColor::White;
	LastMouseTime  = 0.0f;
	LastMouseTick  = 0.0f;
}

SWebInterface::~SWebInterface()
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	for ( TPair<TWeakPtr<IWebInterfaceBrowserWindow>, TWeakPtr<SWindow>> Temp : BrowserWindowWidgets )
	{
		if ( Temp.Key.IsValid() )
		{
			TSharedPtr<IWebInterfaceBrowserWindow> WebBrowserWindow = Temp.Key.Pin();
			if ( WebBrowserWindow.IsValid() )
				WebBrowserWindow->CloseBrowser( false );
		}

		if ( Temp.Value.IsValid() )
		{
			TSharedPtr<SWindow> Window = Temp.Value.Pin();
			if ( Window.IsValid() )
				Window->RequestDestroyWindow();
		}
	}
#endif
}

bool SWebInterface::bPAK = false;
void SWebInterface::Construct( const FArguments& InArgs )
{
	OnLoadCompleted     = InArgs._OnLoadCompleted;
	OnLoadError         = InArgs._OnLoadError;
	OnLoadStarted       = InArgs._OnLoadStarted;
	OnTitleChanged      = InArgs._OnTitleChanged;
	OnUrlChanged        = InArgs._OnUrlChanged;
	OnBeforeNavigation  = InArgs._OnBeforeNavigation;
	OnLoadUrl           = InArgs._OnLoadUrl;
	OnShowDialog        = InArgs._OnShowDialog;
	OnDismissAllDialogs = InArgs._OnDismissAllDialogs;
	OnBeforePopup       = InArgs._OnBeforePopup;
	OnCreateWindow      = InArgs._OnCreateWindow;
	OnCloseWindow       = InArgs._OnCloseWindow;
	OnConsoleEvent      = InArgs._OnConsoleEvent;

	bMouseTransparency          = InArgs._EnableMouseTransparency;
	bVirtualPointerTransparency = InArgs._EnableVirtualPointerTransparency;

	TransparencyDelay     = FMath::Max( 0.0f, InArgs._TransparencyDelay );
	TransparencyThreshold = FMath::Clamp( InArgs._TransparencyThreshold, 0.0f, 1.0f );
	TransparencyTick      = FMath::Max( 0.0f, InArgs._TransparencyTick );
	TransparencyDrag      = InArgs._TransparencyDrag;

	FCreateInterfaceBrowserWindowSettings Settings;
	Settings.bAcceleratedPaint = InArgs._AcceleratedPaint && CanSupportAcceleratedPaint();
	Settings.bUseNativeCursors = InArgs._NativeCursors;
	Settings.BrowserFrameRate  = FMath::Clamp( InArgs._FrameRate, 1, 60 );
	Settings.bUseTransparency  = true;
	Settings.BackgroundColor   = InArgs._BackgroundColor;
	Settings.InitialURL        = InArgs._InitialURL;
	Settings.ContentsToLoad    = InArgs._ContentsToLoad;
	Settings.bShowErrorMessage = UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG;
	Settings.bThumbMouseButtonNavigation = false;

	IWebInterfaceBrowserSingleton* Singleton = IWebInterfaceBrowserModule::Get().GetSingleton();
	if ( Singleton )
	{
#if WITH_CEF3
		if ( !bPAK )
			bPAK = Singleton->RegisterSchemeHandlerFactory( "http", "game.local", new FWebInterfaceBrowserSchemeHandlerFactory() );
#endif

		Singleton->SetDevToolsShortcutEnabled( Settings.bShowErrorMessage );
		BrowserWindow = Singleton->CreateBrowserWindow( Settings );
	}

	ChildSlot
	[
		SAssignNew( BrowserView, SWebInterfaceBrowserView, BrowserWindow )
		.ParentWindow( InArgs._ParentWindow )
		.InitialURL( InArgs._InitialURL )
		.ContentsToLoad( InArgs._ContentsToLoad )
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
		.ShowErrorMessage( true )
#else
		.ShowErrorMessage( false )
#endif
		.SupportsTransparency( true )
		.SupportsThumbMouseButtonNavigation( false )
		.BackgroundColor( InArgs._BackgroundColor )
		.PopupMenuMethod( InArgs._PopupMenuMethod )
		.ViewportSize( InArgs._ViewportSize )
		.OnLoadCompleted( OnLoadCompleted )
		.OnLoadError( OnLoadError )
		.OnLoadStarted( OnLoadStarted )
		.OnTitleChanged( OnTitleChanged )
		.OnUrlChanged( OnUrlChanged )
		.OnBeforePopup( this, &SWebInterface::HandleBeforePopup )
		.OnCreateWindow( this, &SWebInterface::HandleCreateWindow )
		.OnCloseWindow( this, &SWebInterface::HandleCloseWindow )
		.OnBeforeNavigation( OnBeforeNavigation )
		.OnLoadUrl( OnLoadUrl )
#if WITH_CEF3
		.OnMouseLost( this, &SWebInterface::HandleMouseLost )
#endif
		.OnShowDialog( OnShowDialog )
		.OnDismissAllDialogs( OnDismissAllDialogs )
		.Visibility( this, &SWebInterface::GetViewportVisibility )
		.OnSuppressContextMenu( this, &SWebInterface::HandleSuppressContextMenu )
		.OnConsoleLog( this, &SWebInterface::HandleConsoleLog )
		.OnConsoleMessage( this, &SWebInterface::HandleConsoleMessage )
	];
}

void SWebInterface::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SWidget::Tick( AllottedGeometry, InCurrentTime, InDeltaTime );
	if ( HasMouseTransparency() && FSlateApplication::IsInitialized() )
	{
		TSharedPtr<ICursor> Mouse = FSlateApplication::Get().GetPlatformCursor();
		if ( TransparencyTick <= 0.0f
		  || TransparencyTick <= LastMouseTick )
		{
			LastMousePixel = FLinearColor::Transparent;
			LastMouseTime += InDeltaTime;
			LastMouseTick  = 0.0f;

			if ( Mouse.IsValid() && Mouse->GetType() != EMouseCursor::None )
			{
				FVector2D MousePosition = Mouse->GetPosition();
				if ( !MousePosition.ContainsNaN() )
				{
					FVector2D LocalMouse = AllottedGeometry.AbsoluteToLocal( MousePosition );
					FVector2D LocalSize  = AllottedGeometry.GetLocalSize();

					FVector2D LocalUV = LocalSize.X > 0.0f && LocalSize.Y > 0.0f ?
										FVector2D( LocalMouse.X / LocalSize.X, LocalMouse.Y / LocalSize.Y ) :
										FVector2D();

					if ( LocalUV.X >= 0.0f && LocalUV.X <= 1.0f && LocalUV.Y >= 0.0f && LocalUV.Y <= 1.0f )
					{
						int32 X = FMath::FloorToInt( LocalUV.X * GetTextureWidth() );
						int32 Y = FMath::FloorToInt( LocalUV.Y * GetTextureHeight() );
		
						FLinearColor Pixel = ReadTexturePixel( X, Y );
						if ( ( Pixel.A <  TransparencyThreshold && LastMousePixel.A >= TransparencyThreshold )
						  || ( Pixel.A >= TransparencyThreshold && LastMousePixel.A <  TransparencyThreshold ) )
							LastMouseTime = 0.0f;

						LastMousePixel = Pixel;
					}
				}
			}
		}
		else
			LastMouseTick += InDeltaTime;

		if ( TransparencyDrag && Mouse.IsValid() && Mouse->GetType() != EMouseCursor::None )
		{
			bool bMouseCapture = HasMouseCapture();
			if ( IsClickable() )
				bTransparencyForceVisible = bMouseCapture;
			else if ( !bMouseCapture )
				bTransparencyForceVisible = false;
		}
		else
			bTransparencyForceVisible = false;
	}
	else
		LastMousePixel = FLinearColor::White;
}

EVisibility SWebInterface::GetViewportVisibility() const
{
	if ( !BrowserView.IsValid() || !BrowserView->IsInitialized() )
		return EVisibility::Hidden;

	if ( HasMouseTransparency() && !IsClickable() )
		return bTransparencyForceVisible ? EVisibility::Visible : EVisibility::HitTestInvisible;

	return EVisibility::Visible;
}

bool SWebInterface::HandleBeforePopup( FString URL, FString Frame )
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if ( URL.StartsWith( "chrome-devtools://" ) )
		return false;
#endif

	if ( OnBeforePopup.IsBound() )
		return OnBeforePopup.Execute( URL, Frame );

	return true;
}

bool SWebInterface::HandleSuppressContextMenu()
{
	return true;
}

bool SWebInterface::HandleCreateWindow( const TWeakPtr<IWebInterfaceBrowserWindow>& NewBrowserWindow, const TWeakPtr<IWebInterfaceBrowserPopupFeatures>& PopupFeatures )
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if ( !PopupFeatures.IsValid() )
		return false;

	TSharedPtr<IWebInterfaceBrowserPopupFeatures> PopupFeaturesSP = PopupFeatures.Pin();
	if ( !PopupFeaturesSP.IsValid() )
		return false;
	
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow( SharedThis( this ) );
	if ( !ParentWindow.IsValid() )
		return false;

	const int PosX = PopupFeaturesSP->IsXSet() ? PopupFeaturesSP->GetX() : 100;
	const int PosY = PopupFeaturesSP->IsYSet() ? PopupFeaturesSP->GetY() : 100;
	const FVector2D BrowserWindowPosition( PosX, PosY );

	const int Width  = PopupFeaturesSP->IsWidthSet()  ? PopupFeaturesSP->GetWidth()  : 800;
	const int Height = PopupFeaturesSP->IsHeightSet() ? PopupFeaturesSP->GetHeight() : 600;
	const FVector2D BrowserWindowSize( Width, Height );

	const ESizingRule SizingRule = PopupFeaturesSP->IsResizable() ?
								   ESizingRule::UserSized :
								   ESizingRule::FixedSize;

	TSharedPtr<IWebInterfaceBrowserWindow> NewBrowserWindowSP = NewBrowserWindow.Pin();
	if ( !NewBrowserWindowSP.IsValid() )
		return false;

	TSharedRef<SWindow> NewWindow =
		SNew( SWindow )
		.Title( FText::GetEmpty() )
		.ClientSize( BrowserWindowSize )
		.ScreenPosition( BrowserWindowPosition )
		.AutoCenter( EAutoCenter::None )
		.SizingRule( SizingRule )
		.SupportsMaximize( SizingRule != ESizingRule::FixedSize )
		.SupportsMinimize( SizingRule != ESizingRule::FixedSize )
		.HasCloseButton( true )
		.CreateTitleBar( true )
		.IsInitiallyMaximized( PopupFeaturesSP->IsFullscreen() )
		.LayoutBorder( FMargin( 0 ) );

	TSharedPtr<SWebInterfaceBrowser> WebBrowser;
	NewWindow->SetContent(
		SNew( SBorder )
		.VAlign( VAlign_Fill )
		.HAlign( HAlign_Fill )
		.Padding( 0 )
		[
			SAssignNew( WebBrowser, SWebInterfaceBrowser, NewBrowserWindowSP )
				.ShowControls( false )
				.ShowAddressBar( false )
				.OnCreateWindow( this, &SWebInterface::HandleCreateWindow )
				.OnCloseWindow( this, &SWebInterface::HandleCloseWindow )
		] );
	
	{
		struct FLocal
		{
			static void RequestDestroyWindowOverride( const TSharedRef<SWindow>& Window, TWeakPtr<IWebInterfaceBrowserWindow> BrowserWindowPtr )
			{
				TSharedPtr<IWebInterfaceBrowserWindow> BrowserWindow = BrowserWindowPtr.Pin();
				if ( BrowserWindow.IsValid() )
				{
					if ( BrowserWindow->IsClosing() )
						FSlateApplicationBase::Get().RequestDestroyWindow( Window );
					else
						BrowserWindow->CloseBrowser( false );
				}
			}
		};

		NewWindow->SetRequestDestroyWindowOverride( FRequestDestroyWindowOverride::CreateStatic( &FLocal::RequestDestroyWindowOverride, TWeakPtr<IWebInterfaceBrowserWindow>( NewBrowserWindow ) ) );
	}

	FSlateApplication::Get().AddWindow( NewWindow );
	NewWindow->BringToFront();
	FSlateApplication::Get().SetKeyboardFocus( WebBrowser, EFocusCause::SetDirectly );

	BrowserWindowWidgets.Add( NewBrowserWindow, NewWindow );
	return true;
#else
	return false;
#endif
}

bool SWebInterface::HandleCloseWindow( const TWeakPtr<IWebInterfaceBrowserWindow>& BrowserWindowPtr )
{
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
	if ( !BrowserWindowPtr.IsValid() )
		return false;

	TSharedPtr<IWebInterfaceBrowserWindow> WebBrowserWindow = BrowserWindowPtr.Pin();
	if ( !WebBrowserWindow.IsValid() )
		return false;

	if ( WebBrowserWindow->IsClosing() )
	{
		const TWeakPtr<SWindow>* FoundWebBrowserWindow = BrowserWindowWidgets.Find( WebBrowserWindow );
		if ( FoundWebBrowserWindow != nullptr )
		{
			TSharedPtr<SWindow> FoundWindow = FoundWebBrowserWindow->Pin();
			if ( FoundWindow.IsValid() )
				FoundWindow->RequestDestroyWindow();

			BrowserWindowWidgets.Remove( WebBrowserWindow );
			return true;
		}
	}
	else
		WebBrowserWindow->CloseBrowser( false );

	return false;
#else
	return false;
#endif
}

void SWebInterface::HandleMouseLost()
{
	ReleaseMouseCapture();
	bTransparencyForceVisible = false;
}

void SWebInterface::HandleConsoleLog( const FString& Text, FColor Color )
{
	OnConsoleEvent.ExecuteIfBound( Text, Color );
}

void SWebInterface::HandleConsoleMessage( const FString& Message, const FString& Source, int32 Line, EWebInterfaceBrowserConsoleLogSeverity Severity )
{
  //OnConsoleEvent.ExecuteIfBound( Message );
}

bool SWebInterface::HasMouseTransparency() const
{
	return bMouseTransparency && !bVirtualPointerTransparency;
}

bool SWebInterface::HasVirtualPointerTransparency() const
{
	return bVirtualPointerTransparency;
}

float SWebInterface::GetTransparencyDelay() const
{
	return TransparencyDelay;
}

float SWebInterface::GetTransparencyThreshold() const
{
	return TransparencyThreshold;
}

float SWebInterface::GetTransparencyTick() const
{
	return TransparencyTick;
}

bool SWebInterface::GetTransparencyDrag() const
{
	return TransparencyDrag;
}

bool SWebInterface::IsClickable() const
{
	if ( !BrowserView.IsValid() || !BrowserView->IsInitialized() )
		return false;
	if ( HasMouseTransparency() && LastMousePixel.A < TransparencyThreshold && LastMouseTime >= TransparencyDelay )
		return false;

	return true;
}

bool SWebInterface::HasMouseCapture() const
{
	bool bMouseCapture = false;
#if UE_VERSION >= 424
	FSlateApplication::Get().ForEachUser( [&bMouseCapture, this]( FSlateUser& User )
#else
	FSlateApplication::Get().ForEachUser( [&bMouseCapture, this]( FSlateUser* User )
#endif
		{
			if ( HasMouseCapture( User ) )
				bMouseCapture = true;
		} );

	return bMouseCapture;
}

#if UE_VERSION >= 424
bool SWebInterface::HasMouseCapture( FSlateUser& User ) const
{
	return User.HasAnyCapture();

  //TSharedPtr<const SWidget> Widget = AsShared().ToSharedPtr();
  //return User.DoesWidgetHaveAnyCapture( Widget );
}
#else
bool SWebInterface::HasMouseCapture( FSlateUser* User ) const
{
	if (User)
		return FSlateApplication::Get().HasUserMouseCapture(User->GetUserIndex());
	return false;
}
#endif

void SWebInterface::ReleaseMouseCapture()
{
	// temporary until widget is checked above
	if ( !bTransparencyForceVisible )
		return;

#if UE_VERSION >= 424
	FSlateApplication::Get().ForEachUser( [this]( FSlateUser& User )
#else
	FSlateApplication::Get().ForEachUser( [this]( FSlateUser* User )
#endif
		{
			ReleaseMouseCapture( User );
		} );
}

#if UE_VERSION >= 424
void SWebInterface::ReleaseMouseCapture( FSlateUser& User )
{
	if ( HasMouseCapture( User ) )
		User.ReleaseAllCapture();
}
#else
void SWebInterface::ReleaseMouseCapture( FSlateUser* User )
{
	if ( HasMouseCapture( User ) )
		FSlateApplication::Get().ReleaseMouseCaptureForUser(User->GetUserIndex());
}
#endif

int32 SWebInterface::GetTextureWidth() const
{
	if ( !BrowserWindow.IsValid() )
		return 0;

	FSlateShaderResource* Resource = BrowserWindow->GetTexture();
	if ( !Resource )
		return 0;

	return Resource->GetWidth();
}

int32 SWebInterface::GetTextureHeight() const
{
	if ( !BrowserWindow.IsValid() )
		return 0;

	FSlateShaderResource* Resource = BrowserWindow->GetTexture();
	if ( !Resource )
		return 0;

	return Resource->GetHeight();
}

FColor SWebInterface::ReadTexturePixel( int32 X, int32 Y ) const
{
	if ( X < 0 || X >= GetTextureWidth() )
		return FColor::Transparent;
	if ( Y < 0 || Y >= GetTextureHeight() )
		return FColor::Transparent;

	TArray<FColor> Pixels = ReadTexturePixels( X, Y, 1, 1 );
	if ( Pixels.Num() > 0 )
		return Pixels[ 0 ];

	return FColor::Transparent;
}

TArray<FColor> SWebInterface::ReadTexturePixels( int32 X, int32 Y, int32 Width, int32 Height ) const
{
	TArray<FColor> OutPixels;
	if ( !BrowserWindow.IsValid() )
		return OutPixels;

	FSlateShaderResource* Resource = BrowserWindow->GetTexture();
	if ( !Resource || Resource->GetType() != ESlateShaderResource::NativeTexture )
		return OutPixels;

#if UE_VERSION >= 505
	FTextureRHIRef TextureRHI;
	TextureRHI = ( ( TSlateTexture<FTextureRHIRef>* )Resource )->GetTypedResource();
#else
	FTexture2DRHIRef TextureRHI;
	TextureRHI = ( ( TSlateTexture<FTexture2DRHIRef>* )Resource )->GetTypedResource();
#endif

	struct FReadSurfaceContext
	{
#if UE_VERSION >= 505
		FTextureRHIRef Texture;
#else
		FTexture2DRHIRef Texture;
#endif
		TArray<FColor>* OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	int32 ResourceWidth  = (int32)Resource->GetWidth();
	int32 ResourceHeight = (int32)Resource->GetHeight();

	X = FMath::Clamp( X, 0, ResourceWidth  - 1 );
	Y = FMath::Clamp( Y, 0, ResourceHeight - 1 );

	Width  = FMath::Clamp( Width, 1, ResourceWidth );
	Width  = Width - FMath::Max( X + Width - ResourceWidth, 0 );

	Height = FMath::Clamp( Height, 1, ResourceHeight );
	Height = Height - FMath::Max( Y + Height - ResourceHeight, 0 );

	FReadSurfaceContext Context =
	{
		TextureRHI,
		&OutPixels,
		FIntRect( X, Y, X + Width, Y + Height ),
		FReadSurfaceDataFlags()
	};

	ENQUEUE_RENDER_COMMAND( ReadSurfaceCommand )(
		[ Context ]( FRHICommandListImmediate& RHICmdList )
		{
			RHICmdList.ReadSurfaceData(
				Context.Texture,
				Context.Rect,
				*Context.OutData,
				Context.Flags
			);
		} );
	FlushRenderingCommands();

	return OutPixels;
}

bool SWebInterface::CanSupportAcceleratedPaint()
{
#if PLATFORM_WINDOWS
	return WebBrowserUtils::CanSupportAcceleratedPaint();
#else
	return false;
#endif
}

bool SWebInterface::RequiresAcceleratedPaintInterop() const
{
	if ( !BrowserWindow.IsValid() )
		return false;
	
#if PLATFORM_WINDOWS
	if ( !BrowserWindow->IsUsingAcceleratedPaint() )
		return false;

#if UE_VERSION >= 501
	if ( RHIGetInterfaceType() == ERHIInterfaceType::D3D12 )
		return true;
#else
	static bool sbD3D12Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
	if ( sbD3D12Renderer )
		return true;
#endif
#endif

	return false;
}

bool SWebInterface::IsUsingAcceleratedPaint() const
{
#if PLATFORM_WINDOWS
	if ( !BrowserWindow.IsValid() )
		return false;
	
	return BrowserWindow->IsUsingAcceleratedPaint();
#else
	return false;
#endif
}

bool SWebInterface::HasAcceleratedPaintInterop() const
{
	if ( !BrowserWindow.IsValid() )
		return false;

#if PLATFORM_WINDOWS
	if ( !BrowserWindow->IsUsingAcceleratedPaint() )
		return false;

	return BrowserWindow->HasAcceleratedPaintInterop();
#else
	return false;
#endif
}

void SWebInterface::LoadURL( FString NewURL )
{
	if ( BrowserView.IsValid() )
		BrowserView->LoadURL( NewURL );
}

void SWebInterface::LoadString( FString Contents, FString DummyURL )
{
	if ( BrowserView.IsValid() )
		BrowserView->LoadString( Contents, DummyURL );
}

void SWebInterface::Reload()
{
	if ( BrowserView.IsValid() )
		BrowserView->Reload();
}

void SWebInterface::StopLoad()
{
	if ( BrowserView.IsValid() )
		BrowserView->StopLoad();
}

FString SWebInterface::GetUrl() const
{
	if ( BrowserView.IsValid() )
		return BrowserView->GetUrl();

	return FString();
}

bool SWebInterface::IsLoaded() const
{
	if ( BrowserView.IsValid() )
		return BrowserView->IsLoaded();

	return false;
}

bool SWebInterface::IsLoading() const
{
	if ( BrowserView.IsValid() )
		return BrowserView->IsLoading();

	return false;
}

void SWebInterface::ExecuteJavascript( const FString& ScriptText )
{
	if ( BrowserView.IsValid() )
		BrowserView->ExecuteJavascript( ScriptText );
}

void SWebInterface::BindUObject( const FString& Name, UObject* Object, bool bIsPermanent )
{
	if ( BrowserView.IsValid() )
		BrowserView->BindUObject( Name, Object, bIsPermanent );
}

void SWebInterface::UnbindUObject( const FString& Name, UObject* Object, bool bIsPermanent )
{
	if ( BrowserView.IsValid() )
		BrowserView->UnbindUObject( Name, Object, bIsPermanent );
}

void SWebInterface::BindAdapter( const TSharedRef<IWebInterfaceBrowserAdapter>& Adapter )
{
	if ( BrowserView.IsValid() )
		BrowserView->BindAdapter( Adapter );
}

void SWebInterface::UnbindAdapter( const TSharedRef<IWebInterfaceBrowserAdapter>& Adapter )
{
	if ( BrowserView.IsValid() )
		BrowserView->UnbindAdapter( Adapter );
}

void SWebInterface::BindInputMethodSystem( ITextInputMethodSystem* TextInputMethodSystem )
{
	if ( BrowserView.IsValid() )
		BrowserView->BindInputMethodSystem( TextInputMethodSystem );
}

void SWebInterface::UnbindInputMethodSystem()
{
	if ( BrowserView.IsValid() )
		BrowserView->UnbindInputMethodSystem();
}

void SWebInterface::SetParentWindow( TSharedPtr<SWindow> Window )
{
	if ( BrowserView.IsValid() )
		BrowserView->SetParentWindow( Window );
}
#endif

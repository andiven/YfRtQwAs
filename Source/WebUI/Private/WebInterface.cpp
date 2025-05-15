// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterface.h"
#include "WebInterfaceObject.h"
#include "PlatformHttp.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Slate/SceneViewport.h"
#include "SceneView.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#if !UE_SERVER
#include "SWebInterface.h"
#endif

#define LOCTEXT_NAMESPACE "WebInterface"

UWebInterface::UWebInterface( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	bIsVariable = true;
	FrameRate   = 60;

	SetVisibility( ESlateVisibility::SelfHitTestInvisible );

	bEnableMouseTransparency   = false;
	MouseTransparencyThreshold = 0.333f;
	MouseTransparencyDelay     = 0.1f;
	MouseTransparencyTick      = 0.05f;
	MouseTransparencyDrag      = true;

	bEnableVirtualPointerTransparency   = false;
	VirtualPointerTransparencyThreshold = 0.333f;

	bAcceleratedPaint = true;
	bCustomCursors    = false;
}

bool UWebInterface::Load( const FString& File )
{
	FString URL = "http://game.local/";
	if ( File.Len() <= 0 )
		return false;

	FString FilePath = File;
	FilePath = FilePath.Replace( TEXT( "\\" ), TEXT( "/" ) );
	FilePath = FilePath.Replace( TEXT( "//" ), TEXT( "/" ) );

	URL += FilePath;
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->LoadURL( URL );
#endif

	FilePath = FPaths::ProjectContentDir() + File;
	FilePath = FilePath.Replace( TEXT( "\\" ), TEXT( "/" ) );
	FilePath = FilePath.Replace( TEXT( "//" ), TEXT( "/" ) );

	const int64 FileSize = IFileManager::Get().FileSize( *FilePath );
	return FileSize != INDEX_NONE;
}

void UWebInterface::LoadHTML( const FString& HTML )
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->LoadString( HTML, "http://localhost" );
#endif
}

void UWebInterface::LoadURL( const FString& URL )
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->LoadURL( URL );
#endif
}

void UWebInterface::LoadFile( const FString& File, EWebInterfaceDirectory Directory /*= EWebInterfaceDirectory::UI*/ )
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	FString FilePath = Directory == EWebInterfaceDirectory::Content ?
					   IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *FPaths::ProjectContentDir() ) + File :
					   IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *FPaths::ProjectDir() ) + TEXT( "UI/" ) + File;
#else
	FString FilePath = Directory == EWebInterfaceDirectory::Content ?
					   FPaths::ConvertRelativePathToFull( FPaths::ProjectContentDir() ) + File :
					   FPaths::ConvertRelativePathToFull( FPaths::ProjectDir() ) + TEXT( "UI/" ) + File;
#endif

	FilePath = FilePath.Replace( TEXT( "\\" ), TEXT( "/" ) );
	FilePath = FilePath.Replace( TEXT( "//" ), TEXT( "/" ) );

	LoadURL( TEXT( "file:///" ) + FilePath );
}

bool UWebInterface::LoadContent( const FString& File, bool bScript /*= false*/ )
{
	FString FilePath = FPaths::ProjectContentDir() + File;
	FilePath = FilePath.Replace( TEXT( "\\" ), TEXT( "/" ) );
	FilePath = FilePath.Replace( TEXT( "//" ), TEXT( "/" ) );

	FString Text;
	if ( !FFileHelper::LoadFileToString( Text, *FilePath ) )
		return false;

	if ( bScript )
		Execute( Text );
	else
		LoadHTML( Text );

	return true;
}

FString UWebInterface::GetURL() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetUrl();
#endif
	return FString();
}

void UWebInterface::Execute( const FString& Script )
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->ExecuteJavascript( Script );
#endif
}

void UWebInterface::Call( const FString& Function, const FJsonLibraryValue& Data )
{
	// reserved
	if ( Function == "broadcast" )
		return;

#if !UE_SERVER
	if ( !WebInterfaceWidget.IsValid() )
		return;

	if ( Data.GetType() != EJsonLibraryType::Invalid )
		WebInterfaceWidget->ExecuteJavascript( FString::Printf( TEXT( "typeof ue != 'undefined' && typeof ue.interface != 'undefined' && ue.interface[%s](%s)" ),
			*FJsonLibraryValue( Function ).Stringify(),
			*Data.Stringify() ) );
	else
		WebInterfaceWidget->ExecuteJavascript( FString::Printf( TEXT( "typeof ue != 'undefined' && typeof ue.interface != 'undefined' && ue.interface[%s]()" ),
			*FJsonLibraryValue( Function ).Stringify() ) );
#endif
}

void UWebInterface::Bind( const FString& Name, UObject* Object )
{
	if ( !Object )
		return;

	// reserved
	if ( Name.ToLower() == "interface" )
		return;
	
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->BindUObject( Name, Object );
#endif
}

void UWebInterface::Unbind( const FString& Name, UObject* Object )
{
	if ( !Object )
		return;

	// reserved
	if ( Name.ToLower() == "interface" )
		return;
	
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->UnbindUObject( Name, Object );
#endif
}

void UWebInterface::EnableIME()
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->BindInputMethodSystem( FSlateApplication::Get().GetTextInputMethodSystem() );
#endif
}

void UWebInterface::DisableIME()
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		WebInterfaceWidget->UnbindInputMethodSystem();
#endif
}

void FindChildWidgetsOfType( const FString& Type, TSharedRef<SWidget> Widget, TArray<TSharedRef<SWidget>>& Array )
{
	FChildren* Children = Widget->GetChildren();
	if ( !Children )
		return;

	for ( int32 i = 0; i < Children->Num(); i++ )
	{
		TSharedRef<SWidget> Child = Children->GetChildAt( i );
		if ( Type.IsEmpty() || Child->GetTypeAsString() == Type )
			Array.Add( Child );

		FindChildWidgetsOfType( Type, Child, Array );
	}
}

void UWebInterface::Focus( EMouseLockMode MouseLockMode /*= EMouseLockMode::LockOnCapture*/ )
{
	SetVisibility( ESlateVisibility::SelfHitTestInvisible );

#if !UE_SERVER
	UWorld* World = GetWorld();
	if ( !World )
		return;

	UGameViewportClient* GameViewport = World->GetGameViewport();
	UGameInstance*       GameInstance = World->GetGameInstance();
	if ( GameViewport )
	{
		TSharedPtr<SWidget> BrowserWidget = WebInterfaceWidget;
		if ( BrowserWidget.IsValid() )
		{
			TSharedPtr<SViewport> ViewportWidget = GameViewport->GetGameViewportWidget();
			if ( GameInstance && ViewportWidget.IsValid() )
			{
				TSharedRef<SWidget>   BrowserWidgetRef  = BrowserWidget.ToSharedRef();
				TSharedRef<SViewport> ViewportWidgetRef = ViewportWidget.ToSharedRef();

				TArray<TSharedRef<SWidget>> Children;
				FindChildWidgetsOfType( "SViewport", BrowserWidgetRef, Children );
				if ( Children.Num() == 1 )
				{
					BrowserWidgetRef = Children[ 0 ];
					BrowserWidget    = BrowserWidgetRef;
				}

				bool bLockMouseToViewport = MouseLockMode == EMouseLockMode::LockAlways
									   || ( MouseLockMode == EMouseLockMode::LockInFullscreen && GameViewport->IsExclusiveFullscreenViewport() );

				for ( int32 i = 0; i < GameInstance->GetNumLocalPlayers(); i++ )
				{
					ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayerByIndex( i );
					if ( !LocalPlayer )
						continue;

					FReply& SlateOperations = LocalPlayer->GetSlateOperations();
					SlateOperations.SetUserFocus( BrowserWidgetRef );

					if ( bLockMouseToViewport )
						SlateOperations.LockMouseToWidget( ViewportWidgetRef );
					else
						SlateOperations.ReleaseMouseLock();

					SlateOperations.ReleaseMouseCapture();
				}
			}

			FSlateApplication::Get().SetAllUserFocus( BrowserWidget, EFocusCause::SetDirectly );
			FSlateApplication::Get().SetKeyboardFocus( BrowserWidget, EFocusCause::SetDirectly );
		}
		
		GameViewport->SetMouseLockMode( MouseLockMode );
		GameViewport->SetIgnoreInput( true );
#if UE_VERSION >= 426
		GameViewport->SetMouseCaptureMode( EMouseCaptureMode::NoCapture );
#else
		GameViewport->SetCaptureMouseOnClick( EMouseCaptureMode::NoCapture );
#endif
	}
#endif
}

void UWebInterface::Unfocus( EMouseCaptureMode MouseCaptureMode /*= EMouseCaptureMode::CapturePermanently*/ )
{
	SetVisibility( ESlateVisibility::HitTestInvisible );

#if !UE_SERVER
	UWorld* World = GetWorld();
	if ( !World )
		return;
	
	UGameViewportClient* GameViewport = World->GetGameViewport();
	UGameInstance*       GameInstance = World->GetGameInstance();
	if ( GameViewport )
	{
		FSlateApplication::Get().ClearKeyboardFocus( EFocusCause::SetDirectly );
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	
		TSharedPtr<SViewport> ViewportWidget = GameViewport->GetGameViewportWidget();
		if ( GameInstance && ViewportWidget.IsValid() )
		{
			TSharedRef<SViewport> ViewportWidgetRef = ViewportWidget.ToSharedRef();
			for ( int32 i = 0; i < GameInstance->GetNumLocalPlayers(); i++ )
			{
				ULocalPlayer* LocalPlayer = GameInstance->GetLocalPlayerByIndex( i );
				if ( !LocalPlayer )
					continue;
			
				FReply& SlateOperations = LocalPlayer->GetSlateOperations();
				SlateOperations.UseHighPrecisionMouseMovement( ViewportWidgetRef );
				SlateOperations.SetUserFocus( ViewportWidgetRef );
				SlateOperations.LockMouseToWidget( ViewportWidgetRef );
			}
		}

		GameViewport->SetMouseLockMode( EMouseLockMode::LockOnCapture );
		GameViewport->SetIgnoreInput( false );
#if UE_VERSION >= 426
		GameViewport->SetMouseCaptureMode( MouseCaptureMode );
#else
		GameViewport->SetCaptureMouseOnClick( MouseCaptureMode );
#endif
	}
#endif
}

void UWebInterface::ResetMousePosition()
{
	UWorld* World = GetWorld();
	if ( !World )
		return;

	UGameViewportClient* GameViewport = World->GetGameViewport();
	if ( GameViewport && GameViewport->Viewport )
	{
		FIntPoint SizeXY = GameViewport->Viewport->GetSizeXY();
		GameViewport->Viewport->SetMouse( SizeXY.X / 2, SizeXY.Y / 2 );
	}
}

bool UWebInterface::RequiresAcceleratedPaint() const
{
#if !UE_SERVER
	if ( !SWebInterface::CanSupportAcceleratedPaint() )
		return false;
	if ( bNoAcceleratedPaint )
		return false;
	
	return bAcceleratedPaint;
#else
	return false;
#endif
}

bool UWebInterface::IsUsingAcceleratedPaint() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->IsUsingAcceleratedPaint();
#endif
	return false;
}

bool UWebInterface::IsMouseTransparencyEnabled() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->HasMouseTransparency();
#endif
	return false;
}

bool UWebInterface::IsVirtualPointerTransparencyEnabled() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->HasVirtualPointerTransparency();
#endif
	return false;
}

float UWebInterface::GetTransparencyDelay() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetTransparencyDelay();
#endif
	return 0.0f;
}

float UWebInterface::GetTransparencyThreshold() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetTransparencyThreshold();
#endif
	return 0.0f;
}

float UWebInterface::GetTransparencyTick() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetTransparencyTick();
#endif
	return 0.0f;
}

bool UWebInterface::GetTransparencyDrag() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetTransparencyDrag();
#endif
	return true;
}

int32 UWebInterface::GetTextureWidth() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetTextureWidth();
#endif
	return 0;
}

int32 UWebInterface::GetTextureHeight() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->GetTextureHeight();
#endif
	return 0;
}

FColor UWebInterface::ReadTexturePixel( int32 X, int32 Y )
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->ReadTexturePixel( X, Y );
#endif
	return FColor::Transparent;
}

TArray<FColor> UWebInterface::ReadTexturePixels( int32 X, int32 Y, int32 Width, int32 Height )
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->ReadTexturePixels( X, Y, Width, Height );
#endif
	return TArray<FColor>();
}

void UWebInterface::ReleaseSlateResources( bool bReleaseChildren )
{
	Super::ReleaseSlateResources( bReleaseChildren );
#if !UE_SERVER
	WebInterfaceWidget.Reset();
#endif
}

TSharedRef<SWidget> UWebInterface::RebuildWidget()
{
#if !UE_SERVER
	if ( IsDesignTime() )
		return SNew( SBox )
			.HAlign( HAlign_Center )
			.VAlign( VAlign_Center )
			[
				SNew( STextBlock )
				.Text( LOCTEXT( "Web UI", "Web UI" ) )
			];

	WebInterfaceWidget = SNew( SWebInterface )
		.FrameRate( FrameRate )
		.InitialURL( InitialURL )
		.AcceleratedPaint( bAcceleratedPaint && !bNoAcceleratedPaint )
		.NativeCursors( !bCustomCursors )
		.EnableMouseTransparency( bEnableMouseTransparency )
		.EnableVirtualPointerTransparency( bEnableVirtualPointerTransparency )
		.TransparencyDelay( MouseTransparencyDelay )
		.TransparencyTick( MouseTransparencyTick )
		.TransparencyDrag( MouseTransparencyDrag )
		.TransparencyThreshold( bEnableVirtualPointerTransparency ?
								VirtualPointerTransparencyThreshold :
								MouseTransparencyThreshold )
		.OnConsoleEvent( BIND_UOBJECT_DELEGATE( FOnConsoleLogDelegate, HandleConsole ) )
		.OnUrlChanged( BIND_UOBJECT_DELEGATE( FOnTextChanged, HandleUrlChanged ) )
		.OnBeforePopup( BIND_UOBJECT_DELEGATE( FOnBeforePopupDelegate, HandleBeforePopup ) );

#if WITH_CEF3
	MyObject = NewObject<UWebInterfaceObject>();
	if ( MyObject )
	{
		MyObject->MyInterface = this;
		WebInterfaceWidget->BindUObject( "interface", MyObject );
	}
#endif

	return WebInterfaceWidget.ToSharedRef();
#else
	TSharedPtr<SBox> WebInterfaceWidget = SNew( SBox );
	return WebInterfaceWidget.ToSharedRef();
#endif
}

void UWebInterface::HandleUrlChanged( const FText& URL )
{
	FString Hash = URL.ToString();

	int32 Index = Hash.Find( "#" );
	if ( Index >= 0 )
		Hash = Hash.RightChop( Index + 1 );

	if ( ( Hash.StartsWith( "[" ) && Hash.EndsWith( "]" ) ) || ( Hash.StartsWith( "%5B" ) && Hash.EndsWith( "%5D" ) ) )
	{
		FString JSON = FPlatformHttp::UrlDecode( Hash );

		FJsonLibraryValue Value = FJsonLibraryValue::Parse( JSON );
		if ( Value.GetType() == EJsonLibraryType::Array )
		{
			TArray<FJsonLibraryValue> Array = Value.ToArray();
			if ( Array.Num() == 2 || Array.Num() == 3 )
			{
				FJsonLibraryValue Name = Array[ 0 ];
				FJsonLibraryValue Data = Array[ 1 ];
				if ( Name.GetType() == EJsonLibraryType::String )
				{
					FName BroadcastName = *Name.GetString();
					if ( Array.Num() > 2 )
					{
						FJsonLibraryValue Callback = Array[ 2 ];
						if ( Callback.GetType() == EJsonLibraryType::String )
						{
							FString BroadcastCallback = Callback.GetString();
							if ( !BroadcastCallback.IsEmpty() )
							{
								OnInterfaceEvent.Broadcast( BroadcastName, Data, FWebInterfaceCallback( this, BroadcastCallback ) );
								return;
							}
						}
					}
					
					OnInterfaceEvent.Broadcast( BroadcastName, Data, FWebInterfaceCallback() );
					return;
				}
			}
			
			return;
		}
	}

	OnUrlChangedEvent.Broadcast( URL );
}

bool UWebInterface::HandleBeforePopup( FString URL, FString Frame )
{
	OnPopupEvent.Broadcast( URL, Frame );
	return true;
}

void UWebInterface::HandleConsole( const FString& Text, FColor Color )
{
	OnConsoleEvent.Broadcast( Text, Color );
}

#if WITH_EDITOR
const FText UWebInterface::GetPaletteCategory()
{
	return LOCTEXT( "Common", "Common" );
}
#endif

bool UWebInterface::RequiresAcceleratedPaintInterop() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->RequiresAcceleratedPaintInterop();
#endif
	return false;
}

bool UWebInterface::HasAcceleratedPaintInterop() const
{
#if !UE_SERVER
	if ( WebInterfaceWidget.IsValid() )
		return WebInterfaceWidget->HasAcceleratedPaintInterop();
#endif
	return false;
}

bool UWebInterface::bNoAcceleratedPaint = false;
void UWebInterface::OverrideAcceleratedPaint( bool bForceDisable )
{
	bNoAcceleratedPaint = bForceDisable;
}

#undef LOCTEXT_NAMESPACE

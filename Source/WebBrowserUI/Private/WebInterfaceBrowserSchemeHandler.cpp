// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserSchemeHandler.h"
#include "WebInterfaceBrowserHelpers.h"
#if !UE_SERVER
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Http.h"

#define BIN "application/octet-stream"
#define ENC "utf-8"

class FWebInterfaceBrowserSchemeHandlerHeaders
	: public IWebInterfaceBrowserSchemeHandler::IHeaders
{
public:
	FWebInterfaceBrowserSchemeHandlerHeaders()
		: ContentLength( 0 )
		, StatusCode( 0 )
	{
	}

	virtual ~FWebInterfaceBrowserSchemeHandlerHeaders()
	{
		//
	}

	virtual void SetMimeType( const TCHAR* InMimeType ) override
	{
		MimeType = FString( InMimeType );
	}

	virtual void SetEncoding( const TCHAR* InEncoding ) override
	{
		Encoding = FString( InEncoding );
	}

	virtual void SetStatusCode( int32 InStatusCode ) override
	{
		StatusCode = InStatusCode;
	}

	virtual void SetContentLength( int32 InContentLength ) override
	{
		ContentLength = InContentLength;
	}

	virtual void SetRedirect( const TCHAR* InRedirectUrl ) override
	{
		//
	}

	virtual void SetHeader( const TCHAR* Key, const TCHAR* Value ) override
	{
		Headers.Add( FString( Key ), FString( Value ) );
	}

	int64 GetContentLength() const
	{
		return ContentLength;
	}

	FString GetMimeType() const
	{
		return MimeType;
	}

	FString GetEncoding() const
	{
		return Encoding;
	}

	int32 GetStatusCode() const
	{
		return StatusCode;
	}

	const TMap<FString, FString>& GetHeaders() const
	{
		return Headers;
	}

private:
	int64 ContentLength;
	TMap<FString, FString> Headers;

	FString MimeType;
	FString Encoding;

	int32 StatusCode;
};

FWebInterfaceBrowserSchemeHandler::FWebInterfaceBrowserSchemeHandler()
	: MimeType( BIN )
	, Encoding( ENC )
	, ContentLength( 0 )
	, TotalBytesRead( 0 )
	, Reader( nullptr )
{
}

FWebInterfaceBrowserSchemeHandler::~FWebInterfaceBrowserSchemeHandler()
{
	CloseReader();
}

bool FWebInterfaceBrowserSchemeHandler::ProcessUrl( const FString& Url, const FSimpleDelegate& OnHeadersReady )
{
	FString FilePath = Url;
	if ( FilePath.Contains( "://" ) )
	{
		FString Scheme;
		if ( FParse::SchemeNameFromURI( *FilePath, Scheme ) )
			FilePath = FilePath.RightChop( Scheme.Len() + 3 );
	}

	if ( !FilePath.ToLower().StartsWith( "game.local/" ) )
		return false;
	
	FilePath = FilePath.RightChop( 11 );
	return ProcessPath( FilePath, OnHeadersReady );
}

bool FWebInterfaceBrowserSchemeHandler::ProcessPath( const FString& Path, const FSimpleDelegate& OnHeadersReady )
{
	FString FilePath = Path;

	int32 DesiredWidth  = 0;
	int32 DesiredHeight = 0;

	int32 IndexHash  = FilePath.Find( "#" );
	if ( IndexHash >= 0 )
		FilePath = FilePath.Left( IndexHash );

	int32 IndexQuery = FilePath.Find( "?" );
	if ( IndexQuery >= 0 )
	{
		TArray<FString> QueryParts;
		FilePath.RightChop( IndexQuery + 1 ).ParseIntoArray( QueryParts, TEXT( "&" ), true );

		TMap<FString, FString> QueryString;
		for ( int32 i = 0; i < QueryParts.Num(); i++ )
		{
			FString LHS, RHS;
			QueryParts[i].Split( "=", &LHS, &RHS );

			LHS = FPlatformHttp::UrlDecode( LHS );
			RHS = FPlatformHttp::UrlDecode( RHS );

			QueryString.Add( LHS, RHS );
		}

		if ( QueryString.Contains( "w" ) )
			DesiredWidth = FCString::Strtoi( *QueryString[ "w" ], nullptr, 10 );
		if ( QueryString.Contains( "h" ) )
			DesiredHeight = FCString::Strtoi( *QueryString[ "h" ], nullptr, 10 );

		FilePath = FilePath.Left( IndexQuery );
	}

	const FString ContentDirectory = FPaths::ProjectContentDir();
	FilePath = ContentDirectory / FilePath;
	FilePath = FilePath.Replace( TEXT( "\\" ), TEXT( "/" ) );
	FilePath = FilePath.Replace( TEXT( "//" ), TEXT( "/" ) );

	int64 FileSize = IFileManager::Get().FileSize( *FilePath );
	if ( FileSize != INDEX_NONE )
	{
		ContentLength = (int32)FileSize;
		if ( FileSize > INT32_MAX )
			return false;
	
		Encoding = ENC;
		MimeType = FGenericPlatformHttp::GetMimeType( FilePath );
		if ( MimeType.Len() == 0 || MimeType == "application/unknown" )
			MimeType = BIN;

		CreateReader( *FilePath );
		OnHeadersReady.Execute();
	}
	else
	{
		FString FileName      = FPaths::GetBaseFilename( FilePath );
		FString FileExtension = FPaths::GetExtension( FilePath ).ToLower();
		if ( FileExtension == "png" )
			ImageFormat = EImageFormat::PNG;
		else if ( FileExtension == "jpg"
			   || FileExtension == "jpeg" )
			ImageFormat = EImageFormat::JPEG;
		else
			ImageFormat = EImageFormat::Invalid;

		MimeType = FGenericPlatformHttp::GetMimeType( FilePath );
		if ( MimeType.Len() == 0 || MimeType == "application/unknown" )
			MimeType = BIN;
		
		Encoding = "";
		if ( ImageFormat != EImageFormat::Invalid )
		{
			FilePath  = "/Game" / FilePath.RightChop( ContentDirectory.Len() );
			FilePath  = FilePath.LeftChop( FileExtension.Len() + 1 );
			FilePath += "." + FileName;

			if ( IsInGameThread() )
			{
				if ( !CreateImageReader( FilePath, DesiredWidth, DesiredHeight ) )
					ImageFormat = EImageFormat::Invalid;

				OnHeadersReady.Execute();
			}
			else
				AsyncTask( ENamedThreads::GameThread, [this, FilePath, DesiredWidth, DesiredHeight, OnHeadersReady]()
				{
					if ( !CreateImageReader( FilePath, DesiredWidth, DesiredHeight ) )
						ImageFormat = EImageFormat::Invalid;

					OnHeadersReady.Execute();
				} );
		}
		else
			OnHeadersReady.Execute();
	}

	return true;
}

bool FWebInterfaceBrowserSchemeHandler::ProcessRequest( const FString& Verb, const FString& Url, const FSimpleDelegate& OnHeadersReady )
{
	if ( Verb.ToUpper() != "GET" )
		return false;

	return ProcessUrl( Url, OnHeadersReady );
}

void FWebInterfaceBrowserSchemeHandler::GetResponseHeaders( IHeaders& OutHeaders )
{
	if ( ImageSize.X > 0 && ImageSize.Y > 0 && ImagePixels.Num() > 0 && ImageFormat != EImageFormat::Invalid )
	{
		ContentLength = (int32)GetCompressedImageData();
		if ( ContentLength <= 0 )
			MimeType = BIN;
	}

	ImagePixels.Empty();
	if ( Reader || ImageData.Num() > 0 )
	{
		OutHeaders.SetStatusCode( 200 );
		OutHeaders.SetMimeType( *MimeType );
		OutHeaders.SetEncoding( *Encoding );
		OutHeaders.SetContentLength( ContentLength );

		OutHeaders.SetHeader( TEXT( "Access-Control-Allow-Origin" ), TEXT( "*" ) );
	}
	else
		OutHeaders.SetStatusCode( 404 );
}

bool FWebInterfaceBrowserSchemeHandler::ReadResponse( uint8* OutBytes, int32 BytesToRead, int32& BytesRead, const FSimpleDelegate& OnMoreDataReady )
{
	BytesRead = 0;
	if ( !Reader && ImageData.Num() <= 0 )
		return false;

	BytesRead = ContentLength - TotalBytesRead;
	if ( BytesRead <= 0 )
		return false;

	if ( BytesRead > BytesToRead )
		BytesRead = BytesToRead;

	if ( ImageData.Num() > 0 )
		FMemory::Memcpy( OutBytes, ImageData.GetData() + TotalBytesRead, BytesRead);
	else
		Reader->Serialize( OutBytes, BytesRead );

	TotalBytesRead += BytesRead;
	if ( TotalBytesRead < ContentLength )
		OnMoreDataReady.Execute();
	else
		CloseReader();
	
	return true;
}

void FWebInterfaceBrowserSchemeHandler::Cancel()
{
	CloseReader();
}

bool FWebInterfaceBrowserSchemeHandler::CreateReader( const FString& FilePath )
{
	if ( Reader )
		CloseReader();

	Reader = IFileManager::Get().CreateFileReader( *FilePath );
	if ( !Reader )
		return false;

	return true;
}

void FWebInterfaceBrowserSchemeHandler::CloseReader()
{
	MimeType = BIN;
	Encoding = ENC;

	ContentLength  = 0;
	TotalBytesRead = 0;

	ImageFormat = EImageFormat::Invalid;
	ImageSize   = FIntPoint::ZeroValue;

	ImagePixels.Empty();
	ImageData.Empty();

	if ( !Reader )
		return;

	Reader->Close();

	delete Reader;
	Reader = nullptr;
}

bool FWebInterfaceBrowserSchemeHandler::CreateImageReader( const FString& FilePath, int32 DesiredWidth, int32 DesiredHeight )
{
#if UE_VERSION >= 500
	TSoftObjectPtr AssetPtr;
	AssetPtr = FilePath;
#else
	FSoftObjectPath AssetPath(FilePath);
#endif

	check( IsInGameThread() );

	ImageSize = FIntPoint();
	ImagePixels.Empty();

#if UE_VERSION >= 500
	UObject* Asset = AssetPtr.LoadSynchronous();
#else
	UObject* Asset = AssetPath.TryLoad();
#endif

	if ( Asset )
	{
		UTexture2D* Texture = Cast<UTexture2D>( Asset );
		if ( Texture )
		{
			ImageSize = UWebInterfaceBrowserHelpers::GenerateImageFromTexture( ImagePixels, Texture );
			ResizeImageReader( DesiredWidth, DesiredHeight );

			return true;
		}

		UMaterialInterface* Material = Cast<UMaterialInterface>( Asset );
		if ( Material )
		{
			ImageSize = UWebInterfaceBrowserHelpers::GenerateImageFromMaterial( ImagePixels, Material, DesiredWidth, DesiredHeight );
			return true;
		}

		UTextureRenderTarget2D* RenderTarget = Cast<UTextureRenderTarget2D>( Asset );
		if ( RenderTarget )
		{
			ImageSize = UWebInterfaceBrowserHelpers::GenerateImageFromRenderTarget( ImagePixels, RenderTarget );
			ResizeImageReader( DesiredWidth, DesiredHeight );

			return true;
		}
	}

	return false;
}

bool FWebInterfaceBrowserSchemeHandler::ResizeImageReader( int32 DesiredWidth, int32 DesiredHeight )
{
	int32 Width  = DesiredWidth;
	int32 Height = DesiredHeight;

	if ( Width > 0 && Height > 0 )
	{
		if ( Width == ImageSize.X && Height == ImageSize.Y )
			return false;
	}
	else if ( Width > 0 )
	{
		if ( Width == ImageSize.X )
			return false;

		Height = FMath::RoundToInt( Width * ( (float)ImageSize.Y / (float)ImageSize.X ) );
		if ( Height <= 0 )
			return false;
	}
	else if ( Height > 0 )
	{
		if ( Height == ImageSize.Y )
			return false;

		Width = FMath::RoundToInt( Height * ( (float)ImageSize.X / (float)ImageSize.Y ) );
		if ( Width <= 0 )
			return false;
	}
	else
		return false;
	
	ImagePixels = UWebInterfaceBrowserHelpers::ResizeImage( ImageSize.X, ImageSize.Y, ImagePixels, Width, Height );
	ImageSize   = FIntPoint( Width, Height );

	return true;
}

int64 FWebInterfaceBrowserSchemeHandler::GetCompressedImageData()
{
	if ( ImageFormat == EImageFormat::Invalid )
		return 0;
	if ( ImageSize.X <= 0 || ImageSize.Y <= 0 )
		return 0;
	if ( ImagePixels.Num() <= 0 )
		return 0;

	IImageWrapper* ImageWrapper = UWebInterfaceBrowserHelpers::FindOrCreateImageWrapper( ImageFormat );
	if ( !ImageWrapper )
		return 0;

	if ( UWebInterfaceBrowserHelpers::SetImageWrapper( ImageWrapper, ImagePixels, ImageSize ) )
		ImageData = ImageWrapper->GetCompressed();

	UWebInterfaceBrowserHelpers::ReturnImageWrapper( ImageWrapper );
	return ImageData.Num();
}

int32 FWebInterfaceBrowserSchemeHandler::ProcessPath( const FString& Path, TArray<uint8>& Content, FString& MimeType, FString& Encoding, TMap<FString, FString>& Headers )
{
	if ( !IsInGameThread() )
	{
		FEvent* TaskEvent = FPlatformProcess::GetSynchEventFromPool( true );

		int32 StatusCode = 0;
		AsyncTask( ENamedThreads::GameThread, [Path, &Content, &MimeType, &Encoding, &Headers, &StatusCode, TaskEvent]()
			{
				StatusCode = FWebInterfaceBrowserSchemeHandler::ProcessPath( Path, Content, MimeType, Encoding, Headers );
				TaskEvent->Trigger();
			} );
		
		TaskEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool( TaskEvent );

		return StatusCode;
	}

	FWebInterfaceBrowserSchemeHandler SchemeHandler;
	FWebInterfaceBrowserSchemeHandlerHeaders ResponseHeaders;
	bool bSuccess = SchemeHandler.ProcessPath( Path, FSimpleDelegate::CreateLambda( [&Content, &SchemeHandler, &ResponseHeaders]()
		{
			SchemeHandler.GetResponseHeaders( ResponseHeaders );
			Content.SetNum( ResponseHeaders.GetContentLength() );
			if ( Content.Num() > 0 )
			{
				int32 BytesRead;
				SchemeHandler.ReadResponse( Content.GetData(), Content.Num(), BytesRead, FSimpleDelegate() );
			}
		} ) );

	if ( !bSuccess )
		return 0;

	MimeType = ResponseHeaders.GetMimeType();
	Encoding = ResponseHeaders.GetEncoding();

	Headers.Append( ResponseHeaders.GetHeaders() );
	return ResponseHeaders.GetStatusCode();
}


TUniquePtr<IWebInterfaceBrowserSchemeHandler> FWebInterfaceBrowserSchemeHandlerFactory::Create( FString Verb, FString Url )
{
	return MakeUnique<FWebInterfaceBrowserSchemeHandler>();
}
#endif

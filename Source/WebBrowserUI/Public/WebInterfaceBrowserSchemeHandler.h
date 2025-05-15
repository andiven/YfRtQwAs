// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once
#ifndef UE_SERVER
#define UE_SERVER 0
#endif

#if !UE_SERVER
#include "IWebInterfaceBrowserSchemeHandler.h"
#include "IImageWrapper.h"

class WEBBROWSERUI_API FWebInterfaceBrowserSchemeHandler : public IWebInterfaceBrowserSchemeHandler
{
public:
	FWebInterfaceBrowserSchemeHandler();
	virtual ~FWebInterfaceBrowserSchemeHandler();

	bool ProcessUrl ( const FString& Url,  const FSimpleDelegate& OnHeadersReady );
	bool ProcessPath( const FString& Path, const FSimpleDelegate& OnHeadersReady );
	
	virtual bool ProcessRequest( const FString& Verb, const FString& Url, const FSimpleDelegate& OnHeadersReady ) override;
	virtual void GetResponseHeaders( IHeaders& OutHeaders ) override;
	virtual bool ReadResponse( uint8* OutBytes, int32 BytesToRead, int32& BytesRead, const FSimpleDelegate& OnMoreDataReady ) override;
	virtual void Cancel() override;

protected:
	FString MimeType;
	FString Encoding;

	int32 ContentLength;
	int32 TotalBytesRead;

	FArchive* Reader;

	bool CreateReader( const FString& FilePath );
	void CloseReader();

	EImageFormat     ImageFormat;
	FIntPoint        ImageSize;
	TArray64<FColor> ImagePixels;
	TArray64<uint8>  ImageData;

	bool  CreateImageReader( const FString& FilePath, int32 DesiredWidth, int32 DesiredHeight );
	bool  ResizeImageReader( int32 DesiredWidth, int32 DesiredHeight );
	int64 GetCompressedImageData();

public:
	static int32 ProcessPath( const FString& Path, TArray<uint8>& Content, FString& MimeType, FString& Encoding, TMap<FString, FString>& Headers );
};

class WEBBROWSERUI_API FWebInterfaceBrowserSchemeHandlerFactory : public IWebInterfaceBrowserSchemeHandlerFactory
{
	virtual TUniquePtr<IWebInterfaceBrowserSchemeHandler> Create( FString Verb, FString Url ) override;
};
#endif

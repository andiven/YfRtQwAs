// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "CEF/CEFInterfaceBrowserByteResource.h"

#if WITH_CEF3

FCEFInterfaceBrowserByteResource::FCEFInterfaceBrowserByteResource(const CefRefPtr<CefPostDataElement>& PostData, const FString& InMimeType)
	: Position(0)
	, Buffer(nullptr)
	, MimeType(InMimeType)
{
	Size = PostData->GetBytesCount();
	if (Size > 0)
	{
		Buffer = new unsigned char[Size];
		PostData->GetBytes(Size, Buffer);
	}
}

FCEFInterfaceBrowserByteResource::~FCEFInterfaceBrowserByteResource()
{
	delete[] Buffer;
}

void FCEFInterfaceBrowserByteResource::Cancel()
{

}

void FCEFInterfaceBrowserByteResource::GetResponseHeaders(CefRefPtr<CefResponse> Response, int64& ResponseLength, CefString& RedirectUrl)
{
	Response->SetMimeType(TCHAR_TO_WCHAR(*MimeType));
	Response->SetStatus(200);
	Response->SetStatusText("OK");
	ResponseLength = Size;
}

bool FCEFInterfaceBrowserByteResource::ProcessRequest(CefRefPtr<CefRequest> Request, CefRefPtr<CefCallback> Callback)
{
	Callback->Continue();
	return true;
}

bool FCEFInterfaceBrowserByteResource::ReadResponse(void* DataOut, int BytesToRead, int& BytesRead, CefRefPtr<CefCallback> Callback)
{
	int32 BytesLeft = Size - Position;
	BytesRead = BytesLeft >= BytesToRead ? BytesToRead : BytesLeft;
	if (BytesRead > 0)
	{
		FMemory::Memcpy(DataOut, Buffer + Position, BytesRead);
		Position += BytesRead;
		return true;
	}
	return false;
}
#endif

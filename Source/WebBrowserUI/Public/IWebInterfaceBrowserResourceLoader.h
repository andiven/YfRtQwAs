// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

typedef TMap<FString, FString> FInterfaceContextRequestHeaders;
DECLARE_DELEGATE_FourParams(FOnBeforeInterfaceContextResourceLoadDelegate, FString /*Url*/, FString /*ResourceType*/, FInterfaceContextRequestHeaders& /*AdditionalHeaders*/, const bool /*AllowUserCredentials*/);

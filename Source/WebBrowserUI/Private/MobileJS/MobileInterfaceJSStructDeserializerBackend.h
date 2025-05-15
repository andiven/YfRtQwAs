// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_ANDROID  || PLATFORM_IOS

#include "MobileInterfaceJSScripting.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "Serialization/MemoryReader.h"

class FMobileInterfaceJSStructDeserializerBackend
	: public FJsonStructDeserializerBackend
{
public:
	FMobileInterfaceJSStructDeserializerBackend(FMobileInterfaceJSScriptingRef InScripting, const FString& JsonString);

	virtual bool ReadProperty( FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex ) override;

private:
	FMobileInterfaceJSScriptingRef Scripting;
	TArray<uint8> JsonData;
	FMemoryReader Reader;
};

#endif // USE_ANDROID_JNI || PLATFORM_IOS
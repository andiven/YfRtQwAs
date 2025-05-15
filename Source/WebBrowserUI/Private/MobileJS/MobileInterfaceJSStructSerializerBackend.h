// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_ANDROID || PLATFORM_IOS

#include "MobileInterfaceJSScripting.h"
#include "Backends/JsonStructSerializerBackend.h"

class UObject;

/**
 * Implements a writer for UStruct serialization using JavaScript.
 *
 * Based on FJsonStructSerializerBackend, it adds support for certain object types not representable in pure JSON
 *
 */
class FMobileInterfaceJSStructSerializerBackend
	: public FJsonStructSerializerBackend
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InScripting An instance of a web browser scripting obnject.
	 */
	FMobileInterfaceJSStructSerializerBackend(FMobileInterfaceJSScriptingRef InScripting);

public:
	virtual void WriteProperty(const FStructSerializerState& State, int32 ArrayIndex = 0) override;

	FString ToString();

private:
	void WriteUObject(const FStructSerializerState& State, UObject* Value);

	FMobileInterfaceJSScriptingRef Scripting;
	TArray<uint8> ReturnBuffer;
	FMemoryWriter Writer;
};

#endif // PLATFORM_ANDROID || PLATFORM_IOS
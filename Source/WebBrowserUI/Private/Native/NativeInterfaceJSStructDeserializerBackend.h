// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NativeInterfaceJSScripting.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "Serialization/MemoryReader.h"

class FNativeInterfaceJSStructDeserializerBackend
	: public FJsonStructDeserializerBackend
{
public:
	FNativeInterfaceJSStructDeserializerBackend(FNativeInterfaceJSScriptingRef InScripting, FMemoryReader& Reader);

#if UE_VERSION >= 425
	virtual bool ReadProperty( FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex ) override;
#else
	virtual bool ReadProperty( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex ) override;
#endif

private:
	FNativeInterfaceJSScriptingRef Scripting;

};

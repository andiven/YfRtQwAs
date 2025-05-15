// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "MobileInterfaceJSStructSerializerBackend.h"

#if PLATFORM_ANDROID || PLATFORM_IOS

#include "MobileInterfaceJSScripting.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Templates/Casts.h"

void FMobileInterfaceJSStructSerializerBackend::WriteProperty(const FStructSerializerState& State, int32 ArrayIndex)
{
	// The parent class serialzes UObjects as NULLs
	if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(State.ValueProperty))
	{
		WriteUObject(State, ObjectProperty->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
	// basic property type (json serializable)
	else
	{
		FJsonStructSerializerBackend::WriteProperty(State, ArrayIndex);
	}
}

void FMobileInterfaceJSStructSerializerBackend::WriteUObject(const FStructSerializerState& State, UObject* Value)
{
	// Note this function uses WriteRawJSONValue to append non-json data to the output stream.
	FString RawValue = Scripting->ConvertObject(Value);
	if ((State.ValueProperty == nullptr) || (State.ValueProperty->ArrayDim > 1) || (State.ValueProperty->GetOwner<FArrayProperty>() != nullptr))
	{
		GetWriter()->WriteRawJSONValue(RawValue);
	}
	else if (State.KeyProperty != nullptr)
	{
		FString KeyString;
#if UE_VERSION >= 501
		State.KeyProperty->ExportTextItem_Direct(KeyString, State.KeyData, nullptr, nullptr, PPF_None);
#else
		State.KeyProperty->ExportTextItem(KeyString, State.KeyData, nullptr, nullptr, PPF_None);
#endif
		GetWriter()->WriteRawJSONValue(KeyString, RawValue);
	}
	else
	{
		GetWriter()->WriteRawJSONValue(Scripting->GetBindingName(State.ValueProperty), RawValue);
	}
}

FString FMobileInterfaceJSStructSerializerBackend::ToString()
{
	ReturnBuffer.Add(0);
	ReturnBuffer.Add(0); // Add two as we're dealing with UTF-16, so 2 bytes
	return UTF16_TO_TCHAR((UTF16CHAR*)ReturnBuffer.GetData());
}

FMobileInterfaceJSStructSerializerBackend::FMobileInterfaceJSStructSerializerBackend(TSharedRef<class FMobileInterfaceJSScripting> InScripting)
	: FJsonStructSerializerBackend(Writer, EStructSerializerBackendFlags::Legacy)
	, Scripting(InScripting)
	, ReturnBuffer()
	, Writer(ReturnBuffer)
{
}

#endif // PLATFORM_ANDROID || PLATFORM_IOS
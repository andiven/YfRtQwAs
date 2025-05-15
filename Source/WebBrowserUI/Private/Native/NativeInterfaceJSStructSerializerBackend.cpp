// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "Native/NativeInterfaceJSStructSerializerBackend.h"

#include "NativeInterfaceJSScripting.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Templates/Casts.h"

void FNativeInterfaceJSStructSerializerBackend::WriteProperty(const FStructSerializerState& State, int32 ArrayIndex)
{
	// The parent class serialzes UObjects as NULLs
#if UE_VERSION >= 425
	if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(State.ValueProperty))
	{
		WriteUObject(State, CastFieldChecked<FObjectProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
#else
	if (UObjectProperty* ObjectProperty = Cast<UObjectProperty>(State.ValueProperty))
	{
		WriteUObject(State, CastChecked<UObjectProperty>(State.ValueProperty)->GetPropertyValue_InContainer(State.ValueData, ArrayIndex));
	}
#endif
	// basic property type (json serializable)
	else
	{
		FJsonStructSerializerBackend::WriteProperty(State, ArrayIndex);
	}
}

void FNativeInterfaceJSStructSerializerBackend::WriteUObject(const FStructSerializerState& State, UObject* Value)
{
	// Note this function uses WriteRawJSONValue to append non-json data to the output stream.
	FString RawValue = Scripting->ConvertObject(Value);
	if ((State.ValueProperty == nullptr) || (State.ValueProperty->ArrayDim > 1)
#if UE_VERSION >= 425
		|| State.ValueProperty->GetOwner< FArrayProperty>()
#else
		|| (State.ValueProperty->GetOuter()->GetClass() == UArrayProperty::StaticClass())
#endif
		)
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

FNativeInterfaceJSStructSerializerBackend::FNativeInterfaceJSStructSerializerBackend(TSharedRef<class FNativeInterfaceJSScripting> InScripting, FMemoryWriter& Writer)
	: FJsonStructSerializerBackend(Writer, EStructSerializerBackendFlags::Default)
	, Scripting(InScripting)
{
}
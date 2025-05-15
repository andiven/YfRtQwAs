// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "NativeInterfaceJSStructDeserializerBackend.h"
#include "NativeInterfaceJSScripting.h"
#include "UObject/UnrealType.h"
#include "Templates/Casts.h"

namespace NativeInterfaceFuncs
{
	// @todo: this function is copied from CEFJSStructDeserializerBackend.cpp. Move shared utility code to a common header file
	/**
	 * Sets the value of the given property.
	 *
	 * @param Property The property to set.
	 * @param Outer The property that contains the property to be set, if any.
	 * @param Data A pointer to the memory holding the property's data.
	 * @param ArrayIndex The index of the element to set (if the property is an array).
	 * @return true on success, false otherwise.
	 * @see ClearPropertyValue
	 */
#if UE_VERSION >= 425
	template<typename FPropertyType, typename PropertyType>
	bool SetPropertyValue( FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex, const PropertyType& Value )
#else
	template<typename UPropertyType, typename PropertyType>
	bool SetPropertyValue( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, const PropertyType& Value )
#endif
	{
		PropertyType* ValuePtr = nullptr;

#if UE_VERSION >= 425
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Outer);
#else
		UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Outer);
#endif

		if (ArrayProperty != nullptr)
		{
			if (ArrayProperty->Inner != Property)
			{
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->template ContainerPtrToValuePtr<void>(Data));
			int32 Index = ArrayHelper.AddValue();

			ValuePtr = (PropertyType*)ArrayHelper.GetRawPtr(Index);
		}
		else
		{
#if UE_VERSION >= 425
			FPropertyType* TypedProperty = CastField<FPropertyType>(Property);
#else
			UPropertyType* TypedProperty = Cast<UPropertyType>(Property);
#endif

			if (TypedProperty == nullptr || ArrayIndex >= TypedProperty->ArrayDim)
			{
				return false;
			}

			ValuePtr = TypedProperty->template ContainerPtrToValuePtr<PropertyType>(Data, ArrayIndex);
		}

		if (ValuePtr == nullptr)
		{
			return false;
		}

		*ValuePtr = Value;

		return true;
	}
}


#if UE_VERSION >= 425
bool FNativeInterfaceJSStructDeserializerBackend::ReadProperty( FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex )
#else
bool FNativeInterfaceJSStructDeserializerBackend::ReadProperty( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex )
#endif
{
	switch (GetLastNotation())
	{
		case EJsonNotation::String:
		{
#if UE_VERSION >= 425
			if (Property->IsA<FStructProperty>())
#else
			if (Property->IsA<UStructProperty>())
#endif
			{
#if UE_VERSION >= 425
				FStructProperty* StructProperty = CastField<FStructProperty>(Property);
#else
				UStructProperty* StructProperty = Cast<UStructProperty>(Property);
#endif

				if ( StructProperty->Struct == FWebInterfaceJSFunction::StaticStruct())
				{

					FGuid CallbackID;
					if (!FGuid::Parse(GetReader()->GetValueAsString(), CallbackID))
					{
						return false;
					}

					FWebInterfaceJSFunction CallbackObject(Scripting, CallbackID);
#if UE_VERSION >= 425
					return NativeInterfaceFuncs::SetPropertyValue<FStructProperty, FWebInterfaceJSFunction>(Property, Outer, Data, ArrayIndex, CallbackObject);
#else
					return NativeInterfaceFuncs::SetPropertyValue<UStructProperty, FWebInterfaceJSFunction>(Property, Outer, Data, ArrayIndex, CallbackObject);
#endif
				}
			}
		}
		break;
	}

	// If we reach this, default to parent class behavior
	return FJsonStructDeserializerBackend::ReadProperty(Property, Outer, Data, ArrayIndex);
}

FNativeInterfaceJSStructDeserializerBackend::FNativeInterfaceJSStructDeserializerBackend(FNativeInterfaceJSScriptingRef InScripting, FMemoryReader& Reader)
	: FJsonStructDeserializerBackend(Reader)
	, Scripting(InScripting)
{
}

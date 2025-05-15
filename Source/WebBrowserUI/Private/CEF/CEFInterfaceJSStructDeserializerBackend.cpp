// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "CEF/CEFInterfaceJSStructDeserializerBackend.h"
#if WITH_CEF3
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "WebInterfaceJSFunction.h"

/* Internal helpers
 *****************************************************************************/
namespace {

	template<typename ValueType, typename ContainerType, typename KeyType>
	ValueType GetNumeric(CefRefPtr<ContainerType> Container, KeyType Key)
	{
		switch(Container->GetType(Key))
		{
			case VTYPE_BOOL:
				return static_cast<ValueType>(Container->GetBool(Key));
			case VTYPE_INT:
				return static_cast<ValueType>(Container->GetInt(Key));
			case VTYPE_DOUBLE:
				return static_cast<ValueType>(Container->GetDouble(Key));
			case VTYPE_STRING:
			case VTYPE_DICTIONARY:
			case VTYPE_LIST:
			case VTYPE_NULL:
			case VTYPE_BINARY:
			default:
				return static_cast<ValueType>(0);
		}
	}

	template<typename ContainerType, typename KeyType>
	void AssignTokenFromContainer(ContainerType Container, KeyType Key,  EStructDeserializerBackendTokens& OutToken, FString& PropertyName, TSharedPtr<ICefInterfaceContainerWalker>& Retval)
	{
		switch (Container->GetType(Key))
		{
			case VTYPE_NULL:
			case VTYPE_BOOL:
			case VTYPE_INT:
			case VTYPE_DOUBLE:
			case VTYPE_STRING:
				OutToken = EStructDeserializerBackendTokens::Property;
				break;
			case VTYPE_DICTIONARY:
			{
				CefRefPtr<CefDictionaryValue> Dictionary = Container->GetDictionary(Key);
				if (Dictionary->GetType("$type") == VTYPE_STRING )
				{
					OutToken = EStructDeserializerBackendTokens::Property;
				}
				else
				{
					TSharedPtr<ICefInterfaceContainerWalker> NewWalker(new FCefInterfaceDictionaryValueWalker(Retval, Dictionary));
					Retval = NewWalker->GetNextToken(OutToken, PropertyName);
				}
				break;
			}
			case VTYPE_LIST:
			{
				TSharedPtr<ICefInterfaceContainerWalker> NewWalker(new FCefInterfaceListValueWalker(Retval, Container->GetList(Key)));
				Retval = NewWalker->GetNextToken(OutToken, PropertyName);
				break;
			}
			case VTYPE_BINARY:
			case VTYPE_INVALID:
			default:
				OutToken = EStructDeserializerBackendTokens::Error;
				break;
		}
	}

	/**
	 * Gets a pointer to object of the given property.
	 *
	 * @param Property The property to get.
	 * @param Outer The property that contains the property to be get, if any.
	 * @param Data A pointer to the memory holding the property's data.
	 * @param ArrayIndex The index of the element to set (if the property is an array).
	 * @return A pointer to the object represented by the property, null otherwise..
	 * @see ClearPropertyValue
	 */
#if UE_VERSION >= 425
	void* GetPropertyValuePtr( FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex )
#else
	void* GetPropertyValuePtr( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex )
#endif
	{
		check(Property);

#if UE_VERSION >= 425
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Outer))
#else
		if (UArrayProperty* ArrayProperty = Cast<UArrayProperty>(Outer))
#endif
		{
			if (ArrayProperty->Inner != Property)
			{
				return nullptr;
			}

			FScriptArrayHelper ArrayHelper(ArrayProperty, ArrayProperty->template ContainerPtrToValuePtr<void>(Data));
			int32 Index = ArrayHelper.AddValue();

			return ArrayHelper.GetRawPtr(Index);
		}

		if (ArrayIndex >= Property->ArrayDim)
		{
			return nullptr;
		}

		return Property->template ContainerPtrToValuePtr<void>(Data, ArrayIndex);
	}

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
	template<typename PropertyType, typename ValueType>
#if UE_VERSION >= 425
	bool SetPropertyValue( PropertyType* Property, FProperty* Outer, void* Data, int32 ArrayIndex, const ValueType& Value )
#else
	bool SetPropertyValue( PropertyType* Property, UProperty* Outer, void* Data, int32 ArrayIndex, const ValueType& Value )
#endif
	{
		if (void* Ptr = GetPropertyValuePtr(Property, Outer, Data, ArrayIndex))
		{
			*(ValueType*)Ptr = Value;
			return true;
		}

		return false;
	}

	template<typename PropertyType, typename ContainerType, typename KeyType>
#if UE_VERSION >= 425
	bool ReadNumericProperty(FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#else
	bool ReadNumericProperty(UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#endif
	{
		typedef typename PropertyType::TCppType TCppType;
#if UE_VERSION >= 425
		if (PropertyType* TypedProperty = CastField<PropertyType>(Property))
#else
		if (PropertyType* TypedProperty = Cast<PropertyType>(Property))
#endif
		{
			return SetPropertyValue(TypedProperty, Outer, Data, ArrayIndex, GetNumeric<TCppType>(Container, Key));
		}
		else
		{
			return false;
		}
	}

	template<typename ContainerType, typename KeyType>
#if UE_VERSION >= 425
	bool ReadBoolProperty(FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#else
	bool ReadBoolProperty(UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#endif
	{
#if UE_VERSION >= 425
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
#else
		if (UBoolProperty* BoolProperty = Cast<UBoolProperty>(Property))
#endif
		{
			return SetPropertyValue(BoolProperty, Outer, Data, ArrayIndex, GetNumeric<int>(Container, Key)!=0);
		}
		return false;

	}

	template<typename ContainerType, typename KeyType>
#if UE_VERSION >= 425
	bool ReadJSFunctionProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#else
	bool ReadJSFunctionProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#endif
	{
#if UE_VERSION >= 425
		if (Container->GetType(Key) != VTYPE_DICTIONARY || !Property->IsA<FStructProperty>())
#else
		if (Container->GetType(Key) != VTYPE_DICTIONARY || !Property->IsA<UStructProperty>())
#endif
		{
			return false;
		}
		CefRefPtr<CefDictionaryValue> Dictionary = Container->GetDictionary(Key);

#if UE_VERSION >= 425
		FStructProperty* StructProperty = CastField<FStructProperty>(Property);
#else
		UStructProperty* StructProperty = Cast<UStructProperty>(Property);
#endif

		if ( !StructProperty || StructProperty->Struct != FWebInterfaceJSFunction::StaticStruct())
		{
			return false;
		}

		FGuid CallbackID;
		if (!FGuid::Parse(FString(WCHAR_TO_TCHAR(Dictionary->GetString("$id").ToWString().c_str())), CallbackID))
		{
			// Invalid GUID
			return false;
		}
		FWebInterfaceJSFunction CallbackObject(Scripting, CallbackID);
		return SetPropertyValue(StructProperty, Outer, Data, ArrayIndex, CallbackObject);
	}

	template<typename ContainerType, typename KeyType>
#if UE_VERSION >= 425
	bool ReadStringProperty(FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#else
	bool ReadStringProperty(UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
#endif
	{
		if (Container->GetType(Key) == VTYPE_STRING)
		{
			FString StringValue = WCHAR_TO_TCHAR(Container->GetString(Key).ToWString().c_str());

#if UE_VERSION >= 425
			if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
#else
			if (UStrProperty* StrProperty = Cast<UStrProperty>(Property))
#endif
			{
				return SetPropertyValue(StrProperty, Outer, Data, ArrayIndex, StringValue);
			}

#if UE_VERSION >= 425
			if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
#else
			if (UNameProperty* NameProperty = Cast<UNameProperty>(Property))
#endif
			{
				return SetPropertyValue(NameProperty, Outer, Data, ArrayIndex, FName(*StringValue));
			}

#if UE_VERSION >= 425
			if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
#else
			if (UTextProperty* TextProperty = Cast<UTextProperty>(Property))
#endif
			{
				return SetPropertyValue(TextProperty, Outer, Data, ArrayIndex, FText::FromString(StringValue));
			}

#if UE_VERSION >= 425
			if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
#else
			if (UByteProperty* ByteProperty = Cast<UByteProperty>(Property))
#endif
			{
				if (!ByteProperty->Enum)
				{
					return false;
				}

				int32 Index = ByteProperty->Enum->GetIndexByNameString(StringValue);
				if (Index == INDEX_NONE)
				{
					return false;
				}

				return SetPropertyValue(ByteProperty, Outer, Data, ArrayIndex, (uint8)ByteProperty->Enum->GetValueByIndex(Index));
			}

#if UE_VERSION >= 425
			if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
#else
			if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
#endif
			{
				int32 Index = EnumProperty->GetEnum()->GetIndexByNameString(StringValue);
				if (Index == INDEX_NONE)
				{
					return false;
				}

				if (void* ElementPtr = GetPropertyValuePtr(EnumProperty, Outer, Data, ArrayIndex))
				{
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ElementPtr, EnumProperty->GetEnum()->GetValueByIndex(Index));
					return true;
				}

				return false;
			}
		}

		return false;
	}

	template<typename ContainerType, typename KeyType>
#if UE_VERSION >= 425
	bool ReadProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
	{
		return ReadBoolProperty(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadStringProperty(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FByteProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FInt8Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FInt16Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FIntProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FInt64Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FUInt16Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FUInt32Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FUInt64Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FFloatProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<FDoubleProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadJSFunctionProperty(Scripting, Property, Outer, Data, ArrayIndex, Container, Key);
	}
#else
	bool ReadProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex, CefRefPtr<ContainerType> Container, KeyType Key )
	{
		return ReadBoolProperty(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadStringProperty(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UByteProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UInt8Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UInt16Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UIntProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UInt64Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UUInt16Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UUInt32Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UUInt64Property>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UFloatProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadNumericProperty<UDoubleProperty>(Property, Outer, Data, ArrayIndex, Container, Key)
			|| ReadJSFunctionProperty(Scripting, Property, Outer, Data, ArrayIndex, Container, Key);
	}
#endif
}

TSharedPtr<ICefInterfaceContainerWalker> FCefInterfaceListValueWalker::GetNextToken(EStructDeserializerBackendTokens& OutToken, FString& PropertyName)
{
	TSharedPtr<ICefInterfaceContainerWalker> Retval = SharedThis(this);
	Index++;
	if (Index == -1)
	{
		OutToken = EStructDeserializerBackendTokens::ArrayStart;
	}
	else if ( Index < List->GetSize() )
	{
		AssignTokenFromContainer(List, Index, OutToken, PropertyName, Retval);
		PropertyName = FString();
	}
	else
	{
		OutToken = EStructDeserializerBackendTokens::ArrayEnd;
		Retval = Parent;
	}
	return Retval;
}

#if UE_VERSION >= 425
bool FCefInterfaceListValueWalker::ReadProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex)
#else
bool FCefInterfaceListValueWalker::ReadProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex)
#endif
{
	return ::ReadProperty(Scripting, Property, Outer, Data, ArrayIndex, List, Index);
}

TSharedPtr<ICefInterfaceContainerWalker> FCefInterfaceDictionaryValueWalker::GetNextToken(EStructDeserializerBackendTokens& OutToken, FString& PropertyName)
{
	TSharedPtr<ICefInterfaceContainerWalker> Retval = SharedThis(this);
	Index++;
	if (Index == -1)
	{
		OutToken = EStructDeserializerBackendTokens::StructureStart;
	}
	else if ( Index < Keys.size() )
	{
		AssignTokenFromContainer(Dictionary, Keys[Index], OutToken, PropertyName, Retval);
		PropertyName = WCHAR_TO_TCHAR(Keys[Index].ToWString().c_str());
	}
	else
	{
		OutToken = EStructDeserializerBackendTokens::StructureEnd;
		Retval = Parent;
	}
	return Retval;
}

#if UE_VERSION >= 425
bool FCefInterfaceDictionaryValueWalker::ReadProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex)
#else
bool FCefInterfaceDictionaryValueWalker::ReadProperty(TSharedPtr<FCEFInterfaceJSScripting> Scripting, UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex)
#endif
{
	return ::ReadProperty(Scripting, Property, Outer, Data, ArrayIndex, Dictionary, Keys[Index]);
}


/* IStructDeserializerBackend interface
 *****************************************************************************/



const FString& FCEFInterfaceJSStructDeserializerBackend::GetCurrentPropertyName() const
{
	return CurrentPropertyName;
}


FString FCEFInterfaceJSStructDeserializerBackend::GetDebugString() const
{
	return CurrentPropertyName;
}


const FString& FCEFInterfaceJSStructDeserializerBackend::GetLastErrorMessage() const
{
	return CurrentPropertyName;
}


bool FCEFInterfaceJSStructDeserializerBackend::GetNextToken( EStructDeserializerBackendTokens& OutToken )
{
	if (Walker.IsValid())
	{
		Walker = Walker->GetNextToken(OutToken, CurrentPropertyName);
		return true;
	}
	else
	{
		return false;
	}
}


#if UE_VERSION >= 425
bool FCEFInterfaceJSStructDeserializerBackend::ReadProperty( FProperty* Property, FProperty* Outer, void* Data, int32 ArrayIndex )
#else
bool FCEFInterfaceJSStructDeserializerBackend::ReadProperty( UProperty* Property, UProperty* Outer, void* Data, int32 ArrayIndex )
#endif
{
	return Walker->ReadProperty(Scripting, Property, Outer, Data, ArrayIndex);
}


void FCEFInterfaceJSStructDeserializerBackend::SkipArray()
{
	EStructDeserializerBackendTokens Token;
	int32 depth = 1;
	while (GetNextToken(Token) && depth > 0)
	{
		switch (Token)
		{
		case EStructDeserializerBackendTokens::ArrayEnd:
			depth --;
			break;
		case EStructDeserializerBackendTokens::ArrayStart:
			depth ++;
			break;
		default:
			break;
		}
	}
}

void FCEFInterfaceJSStructDeserializerBackend::SkipStructure()
{
	EStructDeserializerBackendTokens Token;
	int32 depth = 1;
	while (GetNextToken(Token) && depth > 0)
	{
		switch (Token)
		{
		case EStructDeserializerBackendTokens::StructureEnd:
			depth --;
			break;
		case EStructDeserializerBackendTokens::StructureStart:
			depth ++;
			break;
		default:
			break;
		}
	}
}

#endif

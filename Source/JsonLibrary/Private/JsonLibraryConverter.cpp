// Copyright 2024 Tracer Interactive, LLC. All Rights Reserved.
#include "JsonLibraryConverter.h"
#include "Internationalization/Culture.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/Package.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "JsonObjectWrapper.h"
#if UE_VERSION >= 505
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif

#define LOCTEXT_NAMESPACE "JsonLibraryConverter"

FString FJsonLibraryConverter::StandardizeCase(const FString &StringIn)
{
	// this probably won't work for all cases, consider downcasing the string fully
	FString FixedString = StringIn;
	FixedString[0] = FChar::ToLower(FixedString[0]); // our json classes/variable start lower case
	FixedString.ReplaceInline(TEXT("ID"), TEXT("Id"), ESearchCase::CaseSensitive); // Id is standard instead of ID, some of our fnames use ID
	return FixedString;
}

// Engine/Source/Runtime/JsonUtilities/Private/JsonObjectConverter.cpp
namespace
{
	const FString ObjectClassNameKey = "_ClassName";

	const FName NAME_DateTime(TEXT("DateTime"));

/** Convert property to JSON, assuming either the property is not an array or the value is an individual array element */
#if UE_VERSION >= 425
TSharedPtr<FJsonValue> ConvertScalarFPropertyToJsonValue(FProperty* Property, const void* Value, int64 CheckFlags, int64 SkipFlags, const FJsonLibraryConverter::CustomExportCallback* ExportCb, FProperty* OuterProperty, EJsonLibraryConversionFlags ConversionFlags)
#else
TSharedPtr<FJsonValue> ConvertScalarUPropertyToJsonValue(UProperty* Property, const void* Value, int64 CheckFlags, int64 SkipFlags, const FJsonLibraryConverter::CustomExportCallback* ExportCb, UProperty* OuterProperty, EJsonLibraryConversionFlags ConversionFlags)
#endif
{
	// See if there's a custom export callback first, so it can override default behavior
	if (ExportCb && ExportCb->IsBound())
	{
		TSharedPtr<FJsonValue> CustomValue = ExportCb->Execute(Property, Value);
		if (CustomValue.IsValid())
		{
			return CustomValue;
		}
		// fall through to default cases
	}

#if UE_VERSION >= 425
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
#else
	if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
#endif
	{
		// export enums as strings
		UEnum* EnumDef = EnumProperty->GetEnum();
		FString StringValue = EnumDef->GetAuthoredNameStringByValue(EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
		return MakeShared<FJsonValueString>(StringValue);
	}
#if UE_VERSION >= 425
	else if (FNumericProperty *NumericProperty = CastField<FNumericProperty>(Property))
#else
	else if (UNumericProperty *NumericProperty = Cast<UNumericProperty>(Property))
#endif
	{
		// see if it's an enum
		UEnum* EnumDef = NumericProperty->GetIntPropertyEnum();
		if (EnumDef != NULL)
		{
			// export enums as strings
			FString StringValue = EnumDef->GetAuthoredNameStringByValue(NumericProperty->GetSignedIntPropertyValue(Value));
			return MakeShared<FJsonValueString>(StringValue);
		}

		// We want to export numbers as numbers
		if (NumericProperty->IsFloatingPoint())
		{
			return MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(Value));
		}
		else if (NumericProperty->IsInteger())
		{
			return MakeShared<FJsonValueNumber>(NumericProperty->GetSignedIntPropertyValue(Value));
		}

		// fall through to default
	}
#if UE_VERSION >= 425
	else if (FBoolProperty *BoolProperty = CastField<FBoolProperty>(Property))
#else
	else if (UBoolProperty *BoolProperty = Cast<UBoolProperty>(Property))
#endif
	{
		// Export bools as bools
		return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(Value));
	}
#if UE_VERSION >= 425
	else if (FStrProperty *StringProperty = CastField<FStrProperty>(Property))
#else
	else if (UStrProperty *StringProperty = Cast<UStrProperty>(Property))
#endif
	{
		return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(Value));
	}
#if UE_VERSION >= 425
	else if (FTextProperty *TextProperty = CastField<FTextProperty>(Property))
#else
	else if (UTextProperty *TextProperty = Cast<UTextProperty>(Property))
#endif
	{
#if UE_VERSION >= 505
		if (EnumHasAnyFlags(ConversionFlags, EJsonLibraryConversionFlags::WriteTextAsComplexString))
		{
			FString TextValueString;
			FTextStringHelper::WriteToBuffer(TextValueString, TextProperty->GetPropertyValue(Value));

			return MakeShared<FJsonValueString>(TextValueString);
		}
#endif

		return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(Value).ToString());
	}
#if UE_VERSION >= 425
	else if (FArrayProperty *ArrayProperty = CastField<FArrayProperty>(Property))
#else
	else if (UArrayProperty *ArrayProperty = Cast<UArrayProperty>(Property))
#endif
	{
		TArray< TSharedPtr<FJsonValue> > Out;
		FScriptArrayHelper Helper(ArrayProperty, Value);
		for (int32 i=0, n=Helper.Num(); i<n; ++i)
		{
			TSharedPtr<FJsonValue> Elem = FJsonLibraryConverter::UPropertyToJsonValue(ArrayProperty->Inner, Helper.GetRawPtr(i), CheckFlags & ( ~CPF_ParmFlags ), SkipFlags, ExportCb, ArrayProperty);
			if ( Elem.IsValid() )
			{
				// add to the array
				Out.Push(Elem);
			}
		}
		return MakeShared<FJsonValueArray>(Out);
	}
#if UE_VERSION >= 425
	else if ( FSetProperty* SetProperty = CastField<FSetProperty>(Property) )
#else
	else if ( USetProperty* SetProperty = Cast<USetProperty>(Property) )
#endif
	{
		TArray< TSharedPtr<FJsonValue> > Out;
		FScriptSetHelper Helper(SetProperty, Value);
#if UE_VERSION >= 504
		for (FScriptSetHelper::FIterator It(Helper); It; ++It)
#else
		for (int32 i=0, n=Helper.Num(); n; ++i)
#endif
		{
#if UE_VERSION < 504
			if (Helper.IsValidIndex(i))
			{
#endif
				TSharedPtr<FJsonValue> Elem = FJsonLibraryConverter::UPropertyToJsonValue(
					SetProperty->ElementProp,
#if UE_VERSION >= 504
					Helper.GetElementPtr(It),
#else
					Helper.GetElementPtr(i),
#endif
					CheckFlags & (~CPF_ParmFlags),
					SkipFlags,
					ExportCb,
					SetProperty);

				if (Elem.IsValid())
				{
					// add to the array
					Out.Push(Elem);
				}

#if UE_VERSION < 504
				--n;
			}
#endif
		}
		return MakeShared<FJsonValueArray>(Out);
	}
#if UE_VERSION >= 425
	else if ( FMapProperty* MapProperty = CastField<FMapProperty>(Property) )
#else
	else if ( UMapProperty* MapProperty = Cast<UMapProperty>(Property) )
#endif
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

		FScriptMapHelper Helper(MapProperty, Value);
#if UE_VERSION >= 504
		for (FScriptMapHelper::FIterator It(Helper); It; ++It)
#else
		for (int32 i=0, n = Helper.Num(); n; ++i)
#endif
		{
#if UE_VERSION < 504
			if (Helper.IsValidIndex(i))
			{
#endif
				TSharedPtr<FJsonValue> KeyElement = FJsonLibraryConverter::UPropertyToJsonValue(
					MapProperty->KeyProp,
#if UE_VERSION >= 504
					Helper.GetKeyPtr(It),
#else
					Helper.GetKeyPtr(i),
#endif
					CheckFlags & (~CPF_ParmFlags),
					SkipFlags,
					ExportCb,
					MapProperty,
					ConversionFlags);
				TSharedPtr<FJsonValue> ValueElement = FJsonLibraryConverter::UPropertyToJsonValue(
					MapProperty->ValueProp,
#if UE_VERSION >= 504
					Helper.GetValuePtr(It),
#else
					Helper.GetValuePtr(i),
#endif
					CheckFlags & (~CPF_ParmFlags),
					SkipFlags,
					ExportCb,
					MapProperty,
					ConversionFlags);
				
				if (KeyElement.IsValid() && ValueElement.IsValid())
				{
					FString KeyString;
					if (!KeyElement->TryGetString(KeyString))
					{
#if UE_VERSION >= 504
						MapProperty->KeyProp->ExportTextItem_Direct(KeyString, Helper.GetKeyPtr(It), nullptr, nullptr, 0);
#elif UE_VERSION >= 501
						MapProperty->KeyProp->ExportTextItem_Direct(KeyString, Helper.GetKeyPtr(i), nullptr, nullptr, 0);
#else
						MapProperty->KeyProp->ExportTextItem(KeyString, Helper.GetKeyPtr(i), nullptr, nullptr, 0);
#endif
						if (KeyString.IsEmpty())
						{
							UE_LOG(LogJson, Error, TEXT("Unable to convert key to string for property %s."), *MapProperty->GetAuthoredName())
							KeyString = FString::Printf(TEXT("Unparsed Key %d"),
#if UE_VERSION >= 504
								It.GetLogicalIndex()
#else
								i
#endif
							);
						}
					}

					// Coerce camelCase map keys for Enum/FName properties
#if UE_VERSION >= 425
					if (CastField<FEnumProperty>(MapProperty->KeyProp) ||
						CastField<FNameProperty>(MapProperty->KeyProp))
#else
					if (Cast<UEnumProperty>(MapProperty->KeyProp) ||
						Cast<UNameProperty>(MapProperty->KeyProp))
#endif
					{
						if (!EnumHasAnyFlags(ConversionFlags, EJsonLibraryConversionFlags::SkipStandardizeCase))
						{
							KeyString = FJsonLibraryConverter::StandardizeCase(KeyString);
						}
					}
					Out->SetField(KeyString, ValueElement);
				}

#if UE_VERSION < 504
				--n;
			}
#endif
		}

		return MakeShared<FJsonValueObject>(Out);
	}
#if UE_VERSION >= 425
	else if (FStructProperty *StructProperty = CastField<FStructProperty>(Property))
#else
	else if (UStructProperty *StructProperty = Cast<UStructProperty>(Property))
#endif
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = StructProperty->Struct->GetCppStructOps();
		// Intentionally exclude the JSON Object wrapper, which specifically needs to export JSON in an object representation instead of a string
		if (StructProperty->Struct != FJsonObjectWrapper::StaticStruct() && TheCppStructOps && TheCppStructOps->HasExportTextItem())
		{
			FString OutValueStr;
			TheCppStructOps->ExportTextItem(OutValueStr, Value, nullptr, nullptr, PPF_None, nullptr);
			return MakeShared<FJsonValueString>(OutValueStr);
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (FJsonLibraryConverter::UStructToJsonObject(StructProperty->Struct, Value, Out, CheckFlags & (~CPF_ParmFlags), SkipFlags, ExportCb, ConversionFlags))
		{
			return MakeShared<FJsonValueObject>(Out);
		}
	}
#if UE_VERSION >= 425
	else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
#else
	else if (UObjectProperty* ObjectProperty = Cast<UObjectProperty>(Property))
#endif
	{
		// Instanced properties should be copied by value, while normal UObject* properties should output as asset references
		UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
		if (Object && (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance) || (OuterProperty && OuterProperty->HasAnyPropertyFlags(CPF_PersistentInstance))))
		{
			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

			Out->SetStringField(ObjectClassNameKey, Object->GetClass()->GetPathName());
			if (FJsonLibraryConverter::UStructToJsonObject(ObjectProperty->GetObjectPropertyValue(Value)->GetClass(), Object, Out, CheckFlags, SkipFlags, ExportCb))
			{
				TSharedRef<FJsonValueObject> JsonObject = MakeShared<FJsonValueObject>(Out);
				JsonObject->Type = EJson::Object;
				return JsonObject;
			}
		}
		else
		{
			FString StringValue;
#if UE_VERSION >= 501
			Property->ExportTextItem_Direct(StringValue, Value, nullptr, nullptr, PPF_None);
#else
			Property->ExportTextItem(StringValue, Value, nullptr, nullptr, PPF_None);
#endif
			return MakeShared<FJsonValueString>(StringValue);
		}
	}
	else
	{
		// Default to export as string for everything else
		FString StringValue;
#if UE_VERSION >= 501
		Property->ExportTextItem_Direct(StringValue, Value, NULL, NULL, PPF_None);
#else
		Property->ExportTextItem(StringValue, Value, NULL, NULL, PPF_None);
#endif
		return MakeShared<FJsonValueString>(StringValue);
	}

	// invalid
	return TSharedPtr<FJsonValue>();
}
}

#if UE_VERSION >= 425
TSharedPtr<FJsonValue> FJsonLibraryConverter::UPropertyToJsonValue(FProperty* Property, const void* Value, int64 CheckFlags, int64 SkipFlags, const CustomExportCallback* ExportCb, FProperty* OuterProperty, EJsonLibraryConversionFlags ConversionFlags)
#else
TSharedPtr<FJsonValue> FJsonLibraryConverter::UPropertyToJsonValue(UProperty* Property, const void* Value, int64 CheckFlags, int64 SkipFlags, const CustomExportCallback* ExportCb, UProperty* OuterProperty, EJsonLibraryConversionFlags ConversionFlags)
#endif
{
	if (Property->ArrayDim == 1)
	{
#if UE_VERSION >= 425
		return ConvertScalarFPropertyToJsonValue(Property, Value, CheckFlags, SkipFlags, ExportCb, OuterProperty, ConversionFlags);
#else
		return ConvertScalarUPropertyToJsonValue(Property, Value, CheckFlags, SkipFlags, ExportCb, OuterProperty, ConversionFlags);
#endif
	}

	TArray< TSharedPtr<FJsonValue> > Array;
	for (int Index = 0; Index != Property->ArrayDim; ++Index)
	{
#if UE_VERSION >= 505
		Array.Add(ConvertScalarFPropertyToJsonValue(Property, (char*)Value + Index * Property->GetElementSize(), CheckFlags, SkipFlags, ExportCb, OuterProperty, ConversionFlags));
#elif UE_VERSION >= 425
		Array.Add(ConvertScalarFPropertyToJsonValue(Property, (char*)Value + Index * Property->ElementSize, CheckFlags, SkipFlags, ExportCb, OuterProperty, ConversionFlags));
#else
		Array.Add(ConvertScalarUPropertyToJsonValue(Property, (char*)Value + Index * Property->ElementSize, CheckFlags, SkipFlags, ExportCb, OuterProperty, ConversionFlags));
#endif
	}
	return MakeShared<FJsonValueArray>(Array);
}

bool FJsonLibraryConverter::UStructToJsonObject(const UStruct* StructDefinition, const void* Struct, TSharedRef<FJsonObject> OutJsonObject, int64 CheckFlags, int64 SkipFlags, const CustomExportCallback* ExportCb, EJsonLibraryConversionFlags ConversionFlags)
{
	return UStructToJsonAttributes(StructDefinition, Struct, OutJsonObject->Values, CheckFlags, SkipFlags, ExportCb, ConversionFlags);
}

bool FJsonLibraryConverter::UStructToJsonAttributes(const UStruct* StructDefinition, const void* Struct, TMap< FString, TSharedPtr<FJsonValue> >& OutJsonAttributes, int64 CheckFlags, int64 SkipFlags, const CustomExportCallback* ExportCb, EJsonLibraryConversionFlags ConversionFlags)
{
	if (SkipFlags == 0)
	{
		// If we have no specified skip flags, skip deprecated, transient and skip serialization by default when writing
		SkipFlags |= CPF_Deprecated | CPF_Transient;
	}

// ---------- UUserDefinedStruct ----------
	bool bUserStruct = StructDefinition->IsA( UUserDefinedStruct::StaticClass() );
// ---------- UUserDefinedStruct ----------

	if (StructDefinition == FJsonObjectWrapper::StaticStruct())
	{
		// Just copy it into the object
		const FJsonObjectWrapper* ProxyObject = (const FJsonObjectWrapper *)Struct;

		if (ProxyObject->JsonObject.IsValid())
		{
			OutJsonAttributes = ProxyObject->JsonObject->Values;
		}
		return true;
	}

#if UE_VERSION >= 425
	for (TFieldIterator<FProperty> It(StructDefinition); It; ++It)
#else
	for (TFieldIterator<UProperty> It(StructDefinition); It; ++It)
#endif
	{
#if UE_VERSION >= 425
		FProperty* Property = *It;
#else
		UProperty* Property = *It;
#endif

		// Check to see if we should ignore this property
		if (CheckFlags != 0 && !Property->HasAnyPropertyFlags(CheckFlags))
		{
			continue;
		}
		if (Property->HasAnyPropertyFlags(SkipFlags))
		{
			continue;
		}

// ---------- UUserDefinedStruct::GetAuthoredNameForField() ----------
		FString PropertyName = Property->GetAuthoredName();
		if (bUserStruct)
		{
			const int32 GuidStrLen = 32;
			const int32 MinimalPostfixlen = GuidStrLen + 3;
			if (PropertyName.Len() > MinimalPostfixlen)
			{
				FString DisplayName = PropertyName.LeftChop(GuidStrLen + 1);
				int FirstCharToRemove = -1;
				const bool bCharFound = DisplayName.FindLastChar(TCHAR('_'), FirstCharToRemove);
				if (bCharFound && (FirstCharToRemove > 0))
				{
					PropertyName = DisplayName.Mid(0, FirstCharToRemove);
				}
			}
		}
// ---------- UUserDefinedStruct::GetAuthoredNameForField() ----------

		FString VariableName = PropertyName;
		if (!EnumHasAnyFlags(ConversionFlags, EJsonLibraryConversionFlags::SkipStandardizeCase))
		{
			VariableName = StandardizeCase(VariableName);
		}

		const void* Value = Property->ContainerPtrToValuePtr<uint8>(Struct);

		// convert the property to a FJsonValue
		TSharedPtr<FJsonValue> JsonValue = UPropertyToJsonValue(Property, Value, CheckFlags, SkipFlags, ExportCb, nullptr, ConversionFlags);
		if (!JsonValue.IsValid())
		{
#if UE_VERSION >= 425
			FFieldClass* PropClass = Property->GetClass();
#else
			UClass* PropClass = Property->GetClass();
#endif

			UE_LOG(LogJson, Error, TEXT("UStructToJsonObject - Unhandled property type '%s': %s"), *PropClass->GetName(), *Property->GetPathName());
			return false;
		}

		// set the value on the output object
		OutJsonAttributes.Add(VariableName, JsonValue);
	}

	return true;
}

template<class CharType, class PrintPolicy>
bool UStructToJsonObjectStringInternal(const TSharedRef<FJsonObject>& JsonObject, FString& OutJsonString, int32 Indent)
{
	TSharedRef<TJsonWriter<CharType, PrintPolicy> > JsonWriter = TJsonWriterFactory<CharType, PrintPolicy>::Create(&OutJsonString, Indent);
	bool bSuccess = FJsonSerializer::Serialize(JsonObject, JsonWriter);
	JsonWriter->Close();
	return bSuccess;
}

bool FJsonLibraryConverter::UStructToJsonObjectString(const UStruct* StructDefinition, const void* Struct, FString& OutJsonString, int64 CheckFlags, int64 SkipFlags, int32 Indent, const CustomExportCallback* ExportCb, bool bPrettyPrint)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	if (UStructToJsonObject(StructDefinition, Struct, JsonObject, CheckFlags, SkipFlags, ExportCb))
	{
		bool bSuccess = false;
		if (bPrettyPrint)
		{
			bSuccess = UStructToJsonObjectStringInternal<TCHAR, TPrettyJsonPrintPolicy<TCHAR> >(JsonObject, OutJsonString, Indent);
		}
		else
		{
			bSuccess = UStructToJsonObjectStringInternal<TCHAR, TCondensedJsonPrintPolicy<TCHAR> >(JsonObject, OutJsonString, Indent);
		}
		if (bSuccess)
		{
			return true;
		}
		else
		{
			UE_LOG(LogJson, Warning, TEXT("UStructToJsonObjectString - Unable to write out JSON"));
		}
	}

	return false;
}

//static
bool FJsonLibraryConverter::GetTextFromObject(const TSharedRef<FJsonObject>& Obj, FText& TextOut)
{
	// get the prioritized culture name list
	FCultureRef CurrentCulture = FInternationalization::Get().GetCurrentCulture();
	TArray<FString> CultureList = CurrentCulture->GetPrioritizedParentCultureNames();

	// try to follow the fall back chain that the engine uses
	FString TextString;
	for (const FString& CultureCode : CultureList)
	{
		if (Obj->TryGetStringField(CultureCode, TextString))
		{
			TextOut = FText::FromString(TextString);
			return true;
		}
	}

	// try again but only search on the locale region (in the localized data). This is a common omission (i.e. en-US source text should be used if no en is defined)
	for (const FString& LocaleToMatch : CultureList)
	{
		int32 SeparatorPos;
		// only consider base language entries in culture chain (i.e. "en")
		if (!LocaleToMatch.FindChar('-', SeparatorPos))
		{
			for (const auto& Pair : Obj->Values)
			{
				// only consider coupled entries now (base ones would have been matched on first path) (i.e. "en-US")
				if (Pair.Key.FindChar('-', SeparatorPos))
				{
					if (Pair.Key.StartsWith(LocaleToMatch))
					{
						TextOut = FText::FromString(Pair.Value->AsString());
						return true;
					}
				}
			}
		}
	}

	// no luck, is this possibly an unrelated json object?
	return false;
}


namespace
{
#if UE_VERSION >= 505
	bool JsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const FJsonLibraryConverter::CustomImportCallback* ImportCb);
#elif UE_VERSION >= 425
	bool JsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason);
#else
	bool JsonValueToUPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, UProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason);
#endif

#if UE_VERSION >= 505
	bool JsonAttributesToUStructWithContainer(const TMap< FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const FJsonLibraryConverter::CustomImportCallback* ImportCb);
#else
	bool JsonAttributesToUStructWithContainer(const TMap< FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason);
#endif

	/** Convert JSON to property, assuming either the property is not an array or the value is an individual array element */
#if UE_VERSION >= 505
	bool ConvertScalarJsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const FJsonLibraryConverter::CustomImportCallback* ImportCb)
#elif UE_VERSION >= 425
	bool ConvertScalarJsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#else
	bool ConvertScalarJsonValueToUPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, UProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#endif
	{
#if UE_VERSION >= 505
		if (ImportCb && ImportCb->IsBound())
		{
			if (ImportCb->Execute(JsonValue, Property, OutValue))
			{
				return true;
			}
			// fall through to default cases
		}
#endif

#if UE_VERSION >= 425
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
#else
		if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::String)
			{
				// see if we were passed a string for the enum
				const UEnum* Enum = EnumProperty->GetEnum();
				check(Enum);
				FString StrValue = JsonValue->AsString();
				int64 IntValue = Enum->GetValueByName(FName(*StrValue), EGetByNameFlags::CheckAuthoredName);
				if (IntValue == INDEX_NONE)
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import enum %s from string value %s for property %s"), *Enum->CppType, *StrValue, *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportEnumFromString", "Unable to import enum {0} from string value {1} for property {2}"), FText::FromString(Enum->CppType), FText::FromString(StrValue), FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
				EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, IntValue);
			}
			else
			{
				// AsNumber will log an error for completely inappropriate types (then give us a default)
				EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, (int64)JsonValue->AsNumber());
			}
		}
#if UE_VERSION >= 425
		else if (FNumericProperty *NumericProperty = CastField<FNumericProperty>(Property))
#else
		else if (UNumericProperty *NumericProperty = Cast<UNumericProperty>(Property))
#endif
		{
			if (NumericProperty->IsEnum() && JsonValue->Type == EJson::String)
			{
				// see if we were passed a string for the enum
				const UEnum* Enum = NumericProperty->GetIntPropertyEnum();
				check(Enum); // should be assured by IsEnum()
				FString StrValue = JsonValue->AsString();
				int64 IntValue = Enum->GetValueByName(FName(*StrValue), EGetByNameFlags::CheckAuthoredName);
				if (IntValue == INDEX_NONE)
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import enum %s from numeric value %s for property %s"), *Enum->CppType, *StrValue, *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportEnumFromNumeric", "Unable to import enum {0} from numeric value {1} for property {2}"), FText::FromString(Enum->CppType), FText::FromString(StrValue), FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
				NumericProperty->SetIntPropertyValue(OutValue, IntValue);
			}
			else if (NumericProperty->IsFloatingPoint())
			{
				// AsNumber will log an error for completely inappropriate types (then give us a default)
				NumericProperty->SetFloatingPointPropertyValue(OutValue, JsonValue->AsNumber());
			}
			else if (NumericProperty->IsInteger())
			{
				if (JsonValue->Type == EJson::String)
				{
					// parse string -> int64 ourselves so we don't lose any precision going through AsNumber (aka double)
					NumericProperty->SetIntPropertyValue(OutValue, FCString::Atoi64(*JsonValue->AsString()));
				}
				else
				{
					// AsNumber will log an error for completely inappropriate types (then give us a default)
					NumericProperty->SetIntPropertyValue(OutValue, (int64)JsonValue->AsNumber());
				}
			}
			else
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import json value into %s numeric property %s"), *Property->GetClass()->GetName(), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportNumericProperty", "Unable to import json value into {0} numeric property {1}"), FText::FromString(Property->GetClass()->GetName()), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
#if UE_VERSION >= 425
		else if (FBoolProperty *BoolProperty = CastField<FBoolProperty>(Property))
#else
		else if (UBoolProperty *BoolProperty = Cast<UBoolProperty>(Property))
#endif
		{
			// AsBool will log an error for completely inappropriate types (then give us a default)
			BoolProperty->SetPropertyValue(OutValue, JsonValue->AsBool());
		}
#if UE_VERSION >= 425
		else if (FStrProperty *StringProperty = CastField<FStrProperty>(Property))
#else
		else if (UStrProperty *StringProperty = Cast<UStrProperty>(Property))
#endif
		{
			// AsString will log an error for completely inappropriate types (then give us a default)
			StringProperty->SetPropertyValue(OutValue, JsonValue->AsString());
		}
#if UE_VERSION >= 425
		else if (FArrayProperty *ArrayProperty = CastField<FArrayProperty>(Property))
#else
		else if (UArrayProperty *ArrayProperty = Cast<UArrayProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::Array)
			{
				TArray< TSharedPtr<FJsonValue> > ArrayValue = JsonValue->AsArray();
				int32 ArrLen = ArrayValue.Num();

				// make the output array size match
				FScriptArrayHelper Helper(ArrayProperty, OutValue);
				Helper.Resize(ArrLen);

				// set the property values
				for (int32 i = 0; i < ArrLen; ++i)
				{
					const TSharedPtr<FJsonValue>& ArrayValueItem = ArrayValue[i];
					if (ArrayValueItem.IsValid() && !ArrayValueItem->IsNull())
					{
#if UE_VERSION >= 505
						if (!JsonValueToFPropertyWithContainer(ArrayValueItem, ArrayProperty->Inner, Helper.GetRawPtr(i), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason, ImportCb))
#elif UE_VERSION >= 425
						if (!JsonValueToFPropertyWithContainer(ArrayValueItem, ArrayProperty->Inner, Helper.GetRawPtr(i), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#else
						if (!JsonValueToUPropertyWithContainer(ArrayValueItem, ArrayProperty->Inner, Helper.GetRawPtr(i), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#endif
						{
							UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import Array element %d for property %s"), i, *Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(LOCTEXT("FailImportArrayElement", "Unable to import Array element {0} for property {1}\n{2}"), FText::AsNumber(i), FText::FromString(Property->GetAuthoredName()), *OutFailReason);
							}
							return false;
						}
					}
				}
			}
			else
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import non-array JSON value into Array property %s"), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportArray", "Unable to import non-array JSON value into Array property {0}"), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
#if UE_VERSION >= 425
		else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
#else
		else if (UMapProperty* MapProperty = Cast<UMapProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> ObjectValue = JsonValue->AsObject();

				FScriptMapHelper Helper(MapProperty, OutValue);

				check(ObjectValue);

				int32 MapSize = ObjectValue->Values.Num();
				Helper.EmptyValues(MapSize);

				// set the property values
				for (const auto& Entry : ObjectValue->Values)
				{
					if (Entry.Value.IsValid() && !Entry.Value->IsNull())
					{
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();

						TSharedPtr<FJsonValueString> TempKeyValue = MakeShared<FJsonValueString>(Entry.Key);
						
#if UE_VERSION >= 505
						if (!JsonValueToFPropertyWithContainer(TempKeyValue, MapProperty->KeyProp, Helper.GetKeyPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason, ImportCb))
#elif UE_VERSION >= 425
						if (!JsonValueToFPropertyWithContainer(TempKeyValue, MapProperty->KeyProp, Helper.GetKeyPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#else
						if (!JsonValueToUPropertyWithContainer(TempKeyValue, MapProperty->KeyProp, Helper.GetKeyPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#endif
						{
							UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import Map element %s key for property %s"), *Entry.Key, *Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(LOCTEXT("FailImportMapElementKey", "Unable to import Map element {0} key for property {1}\n{2}"), FText::FromString(Entry.Key), FText::FromString(Property->GetAuthoredName()), *OutFailReason);
							}
							return false;
						}
						
#if UE_VERSION >= 505
						if (!JsonValueToFPropertyWithContainer(Entry.Value, MapProperty->ValueProp, Helper.GetValuePtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason, ImportCb))
#elif UE_VERSION >= 425
						if (!JsonValueToFPropertyWithContainer(Entry.Value, MapProperty->ValueProp, Helper.GetValuePtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#else
						if (!JsonValueToUPropertyWithContainer(Entry.Value, MapProperty->ValueProp, Helper.GetValuePtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#endif
						{
							UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import Map element %s value for property %s"), *Entry.Key, *Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(LOCTEXT("FailImportMapElementValue", "Unable to import Map element {0} value for property {1}\n{2}"), FText::FromString(Entry.Key), FText::FromString(Property->GetAuthoredName()), *OutFailReason);
							}
							return false;
						}
					}
				}

				Helper.Rehash();
			}
			else
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import non-object JSON value into Map property %s"), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportMap", "Unable to import non-object JSON value into Map property {0}"), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
#if UE_VERSION >= 425
		else if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
#else
		else if (USetProperty* SetProperty = Cast<USetProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::Array)
			{
				TArray< TSharedPtr<FJsonValue> > ArrayValue = JsonValue->AsArray();
				int32 ArrLen = ArrayValue.Num();

				FScriptSetHelper Helper(SetProperty, OutValue);
				Helper.EmptyElements(ArrLen);

				// set the property values
				for (int32 i = 0; i < ArrLen; ++i)
				{
					const TSharedPtr<FJsonValue>& ArrayValueItem = ArrayValue[i];
					if (ArrayValueItem.IsValid() && !ArrayValueItem->IsNull())
					{
						int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
						
#if UE_VERSION >= 505
						if (!JsonValueToFPropertyWithContainer(ArrayValueItem, SetProperty->ElementProp, Helper.GetElementPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason, ImportCb))
#elif UE_VERSION >= 425
						if (!JsonValueToFPropertyWithContainer(ArrayValueItem, SetProperty->ElementProp, Helper.GetElementPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#else
						if (!JsonValueToUPropertyWithContainer(ArrayValueItem, SetProperty->ElementProp, Helper.GetElementPtr(NewIndex), ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#endif
						{
							UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import Set element %d for property %s"), i, *Property->GetAuthoredName());
							if (OutFailReason)
							{
								*OutFailReason = FText::Format(LOCTEXT("FailImportSetElement", "Unable to import Set element {0} for property {1}\n{2}"), FText::AsNumber(i), FText::FromString(Property->GetAuthoredName()), *OutFailReason);
							}
							return false;
						}
					}
				}

				Helper.Rehash();
			}
			else
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import non-array JSON value into Set property %s"), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportSet", "Unable to import non-array JSON value into Set property {0}"), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
#if UE_VERSION >= 425
		else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
#else
		else if (UTextProperty* TextProperty = Cast<UTextProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::String)
			{
				FString StringValue = JsonValue->AsString();
				FText TextValue;
#if UE_VERSION >= 505
				if (!FTextStringHelper::ReadFromBuffer(*StringValue, TextValue))
#endif
				{
					TextValue = FText::FromString(StringValue);
				}

				// assume this string is already localized, so import as invariant
				TextProperty->SetPropertyValue(OutValue, TextValue);
			}
			else if (JsonValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				check(Obj.IsValid()); // should not fail if Type == EJson::Object

				// import the subvalue as a culture invariant string
				FText Text;
				if (!FJsonLibraryConverter::GetTextFromObject(Obj.ToSharedRef(), Text))
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON object with invalid keys into Text property %s"), *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportTextFromObject", "Unable to import JSON object with invalid keys into Text property {0}"), FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
				TextProperty->SetPropertyValue(OutValue, Text);
			}
			else
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON value that is neither string nor object into Text property %s"), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportText", "Unable to import JSON value that is neither string nor object into Text property {0}"), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
#if UE_VERSION >= 425
		else if (FStructProperty *StructProperty = CastField<FStructProperty>(Property))
#else
		else if (UStructProperty *StructProperty = Cast<UStructProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				check(Obj.IsValid()); // should not fail if Type == EJson::Object
#if UE_VERSION >= 505
				if (!JsonAttributesToUStructWithContainer(Obj->Values, StructProperty->Struct, OutValue, ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason, ImportCb))
#else
				if (!JsonAttributesToUStructWithContainer(Obj->Values, StructProperty->Struct, OutValue, ContainerStruct, Container, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#endif
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON object into %s property %s"), *StructProperty->Struct->GetAuthoredName(), *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportStructFromObject", "Unable to import JSON object into {0} property {1}\n{2}"), FText::FromString(StructProperty->Struct->GetAuthoredName()), FText::FromString(Property->GetAuthoredName()), *OutFailReason);
					}
					return false;
				}
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_LinearColor)
			{
				FLinearColor& ColorOut = *(FLinearColor*)OutValue;
				FString ColorString = JsonValue->AsString();

				FColor IntermediateColor;
				IntermediateColor = FColor::FromHex(ColorString);

				ColorOut = IntermediateColor;
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_Color)
			{
				FColor& ColorOut = *(FColor*)OutValue;
				FString ColorString = JsonValue->AsString();

				ColorOut = FColor::FromHex(ColorString);
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetFName() == NAME_DateTime)
			{
				FString DateString = JsonValue->AsString();
				FDateTime& DateTimeOut = *(FDateTime*)OutValue;
				if (DateString == TEXT("min"))
				{
					// min representable value for our date struct. Actual date may vary by platform (this is used for sorting)
					DateTimeOut = FDateTime::MinValue();
				}
				else if (DateString == TEXT("max"))
				{
					// max representable value for our date struct. Actual date may vary by platform (this is used for sorting)
					DateTimeOut = FDateTime::MaxValue();
				}
				else if (DateString == TEXT("now"))
				{
					// this value's not really meaningful from json serialization (since we don't know timezone) but handle it anyway since we're handling the other keywords
					DateTimeOut = FDateTime::UtcNow();
				}
				else if (FDateTime::ParseIso8601(*DateString, DateTimeOut))
				{
					// ok
				}
				else if (FDateTime::Parse(DateString, DateTimeOut))
				{
					// ok
				}
				else
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON string into DateTime property %s"), *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportDateTimeFromString", "Unable to import JSON string into DateTime property {0}"), FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
			}
			else if (JsonValue->Type == EJson::String && StructProperty->Struct->GetCppStructOps() && StructProperty->Struct->GetCppStructOps()->HasImportTextItem())
			{
				UScriptStruct::ICppStructOps* TheCppStructOps = StructProperty->Struct->GetCppStructOps();

				FString ImportTextString = JsonValue->AsString();
				const TCHAR* ImportTextPtr = *ImportTextString;
				if (!TheCppStructOps->ImportTextItem(ImportTextPtr, OutValue, PPF_None, nullptr, (FOutputDevice*)GWarn))
				{
					// Fall back to trying the tagged property approach if custom ImportTextItem couldn't get it done
#if UE_VERSION >= 501
					if (Property->ImportText_Direct(ImportTextPtr, OutValue, nullptr, PPF_None) == nullptr)
#else
					if (Property->ImportText(ImportTextPtr, OutValue, PPF_None, nullptr) == nullptr)
#endif
					{
						UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"), *StructProperty->Struct->GetAuthoredName(), *Property->GetAuthoredName());
						if (OutFailReason)
						{
							*OutFailReason = FText::Format(LOCTEXT("FailImportStructFromString", "Unable to import JSON string into {0} property {1}"), FText::FromString(StructProperty->Struct->GetAuthoredName()), FText::FromString(Property->GetAuthoredName()));
						}
						return false;
					}
				}
			}
			else if (JsonValue->Type == EJson::String)
			{
				FString ImportTextString = JsonValue->AsString();
				const TCHAR* ImportTextPtr = *ImportTextString;
#if UE_VERSION >= 501
				if (Property->ImportText_Direct(ImportTextPtr, OutValue, nullptr, PPF_None) == nullptr)
#else
				if (Property->ImportText(ImportTextPtr, OutValue, PPF_None, nullptr) == nullptr)
#endif
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"), *StructProperty->Struct->GetAuthoredName(), *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportStructFromString", "Unable to import JSON string into {0} property {1}"), FText::FromString(StructProperty->Struct->GetAuthoredName()), FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
			}
			else
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON value that is neither string nor object into %s property %s"), *StructProperty->Struct->GetAuthoredName(), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportStruct", "Unable to import JSON value that is neither string nor object into {0} property {1}"), FText::FromString(StructProperty->Struct->GetAuthoredName()), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}
#if UE_VERSION >= 425
		else if (FObjectProperty *ObjectProperty = CastField<FObjectProperty>(Property))
#else
		else if (UObjectProperty *ObjectProperty = Cast<UObjectProperty>(Property))
#endif
		{
			if (JsonValue->Type == EJson::Object)
			{
				UObject* Outer = GetTransientPackage();
				if (ContainerStruct->IsChildOf(UObject::StaticClass()))
				{
					Outer = (UObject*)Container;
				}

				TSharedPtr<FJsonObject> Obj = JsonValue->AsObject();
				UClass* PropertyClass = ObjectProperty->PropertyClass;

				// If a specific subclass was stored in the Json, use that instead of the PropertyClass
				FString ClassString = Obj->GetStringField(ObjectClassNameKey);
				Obj->RemoveField(ObjectClassNameKey);
				if (!ClassString.IsEmpty())
				{
#if UE_VERSION >= 501
					UClass* FoundClass = FPackageName::IsShortPackageName(ClassString) ? FindFirstObject<UClass>(*ClassString) : UClass::TryFindTypeSlow<UClass>(ClassString);
#else
					UClass* FoundClass = FindObject<UClass>(ANY_PACKAGE, *ClassString);
#endif
					if (FoundClass)
					{
						PropertyClass = FoundClass;
					}
				}

				UObject* createdObj = StaticAllocateObject(PropertyClass, Outer, NAME_None, EObjectFlags::RF_NoFlags, EInternalObjectFlags::None, false);
#if UE_VERSION >= 500
				(*PropertyClass->ClassConstructor)(FObjectInitializer(createdObj, PropertyClass->ClassDefaultObject, EObjectInitializerOptions::None));
#else
				(*PropertyClass->ClassConstructor)(FObjectInitializer(createdObj, PropertyClass->ClassDefaultObject, false, false));
#endif

				ObjectProperty->SetObjectPropertyValue(OutValue, createdObj);

				check(Obj.IsValid()); // should not fail if Type == EJson::Object
#if UE_VERSION >= 505
				if (!JsonAttributesToUStructWithContainer(Obj->Values, PropertyClass, createdObj, PropertyClass, createdObj, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason, ImportCb))
#else
				if (!JsonAttributesToUStructWithContainer(Obj->Values, PropertyClass, createdObj, PropertyClass, createdObj, CheckFlags & (~CPF_ParmFlags), SkipFlags, bStrictMode, OutFailReason))
#endif
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON object into %s property %s"), *PropertyClass->GetAuthoredName(), *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportObjectFromObject", "Unable to import JSON object into {0} property {1}\n{2}"), FText::FromString(PropertyClass->GetAuthoredName()), FText::FromString(Property->GetAuthoredName()), *OutFailReason);
					}
					return false;
				}
			}
			else if (JsonValue->Type == EJson::String)
			{
				// Default to expect a string for everything else
#if UE_VERSION >= 501
				if (Property->ImportText_Direct(*JsonValue->AsString(), OutValue, nullptr, 0) == nullptr)
#else
				if (Property->ImportText(*JsonValue->AsString(), OutValue, 0, nullptr) == nullptr)
#endif
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON string into %s property %s"), *ObjectProperty->PropertyClass->GetAuthoredName(), *Property->GetAuthoredName());
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportObjectFromString", "Unable to import JSON string into {0} property {1}"), FText::FromString(*ObjectProperty->PropertyClass->GetAuthoredName()), FText::FromString(Property->GetAuthoredName()));
					}
					return false;
				}
			}
		}
		else
		{
			// Default to expect a string for everything else
#if UE_VERSION >= 501
			if (Property->ImportText_Direct(*JsonValue->AsString(), OutValue, nullptr, 0) == nullptr)
#else
			if (Property->ImportText(*JsonValue->AsString(), OutValue, 0, nullptr) == nullptr)
#endif
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Unable to import JSON string into property %s"), *Property->GetAuthoredName());
				if (OutFailReason)
				{
					*OutFailReason = FText::Format(LOCTEXT("FailImportFromString", "Unable to import JSON string into property {0}"), FText::FromString(Property->GetAuthoredName()));
				}
				return false;
			}
		}

	return true;
	}


#if UE_VERSION >= 505
	bool JsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const FJsonLibraryConverter::CustomImportCallback* ImportCb)
#elif UE_VERSION >= 425
	bool JsonValueToFPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#else
	bool JsonValueToUPropertyWithContainer(const TSharedPtr<FJsonValue>& JsonValue, UProperty* Property, void* OutValue, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#endif
	{
		if (!JsonValue.IsValid())
		{
			UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Invalid JSON value"));
			if (OutFailReason)
			{
				*OutFailReason = LOCTEXT("InvalidJsonValue", "Invalid JSON value");
			}
			return false;
		}

#if UE_VERSION >= 425
		const bool bArrayOrSetProperty = Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>();
#else
		const bool bArrayOrSetProperty = Property->IsA<UArrayProperty>() || Property->IsA<USetProperty>();
#endif

		const bool bJsonArray = JsonValue->Type == EJson::Array;
		if (!bJsonArray)
		{
			if (bArrayOrSetProperty)
			{
				UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Expecting JSON array"));
				if (OutFailReason)
				{
					*OutFailReason = LOCTEXT("ExpectingJsonArray", "Expecting JSON array");
				}
				return false;
			}

			if (Property->ArrayDim != 1)
			{
				if (bStrictMode)
				{
					UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - Property %s is not an array but has %d elements"), *Property->GetAuthoredName(), Property->ArrayDim);
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("InvalidDimensionOfNonArrayProperty", "Property {0} is not an array but has {1} elements"), FText::FromString(Property->GetAuthoredName()), FText::AsNumber(Property->ArrayDim));
					}
					return false;
				}
				
				UE_LOG(LogJson, Warning, TEXT("Ignoring excess properties when deserializing %s"), *Property->GetAuthoredName());
			}

#if UE_VERSION >= 505
			return ConvertScalarJsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb);
#elif UE_VERSION >= 425
			return ConvertScalarJsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#else
			return ConvertScalarJsonValueToUPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#endif
		}

		// In practice, the ArrayDim == 1 check ought to be redundant, since nested arrays of FProperties are not supported
		if (bArrayOrSetProperty && Property->ArrayDim == 1)
		{
			// Read into TArray
#if UE_VERSION >= 505
			return ConvertScalarJsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb);
#elif UE_VERSION >= 425
			return ConvertScalarJsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#else
			return ConvertScalarJsonValueToUPropertyWithContainer(JsonValue, Property, OutValue, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#endif
		}

		// We're deserializing a JSON array
		const auto& ArrayValue = JsonValue->AsArray();

		if (bStrictMode && (Property->ArrayDim != ArrayValue.Num()))
		{
			UE_LOG(LogJson, Error, TEXT("JsonValueToUProperty - JSON array size is incorrect (has %d elements, but needs %d)"), ArrayValue.Num(), Property->ArrayDim);
			if (OutFailReason)
			{
				*OutFailReason = FText::Format(LOCTEXT("IncorrectArraySize", "JSON array size is incorrect (has {0} elements, but needs {1})"), FText::AsNumber(ArrayValue.Num()), FText::AsNumber(Property->ArrayDim));
			}
			return false;
		}
		
		if (Property->ArrayDim < ArrayValue.Num())
		{
			UE_LOG(LogJson, Warning, TEXT("Ignoring excess properties when deserializing %s"), *Property->GetAuthoredName());
		}

		// Read into native array
		const int32 ItemsToRead = FMath::Clamp(ArrayValue.Num(), 0, Property->ArrayDim);
		for (int Index = 0; Index != ItemsToRead; ++Index)
		{
#if UE_VERSION >= 505
			if (!ConvertScalarJsonValueToFPropertyWithContainer(ArrayValue[Index], Property, static_cast<char*>(OutValue) + Index * Property->GetElementSize(), ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb))
#elif UE_VERSION >= 425
			if (!ConvertScalarJsonValueToFPropertyWithContainer(ArrayValue[Index], Property, static_cast<char*>(OutValue) + Index * Property->ElementSize, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason))
#else
			if (!ConvertScalarJsonValueToUPropertyWithContainer(ArrayValue[Index], Property, static_cast<char*>(OutValue) + Index * Property->ElementSize, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason))
#endif
			{
				return false;
			}
		}
		return true;
	}
	
#if UE_VERSION >= 505
	bool JsonAttributesToUStructWithContainer(const TMap< FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const FJsonLibraryConverter::CustomImportCallback* ImportCb)
#else
	bool JsonAttributesToUStructWithContainer(const TMap< FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, const UStruct* ContainerStruct, void* Container, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#endif
	{
		if (StructDefinition == FJsonObjectWrapper::StaticStruct())
		{
			// Just copy it into the object
			FJsonObjectWrapper* ProxyObject = (FJsonObjectWrapper*)OutStruct;
			ProxyObject->JsonObject = MakeShared<FJsonObject>();
			ProxyObject->JsonObject->Values = JsonAttributes;
			return true;
		}

		int32 NumUnclaimedProperties = JsonAttributes.Num();
		if (NumUnclaimedProperties <= 0)
		{
			return true;
		}

// ---------- UUserDefinedStruct ----------
		bool bUserStruct = StructDefinition->IsA( UUserDefinedStruct::StaticClass() );
// ---------- UUserDefinedStruct ----------

		// iterate over the struct properties
#if UE_VERSION >= 425
		for (TFieldIterator<FProperty> PropIt(StructDefinition); PropIt; ++PropIt)
#else
		for (TFieldIterator<UProperty> PropIt(StructDefinition); PropIt; ++PropIt)
#endif
		{
#if UE_VERSION >= 425
			FProperty* Property = *PropIt;
#else
			UProperty* Property = *PropIt;
#endif

			// Check to see if we should ignore this property
			if (CheckFlags != 0 && !Property->HasAnyPropertyFlags(CheckFlags))
			{
				continue;
			}
			if (Property->HasAnyPropertyFlags(SkipFlags))
			{
				continue;
			}

// ---------- UUserDefinedStruct::GetAuthoredNameForField() ----------
			FString PropertyName = StructDefinition->GetAuthoredNameForField(Property);
			if (bUserStruct)
			{
				const int32 GuidStrLen = 32;
				const int32 MinimalPostfixlen = GuidStrLen + 3;
				if (PropertyName.Len() > MinimalPostfixlen)
				{
					FString DisplayName = PropertyName.LeftChop(GuidStrLen + 1);
					int FirstCharToRemove = -1;
					const bool bCharFound = DisplayName.FindLastChar(TCHAR('_'), FirstCharToRemove);
					if (bCharFound && (FirstCharToRemove > 0))
					{
						PropertyName = DisplayName.Mid(0, FirstCharToRemove);
					}
				}
			}
// ---------- UUserDefinedStruct::GetAuthoredNameForField() ----------

			// find a json value matching this property name
			const TSharedPtr<FJsonValue>* JsonValue = JsonAttributes.Find(PropertyName);
			
			if (!JsonValue)
			{
				if (bStrictMode)
				{
					UE_LOG(LogJson, Error, TEXT("JsonObjectToUStruct - Missing JSON value named %s"), *PropertyName);
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("MissingJsonField", "Missing JSON value named {0}"), FText::FromString(PropertyName));
					}
					return false;
				}
				
				// we allow values to not be found since this mirrors the typical UObject mantra that all the fields are optional when deserializing
				continue;
			}

			if (JsonValue->IsValid() && !(*JsonValue)->IsNull())
			{
				void* Value = Property->ContainerPtrToValuePtr<uint8>(OutStruct);
#if UE_VERSION >= 505
				if (!JsonValueToFPropertyWithContainer(*JsonValue, Property, Value, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb))
#elif UE_VERSION >= 425
				if (!JsonValueToFPropertyWithContainer(*JsonValue, Property, Value, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason))
#else
				if (!JsonValueToUPropertyWithContainer(*JsonValue, Property, Value, ContainerStruct, Container, CheckFlags, SkipFlags, bStrictMode, OutFailReason))
#endif
				{
					UE_LOG(LogJson, Error, TEXT("JsonObjectToUStruct - Unable to import JSON value into property %s"), *PropertyName);
					if (OutFailReason)
					{
						*OutFailReason = FText::Format(LOCTEXT("FailImportValueToProperty", "Unable to import JSON value into property {0}\n{1}"), FText::FromString(PropertyName), *OutFailReason);
					}
					return false;
				}
			}

			if (--NumUnclaimedProperties <= 0)
			{
				// Should we log a warning/error if we still have properties in the JSON data that aren't in the struct definition in strict mode?
				
				// If we found all properties that were in the JsonAttributes map, there is no reason to keep looking for more.
				break;
			}
		}

		return true;
	}
}

#if UE_VERSION >= 505
bool FJsonLibraryConverter::JsonValueToUProperty(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const CustomImportCallback* ImportCb)
#elif UE_VERSION >= 425
bool FJsonLibraryConverter::JsonValueToUProperty(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* OutValue, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#else
bool FJsonLibraryConverter::JsonValueToUProperty(const TSharedPtr<FJsonValue>& JsonValue, UProperty* Property, void* OutValue, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#endif
{
#if UE_VERSION >= 505
	return JsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, nullptr, nullptr, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb);
#elif UE_VERSION >= 425
	return JsonValueToFPropertyWithContainer(JsonValue, Property, OutValue, nullptr, nullptr, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#else
	return JsonValueToUPropertyWithContainer(JsonValue, Property, OutValue, nullptr, nullptr, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#endif
}

#if UE_VERSION >= 505
bool FJsonLibraryConverter::JsonObjectToUStruct(const TSharedRef<FJsonObject>& JsonObject, const UStruct* StructDefinition, void* OutStruct, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const CustomImportCallback* ImportCb)
#else
bool FJsonLibraryConverter::JsonObjectToUStruct(const TSharedRef<FJsonObject>& JsonObject, const UStruct* StructDefinition, void* OutStruct, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#endif
{
#if UE_VERSION >= 505
	return JsonAttributesToUStruct(JsonObject->Values, StructDefinition, OutStruct, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb);
#else
	return JsonAttributesToUStruct(JsonObject->Values, StructDefinition, OutStruct, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#endif
}

#if UE_VERSION >= 505
bool FJsonLibraryConverter::JsonAttributesToUStruct(const TMap<FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason, const CustomImportCallback* ImportCb)
#else
bool FJsonLibraryConverter::JsonAttributesToUStruct(const TMap<FString, TSharedPtr<FJsonValue> >& JsonAttributes, const UStruct* StructDefinition, void* OutStruct, int64 CheckFlags, int64 SkipFlags, const bool bStrictMode, FText* OutFailReason)
#endif
{
#if UE_VERSION >= 505
	return JsonAttributesToUStructWithContainer(JsonAttributes, StructDefinition, OutStruct, StructDefinition, OutStruct, CheckFlags, SkipFlags, bStrictMode, OutFailReason, ImportCb);
#else
	return JsonAttributesToUStructWithContainer(JsonAttributes, StructDefinition, OutStruct, StructDefinition, OutStruct, CheckFlags, SkipFlags, bStrictMode, OutFailReason);
#endif
}

//static 
bool FJsonLibraryConverter::GetTextFromField(const FString& FieldName, const TSharedPtr<FJsonValue>& FieldValue, FText& TextOut)
{
	if (FieldValue.IsValid())
	{
		switch (FieldValue->Type)
		{
			case EJson::Number:
			{
				// number
				TextOut = FText::AsNumber(FieldValue->AsNumber());
				return true;
			}
			case EJson::String:
			{
				if (FieldName.StartsWith(TEXT("date-")))
				{
					FDateTime Dte;
					if (FDateTime::ParseIso8601(*FieldValue->AsString(), Dte))
					{
						TextOut = FText::AsDate(Dte);
						return true;
					}
				}
				else if (FieldName.StartsWith(TEXT("datetime-")))
				{
					FDateTime Dte;
					if (FDateTime::ParseIso8601(*FieldValue->AsString(), Dte))
					{
						TextOut = FText::AsDateTime(Dte);
						return true;
					}
				}
				else
				{
				// culture invariant string
					TextOut = FText::FromString(FieldValue->AsString());
					return true;
				}
				break;
			}
			case EJson::Object:
			{
				// localized string
				if (FJsonLibraryConverter::GetTextFromObject(FieldValue->AsObject().ToSharedRef(), TextOut))
				{
					return true;
				}

				UE_LOG(LogJson, Error, TEXT("Unable to apply JSON parameter %s (could not parse object)"), *FieldName);
				break;
			}
			default:
			{
				UE_LOG(LogJson, Error, TEXT("Unable to apply JSON parameter %s (bad type)"), *FieldName);
				break;
			}
		}
	}
	return false;
}

FFormatNamedArguments FJsonLibraryConverter::ParseTextArgumentsFromJson(const TSharedPtr<const FJsonObject>& JsonObject)
{
	FFormatNamedArguments NamedArgs;
	if (JsonObject.IsValid())
	{
		for (const auto& It : JsonObject->Values)
		{
			FText TextValue;
			if (GetTextFromField(It.Key, It.Value, TextValue))
			{
				NamedArgs.Emplace(It.Key, TextValue);
			}
		}
	}
	return NamedArgs;
}

const FJsonLibraryConverter::CustomExportCallback FJsonLibraryConverter::ExportCallback_WriteISO8601Dates = 
	FJsonLibraryConverter::CustomExportCallback::CreateLambda(
#if UE_VERSION >= 425
		[](FProperty* Prop, const void* Data) -> TSharedPtr<FJsonValue>
#else
		[](UProperty* Prop, const void* Data) -> TSharedPtr<FJsonValue>
#endif
		{
#if UE_VERSION >= 425
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Prop))
#else
			if (UStructProperty* StructProperty = Cast<UStructProperty>(Prop))
#endif
			{
				checkSlow(StructProperty->Struct);
				if (StructProperty->Struct->GetFName() == NAME_DateTime)
				{
					return MakeShared<FJsonValueString>(static_cast<const FDateTime*>(Data)->ToIso8601());
				}
			}
			return {};
		});

#undef LOCTEXT_NAMESPACE

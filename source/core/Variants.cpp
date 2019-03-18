
/**
Copyright (c) 2018 National Instruments Corp.

This software is subject to the terms described in the LICENSE.TXT file

SDG
*/

/*! \file
    \brief Variant data type and variant attribute support functions
*/

#include "TypeDefiner.h"
#include "ExecutionContext.h"
#include "TypeAndDataManager.h"
#include "TDCodecVia.h"
#include <limits>
#include <map>
#include "Variants.h"

namespace Vireo
{
//------------------------------------------------------------
struct DataToVariantParamBlock : public InstructionCore
{
    _ParamImmediateDef(StaticTypeAndData, InputData);
    _ParamDef(TypeRef, OutputVariant);
    NEXT_INSTRUCTION_METHOD()
};

TypeRef ConvertDataToVariant(TypeRef inputType, void* inputData)
{
    TypeManagerRef tm = THREAD_TADM();
    TypeRef variant = DefaultValueType::New(tm, inputType, true);
    variant->CopyData(inputData, variant->Begin(kPAWrite));
    return variant;
}

// Convert data of any type to variant
VIREO_FUNCTION_SIGNATURET(DataToVariant, DataToVariantParamBlock)
{
    TypeRef inputType = _ParamImmediate(InputData._paramType);
    void* inputData = _ParamImmediate(InputData._pData);
    _Param(OutputVariant) = ConvertDataToVariant(inputType, inputData);
    return _NextInstruction();
}

//------------------------------------------------------------
struct VariantToDataParamBlock : public InstructionCore
{
    _ParamImmediateDef(StaticTypeAndData, InputData);
    _ParamDef(ErrorCluster, ErrorClust);
    _ParamImmediateDef(StaticTypeAndData, DestData);
    NEXT_INSTRUCTION_METHOD()
};

// Convert variant to data of given type. Error if the data types don't match
VIREO_FUNCTION_SIGNATURET(VariantToData, VariantToDataParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    if (!errPtr || !errPtr->status) {
        TypeRef inputType = _ParamImmediate(InputData._paramType);
        void* inputData = _ParamImmediate(InputData)._pData;

        TypeRef destType = _ParamImmediate(DestData._paramType);
        void* destData = _ParamImmediate(DestData)._pData;

        if (inputType->IsA(destType)) {
            if (inputType->Name().Compare(&TypeCommon::TypeVariant)) {
                TypeRef variantData = *(TypeRef *)_ParamImmediate(InputData._pData);
                variantData->CopyData(variantData->Begin(kPARead), destData);
            } else {
                inputType->CopyData(inputData, destData);
            }
        } else if (errPtr) {
            errPtr->SetErrorAndAppendCallChain(true, 91, "Variant To Data");
        }
    }
    return _NextInstruction();
}

struct SetVariantAttributeParamBlock : public InstructionCore
{
    _ParamDef(TypeRef, InputVariant);
    _ParamDef(StringRef, Name);
    _ParamImmediateDef(StaticTypeAndData, Value);
    _ParamDef(Boolean, Replaced);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(SetVariantAttribute, SetVariantAttributeParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    bool replaced = false;
    if (!errPtr || !errPtr->status) {
        StringRef name = _Param(Name);
        if (IsStringEmpty(name)) {
            if (errPtr) {
                errPtr->SetErrorAndAppendCallChain(true, 1, "Set Variant Attribute");
            }
        } else {
            TypeManagerRef tm = THREAD_TADM();

            StringRef nameKeyRef = nullptr;
            TypeRef stringType = tm->FindType("String");
            stringType->InitData(&nameKeyRef);
            nameKeyRef->Append(name->Length(), name->Begin());

            TypeRef valueType = _ParamImmediate(Value._paramType);
            TypeRef variantValue = DefaultValueType::New(tm, valueType, true);
            variantValue->CopyData(_ParamImmediate(Value._pData), variantValue->Begin(kPAWrite));

            TypeRef inputVariant = _Param(InputVariant);
            VariantAttributeManager::VariantToAttributeMapType &variantToAttributeMap = VariantAttributeManager::Instance().GetVariantToAttributeMap();
            auto variantToAttributeMapIter = variantToAttributeMap.find(inputVariant);
            if (variantToAttributeMapIter != variantToAttributeMap.end()) {
                VariantAttributeManager::AttributeMapType *attributeMap = variantToAttributeMapIter->second;

                auto pairIterBool = attributeMap->insert(VariantAttributeManager::AttributeMapType::value_type(nameKeyRef, variantValue));
                replaced = !pairIterBool.second;
                if (replaced) {
                    pairIterBool.first->second = variantValue;
                    nameKeyRef->Delete(nameKeyRef);
                }
            } else {
                auto attributeMap = new VariantAttributeManager::AttributeMapType;
                (*attributeMap)[nameKeyRef] = variantValue;
                variantToAttributeMap[inputVariant] = attributeMap;
                replaced = false;
            }
        }
    }
    if (_ParamPointer(Replaced)) {
        _Param(Replaced) = replaced;
    }
    return _NextInstruction();
}

struct GetVariantAttributeParamBlock : public InstructionCore
{
    _ParamDef(TypeRef, InputVariant);
    _ParamDef(StringRef, Name);
    _ParamImmediateDef(StaticTypeAndData, Value);
    _ParamDef(Boolean, Found);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(GetVariantAttribute, GetVariantAttributeParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    bool found = false;
    if (!errPtr || !errPtr->status) {
        TypeRef inputVariant = _Param(InputVariant);
        StringRef name = _Param(Name);
        StaticTypeAndDataRef value = &_ParamImmediate(Value);
        const VariantAttributeManager::VariantToAttributeMapType &variantToAttributeMap = VariantAttributeManager::Instance().GetVariantToAttributeMap();

        const auto variantToAttributeMapIter = variantToAttributeMap.find(inputVariant);
        if (variantToAttributeMapIter != variantToAttributeMap.end()) {
            VariantAttributeManager::AttributeMapType *attributeMap = variantToAttributeMapIter->second;
            auto attributeMapIter = attributeMap->find(name);
            if (attributeMapIter != attributeMap->end()) {
                TypeRef foundValue = attributeMapIter->second;
                if (foundValue->IsA(value->_paramType) || value->_paramType->Name().Compare(&TypeCommon::TypeVariant)) {
                    found = true;
                    value->_paramType->CopyData(foundValue->Begin(kPARead), value->_pData);
                } else {
                    if (errPtr) {
                        errPtr->SetErrorAndAppendCallChain(true, 91, "Get Variant Attribute");  // Incorrect type for default value for the attribute
                    }
                }
            }
        }
    }
    if (_ParamPointer(Found)) {
        _Param(Found) = found;
    }
    return _NextInstruction();
}

struct GetVariantAttributesAllParamBlock : public InstructionCore
{
    _ParamDef(TypeRef, InputVariant);
    _ParamDef(TypedArrayCoreRef, Names);
    _ParamDef(TypedArrayCoreRef, Values);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(GetVariantAttributeAll, GetVariantAttributesAllParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    TypedArrayCoreRef names = _Param(Names);
    TypedArrayCoreRef values = _Param(Values);
    bool bResetOutputArrays = true;
    if (!errPtr || !errPtr->status) {
        TypeRef inputVariant = _Param(InputVariant);
        const VariantAttributeManager::VariantToAttributeMapType &variantToAttributeMap = VariantAttributeManager::Instance().GetVariantToAttributeMap();

        const auto variantToAttributeMapIter = variantToAttributeMap.find(inputVariant);
        if (variantToAttributeMapIter != variantToAttributeMap.end()) {
            VariantAttributeManager::AttributeMapType *attributeMap = variantToAttributeMapIter->second;
            const auto mapSize = attributeMap->size();
            if (mapSize != 0) {
                bResetOutputArrays = false;
                names->Resize1D(mapSize);
                values->Resize1D(mapSize);
                AQBlock1* pNamesInsert = names->BeginAt(0);
                AQBlock1* pValuesInsert = values->BeginAt(0);
                TypeRef namesElementType = names->ElementType();
                TypeRef valuesElementType = values->ElementType();
                const Int32 namesAQSize = namesElementType->TopAQSize();
                const Int32 valuesAQSize = valuesElementType->TopAQSize();
                TypeManagerRef tm = THREAD_TADM();
                for (const auto attributePair : *attributeMap) {
                    String* const* attributeNameStr = &(attributePair.first);
                    TypeRef attributeValue = attributePair.second;
                    namesElementType->CopyData(attributeNameStr, pNamesInsert);
                    if (attributeValue->Name().Compare(&TypeCommon::TypeVariant)) {
                        attributeValue->CopyData(attributeValue->Begin(kPARead), pValuesInsert);
                    } else {
                        TypeRef variant = DefaultValueType::New(tm, attributeValue, true);
                        variant->CopyData(attributeValue->Begin(kPARead), variant->Begin(kPAWrite));
                        *reinterpret_cast<TypeRef *>(pValuesInsert) = variant;
                    }
                    pNamesInsert += namesAQSize;
                    pValuesInsert += valuesAQSize;
                }
            }
        }
    }
    if (bResetOutputArrays) {
        names->Resize1D(0);
        values->Resize1D(0);
    }
    return _NextInstruction();
}

struct DeleteVariantAttributeParamBlock : public InstructionCore
{
    _ParamDef(TypeRef, InputVariant);
    _ParamDef(StringRef, Name);
    _ParamDef(Boolean, Found);
    _ParamDef(ErrorCluster, ErrorClust);
    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(DeleteVariantAttribute, DeleteVariantAttributeParamBlock)
{
    ErrorCluster *errPtr = _ParamPointer(ErrorClust);
    StringRef *name = _ParamPointer(Name);
    _Param(Found) = false;
    if (!errPtr || !errPtr->status) {
        const TypeRef inputVariant = _Param(InputVariant);
        VariantAttributeManager::VariantToAttributeMapType &variantToAttributeMap = VariantAttributeManager::Instance().GetVariantToAttributeMap();

        const auto variantToAttributeMapIter = variantToAttributeMap.find(inputVariant);
        if (variantToAttributeMapIter != variantToAttributeMap.end()) {
            VariantAttributeManager::AttributeMapType *attributeMap = variantToAttributeMapIter->second;
            const auto mapSize = attributeMap->size();
            if (mapSize != 0) {
                if (!name) {
                    for (auto attribute : *attributeMap) {
                        attribute.first->Delete(attribute.first);
                    }
                    attributeMap->clear();
                    variantToAttributeMap.erase(variantToAttributeMapIter);
                    delete attributeMap;
                    _Param(Found) = true;
                } else {
                    const auto attributeMapIterator = attributeMap->find(*name);
                    if (attributeMapIterator != attributeMap->end()) {
                        _Param(Found) = true;
                        attributeMapIterator->first->Delete(attributeMapIterator->first);
                        attributeMap->erase(attributeMapIterator);
                    }
                }
            }
        }
    }
    return _NextInstruction();
}

struct CopyVariantParamBlock : public InstructionCore
{
    _ParamDef(TypeRef, InputVariant);
    _ParamDef(TypeRef, OutputVariant);

    NEXT_INSTRUCTION_METHOD()
};

VIREO_FUNCTION_SIGNATURET(CopyVariant, CopyVariantParamBlock)
{
    TypeRef inputVariant = _Param(InputVariant);
    TypeManagerRef tm = THREAD_TADM();

    if (inputVariant != nullptr) {
        TypeRef destType = DefaultValueType::New(tm, inputVariant, true);
        destType->CopyData(inputVariant->Begin(kPARead), destType->Begin(kPAWrite));

        VariantAttributeManager::VariantToAttributeMapType &variantToAttributeMap = VariantAttributeManager::Instance().GetVariantToAttributeMap();

        const auto variantToAttributeMapIter = variantToAttributeMap.find(inputVariant);
        if (variantToAttributeMapIter != variantToAttributeMap.end()) {
            VariantAttributeManager::AttributeMapType* attributeMapInput = variantToAttributeMapIter->second;
            auto attributeMapOutput = new VariantAttributeManager::AttributeMapType;
            for (auto attribute : *attributeMapInput) {
                StringRef nameKeyRef = nullptr;
                TypeRef stringType = tm->FindType("String");
                stringType->InitData(&nameKeyRef);
                nameKeyRef->Append(attribute.first->Length(), attribute.first->Begin());

                TypeRef valueType = attribute.second;
                TypeRef variantValue = DefaultValueType::New(tm, valueType, true);
                variantValue->CopyData(attribute.second->Begin(kPARead), variantValue->Begin(kPAWrite));

                (*attributeMapOutput)[nameKeyRef] = variantValue;
            }
            variantToAttributeMap[destType] = attributeMapOutput;
        }
        _Param(OutputVariant) = destType;
    }
    return _NextInstruction();
}

DEFINE_VIREO_BEGIN(Variant)

    DEFINE_VIREO_FUNCTION(VariantToData, "p(i(StaticTypeAndData) io(ErrorCluster) o(StaticTypeAndData))");
    DEFINE_VIREO_FUNCTION(DataToVariant, "p(i(StaticTypeAndData) o(Variant))");
    DEFINE_VIREO_FUNCTION(SetVariantAttribute, "p(io(Variant inputVariant) i(String name)"
                                                " i(StaticTypeAndData value) o(Boolean replaced) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(GetVariantAttribute, "p(i(Variant inputVariant) i(String name)"
                                                "io(StaticTypeAndData value) o(Boolean found) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(GetVariantAttributeAll, "p(i(Variant inputVariant) o(Array names)"
                                                   "o(Array values) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(DeleteVariantAttribute, "p(io(Variant inputVariant) i(String name) o(Boolean found) io(ErrorCluster error) )");
    DEFINE_VIREO_FUNCTION(CopyVariant, "p(i(Variant inputVariant) o(Variant outputVariant) )");
    DEFINE_VIREO_FUNCTION_CUSTOM(Convert, ConvertToVariant, "p(i(StaticTypeAndData) o(Variant))")

DEFINE_VIREO_END()

};  // namespace Vireo
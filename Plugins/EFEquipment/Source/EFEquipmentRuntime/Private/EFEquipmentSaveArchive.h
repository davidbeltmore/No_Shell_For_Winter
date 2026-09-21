#pragma once

#include "Components/ACFInventoryComponent.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

/** Native composition/instance serialization remains authoritative. Exclude only
 * the redundant world-local cache in the earlier tagged inventory properties. */
class FEFEquipmentSaveArchive final : public FObjectAndNameAsStringProxyArchive
{
public:
    FEFEquipmentSaveArchive(FArchive& Inner, bool bLoad, bool bNoDelta = false)
        : FObjectAndNameAsStringProxyArchive(Inner, bLoad)
    { ArIsSaveGame = true; ArNoDelta = bNoDelta; }

    virtual bool ShouldSkipProperty(const FProperty* Property) const override
    {
        return (Property && Property->GetOwnerStruct() == FInventoryItem::StaticStruct()
            && Property->GetFName() == TEXT("Item")) || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property);
    }
};

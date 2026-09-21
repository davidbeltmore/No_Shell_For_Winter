#pragma once

// Process-wide CVar ownership is shared by all Director GameInstances (including multi-PIE).
namespace EFCalystoAsyncLoadingBudget
{
    void SetOwnerActive(const void* Owner, bool bActive);
    float GetEffectiveMilliseconds();
}

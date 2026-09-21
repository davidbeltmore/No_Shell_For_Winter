#include "EFClothingMorphSettings.h"

namespace EFClothingMorphSettingsPrivate
{
	constexpr float MinimumEquipmentReconcileSeconds = 0.0f;
	constexpr float MaximumEquipmentReconcileSeconds = 5.0f;
	constexpr float MinimumWorldDiscoverySeconds = 0.10f;
	constexpr float MaximumWorldDiscoverySeconds = 5.0f;
	constexpr float MinimumMorphSyncSeconds = 0.0f;
	constexpr float MaximumMorphSyncSeconds = 1.0f;
	constexpr float MinimumBindingLoadTimeoutSeconds = 0.50f;
	constexpr float MaximumBindingLoadTimeoutSeconds = 15.0f;
}

float UEFClothingMorphSettings::GetEquipmentReconcileFallbackIntervalSeconds() const
{
	return FMath::Clamp(
		FMath::IsFinite(EquipmentReconcileFallbackIntervalSeconds)
			? EquipmentReconcileFallbackIntervalSeconds
			: 2.0f,
		EFClothingMorphSettingsPrivate::MinimumEquipmentReconcileSeconds,
		EFClothingMorphSettingsPrivate::MaximumEquipmentReconcileSeconds);
}

float UEFClothingMorphSettings::GetWorldDiscoveryFallbackIntervalSeconds() const
{
	return FMath::Clamp(
		FMath::IsFinite(WorldDiscoveryFallbackIntervalSeconds)
			? WorldDiscoveryFallbackIntervalSeconds
			: 0.50f,
		EFClothingMorphSettingsPrivate::MinimumWorldDiscoverySeconds,
		EFClothingMorphSettingsPrivate::MaximumWorldDiscoverySeconds);
}

float UEFClothingMorphSettings::GetMorphSyncIntervalSeconds() const
{
	return FMath::Clamp(
		FMath::IsFinite(MorphSyncIntervalSeconds) ? MorphSyncIntervalSeconds : 0.0f,
		EFClothingMorphSettingsPrivate::MinimumMorphSyncSeconds,
		EFClothingMorphSettingsPrivate::MaximumMorphSyncSeconds);
}

float UEFClothingMorphSettings::GetBindingLoadTimeoutSeconds() const
{
	return FMath::Clamp(
		FMath::IsFinite(BindingLoadTimeoutSeconds) ? BindingLoadTimeoutSeconds : 5.0f,
		EFClothingMorphSettingsPrivate::MinimumBindingLoadTimeoutSeconds,
		EFClothingMorphSettingsPrivate::MaximumBindingLoadTimeoutSeconds);
}

int32 UEFClothingMorphSettings::GetMaximumConcurrentBindingLoads() const
{
	return FMath::Clamp(MaximumConcurrentBindingLoads, 1, 16);
}

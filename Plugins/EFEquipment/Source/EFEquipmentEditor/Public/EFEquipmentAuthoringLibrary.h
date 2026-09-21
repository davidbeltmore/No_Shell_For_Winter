#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFEquipmentAuthoringLibrary.generated.h"

class UWidget;
class UUserWidget;
class UACFVendorComponent;

UCLASS()
class EFEQUIPMENTEDITOR_API UEFEquipmentAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Exact project-owned asset batch. Does not save Player, bodies or vendor assets. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildFoundation();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString AuditConsumers();
    /** Read-only graph audit of explicit project candidates discovered by file search. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString AuditBlueprintCalls(const TArray<FString>& AssetPaths);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString InspectInventoryUI();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildInventoryUI();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildWorldActors();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildSaveIntegration();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildStorageTradeIntegration();
    /** Isolates native save-list metadata in PIE; never saves DeveloperSettings config. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static FString BeginSaveFixture(UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static bool EndSaveFixture();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static bool SaveWorldFixture(UObject* WorldContextObject);
    /** PIE-only legacy candidate, isolated from the live inventory and existing user slots. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static FString CreateLegacySaveFixture(UObject* WorldContextObject);
    /** Force an exact saved actor-name collision using a temporary in-memory save. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static FString ProbePickupNameReuse(UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static bool LoadWorldFixture(UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static bool IsSaveFixtureBusy(UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static UACFVendorComponent* CreateVendorFixture(UObject* WorldContextObject);
    /** PIE-only: trigger the real native zero-health callback with bag drop enabled. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA", meta=(WorldContext="WorldContextObject")) static bool TriggerDeathFixture(UObject* WorldContextObject);
    /** PIE-only Slate input fixtures. No OS input injection or asset writes. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static UWidget* FindInventoryWidget(UUserWidget* Root, FName Name);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static bool SendInventoryPointer(UWidget* Target, FName Action, float WheelDelta = 0, FVector2D LocalOffset = FVector2D::ZeroVector);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static bool SendInventoryBackKey(UWidget* Target);
};

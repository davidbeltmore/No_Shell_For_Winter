#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EFEquipmentAuthoringLibrary.generated.h"

class UWidget;
class UUserWidget;

UCLASS()
class EFEQUIPMENTEDITOR_API UEFEquipmentAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Exact project-owned asset batch. Does not save Player, bodies or vendor assets. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildFoundation();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString AuditConsumers();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString InspectInventoryUI();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildInventoryUI();
    /** PIE-only Slate input fixtures. No OS input injection or asset writes. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static UWidget* FindInventoryWidget(UUserWidget* Root, FName Name);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static bool SendInventoryPointer(UWidget* Target, FName Action, float WheelDelta = 0, FVector2D LocalOffset = FVector2D::ZeroVector);
};

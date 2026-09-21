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
    /** Read-only graph audit of explicit project candidates discovered by file search. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString AuditBlueprintCalls(const TArray<FString>& AssetPaths);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString InspectInventoryUI();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildInventoryUI();
    UFUNCTION(BlueprintCallable, Category="EF Equipment|Authoring") static FString BuildWorldActors();
    /** PIE-only Slate input fixtures. No OS input injection or asset writes. */
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static UWidget* FindInventoryWidget(UUserWidget* Root, FName Name);
    UFUNCTION(BlueprintCallable, Category="EF Equipment|QA") static bool SendInventoryPointer(UWidget* Target, FName Action, float WheelDelta = 0, FVector2D LocalOffset = FVector2D::ZeroVector);
};

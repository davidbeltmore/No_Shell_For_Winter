#include "EFEquipmentSaleWidget.h"
#include "EFEquipmentCoordinatorComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UEFEquipmentSaleWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (ConfirmButton) ConfirmButton->OnClicked.AddUniqueDynamic(this, &UEFEquipmentSaleWidget::Confirm);
    if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &UEFEquipmentSaleWidget::Cancel);
}
void UEFEquipmentSaleWidget::SetQuote(UEFEquipmentCoordinatorComponent* Owner, const FEFEquipmentSaleQuote& Quote)
{
    Coordinator = Owner; QuoteId = Quote.QuoteId; bPurchase = Quote.bPurchase;
    if (auto* Heading = Cast<UTextBlock>(GetWidgetFromName(TEXT("SaleHeading"))))
        Heading->SetText(FText::FromString(bPurchase ? TEXT("Confirm purchase") : TEXT("Confirm sale")));
    if (auto* Action = Cast<UTextBlock>(GetWidgetFromName(TEXT("ConfirmLabel"))))
        Action->SetText(FText::FromString(bPurchase ? TEXT("Buy all") : TEXT("Sell all")));
    FString Breakdown;
    for (const FText& Line : Quote.Lines) Breakdown += Line.ToString() + TEXT("\n");
    Breakdown += FString::Printf(TEXT("\nTotal: %.2f\n\n%s this item and everything inside it?"), Quote.TotalPrice, bPurchase ? TEXT("Buy") : TEXT("Sell"));
    if (BreakdownLabel) BreakdownLabel->SetText(Quote.bValid ? FText::FromString(Breakdown) : Quote.Message);
    if (ConfirmButton) ConfirmButton->SetIsEnabled(Quote.bValid);
}
UWidget* UEFEquipmentSaleWidget::NativeGetDesiredFocusTarget() const
{ return ConfirmButton && ConfirmButton->GetIsEnabled() ? ConfirmButton.Get() : CancelButton.Get(); }
void UEFEquipmentSaleWidget::Confirm()
{
    if (!Coordinator) return;
    const auto Result = bPurchase ? Coordinator->ConfirmPurchase(QuoteId) : Coordinator->ConfirmSale(QuoteId);
    if (Result.bSuccess) DeactivateWidget();
    else
    {
        if (BreakdownLabel) BreakdownLabel->SetText(Result.Message);
        if (ConfirmButton) ConfirmButton->SetIsEnabled(false);
        if (CancelButton) CancelButton->SetKeyboardFocus();
    }
}
void UEFEquipmentSaleWidget::Cancel() { DeactivateWidget(); }
void UEFEquipmentSaleWidget::NativeOnDeactivated()
{
    if (Coordinator) Coordinator->CancelSale();
    Super::NativeOnDeactivated();
}

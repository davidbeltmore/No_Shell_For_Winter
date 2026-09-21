#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "ACFVendorComponent.h"
#include "ACFItemsManagerComponent.h"
#include "GameFramework/Pawn.h"
#include "Misc/SecureHash.h"

bool UEFEquipmentCoordinatorComponent::BuildSale(UACFVendorComponent* Vendor, FGuid Guid, int32 Count,
    FEFEquipmentSaleQuote& Quote, FString& Signature, FString& Error)
{
    FInventoryItem Root;
    auto* Pawn = Cast<APawn>(GetOwner());
    if (!IsValid(Vendor) || !Pawn || !Equipment->GetItemByGuid(Guid, Root) || !Root.Item || Count <= 0 || Count > Root.Count
        || !Vendor->GetPawnCurrencyComponent(Pawn))
    { Error = TEXT("The vendor, item, quantity or wallet is unavailable."); return false; }
    if (Count < Root.Count && !Root.Item->Fragments.IsEmpty()) { Error = TEXT("Sell this stateful stack as a whole."); return false; }
    Quote.ItemGuid = Guid; Quote.Count = Count;
    Signature = Guid.ToString() + FString::Printf(TEXT("|%d|%d|%s"), Root.Count, Count, *Root.ItemClass->GetPathName());
    auto Append = [&](const FInventoryItem& Item, int32 Quantity) -> bool
    {
        if (!Item.Item || !Item.ItemClass) { Error = TEXT("The item definition is missing."); return false; }
        const auto Info = Item.ItemClass->GetDefaultObject<UACFItem>()->GetItemInfo();
        if (!Info.bSellable || !Item.Item->GetItemInfo().bSellable)
        { Error = FString::Printf(TEXT("Cannot sell this set: %s cannot be sold."), *Item.Item->GetItemName().ToString()); return false; }
        const double Price = static_cast<double>(Info.CurrencyValue) * Quantity * Vendor->GetVendorPriceMultiplierOnBuy();
        if (!FMath::IsFinite(Price) || Price < 0 || !Vendor->CanPawnSellItemFromHisInventory(Item, Pawn, Quantity))
        { Error = FString::Printf(TEXT("The vendor cannot buy %s."), *Item.Item->GetItemName().ToString()); return false; }
        Quote.TotalPrice += Price;
        Quote.Lines.Add(FText::FromString(FString::Printf(TEXT("%s × %d: %.2f"), *Item.Item->GetItemName().ToString(), Quantity, Price)));
        Signature += TEXT("|") + Item.GetItemGuid().ToString() + FString::Printf(TEXT(":%d:%.9g"), Quantity, Price);
        return true;
    };
    if (!Append(Root, Count)) return false;
    if (auto* Rules = UEFEquipmentRulesFragment::Find(Root.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
    {
        auto* Container = ResolveContainer(Guid, Error); if (!Container || Count != 1 || Root.Count != 1) return false;
        for (const auto& Item : Container->GetInventory()) if (!Append(Item, Item.Count)) return false;
        if (!Rules->FlushContainer(Error)) return false;
        FSHAHash Digest; FSHA1::HashBuffer(Rules->ContainerPayload.GetData(), Rules->ContainerPayload.Num(), Digest.Hash);
        Signature += TEXT("|") + Digest.ToString();
    }
    if (Vendor->VendorUsesCurrency() && (!Vendor->GetVendorCurrencyComp() || Vendor->GetVendorCurrency() < Quote.TotalPrice))
    { Error = TEXT("The vendor cannot afford the satchel and all its contents."); return false; }
    auto* Wallet = Vendor->GetPawnCurrencyComponent(Pawn);
    const float Price = static_cast<float>(Quote.TotalPrice);
    if (!FMath::IsFinite(Price) || !FMath::IsFinite(Wallet->GetCurrentCurrencyAmount() + Price)
        || (Vendor->VendorUsesCurrency() && Wallet == Vendor->GetVendorCurrencyComp()))
    { Error = TEXT("This sale cannot be represented by the native wallets."); return false; }
    double Capacity = 0;
    if (Vendor->GetAvailableSlots() < 1 || !EFEquipmentACF::Capacity(Vendor, Capacity)
        || Vendor->GetCurrentInventoryTotalWeight() + Root.Item->GetItemInfo().ItemWeight * Count > Capacity + 0.000001)
    { Error = TEXT("The vendor has no room for this item."); return false; }
    Quote.bValid = true; return true;
}

FEFEquipmentSaleQuote UEFEquipmentCoordinatorComponent::PreviewSale(UACFVendorComponent* Vendor, FGuid Guid, int32 Count)
{
    CancelSale(); FEFEquipmentSaleQuote Quote; FString Error, Signature;
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) Error = TEXT("Equipment is busy or unavailable.");
    else BuildSale(Vendor, Guid, Count, Quote, Signature, Error);
    if (!Quote.bValid) { Quote.Message = FText::FromString(Error); LastMessage = Quote.Message; OnStateChanged.Broadcast(); return Quote; }
    Quote.QuoteId = FGuid::NewGuid(); PendingSale = Quote; SaleVendor = Vendor; SaleSignature = Signature;
    return Quote;
}

void UEFEquipmentCoordinatorComponent::CancelSale()
{ PendingSale = FEFEquipmentSaleQuote(); SaleVendor.Reset(); SaleSignature.Reset(); }

FEFEquipmentResult UEFEquipmentCoordinatorComponent::ConfirmSale(FGuid QuoteId)
{
    if (bOperation || !GetOwner()->HasAuthority() || !PendingSale.bValid || PendingSale.QuoteId != QuoteId)
        return Publish(FEFEquipmentResult::Failure(TEXT("This sale confirmation is no longer valid.")));
    FEFEquipmentSaleQuote Current; FString Signature, Error; auto* Vendor = SaleVendor.Get();
    if (!BuildSale(Vendor, PendingSale.ItemGuid, PendingSale.Count, Current, Signature, Error) || Signature != SaleSignature || Current.TotalPrice != PendingSale.TotalPrice)
    { CancelSale(); return Publish(FEFEquipmentResult::Failure(Error.IsEmpty() ? TEXT("The contents or price changed. Review the sale again.") : Error)); }
    const auto Quote = PendingSale; CancelSale(); // Consume once, before any callbacks.
    FInventoryItem Item; Equipment->GetItemByGuid(Quote.ItemGuid, Item);
    const FGameplayTag PreviousSlot = Item.EquipmentSlot;
    if (Item.bIsEquipped && !RequestUnequip(Item.GetItemGuid()).bSuccess) return Publish(FEFEquipmentResult::Failure(TEXT("The item could not be unequipped.")));
    bool bTransferred = false; FEFEquipmentResult Receipt;
    {
        TGuardValue<bool> Operation(bOperation, true);
        auto* Wallet = Vendor->GetPawnCurrencyComponent(Cast<APawn>(GetOwner()));
        auto* VendorWallet = Vendor->VendorUsesCurrency() ? Vendor->GetVendorCurrencyComp() : nullptr;
        const float BuyerBefore = Wallet->GetCurrentCurrencyAmount();
        const float VendorBefore = VendorWallet ? VendorWallet->GetCurrentCurrencyAmount() : 0;
        const float Price = static_cast<float>(Quote.TotalPrice);
        // The adapter publishes inventory notifications only after this settlement
        // succeeds. A rejected settlement restores balances and both item lists.
        bTransferred = EFEquipmentACF::Transfer(Equipment, Vendor, Quote.ItemGuid, Quote.Count, false, Error, &Receipt,
            [&](FString& Failure)
            {
                if (VendorWallet) VendorWallet->RemoveCurrency(Price);
                Wallet->AddCurrency(Price);
                bool bSettled = Wallet->GetCurrentCurrencyAmount() == BuyerBefore + Price
                    && (!VendorWallet || VendorWallet->GetCurrentCurrencyAmount() == VendorBefore - Price);
#if WITH_DEV_AUTOMATION_TESTS
                if (TestFailurePoint == 3) { TestFailurePoint = 0; bSettled = false; }
#endif
                if (!bSettled)
                {
                    Wallet->SetCurrency(BuyerBefore);
                    if (VendorWallet) VendorWallet->SetCurrency(VendorBefore);
                    const bool bRecovered = Wallet->GetCurrentCurrencyAmount() == BuyerBefore
                        && (!VendorWallet || VendorWallet->GetCurrentCurrencyAmount() == VendorBefore);
                    Failure = bRecovered ? TEXT("The sale could not settle. Original balances were restored.")
                        : TEXT("The sale failed and wallet recovery is incomplete. Item quantities were retained.");
                }
                return bSettled;
            });
        if (bTransferred)
        {
            if (auto* Manager = Vendor->GetItemsManager()) Manager->OnItemSold.Broadcast(Item);
        }
    }
    if (!bTransferred && PreviousSlot.IsValid()) RequestEquip(Quote.ItemGuid, PreviousSlot);
    RefreshState(); return Publish(bTransferred ? Receipt : FEFEquipmentResult::Failure(Error));
}

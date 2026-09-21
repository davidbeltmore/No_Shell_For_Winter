#include "EFEquipmentCoordinatorComponent.h"
#include "EFEquipmentACFAdapter.h"
#include "EFEquipmentData.h"
#include "EFEquipmentItems.h"
#include "EFContainerInventoryComponent.h"
#include "ACFVendorComponent.h"
#include "ACFItemsManagerComponent.h"
#include "GameFramework/Pawn.h"
#include "Misc/SecureHash.h"
#include "UObject/StrongObjectPtr.h"

bool UEFEquipmentCoordinatorComponent::BuildPurchase(UACFVendorComponent* Vendor, FGuid Guid, int32 Count,
    FEFEquipmentSaleQuote& Quote, FString& Signature, FString& Error)
{
    auto* Pawn = Cast<APawn>(GetOwner()); FInventoryItem Root;
    if (!IsValid(Vendor) || !Pawn || !Vendor->GetItemByGuid(Guid, Root) || !Root.Item || Count <= 0 || Count > Root.Count
        || !Vendor->GetPawnCurrencyComponent(Pawn))
    { Error = TEXT("The vendor, item, quantity or wallet is unavailable."); return false; }
    if (Count < Root.Count && !Root.Item->Fragments.IsEmpty()) { Error = TEXT("Buy this stateful stack as a whole."); return false; }
    auto Request = Root; Request.Count = Count;
    if (!Vendor->CanPawnBuyItems(Request, Pawn)) { Error = TEXT("The vendor cannot complete this purchase."); return false; }
    Quote.bPurchase = true; Quote.ItemGuid = Guid; Quote.Count = Count;
    Signature = Guid.ToString() + FString::Printf(TEXT("|%d|%d|%s"), Root.Count, Count, *Root.ItemClass->GetPathName());
    double AddedWeight = 0;
    auto Append = [&](const FInventoryItem& Item, int32 Quantity)
    {
        if (!Item.Item || !Item.ItemClass)
        { Error = TEXT("This set contains an unavailable item definition."); return false; }
        const auto Info = Item.ItemClass->GetDefaultObject<UACFItem>()->GetItemInfo();
        const double Price = static_cast<double>(Info.CurrencyValue) * Quantity * Vendor->GetVendorPriceMultiplierOnSell();
        const double Weight = static_cast<double>(Item.Item->GetItemInfo().ItemWeight) * Quantity;
        if (!FMath::IsFinite(Price) || Price < 0 || !FMath::IsFinite(Weight) || Weight < 0)
        { Error = TEXT("The item's trade price or weight is invalid."); return false; }
        Quote.TotalPrice += Price; AddedWeight += Weight;
        Quote.Lines.Add(FText::FromString(FString::Printf(TEXT("%s x %d: %.2f"), *Item.Item->GetItemName().ToString(), Quantity, Price)));
        Signature += TEXT("|") + Item.GetItemGuid().ToString() + FString::Printf(TEXT(":%d:%.9g"), Quantity, Price); return true;
    };
    if (!Append(Root, Count)) return false;
    if (auto* Rules = UEFEquipmentRulesFragment::Find(Root.Item); Rules && Rules->Profile && Rules->Profile->bContainer)
    {
        if (Root.Count != 1 || Count != 1 || !Rules->FlushContainer(Error)) return false;
        TStrongObjectPtr<UEFContainerInventoryComponent> Inspection(NewObject<UEFContainerInventoryComponent>(GetOwner()));
        Inspection->InitializeContainer(Guid, Rules->Profile->ContentCapacity);
        if (!Inspection->ReadPayload(Rules->ContainerPayload, Error)) return false;
        for (const auto& Child : Inspection->GetInventory()) if (!Append(Child, Child.Count)) return false;
        FSHAHash Hash; FSHA1::HashBuffer(Rules->ContainerPayload.GetData(), Rules->ContainerPayload.Num(), Hash.Hash); Signature += TEXT("|") + Hash.ToString();
        Inspection->DestroyComponent();
    }
    auto* Wallet = Vendor->GetPawnCurrencyComponent(Pawn);
    auto* VendorWallet = Vendor->VendorUsesCurrency() ? Vendor->GetVendorCurrencyComp() : nullptr;
    const float Price = static_cast<float>(Quote.TotalPrice);
    if (!FMath::IsFinite(Price) || !FMath::IsFinite(Wallet->GetCurrentCurrencyAmount()) || Wallet->GetCurrentCurrencyAmount() < Price
        || (Vendor->VendorUsesCurrency() && (!VendorWallet || VendorWallet == Wallet || !FMath::IsFinite(VendorWallet->GetCurrentCurrencyAmount() + Price))))
    { Error = TEXT("You cannot afford this item and all its contents."); return false; }
    if (Equipment->GetAvailableSlots() < 1 || (AddedWeight > 0 && GetWeightSummary().EffectiveWeight + AddedWeight > BaseCarryCapacity + 0.000001))
    { Error = TEXT("This item and its contents would exceed your inventory capacity."); return false; }
    Quote.bValid = true; return true;
}

FEFEquipmentSaleQuote UEFEquipmentCoordinatorComponent::PreviewPurchase(UACFVendorComponent* Vendor, FGuid Guid, int32 Count)
{
    CancelSale(); FEFEquipmentSaleQuote Quote; Quote.bPurchase = true; FString Error, Signature;
    if (bOperation || !InitializeEquipment() || !GetOwner()->HasAuthority()) Error = TEXT("Equipment is busy or unavailable.");
    else BuildPurchase(Vendor, Guid, Count, Quote, Signature, Error);
    if (!Quote.bValid) { Quote.Message = FText::FromString(Error); LastMessage = Quote.Message; OnStateChanged.Broadcast(); return Quote; }
    Quote.QuoteId = FGuid::NewGuid(); PendingSale = Quote; SaleVendor = Vendor; SaleSignature = Signature; return Quote;
}

FEFEquipmentResult UEFEquipmentCoordinatorComponent::ConfirmPurchase(FGuid QuoteId)
{
    if (bOperation || !GetOwner()->HasAuthority() || !PendingSale.bValid || !PendingSale.bPurchase || PendingSale.QuoteId != QuoteId)
        return Publish(FEFEquipmentResult::Failure(TEXT("This purchase confirmation is no longer valid.")));
    auto* Vendor = SaleVendor.Get(); FEFEquipmentSaleQuote Current; FString Error, Signature;
    if (!BuildPurchase(Vendor, PendingSale.ItemGuid, PendingSale.Count, Current, Signature, Error) || Signature != SaleSignature || Current.TotalPrice != PendingSale.TotalPrice)
    { CancelSale(); return Publish(FEFEquipmentResult::Failure(Error.IsEmpty() ? TEXT("The contents or price changed. Review the purchase again.") : Error)); }
    const auto Quote = PendingSale; CancelSale(); FInventoryItem Item; Vendor->GetItemByGuid(Quote.ItemGuid, Item);
    FEFEquipmentResult Receipt; bool Success;
    {
        TGuardValue<bool> Operation(bOperation, true);
        auto* Wallet = Vendor->GetPawnCurrencyComponent(Cast<APawn>(GetOwner())); auto* VendorWallet = Vendor->VendorUsesCurrency() ? Vendor->GetVendorCurrencyComp() : nullptr;
        const float Before = Wallet->GetCurrentCurrencyAmount(), VendorBefore = VendorWallet ? VendorWallet->GetCurrentCurrencyAmount() : 0;
        const float Price = static_cast<float>(Quote.TotalPrice);
        Success = EFEquipmentACF::Transfer(Vendor, Equipment, Quote.ItemGuid, Quote.Count, true, Error, &Receipt, [&](FString& Failure)
        {
            Wallet->RemoveCurrency(Price); if (VendorWallet) VendorWallet->AddCurrency(Price);
            bool Settled = Wallet->GetCurrentCurrencyAmount() == Before - Price && (!VendorWallet || VendorWallet->GetCurrentCurrencyAmount() == VendorBefore + Price);
#if WITH_DEV_AUTOMATION_TESTS
            if (TestFailurePoint == 4) { TestFailurePoint = 0; Settled = false; }
#endif
            if (!Settled)
            {
                Wallet->SetCurrency(Before); if (VendorWallet) VendorWallet->SetCurrency(VendorBefore);
                Failure = Wallet->GetCurrentCurrencyAmount() == Before && (!VendorWallet || VendorWallet->GetCurrentCurrencyAmount() == VendorBefore)
                    ? TEXT("The purchase could not settle. Original balances were restored.") : TEXT("The purchase failed and wallet recovery is incomplete. Item quantities were retained.");
            }
            return Settled;
        });
        if (Success) if (auto* Manager = Vendor->GetItemsManager()) Manager->OnItemPurchased.Broadcast(Item);
    }
    RefreshState(); return Publish(Success ? Receipt : FEFEquipmentResult::Failure(Error));
}

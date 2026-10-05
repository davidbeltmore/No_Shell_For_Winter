#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ProjectInnerStateGlyph.generated.h"

/** Tiny vector symbols: resolution independent, no texture loads or per-frame bindings. */
UCLASS()
class EFPROJECTSYSTEMSGAMEPLAY_API UProjectInnerStateGlyph : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") FName Symbol;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inner State") FLinearColor Tint=FLinearColor::White;
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<class SProjectInnerStateGlyph> Glyph;
};

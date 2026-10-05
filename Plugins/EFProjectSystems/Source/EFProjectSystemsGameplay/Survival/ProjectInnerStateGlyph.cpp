#include "Survival/ProjectInnerStateGlyph.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"

class SProjectInnerStateGlyph : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SProjectInnerStateGlyph) {} SLATE_END_ARGS()
    void Construct(const FArguments&) {}
    FName Symbol;
    FLinearColor Tint=FLinearColor::White;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(20,20); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,
        int32 Layer,const FWidgetStyle& Style,bool) const override
    {
        const FVector2D Size=G.GetLocalSize();
        const double Scale=FMath::Min(Size.X,Size.Y)/24.0;
        const FVector2D Offset=(Size-FVector2D(24,24)*Scale)*.5;
        auto Line=[&](std::initializer_list<FVector2D> Points)
        {
            TArray<FVector2D> P; for (const auto& V:Points) P.Add(Offset+V*Scale);
            FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Tint*Style.GetColorAndOpacityTint(),true,1.3f);
        };
        if (Symbol==TEXT("Stress")) Line({{14,2},{5,13},{11,13},{9,22},{19,10},{13,10},{14,2}});
        else if (Symbol==TEXT("Stimulation")) Line({{12,21},{3,12},{2,7},{5,3},{9,3},{12,6},{15,3},{19,3},{22,7},{21,12},{12,21}});
        else if (Symbol==TEXT("Hygiene")) { Line({{5,10},{17,10},{20,13},{20,19},{17,22},{5,22},{2,19},{2,13},{5,10}}); Line({{7,14},{15,14},{16,16},{15,18},{7,18}}); Line({{7,3},{9,2},{11,3},{11,5},{9,6},{7,5},{7,3}}); Line({{16,5},{18,4},{20,5},{20,7},{18,8},{16,7},{16,5}}); }
        else if (Symbol==TEXT("Bathroom")) { Line({{4,3},{9,3},{9,12},{21,12},{20,17},{17,19},{17,22},{9,22},{10,17},{6,14},{6,8},{4,8},{4,3}}); Line({{11,9},{21,9}}); }
        else if (Symbol==TEXT("Air")) { Line({{2,7},{15,7},{17,5},{16,2},{13,2},{12,4}}); Line({{2,12},{20,12},{22,10},{22,7},{20,6}}); Line({{2,17},{15,17},{17,19},{16,22},{13,22},{12,20}}); }
        else if (Symbol==TEXT("Composure")) { Line({{12,2},{21,6},{20,14},{17,19},{12,22},{7,19},{4,14},{3,6},{12,2}}); Line({{8,12},{11,15},{17,8}}); }
        else if (Symbol==TEXT("Sickness")) Line({{9,2},{15,2},{15,9},{22,9},{22,15},{15,15},{15,22},{9,22},{9,15},{2,15},{2,9},{9,9},{9,2}});
        else if (Symbol==TEXT("Poison")) { Line({{8,2},{16,2},{16,5},{14,5},{14,10},{21,20},{20,22},{4,22},{3,20},{10,10},{10,5},{8,5},{8,2}}); Line({{7,17},{17,17}}); }
        else if (Symbol==TEXT("Curse")) { Line({{16,2},{9,3},{4,8},{3,14},{6,20},{12,22},{18,20},{21,15},{15,17},{10,14},{9,9},{11,5},{16,2}}); }
        else { Line({{5,12},{19,12}}); }
        return Layer;
    }
};
TSharedRef<SWidget> UProjectInnerStateGlyph::RebuildWidget() { Glyph=SNew(SProjectInnerStateGlyph); SynchronizeProperties(); return Glyph.ToSharedRef(); }
void UProjectInnerStateGlyph::SynchronizeProperties() { Super::SynchronizeProperties(); if (Glyph) { Glyph->Symbol=Symbol; Glyph->Tint=Tint; Glyph->Invalidate(EInvalidateWidgetReason::Paint); } }
void UProjectInnerStateGlyph::ReleaseSlateResources(bool bReleaseChildren) { Super::ReleaseSlateResources(bReleaseChildren); Glyph.Reset(); }

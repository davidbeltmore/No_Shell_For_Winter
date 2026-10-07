#include "Survival/ProjectInnerStateGlyph.h"
#include "Survival/ProjectInnerStateMath.h"
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
        if (Symbol==TEXT("Stress"))
        {
            // A controlled circle yields at one point: deformed rim and two separated crack edges.
            Line({{20.5,9},{21,12},{20.7,14.3},{19.8,16.5},{18.4,18.4},{16.5,19.8},
                {14.3,20.7},{12,21},{9.7,20.7},{7.5,19.8},{5.6,18.4},{4.2,16.5},
                {3.3,14.3},{3,12},{3.3,9.7},{4.2,7.5},{5.6,5.6},{7.5,4.2},
                {9.7,3.3},{12,3},{14.3,3.3},{16,4.8},{14.8,6.7},{17,8.3}});
            Line({{20.5,9},{22,6.5},{19.7,5.2},{20.5,3.4}});
        }
        else if (Symbol==TEXT("Stimulation") || Symbol==TEXT("StimulationHappy") || Symbol==TEXT("StimulationSad"))
        {
            Line({{12,2},{15.8,2.8},{19.1,4.9},{21.2,8.2},{22,12},{21.2,15.8},
                {19.1,19.1},{15.8,21.2},{12,22},{8.2,21.2},{4.9,19.1},{2.8,15.8},
                {2,12},{2.8,8.2},{4.9,4.9},{8.2,2.8},{12,2}});
            Line({{8,8},{8,10}}); Line({{16,8},{16,10}});
            if (Symbol==TEXT("StimulationHappy")) Line({{7,14},{9,16.5},{12,17.5},{15,16.5},{17,14}});
            else if (Symbol==TEXT("StimulationSad")) Line({{7,17},{9,14.5},{12,13.5},{15,14.5},{17,17}});
            else Line({{7,15.5},{17,15.5}});
        }
        else if (Symbol==TEXT("Hygiene")) { Line({{5,10},{17,10},{20,13},{20,19},{17,22},{5,22},{2,19},{2,13},{5,10}}); Line({{7,14},{15,14},{16,16},{15,18},{7,18}}); Line({{7,3},{9,2},{11,3},{11,5},{9,6},{7,5},{7,3}}); Line({{16,5},{18,4},{20,5},{20,7},{18,8},{16,7},{16,5}}); }
        else if (Symbol==TEXT("Bathroom")) { Line({{4,3},{9,3},{9,12},{21,12},{20,17},{17,19},{17,22},{9,22},{10,17},{6,14},{6,8},{4,8},{4,3}}); Line({{11,9},{21,9}}); }
        else if (Symbol==TEXT("Air"))
        {
            // Oxygen: two separate bubbles and a compact subscript 2, legible at HUD size.
            Line({{8.5,9},{11,9.5},{13.1,10.9},{14.5,13},{15,15.5},{14.5,18},
                {13.1,20.1},{11,21.5},{8.5,22},{6,21.5},{3.9,20.1},{2.5,18},
                {2,15.5},{2.5,13},{3.9,10.9},{6,9.5},{8.5,9}});
            Line({{8.5,12},{10.2,12.4},{11.5,13.5},{12,15}});
            Line({{17.5,2},{19.2,2.3},{20.7,3.3},{21.7,4.8},{22,6.5},{21.7,8.2},
                {20.7,9.7},{19.2,10.7},{17.5,11},{15.8,10.7},{14.3,9.7},
                {13.3,8.2},{13,6.5},{13.3,4.8},{14.3,3.3},{15.8,2.3},{17.5,2}});
            Line({{17.5,4.5},{18.7,4.9},{19.5,6}});
            Line({{17.5,15},{18.7,14},{20.5,14},{22,15.3},{22,16.7},
                {20.8,18.2},{17.5,22},{22,22}});
        }
        else if (Symbol==TEXT("Composure")) { Line({{12,2},{21,6},{20,14},{17,19},{12,22},{7,19},{4,14},{3,6},{12,2}}); Line({{8,12},{11,15},{17,8}}); }
        else if (Symbol==TEXT("Sickness")) Line({{9,2},{15,2},{15,9},{22,9},{22,15},{15,15},{15,22},{9,22},{9,15},{2,15},{2,9},{9,9},{9,2}});
        else if (Symbol==TEXT("Poison")) { Line({{8,2},{16,2},{16,5},{14,5},{14,10},{21,20},{20,22},{4,22},{3,20},{10,10},{10,5},{8,5},{8,2}}); Line({{7,17},{17,17}}); }
        else if (Symbol==TEXT("Curse")) { Line({{16,2},{9,3},{4,8},{3,14},{6,20},{12,22},{18,20},{21,15},{15,17},{10,14},{9,9},{11,5},{16,2}}); }
        else { Line({{5,12},{19,12}}); }
        return Layer;
    }
};
FName UProjectInnerStateGlyph::ResolveStimulationSymbol(double Value)
{
    const int32 Mood=FProjectInnerStateMath::GetStimulationMoodBand(Value);
    return Mood>0?FName(TEXT("StimulationHappy")):Mood<0?FName(TEXT("StimulationSad")):FName(TEXT("Stimulation"));
}
TSharedRef<SWidget> UProjectInnerStateGlyph::RebuildWidget() { Glyph=SNew(SProjectInnerStateGlyph); SynchronizeProperties(); return Glyph.ToSharedRef(); }
void UProjectInnerStateGlyph::SynchronizeProperties() { Super::SynchronizeProperties(); if (Glyph) { Glyph->Symbol=Symbol; Glyph->Tint=Tint; Glyph->Invalidate(EInvalidateWidgetReason::Paint); } }
void UProjectInnerStateGlyph::ReleaseSlateResources(bool bReleaseChildren) { Super::ReleaseSlateResources(bReleaseChildren); Glyph.Reset(); }

#include "Survival/ProjectInnerStateMath.h"

namespace
{
    constexpr double Epsilon = 1.e-9;
    bool PositiveFinite(double Value) { return FMath::IsFinite(Value) && Value > 0.0; }
    double NonNegative(double Value) { return FMath::IsFinite(Value) ? FMath::Max(0.0, Value) : 0.0; }
    int32 SpeedIndex(EProjectPoisonSpeed Speed) { return FMath::Clamp(static_cast<int32>(Speed), 0, 2); }
}

double FProjectPoisonTuning::Delay(EProjectPoisonSpeed Speed) const
{
    return NonNegative(Speed == EProjectPoisonSpeed::Fast ? FastDelay : Speed == EProjectPoisonSpeed::Slow ? SlowDelay : NormalDelay);
}
double FProjectPoisonTuning::Rate(EProjectPoisonSpeed Speed) const
{
    return NonNegative(Speed == EProjectPoisonSpeed::Fast ? FastRate : Speed == EProjectPoisonSpeed::Slow ? SlowRate : NormalRate);
}

bool FProjectPoisonLedger::AddDose(FGuid ExposureId, FName Toxin, EProjectPoisonSpeed Speed,
    double Points, double MaxHealth, const FProjectPoisonTuning& Tuning)
{
    if (!ExposureId.IsValid() || RecentExposureIds.Contains(ExposureId) || !PositiveFinite(Points)
        || !PositiveFinite(MaxHealth) || static_cast<uint8>(Speed) > static_cast<uint8>(EProjectPoisonSpeed::Slow)) return false;
    const double Damage = (Points / 100.0) * MaxHealth;
    if (!PositiveFinite(Damage) || !FMath::IsFinite(GetPendingDamage() + Damage)
        || !FMath::IsFinite(GetPendingPoints() + Points)) return false;
    auto& Dose = Doses.AddDefaulted_GetRef();
    Dose.Toxin = Toxin; Dose.Speed = Speed; Dose.RemainingPoints = Points;
    Dose.ReferenceMaxHealth = MaxHealth; Dose.ActivateAtSeconds = ElapsedSeconds + Tuning.Delay(Speed);
    RecentExposureIds.Add(ExposureId);
    if (RecentExposureIds.Num() > 256) RecentExposureIds.RemoveAt(0, RecentExposureIds.Num() - 256, EAllowShrinking::No);
    if (GetPendingPoints() >= NonNegative(Tuning.ImmediateThreshold))
        for (auto& Entry : Doses) Entry.ActivateAtSeconds = FMath::Min(Entry.ActivateAtSeconds, ElapsedSeconds);
    return true;
}

double FProjectPoisonLedger::GetPendingPoints() const
{
    double Result = 0.0;
    for (const auto& Dose : Doses) Result += Dose.RemainingPoints;
    return Result;
}
double FProjectPoisonLedger::GetPendingDamage() const
{
    double Result = 0.0;
    for (const auto& Dose : Doses) Result += (Dose.RemainingPoints / 100.0) * Dose.ReferenceMaxHealth;
    return Result;
}
bool FProjectPoisonLedger::HasActiveDose() const
{
    for (const auto& Dose : Doses)
        if (Dose.RemainingPoints > 0.0 && Dose.ActivateAtSeconds <= ElapsedSeconds + Epsilon) return true;
    return false;
}

double FProjectPoisonLedger::Advance(double Seconds, const FProjectPoisonTuning& Tuning)
{
    if (!PositiveFinite(Seconds) || !FMath::IsFinite(ElapsedSeconds + Seconds)) return 0.0;
    const double End = ElapsedSeconds + Seconds;
    double Damage = 0.0;
    // Integrate exactly between activation/depletion boundaries, independent of caller frame rate.
    while (ElapsedSeconds < End && !Doses.IsEmpty())
    {
        double Active[3] = {};
        double Rates[3] = {};
        double Step = End - ElapsedSeconds;
        for (const auto& Dose : Doses)
        {
            if (Dose.ActivateAtSeconds <= ElapsedSeconds + Epsilon) Active[SpeedIndex(Dose.Speed)] += Dose.RemainingPoints;
            else Step = FMath::Min(Step, Dose.ActivateAtSeconds - ElapsedSeconds);
        }
        double TotalRate = 0.0;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Rates[Index] = Active[Index] > 0.0 ? Tuning.Rate(static_cast<EProjectPoisonSpeed>(Index)) : 0.0;
            TotalRate += Rates[Index];
        }
        const double Scale = TotalRate > 0.0 ? FMath::Min(1.0, NonNegative(Tuning.CombinedRateLimit) / TotalRate) : 0.0;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Rates[Index] *= Scale;
            if (Rates[Index] > 0.0) Step = FMath::Min(Step, Active[Index] / Rates[Index]);
        }
        if (Step <= 0.0 || ElapsedSeconds + Step == ElapsedSeconds) break;
        for (auto& Dose : Doses)
        {
            const int32 Index = SpeedIndex(Dose.Speed);
            if (Dose.ActivateAtSeconds > ElapsedSeconds + Epsilon || Active[Index] <= 0.0) continue;
            const double Fraction = FMath::Min(1.0, Rates[Index] * Step / Active[Index]);
            const double Consumed = Dose.RemainingPoints * Fraction;
            Damage += (Consumed / 100.0) * Dose.ReferenceMaxHealth;
            Dose.RemainingPoints -= Consumed;
        }
        ElapsedSeconds += Step;
        Doses.RemoveAll([](const FProjectPoisonDose& Dose) { return Dose.RemainingPoints <= 0.0; });
    }
    ElapsedSeconds = End;
    return Damage;
}

double FProjectPoisonLedger::Neutralize(FName Toxin)
{
    double Removed = 0.0;
    Doses.RemoveAll([&](const FProjectPoisonDose& Dose)
    {
        if (!Toxin.IsNone() && Dose.Toxin != Toxin) return false;
        Removed += (Dose.RemainingPoints / 100.0) * Dose.ReferenceMaxHealth;
        return true;
    });
    return Removed;
}

void FProjectPoisonLedger::Sanitize()
{
    ElapsedSeconds = NonNegative(ElapsedSeconds);
    Doses.RemoveAll([](const FProjectPoisonDose& Dose)
    {
        return !PositiveFinite(Dose.RemainingPoints) || !PositiveFinite(Dose.ReferenceMaxHealth)
            || !FMath::IsFinite(Dose.ActivateAtSeconds)
            || !FMath::IsFinite((Dose.RemainingPoints / 100.0) * Dose.ReferenceMaxHealth)
            || static_cast<uint8>(Dose.Speed) > static_cast<uint8>(EProjectPoisonSpeed::Slow);
    });
    if (RecentExposureIds.Num() > 256) RecentExposureIds.RemoveAt(0, RecentExposureIds.Num() - 256);
}

double FProjectInnerStateMath::ApproachHalfLife(double Current, double Target, double Elapsed, double HalfLife)
{
    if (!FMath::IsFinite(Current) || !FMath::IsFinite(Target)) return 0.0;
    if (!PositiveFinite(Elapsed)) return Current;
    return PositiveFinite(HalfLife) ? Target + (Current - Target) * FMath::Exp2(-Elapsed / HalfLife) : Target;
}
double FProjectInnerStateMath::IntegrateWellbeing(double Current, double NetPointsPerHour, double Hours, double HalfLifeHours)
{
    if (!FMath::IsFinite(Current) || !FMath::IsFinite(NetPointsPerHour)) return 0.0;
    if (!PositiveFinite(Hours)) return FMath::Clamp(Current, -100.0, 100.0);
    const double Rate = FMath::Clamp(NetPointsPerHour, -1.5, 1.5);
    if (!PositiveFinite(HalfLifeHours)) return FMath::Clamp(Current + Rate * Hours, -100.0, 100.0);
    const double Equilibrium = Rate * HalfLifeHours / FMath::Loge(2.0);
    return FMath::Clamp(ApproachHalfLife(Current, Equilibrium, Hours, HalfLifeHours), -100.0, 100.0);
}
double FProjectInnerStateMath::MoveTowards(double Current, double Target, double MaximumDelta)
{
    if (!FMath::IsFinite(Current) || !FMath::IsFinite(Target)) return 0.0;
    return Current + FMath::Clamp(Target - Current, -NonNegative(MaximumDelta), NonNegative(MaximumDelta));
}
double FProjectInnerStateMath::SeverityBelow(double Value, double Threshold)
{
    return FMath::IsFinite(Value) && PositiveFinite(Threshold) ? FMath::Clamp((Threshold - Value) / Threshold, 0.0, 1.0) : 0.0;
}
double FProjectInnerStateMath::SeverityAbove(double Value, double Threshold, double Maximum)
{
    return FMath::IsFinite(Value) && FMath::IsFinite(Threshold) && FMath::IsFinite(Maximum) && Maximum > Threshold
        ? FMath::Clamp((Value - Threshold) / (Maximum - Threshold), 0.0, 1.0) : 0.0;
}
double FProjectInnerStateMath::ExposureProbability(double HazardPerHour, double Hours)
{
    return 1.0 - FMath::Exp(-NonNegative(HazardPerHour) * NonNegative(Hours));
}
double FProjectInnerStateMath::AdvanceAwareness(double Current, double Capacity, double LossPerSecond,
    double RecoveryPerSecond, double CapacityFallPerSecond, double Seconds, bool bAllowRecovery)
{
    Current = FMath::Clamp(NonNegative(Current), 0.0, 100.0);
    Capacity = FMath::Clamp(NonNegative(Capacity), 0.0, 100.0);
    if (!PositiveFinite(Seconds)) return Current;
    if (PositiveFinite(LossPerSecond)) return FMath::Max(0.0, Current - LossPerSecond * Seconds);
    if (Current <= Capacity && !bAllowRecovery) return Current;
    const double Rate = Current > Capacity ? NonNegative(CapacityFallPerSecond) : NonNegative(RecoveryPerSecond);
    return MoveTowards(Current, Capacity, Rate * Seconds);
}

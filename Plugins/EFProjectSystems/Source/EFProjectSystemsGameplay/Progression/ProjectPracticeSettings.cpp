#include "Progression/ProjectPracticeSettings.h"

UProjectPracticeSettings::UProjectPracticeSettings()
{
    const auto Add = [this](const TCHAR* Id, const TCHAR* Attribute, const TCHAR* Name, EProjectPracticeMethod Method, double Hours)
    {
        auto& D = Activities.AddDefaulted_GetRef();
        D.Id = Id; D.Attribute = Attribute; D.Name = FText::FromString(Name); D.Method = Method; D.DurationHours = Hours;
        D.Description = FText::FromString(Name);
        D.Animation = FSoftObjectPath(TEXT("/Game/_Game/Animations/Emotes/Sit/Anim_KA_Idle53_Seiza_Loop1.Anim_KA_Idle53_Seiza_Loop1"));
    };
    Add(TEXT("PushUps"), TEXT("Strength"), TEXT("Physical conditioning"), EProjectPracticeMethod::Physical, 3.0);
    Add(TEXT("JumpRope"), TEXT("Endurance"), TEXT("Endurance practice"), EProjectPracticeMethod::Physical, 3.0);
    Add(TEXT("ReadBook"), TEXT("Intelligence"), TEXT("Read a book"), EProjectPracticeMethod::Book, 48.0);
    Add(TEXT("Meditate"), TEXT("Willpower"), TEXT("Meditate"), EProjectPracticeMethod::Continuous, 0.0);
    Add(TEXT("Pray"), TEXT("Faith"), TEXT("Pray"), EProjectPracticeMethod::Continuous, 0.0);
    Add(TEXT("PracticeLock"), TEXT("Cunning"), TEXT("Practice a lock"), EProjectPracticeMethod::Lock, 0.0);
    Add(TEXT("RehearseSpeech"), TEXT("Charisma"), TEXT("Rehearse a conversation"), EProjectPracticeMethod::Continuous, 0.0);
    Add(TEXT("Balance"), TEXT("Dexterity"), TEXT("Balance practice"), EProjectPracticeMethod::Physical, 1.0);
    Add(TEXT("Constitution"), TEXT("Constitution"), TEXT("Gentle conditioning"), EProjectPracticeMethod::Physical, 2.0);
    Add(TEXT("Offensive"), TEXT("Offensive"), TEXT("Attack forms"), EProjectPracticeMethod::Physical, 1.5);
    Add(TEXT("Defensive"), TEXT("Defensive"), TEXT("Guard practice"), EProjectPracticeMethod::Physical, 1.5);
    Add(TEXT("Celerity"), TEXT("Celerity"), TEXT("Footwork"), EProjectPracticeMethod::Physical, 1.5);
    for (auto& D : Activities)
    {
        if (D.Method == EProjectPracticeMethod::Physical)
            D.Animation = FSoftObjectPath(TEXT("/Game/_Game/Animations/Emotes/General/Anim_KA_Idle05_Stretch.Anim_KA_Idle05_Stretch"));
        if (D.Id == TEXT("RehearseSpeech"))
            D.Animation = FSoftObjectPath(TEXT("/Game/_Game/Animations/Emotes/Sit/Anim_KA_Sit_Speak03_Loop.Anim_KA_Sit_Speak03_Loop"));
        if (D.Id == TEXT("Offensive") || D.Id == TEXT("Defensive"))
            D.Animation = FSoftObjectPath(TEXT("/Game/_Game/Animations/Emotes/General/Anim_KA_Combat_BareHands_Idle01.Anim_KA_Combat_BareHands_Idle01"));
    }
}

const FProjectPracticeDefinition* UProjectPracticeSettings::Find(FName Id) const
{
    return Activities.FindByPredicate([Id](const auto& D) { return D.Id == Id; });
}

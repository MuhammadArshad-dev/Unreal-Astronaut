#include "AstronautIdleAnimComponent.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"

DEFINE_LOG_CATEGORY_STATIC(LogAstronautIdle, Log, All);

UAstronautIdleAnimComponent::UAstronautIdleAnimComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAstronautIdleAnimComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        for (UActorComponent* Component : Owner->GetComponents())
        {
            USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Component);
            if (SkelMesh && SkelMesh->GetFName() == TEXT("Body"))
            {
                BodyMesh = SkelMesh;
                break;
            }
        }
    }

    if (!BodyMesh)
    {
        UE_LOG(LogAstronautIdle, Warning, TEXT("UAstronautIdleAnimComponent: no SkeletalMeshComponent named 'Body' found on owner"));
        return;
    }

    StartIdleLoop();
}

TArray<UAnimSequence*> UAstronautIdleAnimComponent::GetIdleClips() const
{
    TArray<UAnimSequence*> Clips;
    if (BreathingIdle) Clips.Add(BreathingIdle);
    if (NeutralIdle) Clips.Add(NeutralIdle);
    if (WeightShift) Clips.Add(WeightShift);
    return Clips;
}

void UAstronautIdleAnimComponent::StartIdleLoop()
{
    if (bIdleLoopActive)
    {
        return;
    }
    bIdleLoopActive = true;
    PlayNextIdleClip();
}

void UAstronautIdleAnimComponent::StopIdleLoop()
{
    bIdleLoopActive = false;
}

void UAstronautIdleAnimComponent::PlayNextIdleClip()
{
    if (!bIdleLoopActive || !BodyMesh)
    {
        return;
    }

    UAnimInstance* AnimInstance = BodyMesh->GetAnimInstance();
    if (!AnimInstance)
    {
        UE_LOG(LogAstronautIdle, Warning, TEXT("UAstronautIdleAnimComponent: Body has no AnimInstance (missing/uncompiled Anim Blueprint)"));
        return;
    }

    TArray<UAnimSequence*> Clips = GetIdleClips();
    if (Clips.Num() == 0)
    {
        UE_LOG(LogAstronautIdle, Warning, TEXT("UAstronautIdleAnimComponent: no idle AnimSequences assigned"));
        return;
    }

    int32 NextIndex = FMath::RandRange(0, Clips.Num() - 1);
    if (Clips.Num() > 1)
    {
        while (NextIndex == LastPlayedIndex)
        {
            NextIndex = FMath::RandRange(0, Clips.Num() - 1);
        }
    }
    LastPlayedIndex = NextIndex;

    UAnimSequence* NextClip = Clips[NextIndex];
    UAnimMontage* PlayingMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
        NextClip, SlotName, BlendTime, BlendTime, 1.0f, 1);

    if (!PlayingMontage)
    {
        UE_LOG(LogAstronautIdle, Warning, TEXT("UAstronautIdleAnimComponent: PlaySlotAnimationAsDynamicMontage failed for '%s' — is the '%s' Slot node wired to Output Pose in the Body Anim Blueprint?"),
            *NextClip->GetName(), *SlotName.ToString());
        return;
    }

    FOnMontageEnded EndDelegate;
    EndDelegate.BindUObject(this, &UAstronautIdleAnimComponent::HandleIdleMontageEnded);
    AnimInstance->Montage_SetEndDelegate(EndDelegate, PlayingMontage);
}

void UAstronautIdleAnimComponent::HandleIdleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (bInterrupted)
    {
        return;
    }
    PlayNextIdleClip();
}

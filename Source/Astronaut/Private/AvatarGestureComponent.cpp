#include "AvatarGestureComponent.h"
#include "CompanionServiceSubsystem.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

DEFINE_LOG_CATEGORY_STATIC(LogAvatarGesture, Log, All);

UAvatarGestureComponent::UAvatarGestureComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAvatarGestureComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UCompanionServiceSubsystem* CompanionService = UCompanionServiceSubsystem::GetCompanionService(this))
    {
        CompanionService->OnGestureReceived.AddDynamic(this, &UAvatarGestureComponent::HandleGestureReceived);
        UE_LOG(LogAvatarGesture, Log, TEXT("UAvatarGestureComponent subscribed to OnGestureReceived"));
    }
}

void UAvatarGestureComponent::HandleGestureReceived(const FString& Tag, int32 SentenceIndex)
{
    UE_LOG(LogAvatarGesture, Log, TEXT("Gesture received: tag='%s', sentence_index=%d"), *Tag, SentenceIndex);
    PlayGesture(Tag);
}

void UAvatarGestureComponent::PlayGesture(const FString& Tag)
{
    UAnimMontage* MontageToPlay = nullptr;

    if (Tag == TEXT("point_up"))
    {
        MontageToPlay = PointUpMontage;
    }
    else if (Tag == TEXT("wave"))
    {
        MontageToPlay = WaveMontage;
    }
    else if (Tag == TEXT("shrug"))
    {
        MontageToPlay = ShrugMontage;
    }
    else if (Tag == TEXT("explain"))
    {
        MontageToPlay = ExplainMontage;
    }
    else if (Tag == TEXT("nod"))
    {
        MontageToPlay = NodMontage;
    }

    if (!MontageToPlay)
    {
        UE_LOG(LogAvatarGesture, Warning, TEXT("No AnimMontage assigned for gesture tag '%s'"), *Tag);
        return;
    }

    USkeletalMeshComponent* SkeletalMeshComp = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
    if (SkeletalMeshComp && SkeletalMeshComp->GetAnimInstance())
    {
        SkeletalMeshComp->GetAnimInstance()->Montage_Play(MontageToPlay);
        UE_LOG(LogAvatarGesture, Log, TEXT("Playing gesture AnimMontage '%s' for tag '%s'"), *MontageToPlay->GetName(), *Tag);
    }
}

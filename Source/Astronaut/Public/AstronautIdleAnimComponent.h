#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AstronautIdleAnimComponent.generated.h"

class UAnimSequence;
class UAnimMontage;
class USkeletalMeshComponent;

/**
 * Keeps the character's Body mesh out of a single-clip idle loop by crossfading
 * between several idle AnimSequences in randomized (non-repeating) order via
 * PlaySlotAnimationAsDynamicMontage.
 *
 * Requires a Slot node (see SlotName) wired to Output Pose in the Body's Anim
 * Blueprint AnimGraph — PlaySlotAnimationAsDynamicMontage is a no-op without one.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ASTRONAUT_API UAstronautIdleAnimComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAstronautIdleAnimComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle Animation")
    TObjectPtr<UAnimSequence> BreathingIdle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle Animation")
    TObjectPtr<UAnimSequence> NeutralIdle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle Animation")
    TObjectPtr<UAnimSequence> WeightShift;

    /** Slot node name in the Body Anim Blueprint's AnimGraph these idle clips play through. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle Animation")
    FName SlotName = TEXT("DefaultSlot");

    /** Crossfade duration (seconds) blending from one idle clip into the next. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle Animation", meta = (ClampMin = "0.0"))
    float BlendTime = 0.6f;

    /** Starts (or no-ops if already running) the randomized idle loop. */
    UFUNCTION(BlueprintCallable, Category = "Idle Animation")
    void StartIdleLoop();

    /** Stops chaining further idle clips. Does not cut off whatever is currently mid-playback. */
    UFUNCTION(BlueprintCallable, Category = "Idle Animation")
    void StopIdleLoop();

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> BodyMesh = nullptr;

    bool bIdleLoopActive = false;
    int32 LastPlayedIndex = INDEX_NONE;

    TArray<UAnimSequence*> GetIdleClips() const;
    void PlayNextIdleClip();

    UFUNCTION()
    void HandleIdleMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};

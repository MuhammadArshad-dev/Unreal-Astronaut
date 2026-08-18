#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AvatarGestureComponent.generated.h"

class UAnimMontage;

/**
 * Listens for gesture tags broadcast by UCompanionServiceSubsystem
 * ("point_up", "wave", "shrug", "explain", "nod") and plays corresponding
 * AnimMontages on the character's skeletal mesh.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ASTRONAUT_API UAvatarGestureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAvatarGestureComponent();

    /** Montages for each recognized gesture tag. Assign in Editor details panel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> PointUpMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> WaveMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> ShrugMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> ExplainMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> NodMontage;

    /** Plays the gesture animation for the given tag string. */
    UFUNCTION(BlueprintCallable, Category = "Gestures")
    void PlayGesture(const FString& Tag);

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void HandleGestureReceived(const FString& Tag, int32 SentenceIndex);
};

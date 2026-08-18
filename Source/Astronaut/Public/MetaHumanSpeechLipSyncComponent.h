#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MetaHumanSpeechLipSyncComponent.generated.h"

class ISpeechAnimationSolver;
class ILiveLinkSource;

/**
 * Drives a MetaHuman's face at runtime from streamed PCM audio, using
 * Epic's own StreamingADA speech-to-curve solver plus a self-registered
 * LiveLink source. This is the same curve set (CTRL_expressions_*) and the
 * same LiveLinkPose node that ABP_Face already reads for Live Link Face —
 * the MetaHuman character's LiveLinkSubject property just needs to match
 * SubjectName below, with UseLiveLink=true and UseARKit=false.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ASTRONAUT_API UMetaHumanSpeechLipSyncComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMetaHumanSpeechLipSyncComponent();

    /** Must match the LiveLinkSubject name set on the MetaHuman character. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lip Sync")
    FName SubjectName = TEXT("AstronautSpeech");

    /**
     * Feeds one chunk of 16-bit signed PCM audio for real-time viseme
     * solving. Call progressively during playback (e.g. every ~20ms), not
     * all at once — the solver's curve output reflects only the most
     * recently solved frame, so timing here is what makes it "lip sync"
     * rather than a single static pose.
     */
    UFUNCTION(BlueprintCallable, Category = "Lip Sync")
    void FeedAudioSamples(const TArray<uint8>& PCM16Data, int32 SampleRate, int32 NumChannels);

    /**
     * Sets the idle facial expression state ("idle", "listening", "speaking").
     * Drives brow/mouth curves through the same LiveLink pipeline as lip sync
     * (the only reliable way to move this MetaHuman's face without editing
     * ABP_Face's AnimGraph directly). A periodic idle timer also blinks the
     * character regardless of state, so it never looks frozen/glassy-eyed.
     */
    UFUNCTION(BlueprintCallable, Category = "Lip Sync")
    void SetExpressionState(const FString& State);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    TSharedPtr<ISpeechAnimationSolver> Solver;
    TSharedPtr<ILiveLinkSource> LiveLinkSource;
    FGuid SourceGuid;
    bool bStaticDataPushed = false;

    FString CurrentExpressionState = TEXT("idle");
    FTimerHandle IdleExpressionTimerHandle;
    void IdleExpressionTick();
    int32 FindCurveIndex(const FString& CurveName) const;

    void PushStaticDataIfNeeded();
};

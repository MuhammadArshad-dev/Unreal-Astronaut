#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "MicCaptureComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioBufferCaptured, const TArray<uint8>&, PCMData);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ASTRONAUT_API UMicCaptureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMicCaptureComponent();

    UFUNCTION(BlueprintCallable, Category = "Mic Capture")
    void StartCapture();

    UFUNCTION(BlueprintCallable, Category = "Mic Capture")
    void StopCapture();

    UPROPERTY(BlueprintAssignable)
    FOnAudioBufferCaptured OnAudioBufferCaptured;

private:
    TUniquePtr<Audio::FAudioCapture> AudioCapture;

    // Resample state, carried across capture callbacks so the linear
    // interpolation is continuous at buffer boundaries instead of clicking.
    double ResampleCarryPhase = 0.0;
    float LastMonoSample = 0.0f;
    bool bHasLastMonoSample = false;

    void OnAudioCaptureCallback(const void* AudioData, int32 NumFrames,
        int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow);

    // Downmixes to mono and resamples to TargetSampleRate (16kHz, what
    // Deepgram Nova-3 / the companion service's protocol expects), producing
    // signed 16-bit PCM bytes.
    TArray<uint8> DownmixAndResampleTo16kMono(const float* FloatData, int32 NumFrames,
        int32 NumChannels, int32 SampleRate);

    static constexpr int32 TargetSampleRate = 16000;
};

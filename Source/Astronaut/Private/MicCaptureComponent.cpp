#include "MicCaptureComponent.h"
#include "Async/Async.h"

UMicCaptureComponent::UMicCaptureComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    AudioCapture = MakeUnique<Audio::FAudioCapture>();
}

void UMicCaptureComponent::StartCapture()
{
    Audio::FAudioCaptureDeviceParams Params;
    // Deliberately left at defaults (device-native rate/channel count).
    // Forcing SampleRate=16000/NumInputChannels=1 here crashed inside
    // CoreAudio's RtApiCore::startStream() on macOS — most hardware doesn't
    // natively run at 16kHz and the RtAudio backend doesn't negotiate that
    // gracefully. DownmixAndResampleTo16kMono() already converts whatever
    // native format the device hands back, so no request is needed here.

    ResampleCarryPhase = 0.0;
    bHasLastMonoSample = false;

    AudioCapture->OpenAudioCaptureStream(Params,
        [this](const void* AudioData, int32 NumFrames, int32 NumChannels,
               int32 SampleRate, double StreamTime, bool bOverflow)
        {
            OnAudioCaptureCallback(AudioData, NumFrames, NumChannels, SampleRate, StreamTime, bOverflow);
        }, 1024);

    AudioCapture->StartStream();
}

void UMicCaptureComponent::StopCapture()
{
    if (AudioCapture.IsValid())
    {
        AudioCapture->StopStream();
        AudioCapture->CloseStream();
    }
}

void UMicCaptureComponent::OnAudioCaptureCallback(const void* AudioData, int32 NumFrames,
    int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow)
{
    const float* FloatData = static_cast<const float*>(AudioData);
    TArray<uint8> PCMBytes = DownmixAndResampleTo16kMono(FloatData, NumFrames, NumChannels, SampleRate);

    AsyncTask(ENamedThreads::GameThread, [this, PCMBytes]()
    {
        OnAudioBufferCaptured.Broadcast(PCMBytes);
    });
}

TArray<uint8> UMicCaptureComponent::DownmixAndResampleTo16kMono(const float* FloatData, int32 NumFrames,
    int32 NumChannels, int32 SampleRate)
{
    // Downmix interleaved input to mono.
    TArray<float> Mono;
    Mono.SetNumUninitialized(NumFrames);
    for (int32 Frame = 0; Frame < NumFrames; ++Frame)
    {
        float Sum = 0.0f;
        for (int32 Ch = 0; Ch < NumChannels; ++Ch)
        {
            Sum += FloatData[Frame * NumChannels + Ch];
        }
        Mono[Frame] = Sum / static_cast<float>(FMath::Max(NumChannels, 1));
    }

    // Already at the target rate (and reset carry state so a later device
    // rate change can't leave us with a stale phase from a different ratio).
    if (SampleRate == TargetSampleRate)
    {
        ResampleCarryPhase = 0.0;
        bHasLastMonoSample = NumFrames > 0;
        if (bHasLastMonoSample) LastMonoSample = Mono[NumFrames - 1];

        TArray<uint8> PCMBytes;
        PCMBytes.Reserve(NumFrames * sizeof(int16));
        for (int32 i = 0; i < NumFrames; ++i)
        {
            int16 Sample = static_cast<int16>(FMath::Clamp(Mono[i], -1.0f, 1.0f) * 32767.0f);
            PCMBytes.Append(reinterpret_cast<const uint8*>(&Sample), sizeof(int16));
        }
        return PCMBytes;
    }

    // Linear-interpolation resample down/up to 16kHz. Position 0 in this
    // buffer's local index space lines up with Mono[0]; a position of -1
    // means "one source sample before this buffer", satisfied by
    // LastMonoSample so interpolation is continuous across callbacks
    // instead of clicking at buffer boundaries.
    const double Step = static_cast<double>(SampleRate) / static_cast<double>(TargetSampleRate);
    TArray<uint8> PCMBytes;
    PCMBytes.Reserve(static_cast<int32>(NumFrames / Step) + 2);

    double Pos = ResampleCarryPhase;
    while (Pos < NumFrames && NumFrames > 0)
    {
        const int32 IndexFloor = FMath::FloorToInt(Pos);
        const float Frac = static_cast<float>(Pos - IndexFloor);

        const float SampleAtFloor = (IndexFloor < 0)
            ? (bHasLastMonoSample ? LastMonoSample : Mono[0])
            : Mono[FMath::Clamp(IndexFloor, 0, NumFrames - 1)];
        const float SampleAtCeil = Mono[FMath::Clamp(IndexFloor + 1, 0, NumFrames - 1)];

        const float Interpolated = FMath::Lerp(SampleAtFloor, SampleAtCeil, Frac);
        const int16 Sample = static_cast<int16>(FMath::Clamp(Interpolated, -1.0f, 1.0f) * 32767.0f);
        PCMBytes.Append(reinterpret_cast<const uint8*>(&Sample), sizeof(int16));

        Pos += Step;
    }

    if (NumFrames > 0)
    {
        ResampleCarryPhase = Pos - NumFrames;
        LastMonoSample = Mono[NumFrames - 1];
        bHasLastMonoSample = true;
    }

    return PCMBytes;
}
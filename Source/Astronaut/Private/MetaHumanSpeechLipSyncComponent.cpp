#include "MetaHumanSpeechLipSyncComponent.h"
#include "CompanionServiceSubsystem.h"

#include "ISpeechAnimationSolver.h"
#include "SpeechAnimationSolverV4.h"
#include "SpeechAnimationSolverTypes.h"
#include "NNEModelData.h"

#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Roles/LiveLinkBasicTypes.h"
#include "Features/IModularFeatures.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanLipSync, Log, All);

namespace
{
    // Minimal push-only LiveLink source: this component is the sole producer
    // of "AstronautSpeech" frames, so there's nothing to poll or reconnect —
    // just enough of the interface for the LiveLink client to accept pushes.
    class FSpeechLipSyncLiveLinkSource : public ILiveLinkSource
    {
    public:
        virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override {}
        virtual bool IsSourceStillValid() const override { return true; }
        virtual bool RequestSourceShutdown() override { return true; }
        virtual FText GetSourceType() const override { return NSLOCTEXT("Astronaut", "LipSyncSourceType", "Astronaut Speech Lip Sync"); }
        virtual FText GetSourceMachineName() const override { return NSLOCTEXT("Astronaut", "LipSyncMachineName", "Local"); }
        virtual FText GetSourceStatus() const override { return NSLOCTEXT("Astronaut", "LipSyncStatus", "Active"); }
    };

    // Idle expression tick runs at this rate — fast enough for blinking to
    // read as natural, slow enough not to fight with the much higher-rate
    // viseme pushes during actual speech (whichever push landed most
    // recently is what LiveLink reports, so speech naturally dominates
    // while talking and idle expression takes over between sentences).
    constexpr float IdleExpressionTickInterval = 0.1f;
}

UMetaHumanSpeechLipSyncComponent::UMetaHumanSpeechLipSyncComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UMetaHumanSpeechLipSyncComponent::BeginPlay()
{
    Super::BeginPlay();

    const FString ModelPath = ISpeechAnimationSolver::GetLatestModelAssetPath();
    UNNEModelData* ModelData = LoadObject<UNNEModelData>(GetTransientPackage(), *ModelPath);
    if (!ModelData)
    {
        UE_LOG(LogMetaHumanLipSync, Warning, TEXT("Could not load speech animation model at %s"), *ModelPath);
        return;
    }

    TSharedPtr<FSpeechAnimationSolverV4> V4Solver = MakeShared<FSpeechAnimationSolverV4>(ModelData, TEXT("NNERuntimeORTCpu"));
    if (!V4Solver->Initialize())
    {
        UE_LOG(LogMetaHumanLipSync, Warning, TEXT("Failed to initialize speech animation solver"));
        return;
    }
    Solver = V4Solver;

    if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        UE_LOG(LogMetaHumanLipSync, Warning, TEXT("LiveLink modular feature not available"));
        return;
    }

    ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    LiveLinkSource = MakeShared<FSpeechLipSyncLiveLinkSource>();
    SourceGuid = LiveLinkClient.AddSource(LiveLinkSource);

    if (UCompanionServiceSubsystem* CompanionService = UCompanionServiceSubsystem::GetCompanionService(this))
    {
        CompanionService->RegisterLipSyncComponent(this);
        UE_LOG(LogMetaHumanLipSync, Log, TEXT("Registered UMetaHumanSpeechLipSyncComponent with CompanionServiceSubsystem"));
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            IdleExpressionTimerHandle, this, &UMetaHumanSpeechLipSyncComponent::IdleExpressionTick,
            IdleExpressionTickInterval, /*bLoop=*/true);
    }
}

void UMetaHumanSpeechLipSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(IdleExpressionTimerHandle);
    }

    if (LiveLinkSource.IsValid() && IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
        LiveLinkClient.RemoveSource(LiveLinkSource);
    }
    LiveLinkSource.Reset();
    Solver.Reset();

    Super::EndPlay(EndPlayReason);
}

void UMetaHumanSpeechLipSyncComponent::PushStaticDataIfNeeded()
{
    if (bStaticDataPushed || !Solver.IsValid() || !LiveLinkSource.IsValid())
    {
        return;
    }
    if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        return;
    }

    ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    const FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

    // ABP_Face's LiveLinkPose node consumes data the same way Epic's own
    // "Live Link Face" app source does (see LiveLinkFaceSource.cpp): a
    // ULiveLinkBasicRole subject with named PropertyNames/PropertyValues,
    // NOT a skeleton/animation role. Pushing the wrong role means the node
    // silently ignores every frame — face never moves, no error anywhere.
    FLiveLinkStaticDataStruct StaticDataStruct(FLiveLinkBaseStaticData::StaticStruct());
    FLiveLinkBaseStaticData& StaticData = *StaticDataStruct.Cast<FLiveLinkBaseStaticData>();
    StaticData.PropertyNames = Solver->GetCurveNames();

    LiveLinkClient.PushSubjectStaticData_AnyThread(SubjectKey, ULiveLinkBasicRole::StaticClass(), MoveTemp(StaticDataStruct));
    bStaticDataPushed = true;
}

int32 UMetaHumanSpeechLipSyncComponent::FindCurveIndex(const FString& CurveName) const
{
    if (!Solver.IsValid())
    {
        return INDEX_NONE;
    }
    const TArray<FName>& CurveNames = Solver->GetCurveNames();
    for (int32 i = 0; i < CurveNames.Num(); ++i)
    {
        if (CurveNames[i].ToString().Equals(CurveName, ESearchCase::IgnoreCase))
        {
            return i;
        }
    }
    return INDEX_NONE;
}

void UMetaHumanSpeechLipSyncComponent::SetExpressionState(const FString& State)
{
    CurrentExpressionState = State.ToLower();
}

void UMetaHumanSpeechLipSyncComponent::IdleExpressionTick()
{
    if (!Solver.IsValid() || !LiveLinkSource.IsValid())
    {
        return;
    }

    PushStaticDataIfNeeded();

    const int32 NumCurves = Solver->GetCurveNames().Num();
    TArray<float> CurveValues;
    CurveValues.SetNumZeroed(NumCurves);

    // Simple periodic blink: mostly open, with a short close-pulse every ~4s.
    const double Now = FPlatformTime::Seconds();
    const double CyclePos = FMath::Fmod(Now, 4.0);
    const float BlinkValue = (CyclePos > 3.85) ? 1.0f : 0.0f;

    auto SetCurve = [&](const TCHAR* Name, float Value)
    {
        const int32 Index = FindCurveIndex(Name);
        if (Index != INDEX_NONE)
        {
            CurveValues[Index] = Value;
        }
    };

    SetCurve(TEXT("CTRL_expressions_eyeBlinkL"), BlinkValue);
    SetCurve(TEXT("CTRL_expressions_eyeBlinkR"), BlinkValue);

    if (CurrentExpressionState == TEXT("speaking"))
    {
        SetCurve(TEXT("CTRL_expressions_browRaiseInL"), 0.08f);
        SetCurve(TEXT("CTRL_expressions_browRaiseInR"), 0.08f);
        SetCurve(TEXT("CTRL_expressions_mouthCornerPullL"), 0.1f);
        SetCurve(TEXT("CTRL_expressions_mouthCornerPullR"), 0.1f);
    }
    else if (CurrentExpressionState == TEXT("listening"))
    {
        SetCurve(TEXT("CTRL_expressions_browRaiseOuterL"), 0.15f);
        SetCurve(TEXT("CTRL_expressions_browRaiseOuterR"), 0.15f);
    }

    if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        return;
    }

    ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    const FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

    FLiveLinkFrameDataStruct FrameDataStruct(FLiveLinkBaseFrameData::StaticStruct());
    FLiveLinkBaseFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkBaseFrameData>();
    FrameData.WorldTime = FLiveLinkWorldTime(Now);
    FrameData.PropertyValues = CurveValues;

    LiveLinkClient.PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(FrameDataStruct));
}

void UMetaHumanSpeechLipSyncComponent::FeedAudioSamples(const TArray<uint8>& PCM16Data, int32 SampleRate, int32 NumChannels)
{
    if (!Solver.IsValid() || !LiveLinkSource.IsValid() || PCM16Data.Num() < 2)
    {
        return;
    }

    PushStaticDataIfNeeded();

    const int16* Samples = reinterpret_cast<const int16*>(PCM16Data.GetData());
    const int32 NumSamples = PCM16Data.Num() / sizeof(int16);

    FSpeechAnimationAudioFrame InFrame;
    InFrame.AudioSamples.SetNumUninitialized(NumSamples);
    for (int32 i = 0; i < NumSamples; ++i)
    {
        InFrame.AudioSamples[i] = static_cast<float>(Samples[i]) / 32768.0f;
    }
    InFrame.SamplesCount = NumSamples;
    InFrame.SampleRate = SampleRate;
    InFrame.NumChannels = NumChannels;
    InFrame.bContiguous = true;

    FSpeechAnimationFrameData OutFrame;
    if (!Solver->SolveAudioFrame(InFrame, OutFrame))
    {
        return;
    }

    // Direct curve evaluation on Face skeletal mesh as fallback/direct drive
    if (AActor* Owner = GetOwner())
    {
        const TArray<FName>& CurveNames = Solver->GetCurveNames();
        USkeletalMeshComponent* TargetMesh = nullptr;
        TArray<USkeletalMeshComponent*> SkelComponents;
        Owner->GetComponents<USkeletalMeshComponent>(SkelComponents);
        for (USkeletalMeshComponent* Comp : SkelComponents)
        {
            if (Comp && Comp->GetName().Contains(TEXT("Face")))
            {
                TargetMesh = Comp;
                break;
            }
        }
        if (!TargetMesh && SkelComponents.Num() > 0)
        {
            TargetMesh = SkelComponents[0];
        }

        if (TargetMesh)
        {
            for (int32 i = 0; i < CurveNames.Num() && i < OutFrame.CurveValues.Num(); ++i)
            {
                TargetMesh->SetMorphTarget(CurveNames[i], OutFrame.CurveValues[i]);
            }
        }
    }

    if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        return;
    }

    ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    const FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

    FLiveLinkFrameDataStruct FrameDataStruct(FLiveLinkBaseFrameData::StaticStruct());
    FLiveLinkBaseFrameData& FrameData = *FrameDataStruct.Cast<FLiveLinkBaseFrameData>();
    FrameData.WorldTime = FLiveLinkWorldTime(FPlatformTime::Seconds());
    FrameData.PropertyValues = OutFrame.CurveValues;

    LiveLinkClient.PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(FrameDataStruct));
}

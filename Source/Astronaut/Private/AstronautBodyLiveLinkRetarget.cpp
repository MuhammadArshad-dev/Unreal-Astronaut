#include "AstronautBodyLiveLinkRetarget.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Roles/LiveLinkBasicTypes.h"
#include "BonePose.h"

DEFINE_LOG_CATEGORY_STATIC(LogAstronautBodyRetarget, Log, All);

void UAstronautBodyLiveLinkRetarget::Initialize()
{
    IdleSequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Astronaut/Audio/IdleAS_M_Unarmed_Idle_01Anim.IdleAS_M_Unarmed_Idle_01Anim"));
    PlaybackTime = 0.0f;
    UE_LOG(LogAstronautBodyRetarget, Warning, TEXT("Initialize called. IdleSequence=%s"), IdleSequence.IsValid() ? *IdleSequence->GetName() : TEXT("NULL"));
}

void UAstronautBodyLiveLinkRetarget::BuildPoseAndCurveFromBaseData(float DeltaTime, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InBaseFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve)
{
    static int32 CallCount = 0;
    ++CallCount;
    if (CallCount % 60 == 1)
    {
        UE_LOG(LogAstronautBodyRetarget, Warning, TEXT("BuildPoseAndCurveFromBaseData call #%d, DeltaTime=%f, BoneContainerNumBones=%d, IdleSequenceValid=%d, PropertyNames=%d"),
            CallCount, DeltaTime, OutPose.GetBoneContainer().GetCompactPoseNumBones(), IdleSequence.IsValid(),
            InBaseStaticData ? InBaseStaticData->PropertyNames.Num() : -1);
    }

    OutPose.ResetToRefPose();

    if (UAnimSequence* Sequence = IdleSequence.Get())
    {
        const float SequenceLength = Sequence->GetPlayLength();
        if (SequenceLength > 0.0f)
        {
            PlaybackTime = FMath::Fmod(PlaybackTime + DeltaTime, SequenceLength);
            if (PlaybackTime < 0.0f)
            {
                PlaybackTime += SequenceLength;
            }
        }

        FBlendedCurve ScratchCurve;
        UE::Anim::FStackAttributeContainer ScratchAttributes;
        FAnimationPoseData PoseData(OutPose, ScratchCurve, ScratchAttributes);

        const FAnimExtractContext ExtractContext(static_cast<double>(PlaybackTime), false);
        Sequence->GetBonePose(PoseData, ExtractContext);

        if (CallCount % 60 == 1 && OutPose.GetNumBones() > 0)
        {
            const FCompactPoseBoneIndex RootIndex(0);
            const FTransform& RootTransform = OutPose[RootIndex];
            UE_LOG(LogAstronautBodyRetarget, Warning, TEXT("PlaybackTime=%f SeqLen=%f RootBoneLoc=%s RootBoneScale=%s"),
                PlaybackTime, SequenceLength, *RootTransform.GetLocation().ToString(), *RootTransform.GetScale3D().ToString());
        }
    }

    OutCurve.InitFrom(OutPose.GetBoneContainer());

    if (InBaseStaticData && InBaseFrameData && InBaseStaticData->PropertyNames.Num() == InBaseFrameData->PropertyValues.Num())
    {
        TMap<FName, float> CurveMap;
        CurveMap.Reserve(InBaseStaticData->PropertyNames.Num());
        for (int32 i = 0; i < InBaseStaticData->PropertyNames.Num(); ++i)
        {
            CurveMap.Add(InBaseStaticData->PropertyNames[i], InBaseFrameData->PropertyValues[i]);
        }
        BuildCurveData(CurveMap, OutPose, OutCurve);
    }
}

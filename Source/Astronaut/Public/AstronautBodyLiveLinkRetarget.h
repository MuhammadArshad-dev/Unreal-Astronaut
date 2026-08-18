#pragma once

#include "CoreMinimal.h"
#include "LiveLinkRetargetAsset.h"
#include "AstronautBodyLiveLinkRetarget.generated.h"

class UAnimSequence;

/**
 * Retarget asset for the Body skeletal mesh's LiveLinkInstance. The "AstronautSpeech"
 * subject only ever carries curve data (ULiveLinkBasicRole, no bones — see
 * MetaHumanSpeechLipSyncComponent), so left to the default LiveLinkRemapAsset, Body's
 * bones would sit at reference pose (T-pose) forever. This plays a looping idle
 * AnimSequence for the pose while still passing the incoming curves through, so Body
 * gets a natural stance AND Face (which copies pose+curves from Body, its attach
 * parent) keeps receiving the speech/expression curves.
 */
UCLASS()
class ASTRONAUT_API UAstronautBodyLiveLinkRetarget : public ULiveLinkRetargetAsset
{
    GENERATED_BODY()

public:
    virtual void Initialize() override;
    virtual void BuildPoseAndCurveFromBaseData(float DeltaTime, const FLiveLinkBaseStaticData* InBaseStaticData, const FLiveLinkBaseFrameData* InBaseFrameData, FCompactPose& OutPose, FBlendedCurve& OutCurve) override;

private:
    TWeakObjectPtr<UAnimSequence> IdleSequence;
    float PlaybackTime = 0.0f;
};

#include "AstronautBodyAnimInstance.h"
#include "AstronautBodyLiveLinkRetarget.h"

FAnimInstanceProxy* UAstronautBodyAnimInstance::CreateAnimInstanceProxy()
{
    FAnimInstanceProxy* Proxy = Super::CreateAnimInstanceProxy();
    static_cast<FLiveLinkInstanceProxy*>(Proxy)->PoseNode.RetargetAsset = UAstronautBodyLiveLinkRetarget::StaticClass();
    return Proxy;
}

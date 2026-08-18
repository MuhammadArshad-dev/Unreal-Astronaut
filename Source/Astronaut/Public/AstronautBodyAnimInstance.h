#pragma once

#include "CoreMinimal.h"
#include "LiveLinkInstance.h"
#include "AstronautBodyAnimInstance.generated.h"

/**
 * Thin subclass of the native LiveLinkInstance that hardcodes
 * UAstronautBodyLiveLinkRetarget as its retarget asset in C++. Setting the retarget
 * asset via a Blueprint pin default proved unreliable — the class reference on the
 * "Live Link Retarget Asset Class Reference" pin kept getting dropped on editor
 * restart even after explicit saves — so it's pinned here instead, where it can't
 * be silently lost.
 */
UCLASS(transient)
class ASTRONAUT_API UAstronautBodyAnimInstance : public ULiveLinkInstance
{
    GENERATED_BODY()

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

# State-Driven Avatar Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the astronaut character play looping animations that track the companion service's avatar state (listening / thinking / talking), with gesture tags interrupting and then resuming the talking loop, while leaving the existing idle system untouched.

**Architecture:** All new logic lives in the existing `UAvatarGestureComponent` (on `BP_Cooper`). It subscribes to `UCompanionServiceSubsystem::OnAvatarStateChange` (already broadcasting `"idle"|"listening"|"thinking"|"talking"`, confirmed against the companion-service source) alongside its existing `OnGestureReceived` subscription, and drives `Montage_Play` on the Body skeletal mesh's `AnimInstance`, using `FOnMontageEnded` end-delegates (same pattern as the sibling `UAstronautIdleAnimComponent`) to self-chain a state's animation until the state changes or a gesture interrupts it.

**Tech Stack:** Unreal Engine 5.8, C++ (UBT/Live Coding build), Blueprint asset editing via the project's Unreal MCP toolset (`editor_toolset.toolsets.*`).

## Global Constraints

- No backend/protocol changes — the companion service already sends everything this needs (verified directly against `~/Desktop/moonwalkers-companion-service/src/server/{protocol,sessionHandler}.ts`).
- Idle behavior is out of scope and must not be modified — only handed back to cleanly (`StopIdleLoop()` then `StartIdleLoop()` on `UAstronautIdleAnimComponent`, required because its own end-delegate does not self-resume after an external interruption).
- No new animation assets — `AS_Listening_Montage` and `AS_Thinking_Montage` already exist; only need to be assigned to two new component properties.
- New property must be named `ThinkingStateMontage`, not `ThinkingMontage` — `ThinkingMontage` already exists on this component as the *gesture-tag* montage (a different trigger moment) and must not be reused or overwritten.
- No automated test harness exists in this project; verification is compiling cleanly plus manual PIE checks against the log categories already established this session (`LogAvatarGesture`, `LogCompanionService`).
- Per this project's established, previously-hit bug: whenever a Blueprint property is set via the MCP `ObjectTools.set_properties` tool, it must be verified on the **placed level instance** (not just the Blueprint's class default object) — CDO values do not automatically propagate to already-placed actors' per-instance component data.

---

### Task 1: Refactor shared anim-instance lookup and cache the sibling idle component

**Files:**
- Modify: `Source/Astronaut/Public/AvatarGestureComponent.h` (currently 51 lines)
- Modify: `Source/Astronaut/Private/AvatarGestureComponent.cpp` (currently 73 lines)

**Interfaces:**
- Consumes: `UAstronautIdleAnimComponent::StopIdleLoop()` / `StartIdleLoop()` (public, no params, declared in `Source/Astronaut/Public/AstronautIdleAnimComponent.h`).
- Produces: `UAnimInstance* UAvatarGestureComponent::GetBodyAnimInstance() const` — used by Tasks 2 and 3. `TObjectPtr<UAstronautIdleAnimComponent> IdleAnimComponent` member (cached, may be `nullptr` if absent) — used by Task 2.

This task extracts the repeated "find the Body skeletal mesh's AnimInstance" logic already duplicated once (in `PlayGesture`) into a shared helper, and caches a pointer to the sibling idle component. No new animation behavior yet — existing gesture playback must work identically after this change.

- [ ] **Step 1: Add the new forward declarations and `GetBodyAnimInstance` declaration to the header**

Edit `Source/Astronaut/Public/AvatarGestureComponent.h`. Replace the top of the file (forward declarations) and the `private:` section:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AvatarGestureComponent.generated.h"

class UAnimMontage;
class UAnimInstance;
class UAstronautIdleAnimComponent;

/**
 * Listens for gesture tags broadcast by UCompanionServiceSubsystem
 * ("Shrugging", "Thinking", "Refusing", "Talkingloop", "Talking2Loop", "Nodding")
 * and plays corresponding AnimMontages on the character's skeletal mesh.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ASTRONAUT_API UAvatarGestureComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAvatarGestureComponent();

    /** Montages for each recognized gesture tag. Assign in Editor details panel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> ShruggingMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> ThinkingMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> RefusingMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> TalkingloopMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> Talking2LoopMontage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> NoddingMontage;

    /** Plays the gesture animation for the given tag string. */
    UFUNCTION(BlueprintCallable, Category = "Gestures")
    void PlayGesture(const FString& Tag);

protected:
    virtual void BeginPlay() override;

private:
    UFUNCTION()
    void HandleGestureReceived(const FString& Tag, int32 SentenceIndex);

    /** Finds the Body skeletal mesh's AnimInstance, or nullptr if either is missing. */
    UAnimInstance* GetBodyAnimInstance() const;

    /** Cached in BeginPlay. Idle is owned by this component, not us. */
    UPROPERTY()
    TObjectPtr<UAstronautIdleAnimComponent> IdleAnimComponent = nullptr;
};
```

- [ ] **Step 2: Implement `GetBodyAnimInstance`, cache `IdleAnimComponent`, and use the helper in `PlayGesture`**

Edit `Source/Astronaut/Private/AvatarGestureComponent.cpp` in full:

```cpp
#include "AvatarGestureComponent.h"
#include "CompanionServiceSubsystem.h"
#include "AstronautIdleAnimComponent.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

DEFINE_LOG_CATEGORY_STATIC(LogAvatarGesture, Log, All);

UAvatarGestureComponent::UAvatarGestureComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UAvatarGestureComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        IdleAnimComponent = Owner->FindComponentByClass<UAstronautIdleAnimComponent>();
    }

    if (UCompanionServiceSubsystem* CompanionService = UCompanionServiceSubsystem::GetCompanionService(this))
    {
        CompanionService->OnGestureReceived.AddDynamic(this, &UAvatarGestureComponent::HandleGestureReceived);
        UE_LOG(LogAvatarGesture, Log, TEXT("UAvatarGestureComponent subscribed to OnGestureReceived"));
    }
}

UAnimInstance* UAvatarGestureComponent::GetBodyAnimInstance() const
{
    USkeletalMeshComponent* SkeletalMeshComp = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
    return SkeletalMeshComp ? SkeletalMeshComp->GetAnimInstance() : nullptr;
}

void UAvatarGestureComponent::HandleGestureReceived(const FString& Tag, int32 SentenceIndex)
{
    UE_LOG(LogAvatarGesture, Log, TEXT("Gesture received: tag='%s', sentence_index=%d"), *Tag, SentenceIndex);
    PlayGesture(Tag);
}

void UAvatarGestureComponent::PlayGesture(const FString& Tag)
{
    UAnimMontage* MontageToPlay = nullptr;

    if (Tag == TEXT("Shrugging"))
    {
        MontageToPlay = ShruggingMontage;
    }
    else if (Tag == TEXT("Thinking"))
    {
        MontageToPlay = ThinkingMontage;
    }
    else if (Tag == TEXT("Refusing"))
    {
        MontageToPlay = RefusingMontage;
    }
    else if (Tag == TEXT("Talkingloop"))
    {
        MontageToPlay = TalkingloopMontage;
    }
    else if (Tag == TEXT("Talking2Loop"))
    {
        MontageToPlay = Talking2LoopMontage;
    }
    else if (Tag == TEXT("Nodding"))
    {
        MontageToPlay = NoddingMontage;
    }

    if (!MontageToPlay)
    {
        UE_LOG(LogAvatarGesture, Warning, TEXT("No AnimMontage assigned for gesture tag '%s'"), *Tag);
        return;
    }

    if (UAnimInstance* AnimInstance = GetBodyAnimInstance())
    {
        AnimInstance->Montage_Play(MontageToPlay);
        UE_LOG(LogAvatarGesture, Log, TEXT("Playing gesture AnimMontage '%s' for tag '%s'"), *MontageToPlay->GetName(), *Tag);
    }
}
```

- [ ] **Step 3: Rebuild via Live Coding and verify zero compile errors**

Live Coding is safe here — this step only adds new declarations/members and refactors one existing function body; no `UPROPERTY` is renamed or removed. In the running editor, trigger Live Coding compile (Ctrl+Alt+F11, or the Compile button) and check the result via the MCP log tool:

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "LogLiveCoding", "pattern": "Error|error", "maxEntries": 30})
```

Expected: empty `returnValue` array (no errors). If Live Coding reports it can't apply the change (rare for pure additions), fall back to closing the editor and running the full `Build.sh AstronautEditor Mac Development` rebuild used earlier this session, then relaunching.

- [ ] **Step 4: Manually verify existing gesture playback still works**

In PIE, trigger any gesture tag (e.g. ask a question likely to produce `[gesture: Nodding]`) and confirm the montage still plays, exactly as it did before this refactor. Check logs:

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "LogAvatarGesture", "pattern": "Playing gesture AnimMontage", "maxEntries": 10})
```

Expected: a fresh `Playing gesture AnimMontage '...' for tag '...'` line.

- [ ] **Step 5: Commit**

```bash
cd "/Users/muhammad/Documents/Unreal Projects/Astronaut Final With Background"
git add Source/Astronaut/Public/AvatarGestureComponent.h Source/Astronaut/Private/AvatarGestureComponent.cpp
git commit -m "$(cat <<'EOF'
Extract shared AnimInstance lookup in AvatarGestureComponent

Prep for state-driven animation: cache the sibling idle component and
factor the repeated Body-mesh AnimInstance lookup into one helper
before adding new call sites in the next task.
EOF
)"
```

---

### Task 2: Listening/thinking looping animation + idle handoff

**Files:**
- Modify: `Source/Astronaut/Public/AvatarGestureComponent.h`
- Modify: `Source/Astronaut/Private/AvatarGestureComponent.cpp`

**Interfaces:**
- Consumes: `GetBodyAnimInstance()`, `IdleAnimComponent` (from Task 1). `UCompanionServiceSubsystem::OnAvatarStateChange` (`DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAvatarStateChange, const FString&, State)`, declared in `Source/Astronaut/Public/CompanionServiceSubsystem.h:13`).
- Produces: `FString CurrentAvatarState` member — used by Task 3's talking-loop and gesture-resume logic to check whether the avatar is still talking. `void PlayLoopingStateMontage(UAnimMontage* Montage, const FString& OwningState)` — reused by Task 3 for consistency, though Task 3 has its own random-pick variant for talking.

- [ ] **Step 1: Add the two new montage properties, state tracking, and new method declarations to the header**

Edit `Source/Astronaut/Public/AvatarGestureComponent.h`, inserting after the existing `NoddingMontage` property and before `PlayGesture`:

```cpp
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> NoddingMontage;

    /** Looped while the avatar state is "listening" (mic open). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> ListeningMontage;

    /**
     * Looped while the avatar state is "thinking" (waiting on the LLM/RAG
     * response, before any talking starts). Deliberately a separate property
     * from ThinkingMontage above — that one is the gesture-tag montage,
     * played mid-sentence while already talking, a different trigger moment.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
    TObjectPtr<UAnimMontage> ThinkingStateMontage;

    /** Plays the gesture animation for the given tag string. */
    UFUNCTION(BlueprintCallable, Category = "Gestures")
    void PlayGesture(const FString& Tag);
```

And update the `private:` section to add the state-change handler and looping helper:

```cpp
private:
    UFUNCTION()
    void HandleGestureReceived(const FString& Tag, int32 SentenceIndex);

    UFUNCTION()
    void HandleAvatarStateChange(const FString& State);

    /** Finds the Body skeletal mesh's AnimInstance, or nullptr if either is missing. */
    UAnimInstance* GetBodyAnimInstance() const;

    /** Plays Montage and re-triggers itself on end, as long as CurrentAvatarState still equals OwningState and playback wasn't interrupted. */
    void PlayLoopingStateMontage(UAnimMontage* Montage, const FString& OwningState);
    void HandleLoopingMontageEnded(UAnimMontage* Montage, bool bInterrupted, FString OwningState);

    /** Mirrors UCompanionServiceSubsystem's last-broadcast avatar state. "idle" until the first state_change arrives. */
    FString CurrentAvatarState = TEXT("idle");

    /** Cached in BeginPlay. Idle is owned by this component, not us. */
    UPROPERTY()
    TObjectPtr<UAstronautIdleAnimComponent> IdleAnimComponent = nullptr;
```

- [ ] **Step 2: Subscribe to `OnAvatarStateChange` in `BeginPlay`**

Edit `Source/Astronaut/Private/AvatarGestureComponent.cpp`, replacing the `BeginPlay` body:

```cpp
void UAvatarGestureComponent::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        IdleAnimComponent = Owner->FindComponentByClass<UAstronautIdleAnimComponent>();
    }

    if (UCompanionServiceSubsystem* CompanionService = UCompanionServiceSubsystem::GetCompanionService(this))
    {
        CompanionService->OnGestureReceived.AddDynamic(this, &UAvatarGestureComponent::HandleGestureReceived);
        CompanionService->OnAvatarStateChange.AddDynamic(this, &UAvatarGestureComponent::HandleAvatarStateChange);
        UE_LOG(LogAvatarGesture, Log, TEXT("UAvatarGestureComponent subscribed to OnGestureReceived and OnAvatarStateChange"));
    }
}
```

- [ ] **Step 3: Implement `HandleAvatarStateChange`, `PlayLoopingStateMontage`, and `HandleLoopingMontageEnded`**

Append to `Source/Astronaut/Private/AvatarGestureComponent.cpp` (after `GetBodyAnimInstance`, before `HandleGestureReceived` — exact position doesn't matter, but keep it grouped with the other new state-handling code):

```cpp
void UAvatarGestureComponent::HandleAvatarStateChange(const FString& State)
{
    UE_LOG(LogAvatarGesture, Log, TEXT("Avatar state changed to '%s'"), *State);
    CurrentAvatarState = State;

    if (State == TEXT("idle"))
    {
        if (IdleAnimComponent)
        {
            IdleAnimComponent->StopIdleLoop();
            IdleAnimComponent->StartIdleLoop();
        }
        return;
    }

    if (IdleAnimComponent)
    {
        IdleAnimComponent->StopIdleLoop();
    }

    if (State == TEXT("listening"))
    {
        PlayLoopingStateMontage(ListeningMontage, State);
    }
    else if (State == TEXT("thinking"))
    {
        PlayLoopingStateMontage(ThinkingStateMontage, State);
    }
}

void UAvatarGestureComponent::PlayLoopingStateMontage(UAnimMontage* Montage, const FString& OwningState)
{
    if (!Montage)
    {
        UE_LOG(LogAvatarGesture, Warning, TEXT("No AnimMontage assigned for avatar state '%s'"), *OwningState);
        return;
    }

    UAnimInstance* AnimInstance = GetBodyAnimInstance();
    if (!AnimInstance)
    {
        return;
    }

    AnimInstance->Montage_Play(Montage);

    FOnMontageEnded EndDelegate;
    EndDelegate.BindUObject(this, &UAvatarGestureComponent::HandleLoopingMontageEnded, OwningState);
    AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);
}

void UAvatarGestureComponent::HandleLoopingMontageEnded(UAnimMontage* Montage, bool bInterrupted, FString OwningState)
{
    if (bInterrupted || CurrentAvatarState != OwningState)
    {
        return;
    }
    PlayLoopingStateMontage(Montage, OwningState);
}
```

- [ ] **Step 4: Rebuild via Live Coding and verify zero compile errors**

Same check as Task 1 Step 3:

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "LogLiveCoding", "pattern": "Error|error", "maxEntries": 30})
```

Expected: empty `returnValue` array.

- [ ] **Step 5: Assign the two new montages on both the Blueprint class default and the placed level instance**

Both assignments are required — this project already hit the bug where a class-default-only assignment leaves the placed level actor showing `None` at runtime. Use `ObjectTools.set_properties` one property at a time (batched multi-property calls on this object were separately observed this session to only apply the first property):

```
mcp__unreal__call_tool(tool_name="set_properties", toolset_name="editor_toolset.toolsets.object.ObjectTools",
  arguments={"instance": {"refPath": "/Game/MetaHumans/Cooper/BP_Cooper.BP_Cooper_C:AvatarGesture_GEN_VARIABLE"},
             "values": "{\"listeningMontage\":{\"refPath\":\"/Game/MetaHumans/Animations/Sequences/Body/AS_Listening_Montage.AS_Listening_Montage\"}}"})

mcp__unreal__call_tool(tool_name="set_properties", toolset_name="editor_toolset.toolsets.object.ObjectTools",
  arguments={"instance": {"refPath": "/Game/MetaHumans/Cooper/BP_Cooper.BP_Cooper_C:AvatarGesture_GEN_VARIABLE"},
             "values": "{\"thinkingStateMontage\":{\"refPath\":\"/Game/MetaHumans/Animations/Sequences/Body/AS_Thinking_Montage.AS_Thinking_Montage\"}}"})

mcp__unreal__call_tool(tool_name="set_properties", toolset_name="editor_toolset.toolsets.object.ObjectTools",
  arguments={"instance": {"refPath": "/Game/Lvl_Metahuman.Lvl_Metahuman:PersistentLevel.BP_Cooper_C_1.AvatarGesture"},
             "values": "{\"listeningMontage\":{\"refPath\":\"/Game/MetaHumans/Animations/Sequences/Body/AS_Listening_Montage.AS_Listening_Montage\"}}"})

mcp__unreal__call_tool(tool_name="set_properties", toolset_name="editor_toolset.toolsets.object.ObjectTools",
  arguments={"instance": {"refPath": "/Game/Lvl_Metahuman.Lvl_Metahuman:PersistentLevel.BP_Cooper_C_1.AvatarGesture"},
             "values": "{\"thinkingStateMontage\":{\"refPath\":\"/Game/MetaHumans/Animations/Sequences/Body/AS_Thinking_Montage.AS_Thinking_Montage\"}}"})
```

Then verify each of the 4 calls actually stuck (`get_properties` on both refPaths for `listeningMontage`/`thinkingStateMontage` — expect the assigned `refPath`, not `"None"`), compile the Blueprint (`editor_toolset.toolsets.blueprint.BlueprintTools.compile_blueprint` on `/Game/MetaHumans/Cooper/BP_Cooper.BP_Cooper`, expect `{"returnValue": null}`), and save (`editor_toolset.toolsets.asset.AssetTools.save_assets` with `asset_paths: []`, expect `{"returnValue": true}`).

- [ ] **Step 6: Manually verify listening/thinking looping and idle handoff in PIE**

Press push-to-talk and hold for several seconds — confirm the Listening montage plays and visibly repeats (not just once) while held. Release and confirm Thinking plays and loops until a reply's `talking` state arrives (Task 3 not implemented yet, so at this point "talking" does nothing new — that's expected). Let a full turn finish and confirm the character returns to its normal randomized idle behavior (not frozen in a T-pose or stuck on the last Thinking pose). Check the state-transition log trail:

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "LogAvatarGesture", "pattern": "Avatar state changed", "maxEntries": 20})
```

Expected: a sequence of `Avatar state changed to 'listening'` → `'thinking'` → `'talking'` → `'idle'` lines matching what you did in PIE.

- [ ] **Step 7: Commit**

```bash
cd "/Users/muhammad/Documents/Unreal Projects/Astronaut Final With Background"
git add Source/Astronaut/Public/AvatarGestureComponent.h Source/Astronaut/Private/AvatarGestureComponent.cpp
git commit -m "$(cat <<'EOF'
Add listening/thinking looping animation and idle handoff

AvatarGestureComponent now subscribes to OnAvatarStateChange and loops
ListeningMontage/ThinkingStateMontage while the avatar is in those
states, self-chaining via a Montage end-delegate until the state
changes. Returning to idle explicitly restarts the sibling
AstronautIdleAnimComponent's loop, since its own end-delegate does not
resume itself after an external interruption.
EOF
)"
```

Note: this commit covers source only. The BP_Cooper asset edits from Step 5 were already saved to disk directly via the editor in that step (Unreal `.uasset` files are binary and aren't meaningfully diffed/reviewed as text — this matches how the montage assignments earlier this session were saved, not committed as a separate git step).

---

### Task 3: Talking loop with random alternation + gesture interrupt/resume

**Files:**
- Modify: `Source/Astronaut/Public/AvatarGestureComponent.h`
- Modify: `Source/Astronaut/Private/AvatarGestureComponent.cpp`

**Interfaces:**
- Consumes: `GetBodyAnimInstance()`, `CurrentAvatarState`, `TalkingloopMontage`/`Talking2LoopMontage` (both already exist as properties from before this plan, already assigned to `AS_Talkingloop_Montage`/`AS_Talking2Loop_Montage` per the earlier gesture-vocabulary work).
- Produces: `void PlayRandomTalkingLoopClip()` — the "talking" state's looping entry point.

- [ ] **Step 1: Add the new method declarations to the header**

Edit `Source/Astronaut/Public/AvatarGestureComponent.h`, extending the `private:` section from Task 2:

```cpp
    /** Plays a random TalkingloopMontage/Talking2LoopMontage clip and re-triggers itself on end, as long as CurrentAvatarState is still "talking" and playback wasn't interrupted. */
    void PlayRandomTalkingLoopClip();
    void HandleTalkingLoopMontageEnded(UAnimMontage* Montage, bool bInterrupted);

    /** Resumes the talking loop after a gesture montage finishes uninterrupted, if still talking. */
    void HandleGestureMontageEnded(UAnimMontage* Montage, bool bInterrupted);
```

- [ ] **Step 2: Wire the `"talking"` branch in `HandleAvatarStateChange`**

Edit `Source/Astronaut/Private/AvatarGestureComponent.cpp`, updating `HandleAvatarStateChange`:

```cpp
    if (State == TEXT("listening"))
    {
        PlayLoopingStateMontage(ListeningMontage, State);
    }
    else if (State == TEXT("thinking"))
    {
        PlayLoopingStateMontage(ThinkingStateMontage, State);
    }
    else if (State == TEXT("talking"))
    {
        PlayRandomTalkingLoopClip();
    }
```

- [ ] **Step 3: Implement `PlayRandomTalkingLoopClip` and `HandleTalkingLoopMontageEnded`**

Append after `HandleLoopingMontageEnded`:

```cpp
void UAvatarGestureComponent::PlayRandomTalkingLoopClip()
{
    TArray<UAnimMontage*> Clips;
    if (TalkingloopMontage)
    {
        Clips.Add(TalkingloopMontage);
    }
    if (Talking2LoopMontage)
    {
        Clips.Add(Talking2LoopMontage);
    }

    if (Clips.Num() == 0)
    {
        UE_LOG(LogAvatarGesture, Warning, TEXT("No talking-loop AnimMontage assigned (TalkingloopMontage/Talking2LoopMontage)"));
        return;
    }

    UAnimMontage* NextClip = Clips[FMath::RandRange(0, Clips.Num() - 1)];

    UAnimInstance* AnimInstance = GetBodyAnimInstance();
    if (!AnimInstance)
    {
        return;
    }

    AnimInstance->Montage_Play(NextClip);

    FOnMontageEnded EndDelegate;
    EndDelegate.BindUObject(this, &UAvatarGestureComponent::HandleTalkingLoopMontageEnded);
    AnimInstance->Montage_SetEndDelegate(EndDelegate, NextClip);
}

void UAvatarGestureComponent::HandleTalkingLoopMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (bInterrupted || CurrentAvatarState != TEXT("talking"))
    {
        return;
    }
    PlayRandomTalkingLoopClip();
}
```

- [ ] **Step 4: Wire gesture interrupt/resume in `PlayGesture`**

Replace the tail of `PlayGesture` (from the `if (UAnimInstance* AnimInstance = ...)` block onward) with:

```cpp
    UAnimInstance* AnimInstance = GetBodyAnimInstance();
    if (AnimInstance)
    {
        AnimInstance->Montage_Play(MontageToPlay);
        UE_LOG(LogAvatarGesture, Log, TEXT("Playing gesture AnimMontage '%s' for tag '%s'"), *MontageToPlay->GetName(), *Tag);

        FOnMontageEnded EndDelegate;
        EndDelegate.BindUObject(this, &UAvatarGestureComponent::HandleGestureMontageEnded);
        AnimInstance->Montage_SetEndDelegate(EndDelegate, MontageToPlay);
    }
}

void UAvatarGestureComponent::HandleGestureMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (!bInterrupted && CurrentAvatarState == TEXT("talking"))
    {
        PlayRandomTalkingLoopClip();
    }
}
```

(The closing brace of `PlayGesture` moves up one level to right after the `EndDelegate` binding; `HandleGestureMontageEnded` is a new, separate function after it.)

- [ ] **Step 5: Rebuild via Live Coding and verify zero compile errors**

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "LogLiveCoding", "pattern": "Error|error", "maxEntries": 30})
```

Expected: empty `returnValue` array.

- [ ] **Step 6: Manually verify talking loop, gesture interrupt, and resume in PIE**

Trigger a full conversational turn with a reply long enough to contain multiple sentences, ideally one likely to include a gesture tag. Confirm:
1. Once `talking` starts, the character visibly alternates between the two talking-loop clips (not stuck on one).
2. When a gesture fires mid-reply, it visibly interrupts the loop and plays the gesture-specific animation.
3. After the gesture finishes, the talking loop resumes (does not freeze, does not silently fall back to idle mid-turn).
4. When the turn completes (`state_change: idle`), the character returns cleanly to the idle system.

Check logs for the full sequence:

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "LogAvatarGesture", "pattern": "", "maxEntries": 50})
```

Expected: interleaved `Avatar state changed to 'talking'`, `Gesture received: tag=...`, `Playing gesture AnimMontage ...` lines with no `Warning: No AnimMontage assigned` entries.

- [ ] **Step 7: Commit**

```bash
cd "/Users/muhammad/Documents/Unreal Projects/Astronaut Final With Background"
git add Source/Astronaut/Public/AvatarGestureComponent.h Source/Astronaut/Private/AvatarGestureComponent.cpp
git commit -m "$(cat <<'EOF'
Add talking-loop animation with gesture interrupt/resume

While the avatar state is "talking", AvatarGestureComponent now loops
a randomly-picked Talkingloop/Talking2Loop clip. A gesture tag mid-turn
interrupts it as before, and now resumes the talking loop afterward if
the avatar is still talking — matching how the LLM already emits
inline gesture tags without pausing the overall speaking beat.
EOF
)"
```

---

### Task 4: Final end-to-end verification

**Files:** none (verification only, no code changes).

**Interfaces:** none — this task exercises everything Tasks 1–3 produced together.

- [ ] **Step 1: Run through the full spec testing checklist in one continuous PIE session**

Referencing `docs/superpowers/specs/2026-08-21-state-driven-avatar-animation-design.md`'s Testing section:

1. Press push-to-talk → Listening montage plays and loops while held.
2. Release → Thinking montage plays and loops until a reply starts.
3. Confirm Talkingloop/Talking2Loop alternate randomly while the reply speaks.
4. Confirm a gesture tag mid-reply interrupts the talk loop, plays the gesture, then resumes the talk loop.
5. Confirm turn completion returns cleanly to the idle system's own animation (not frozen).
6. Press push-to-talk again mid-reply — confirm immediate switch to Listening, audio cuts, and no stale resume-to-talking occurs afterward.

- [ ] **Step 2: Confirm no regressions in existing caption/audio behavior**

This component's changes don't touch captions or audio, but both share the same `OnAvatarStateChange`/`OnGestureReceived` broadcast — confirm captions (fixed earlier this session) and audio playback still work normally during this same PIE pass, since a botched delegate binding could theoretically starve other listeners. Check for any new `Error` entries across the board:

```
mcp__unreal__call_tool(tool_name="GetLogEntries", toolset_name="EditorToolset.LogsToolset",
  arguments={"category": "", "pattern": "Error", "maxEntries": 50})
```

Expected: no new errors attributable to this session's PIE run beyond pre-existing, unrelated ones (e.g. the known `turn_complete`/`cancel_turn` unknown-message-type warnings, which are warnings, not errors, and out of scope for this plan).

- [ ] **Step 3: Report results**

Summarize what was verified against the checklist above (pass/fail per item) back to the user. If any item fails, that's a new bug to root-cause via the systematic-debugging skill, not a reason to silently patch around it here.

# State-driven avatar animation

## Problem

The astronaut currently only reacts to explicit `gesture` messages from the companion service (`OnGestureReceived` → `AvatarGestureComponent::PlayGesture`). It has no animation tied to the avatar's broader conversational state: mic open, waiting on the LLM, or actively speaking. Idle is already handled separately and correctly by `AstronautIdleAnimComponent` and is out of scope here.

Goal: while the avatar is listening, thinking, or talking, play the matching animation continuously (looping as needed), automatically interrupted by discrete gesture tags during talking, and hand cleanly back to idle when the turn ends.

## Source of truth for states

Confirmed directly from the companion-service source (`~/Desktop/moonwalkers-companion-service`, `src/server/protocol.ts` and `src/server/sessionHandler.ts`), not assumed:

- `AvatarState = "idle" | "listening" | "thinking" | "talking"`, sent as `{ type: "state_change", state: ... }`.
- Sequence per turn: `listening` (session_start) → `thinking` (runTurn starts) → `talking` (first sentence emitted) → `idle` (turn_complete).
- Pressing push-to-talk again while a turn is active (`onSessionStart` with `this.activeAbort` set) aborts the current turn server-side and always proceeds to send a fresh `listening` state once STT starts — same as a cold start. No special "interrupted" state exists or is needed; a talking→listening transition is just an ordinary state transition from the client's point of view.
- This is already wired into `UCompanionServiceSubsystem::OnAvatarStateChange` (broadcast on every `state_change` message) and `OnGestureReceived` (broadcast on every `gesture` message). No backend or protocol changes needed — this entire feature is client-side logic reacting to signals that already exist.

Also relevant: `UCompanionServiceSubsystem::HandleMessage` already calls `StopAndClearAudioQueue()` whenever `state == "listening"`, so audio cutoff and our animation switch will be driven off the same message, staying in sync automatically.

## Non-goals

- Idle behavior: untouched. `AstronautIdleAnimComponent` keeps owning it.
- No new assets: `AS_Listening_Montage`, `AS_Thinking_Montage`, `AS_Talkingloop_Montage`, `AS_Talking2Loop_Montage` already exist and just need two of them (`Listening`, `Thinking`) assigned as new component properties.
- No backend/protocol changes.

## Architecture

Extend `AvatarGestureComponent` in place (no new component, no rename). It already owns `Montage_Play` plumbing, the skeletal-mesh lookup, and — notably — already has `TalkingloopMontage`/`Talking2LoopMontage` properties from the earlier gesture-vocabulary rename, unused until now. Renaming the class would re-trigger the exact "per-instance property override resets to None" bug already hit and fixed once this session, for no functional benefit.

### New properties

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
TObjectPtr<UAnimMontage> ListeningMontage;   // AS_Listening_Montage

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gestures")
TObjectPtr<UAnimMontage> ThinkingStateMontage;    // AS_Thinking_Montage
```

Note: `ThinkingMontage` already exists on the component as the *gesture-tag* montage (`Thinking` is one of the 6 vocabulary tags, played when the LLM emits it mid-sentence while already talking). That's a different trigger moment from the new avatar-state `"thinking"` (waiting for the LLM/RAG response, before any talking starts), so the new property is named `ThinkingStateMontage` to avoid reusing — and conflating — an existing, differently-purposed property. Both may end up pointing at the same `AS_Thinking_Montage` asset in the editor, or not — that's an editor-time choice, not a code one.

### New state

```cpp
FString CurrentAvatarState = TEXT("idle");

UPROPERTY()
TObjectPtr<UAstronautIdleAnimComponent> IdleAnimComponent = nullptr; // cached in BeginPlay via FindComponentByClass
```

### BeginPlay

In addition to the existing `OnGestureReceived` subscription, also bind `OnAvatarStateChange` → `HandleAvatarStateChange`. Cache `IdleAnimComponent` via `FindComponentByClass<UAstronautIdleAnimComponent>()` (unambiguous — only one exists on the actor).

### HandleAvatarStateChange(State)

```
CurrentAvatarState = State

if State == "idle":
    IdleAnimComponent->StopIdleLoop()
    IdleAnimComponent->StartIdleLoop()   // must Stop then Start — Start no-ops if its
                                          // internal flag thinks it's already running,
                                          // and its own end-delegate bails out on
                                          // bInterrupted without ever resuming itself.
elif State == "listening":
    IdleAnimComponent->StopIdleLoop()
    PlayLoopingStateMontage(ListeningMontage, State)
elif State == "thinking":
    IdleAnimComponent->StopIdleLoop()
    PlayLoopingStateMontage(ThinkingStateMontage, State)
elif State == "talking":
    IdleAnimComponent->StopIdleLoop()
    PlayRandomTalkingLoopClip()
```

### Looping mechanism

`PlayLoopingStateMontage(Montage, OwningState)`: `Montage_Play(Montage)`, then bind an `FOnMontageEnded` delegate. On end: if `bInterrupted` OR `CurrentAvatarState != OwningState`, stop (don't chain). Otherwise call itself again with the same montage.

`PlayRandomTalkingLoopClip()`: same shape, but each successful (non-interrupted, still-`talking`) chain picks `Talkingloop` or `Talking2Loop` at random (per approved answer — matches the idle system's randomized, non-repeating feel) rather than always replaying the same clip.

This single `bInterrupted` + state-match check is sufficient to correctly handle every transition — state changes, gesture interrupts, rapid re-interrupts, and turn-ending — without special-casing each one individually.

### Gesture interrupt/resume during talking

`HandleGestureReceived` (existing) also binds an end delegate to the gesture montage it plays. When that montage ends without interruption and `CurrentAvatarState` is still `"talking"`, call `PlayRandomTalkingLoopClip()` to resume. If the state moved on before the gesture finished (e.g. turn ended, or the user interrupted with push-to-talk), the state-mismatch check means it correctly does nothing.

### Talking → interrupted → listening

No special code path. The server always sends a fresh `listening` state on a new `session_start`, even mid-turn. `HandleAvatarStateChange("listening")` fires exactly as it would from idle: stops the idle loop (harmless no-op state-wise, it wasn't running), plays `ListeningMontage`, which naturally interrupts whatever was on the shared slot. The interrupted montage's own end-delegate correctly declines to resume because `CurrentAvatarState` no longer matches.

### Failure mode

If `ListeningMontage` or `ThinkingStateMontage` isn't assigned, log a warning (matching `PlayGesture`'s existing "No AnimMontage assigned" pattern) and skip — no crash.

## Testing

No automated test harness exists for Blueprint/animation behavior in this project; verification is manual in PIE, per existing project convention (see prior session's caption-fix and gesture-montage verification, both confirmed via live PIE + log inspection). Plan:

1. Compile, assign `ListeningMontage`/`ThinkingStateMontage` in the editor, save.
2. PIE: press push-to-talk → confirm Listening montage plays and loops while held.
3. Release → confirm Thinking montage plays and loops until a reply starts.
4. Confirm Talkingloop/Talking2Loop alternate randomly while the reply speaks.
5. Confirm a gesture tag mid-reply interrupts the talk loop, plays the gesture, then resumes the talk loop.
6. Confirm turn completion returns cleanly to the idle system's own animation (not frozen).
7. Press push-to-talk again mid-reply — confirm immediate switch to Listening, audio cuts, and no stale resume-to-talking occurs afterward.

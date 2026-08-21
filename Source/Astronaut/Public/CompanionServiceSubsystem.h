#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/TimerHandle.h"
#include "CompanionServiceSubsystem.generated.h"

class IWebSocket;
class FJsonObject;
class UAudioComponent;
class UMetaHumanSpeechLipSyncComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAvatarStateChange, const FString&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranscriptReceived, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnReplyTextReceived, const FString&, Text, int32, SentenceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGestureReceived, const FString&, Tag, int32, SentenceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectionStatusChanged, bool, bOnline);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLLMFinishedTalking);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSentenceAudioFinished, int32, SentenceIndex);

/**
 * WebSocket client for the Moonwalkers Holobox companion service
 * (src/server/protocol.ts in moonwalkers-companion-service). Owns the single
 * persistent connection to ws://127.0.0.1:9000, matches the fixed message
 * contract exactly, and is the one place in the UE5 app that talks to it.
 *
 * A GameInstanceSubsystem rather than an actor component because this is a
 * single kiosk-wide connection, not something per-actor or tied to a level.
 */
UCLASS()
class ASTRONAUT_API UCompanionServiceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * Native subsystem lookup for Blueprint call sites that need it repeatedly
     * (e.g. per audio-buffer callback). The MCP-generated K2Node_GetSubsystem
     * Blueprint node reliably resolves only its first occurrence in a project;
     * additional instances of that node return null. Routing every repeated
     * lookup through this native function avoids the broken node entirely.
     */
    UFUNCTION(BlueprintCallable, Category = "Companion Service", meta = (WorldContext = "WorldContextObject"))
    static UCompanionServiceSubsystem* GetCompanionService(const UObject* WorldContextObject);

    /** ws://host:port of the companion service. Defaults to the local dev port from .env.example. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Companion Service")
    FString ServerUrl = TEXT("ws://127.0.0.1:9000");

    /** True once the socket has completed its handshake with the companion service. */
    UPROPERTY(BlueprintReadOnly, Category = "Companion Service")
    bool bIsConnected = false;

    /** True between BeginListening() and EndListening() — audio_chunk is only sent while this is true. */
    UPROPERTY(BlueprintReadOnly, Category = "Companion Service")
    bool bIsListening = false;

    /** Opens the WebSocket connection. Safe to call again if already connected/connecting. */
    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void Connect();

    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void Disconnect();

    /**
     * Push-to-talk pressed: sends session_start (this also doubles as the
     * protocol's interrupt signal if the avatar is mid-turn) and opens the
     * audio_chunk stream.
     */
    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void BeginListening();

    /** Push-to-talk released: stops streaming audio_chunk and sends push_to_talk_released. */
    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void EndListening();

    /** Visitor-initiated end of conversation (US-7) — clears server-side history, returns avatar to idle. */
    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void EndConversation();

    /**
     * Streams one captured PCM buffer to the server as an audio_chunk
     * message. Expects 16-bit signed PCM, 16kHz, mono (what
     * UMicCaptureComponent::OnAudioBufferCaptured already produces) — this
     * function does not resample. No-ops if not currently listening/connected.
     */
    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void SendAudioChunk(const TArray<uint8>& PCM16Data);

    /**
     * Registers the component that receives progressively-fed PCM16 audio
     * for real-time facial-curve solving while replies play back. Call once
     * at BeginPlay with the lip sync component on the avatar actor.
     */
    UFUNCTION(BlueprintCallable, Category = "Companion Service")
    void RegisterLipSyncComponent(UMetaHumanSpeechLipSyncComponent* Component);

    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnAvatarStateChange OnAvatarStateChange;

    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnTranscriptReceived OnTranscriptReceived;

    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnReplyTextReceived OnReplyTextReceived;

    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnGestureReceived OnGestureReceived;

    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnConnectionStatusChanged OnConnectionStatusChanged;

    /**
     * Fires once after the complete LLM reply has finished playing locally.
     * It does not fire when playback is interrupted by a new listening turn
     * or by EndConversation().
     */
    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnLLMFinishedTalking OnLLMFinishedTalking;

    /**
     * Fires each time one sentence's audio clip finishes playing locally,
     * carrying that sentence's index. Unlike OnLLMFinishedTalking (whole
     * reply), this lets UI (e.g. captions) stay paced to what's actually
     * audible sentence-by-sentence instead of a fixed timer.
     */
    UPROPERTY(BlueprintAssignable, Category = "Companion Service")
    FOnSentenceAudioFinished OnSentenceAudioFinished;

private:
    TSharedPtr<IWebSocket> Socket;
    FTimerHandle ReconnectTimerHandle;
    bool bIntentionalDisconnect = false;
    // Guards against overlapping Connect() calls creating two live sockets at
    // once (observed at PIE start: two Connect() calls landing milliseconds
    // apart raced two TCP connections against the server's single-client
    // slot, which then fought each other indefinitely via auto-reconnect).
    bool bIsConnecting = false;

    void HandleConnected();
    void HandleConnectionError(const FString& Error);
    void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void HandleMessage(const FString& MessageString);

    void SendJson(const TSharedRef<FJsonObject>& JsonObject);

    /** One decoded sentence of ElevenLabs audio, waiting its turn to play. */
    struct FQueuedAudioClip
    {
        TArray<uint8> Pcm16Bytes;
        int32 SampleRate = 0;
        int32 NumChannels = 0;
        int32 SentenceIndex = 0;
    };

    TArray<FQueuedAudioClip> AudioQueue;

    UPROPERTY()
    TObjectPtr<UAudioComponent> ActiveAudioComponent = nullptr;

    // Sentence index of the clip ActiveAudioComponent is currently playing,
    // so HandleQueuedAudioFinished knows which sentence just finished.
    int32 ActiveClipSentenceIndex = 0;

    FTimerHandle NextClipTimerHandle;
    FTimerHandle LipSyncFeedTimerHandle;

    UPROPERTY()
    TObjectPtr<UMetaHumanSpeechLipSyncComponent> LipSyncComponent = nullptr;

    TArray<uint8> ActiveClipLipSyncPcm16;
    int32 ActiveClipLipSyncSampleRate = 0;
    int32 ActiveClipLipSyncNumChannels = 0;
    int32 LipSyncFeedByteOffset = 0;

    // The service can stream several sentence clips for one reply. Waiting
    // for its final idle state as well as an empty local queue prevents the
    // completion event from firing in a short gap between sentence clips.
    bool bLLMTurnInProgress = false;
    bool bServerFinishedLLMTurn = false;
    bool bReceivedAudioForLLMTurn = false;

    void PlayReceivedAudio(const FString& Base64Mp3, int32 SentenceIndex);
    void PlayNextQueuedAudio();
    void FeedLipSyncAudioTick();
    void TryBroadcastLLMFinishedTalking();

    UFUNCTION()
    void HandleQueuedAudioFinished();

    void StopAndClearAudioQueue();
};

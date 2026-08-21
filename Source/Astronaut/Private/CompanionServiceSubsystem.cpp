#include "CompanionServiceSubsystem.h"
#include "Mp3Decoder.h"

#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "MetaHumanSpeechLipSyncComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionService, Log, All);

namespace CompanionServiceProtocol
{
    constexpr float ReconnectDelaySeconds = 3.0f;
}

UCompanionServiceSubsystem* UCompanionServiceSubsystem::GetCompanionService(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }
    if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr)
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            return GameInstance->GetSubsystem<UCompanionServiceSubsystem>();
        }
    }
    return nullptr;
}

void UCompanionServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FModuleManager::Get().LoadModuleChecked<FWebSocketsModule>("WebSockets");
    Connect();
}

void UCompanionServiceSubsystem::Deinitialize()
{
    if (Socket.IsValid())
    {
        Socket->Close();
        Socket.Reset();
    }
    Super::Deinitialize();
}

void UCompanionServiceSubsystem::Connect()
{
    if (bIsConnecting || (Socket.IsValid() && Socket->IsConnected()))
    {
        return;
    }

    bIsConnecting = true;
    bIntentionalDisconnect = false;
    Socket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);

    Socket->OnConnected().AddUObject(this, &UCompanionServiceSubsystem::HandleConnected);
    Socket->OnConnectionError().AddUObject(this, &UCompanionServiceSubsystem::HandleConnectionError);
    Socket->OnClosed().AddUObject(this, &UCompanionServiceSubsystem::HandleClosed);
    Socket->OnMessage().AddUObject(this, &UCompanionServiceSubsystem::HandleMessage);

    UE_LOG(LogCompanionService, Log, TEXT("Connecting to companion service at %s"), *ServerUrl);
    Socket->Connect();
}

void UCompanionServiceSubsystem::Disconnect()
{
    bIntentionalDisconnect = true;
    bIsConnecting = false;
    bIsListening = false;

    if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
    {
        World->GetTimerManager().ClearTimer(ReconnectTimerHandle);
    }

    if (Socket.IsValid())
    {
        Socket->Close();
    }
}

void UCompanionServiceSubsystem::HandleConnected()
{
    bIsConnected = true;
    bIsConnecting = false;
    UE_LOG(LogCompanionService, Log, TEXT("Companion service connected"));
}

void UCompanionServiceSubsystem::HandleConnectionError(const FString& Error)
{
    bIsConnected = false;
    bIsConnecting = false;
    UE_LOG(LogCompanionService, Warning, TEXT("Companion service connection error: %s"), *Error);

    if (!bIntentionalDisconnect)
    {
        if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
        {
            World->GetTimerManager().SetTimer(
                ReconnectTimerHandle, this, &UCompanionServiceSubsystem::Connect,
                CompanionServiceProtocol::ReconnectDelaySeconds, /*bLoop=*/false);
        }
    }
}

void UCompanionServiceSubsystem::HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    bIsConnected = false;
    bIsConnecting = false;
    bIsListening = false;
    UE_LOG(LogCompanionService, Warning, TEXT("Companion service connection closed (code=%d, clean=%s): %s"),
        StatusCode, bWasClean ? TEXT("true") : TEXT("false"), *Reason);

    const bool bWasReplaced = Reason == TEXT("replaced by new connection");

    if (!bIntentionalDisconnect && !bWasReplaced)
    {
        if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
        {
            World->GetTimerManager().SetTimer(
                ReconnectTimerHandle, this, &UCompanionServiceSubsystem::Connect,
                CompanionServiceProtocol::ReconnectDelaySeconds, /*bLoop=*/false);
        }
    }
}

void UCompanionServiceSubsystem::SendJson(const TSharedRef<FJsonObject>& JsonObject)
{
    if (!Socket.IsValid() || !bIsConnected)
    {
        return;
    }

    FString Payload;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(JsonObject, Writer);
    Socket->Send(Payload);
}

void UCompanionServiceSubsystem::BeginListening()
{
    if (!bIsConnected)
    {
        UE_LOG(LogCompanionService, Warning, TEXT("BeginListening called while not connected to companion service"));
        return;
    }

    bIsListening = true;

    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("type"), TEXT("session_start"));
    SendJson(Json);
}

void UCompanionServiceSubsystem::EndListening()
{
    if (!bIsListening)
    {
        return;
    }
    bIsListening = false;

    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("type"), TEXT("push_to_talk_released"));
    SendJson(Json);
}

void UCompanionServiceSubsystem::EndConversation()
{
    bIsListening = false;
    StopAndClearAudioQueue();

    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("type"), TEXT("end_conversation"));
    SendJson(Json);
}

void UCompanionServiceSubsystem::SendAudioChunk(const TArray<uint8>& PCM16Data)
{
    if (!bIsListening || !bIsConnected || PCM16Data.Num() == 0)
    {
        return;
    }

    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("type"), TEXT("audio_chunk"));
    Json->SetStringField(TEXT("data"), FBase64::Encode(PCM16Data));
    SendJson(Json);
}

void UCompanionServiceSubsystem::RegisterLipSyncComponent(UMetaHumanSpeechLipSyncComponent* Component)
{
    LipSyncComponent = Component;
}

void UCompanionServiceSubsystem::HandleMessage(const FString& MessageString)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageString);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        UE_LOG(LogCompanionService, Warning, TEXT("Failed to parse companion service message: %s"), *MessageString);
        return;
    }

    FString Type;
    if (!Json->TryGetStringField(TEXT("type"), Type))
    {
        return;
    }

    if (Type == TEXT("state_change"))
    {
        FString State;
        Json->TryGetStringField(TEXT("state"), State);
        if (State == TEXT("listening"))
        {
            StopAndClearAudioQueue();
        }
        else if (State == TEXT("thinking") || State == TEXT("speaking"))
        {
            // "thinking" starts the reply turn. Keep the same turn alive when
            // it advances to "speaking" so streamed sentence clips count as
            // one answer rather than separate completion events.
            if (!bLLMTurnInProgress)
            {
                bLLMTurnInProgress = true;
                bReceivedAudioForLLMTurn = false;
            }
            bServerFinishedLLMTurn = false;
        }
        else if (State == TEXT("idle") && bLLMTurnInProgress)
        {
            bServerFinishedLLMTurn = true;
            TryBroadcastLLMFinishedTalking();
        }
        OnAvatarStateChange.Broadcast(State);
    }
    else if (Type == TEXT("transcript"))
    {
        FString Text;
        Json->TryGetStringField(TEXT("text"), Text);
        OnTranscriptReceived.Broadcast(Text);
    }
    else if (Type == TEXT("reply_text"))
    {
        FString Text;
        int32 SentenceIndex = 0;
        Json->TryGetStringField(TEXT("text"), Text);
        Json->TryGetNumberField(TEXT("sentence_index"), SentenceIndex);
        OnReplyTextReceived.Broadcast(Text, SentenceIndex);
    }
    else if (Type == TEXT("audio_chunk"))
    {
        FString Data;
        int32 SentenceIndex = 0;
        Json->TryGetNumberField(TEXT("sentence_index"), SentenceIndex);
        if (Json->TryGetStringField(TEXT("data"), Data))
        {
            PlayReceivedAudio(Data, SentenceIndex);
        }
    }
    else if (Type == TEXT("gesture"))
    {
        FString Tag;
        int32 SentenceIndex = 0;
        Json->TryGetStringField(TEXT("tag"), Tag);
        Json->TryGetNumberField(TEXT("sentence_index"), SentenceIndex);
        OnGestureReceived.Broadcast(Tag, SentenceIndex);
    }
    else if (Type == TEXT("connection_status"))
    {
        FString Status;
        Json->TryGetStringField(TEXT("status"), Status);
        OnConnectionStatusChanged.Broadcast(Status == TEXT("online"));
    }
    else
    {
        UE_LOG(LogCompanionService, Warning, TEXT("Unknown companion service message type: %s"), *Type);
    }
}

void UCompanionServiceSubsystem::PlayReceivedAudio(const FString& Base64Mp3, int32 SentenceIndex)
{
    TArray<uint8> Mp3Bytes;
    if (!FBase64::Decode(Base64Mp3, Mp3Bytes))
    {
        UE_LOG(LogCompanionService, Warning, TEXT("audio_chunk had invalid base64 data"));
        return;
    }

    FQueuedAudioClip Clip;
    if (!FMp3Decoder::DecodeToPcm16(Mp3Bytes, Clip.Pcm16Bytes, Clip.SampleRate, Clip.NumChannels) || Clip.Pcm16Bytes.Num() == 0)
    {
        UE_LOG(LogCompanionService, Warning, TEXT("Failed to decode audio_chunk MP3 payload"));
        return;
    }
    Clip.SentenceIndex = SentenceIndex;

    // Be tolerant of a service that starts sending audio before its
    // state_change("speaking") notification reaches this client.
    bLLMTurnInProgress = true;
    bReceivedAudioForLLMTurn = true;

    const bool bWasIdle = (AudioQueue.Num() == 0 && ActiveAudioComponent == nullptr);
    AudioQueue.Add(MoveTemp(Clip));

    if (bWasIdle)
    {
        PlayNextQueuedAudio();
    }
}

void UCompanionServiceSubsystem::PlayNextQueuedAudio()
{
    if (AudioQueue.Num() == 0)
    {
        ActiveAudioComponent = nullptr;
        return;
    }

    FQueuedAudioClip Clip = MoveTemp(AudioQueue[0]);
    AudioQueue.RemoveAt(0);
    ActiveClipSentenceIndex = Clip.SentenceIndex;

    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World)
    {
        ActiveAudioComponent = nullptr;
        return;
    }

    USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(Clip.SampleRate);
    Wave->NumChannels = Clip.NumChannels;
    const float ClipDuration = static_cast<float>(Clip.Pcm16Bytes.Num() / sizeof(int16)) / Clip.NumChannels / Clip.SampleRate;
    Wave->Duration = ClipDuration;
    Wave->SoundGroup = SOUNDGROUP_Voice;
    Wave->bLooping = false;
    Wave->QueueAudio(Clip.Pcm16Bytes.GetData(), Clip.Pcm16Bytes.Num());

    ActiveAudioComponent = UGameplayStatics::SpawnSound2D(World, Wave);
    if (ActiveAudioComponent)
    {
        World->GetTimerManager().SetTimer(
            NextClipTimerHandle, this, &UCompanionServiceSubsystem::HandleQueuedAudioFinished,
            FMath::Max(ClipDuration, 0.01f), /*bLoop=*/false);

        if (LipSyncComponent)
        {
            ActiveClipLipSyncPcm16 = Clip.Pcm16Bytes;
            ActiveClipLipSyncSampleRate = Clip.SampleRate;
            ActiveClipLipSyncNumChannels = Clip.NumChannels;
            LipSyncFeedByteOffset = 0;

            World->GetTimerManager().SetTimer(
                LipSyncFeedTimerHandle, this, &UCompanionServiceSubsystem::FeedLipSyncAudioTick, 0.02f, /*bLoop=*/true);
        }
    }
    else
    {
        PlayNextQueuedAudio();
    }
}

void UCompanionServiceSubsystem::FeedLipSyncAudioTick()
{
    if (!LipSyncComponent || ActiveClipLipSyncPcm16.Num() == 0)
    {
        return;
    }

    const int32 BytesPerFrame = sizeof(int16) * FMath::Max(ActiveClipLipSyncNumChannels, 1);
    const int32 ChunkBytes = FMath::Max(BytesPerFrame,
        static_cast<int32>(ActiveClipLipSyncSampleRate * 0.02f) * BytesPerFrame);
    const int32 Remaining = ActiveClipLipSyncPcm16.Num() - LipSyncFeedByteOffset;

    if (Remaining <= 0)
    {
        if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
        {
            World->GetTimerManager().ClearTimer(LipSyncFeedTimerHandle);
        }
        return;
    }

    const int32 ThisChunk = FMath::Min(ChunkBytes, Remaining);
    TArray<uint8> Chunk(ActiveClipLipSyncPcm16.GetData() + LipSyncFeedByteOffset, ThisChunk);
    LipSyncComponent->FeedAudioSamples(Chunk, ActiveClipLipSyncSampleRate, ActiveClipLipSyncNumChannels);
    LipSyncFeedByteOffset += ThisChunk;
}

void UCompanionServiceSubsystem::HandleQueuedAudioFinished()
{
    ActiveAudioComponent = nullptr;
    OnSentenceAudioFinished.Broadcast(ActiveClipSentenceIndex);
    PlayNextQueuedAudio();
    TryBroadcastLLMFinishedTalking();
}

void UCompanionServiceSubsystem::TryBroadcastLLMFinishedTalking()
{
    if (!bLLMTurnInProgress || !bServerFinishedLLMTurn || !bReceivedAudioForLLMTurn ||
        ActiveAudioComponent || AudioQueue.Num() > 0)
    {
        return;
    }

    // Reset before broadcasting so a Blueprint listener that immediately
    // starts another turn cannot cause this reply to complete a second time.
    bLLMTurnInProgress = false;
    bServerFinishedLLMTurn = false;
    bReceivedAudioForLLMTurn = false;

    UE_LOG(LogCompanionService, Log, TEXT("LLM reply finished playing"));
    OnLLMFinishedTalking.Broadcast();
}

void UCompanionServiceSubsystem::StopAndClearAudioQueue()
{
    AudioQueue.Empty();

    if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
    {
        World->GetTimerManager().ClearTimer(NextClipTimerHandle);
        World->GetTimerManager().ClearTimer(LipSyncFeedTimerHandle);
    }

    if (ActiveAudioComponent)
    {
        ActiveAudioComponent->Stop();
        ActiveAudioComponent = nullptr;
    }

    ActiveClipLipSyncPcm16.Reset();
    LipSyncFeedByteOffset = 0;

    bLLMTurnInProgress = false;
    bServerFinishedLLMTurn = false;
    bReceivedAudioForLLMTurn = false;
}

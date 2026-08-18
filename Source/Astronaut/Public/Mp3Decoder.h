#pragma once

#include "CoreMinimal.h"

/**
 * Thin wrapper around the embedded dr_mp3 single-header decoder
 * (ThirdParty/dr_mp3/dr_mp3.h, public domain / MIT-0) so the rest of the
 * module never touches the C library directly. Used to play back ElevenLabs
 * TTS audio, which the companion service sends as base64 MP3
 * (see protocol.ts AudioChunkOutMessage).
 */
class ASTRONAUT_API FMp3Decoder
{
public:
    /**
     * Fully decodes an in-memory MP3 buffer to interleaved signed 16-bit PCM.
     * Returns false if the buffer isn't valid MP3 data.
     */
    static bool DecodeToPcm16(const TArray<uint8>& Mp3Bytes, TArray<uint8>& OutPCM16Bytes,
        int32& OutSampleRate, int32& OutNumChannels);
};

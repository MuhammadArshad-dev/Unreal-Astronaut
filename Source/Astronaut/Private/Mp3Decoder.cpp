#include "Mp3Decoder.h"

// Single translation unit that compiles the dr_mp3 implementation. Every
// other file only ever sees Mp3Decoder.h, never dr_mp3.h directly.
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3/dr_mp3.h"

bool FMp3Decoder::DecodeToPcm16(const TArray<uint8>& Mp3Bytes, TArray<uint8>& OutPCM16Bytes,
    int32& OutSampleRate, int32& OutNumChannels)
{
    OutPCM16Bytes.Reset();
    OutSampleRate = 0;
    OutNumChannels = 0;

    if (Mp3Bytes.Num() == 0)
    {
        return false;
    }

    drmp3 Decoder;
    if (!drmp3_init_memory(&Decoder, Mp3Bytes.GetData(), Mp3Bytes.Num(), nullptr))
    {
        return false;
    }

    const drmp3_uint64 TotalFrames = drmp3_get_pcm_frame_count(&Decoder);
    if (TotalFrames == 0)
    {
        drmp3_uninit(&Decoder);
        return false;
    }

    OutSampleRate = static_cast<int32>(Decoder.sampleRate);
    OutNumChannels = static_cast<int32>(Decoder.channels);

    const int64 TotalSamples = static_cast<int64>(TotalFrames) * OutNumChannels;
    OutPCM16Bytes.SetNumUninitialized(TotalSamples * sizeof(int16));

    const drmp3_uint64 FramesRead = drmp3_read_pcm_frames_s16(
        &Decoder, TotalFrames, reinterpret_cast<drmp3_int16*>(OutPCM16Bytes.GetData()));

    drmp3_uninit(&Decoder);

    if (FramesRead == 0)
    {
        OutPCM16Bytes.Reset();
        return false;
    }

    // The decoder can legitimately return fewer frames than the estimated
    // count (e.g. a truncated/estimated-VBR stream) — trim rather than ship
    // uninitialized tail bytes to the audio engine.
    OutPCM16Bytes.SetNum(static_cast<int64>(FramesRead) * OutNumChannels * sizeof(int16), EAllowShrinking::No);

    return true;
}

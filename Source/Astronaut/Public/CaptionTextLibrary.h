#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CaptionTextLibrary.generated.h"

UCLASS()
class ASTRONAUT_API UCaptionTextLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Hard-wraps FullText to WordsPerLine words per line, joined with "\n".
     * Native word-wrap rather than UMG autowrap because autowrap breaks on
     * pixel width, not word count. Does NOT truncate — the caption box must
     * size itself to whatever this returns (see WBP_PushToTalk.CaptionText's
     * CanvasPanelSlot, which needs bAutoSize=true). A fixed-height box with
     * this text and Clip overflow silently clips the earliest lines off the
     * top as the response grows, which reads as "the first half is missing".
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Captions")
    static FString FormatCaption(const FString& FullText, int32 WordsPerLine = 10);

    /**
     * Splits FullText's wrapped lines (as produced by FormatCaption) into
     * pages of at most MaxLines lines each, so a very long response is shown
     * a page at a time instead of ever being clipped or shrunk.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Captions")
    static TArray<FString> PaginateCaption(const FString& WrappedText, int32 MaxLines = 4);
};

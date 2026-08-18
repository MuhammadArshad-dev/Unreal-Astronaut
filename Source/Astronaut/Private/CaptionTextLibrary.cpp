#include "CaptionTextLibrary.h"

FString UCaptionTextLibrary::FormatCaption(const FString& FullText, int32 WordsPerLine)
{
    WordsPerLine = FMath::Max(WordsPerLine, 1);

    TArray<FString> Words;
    FullText.ParseIntoArray(Words, TEXT(" "), true);

    FString Result;
    for (int32 i = 0; i < Words.Num(); ++i)
    {
        Result += Words[i];
        const bool bLastWord = (i == Words.Num() - 1);
        if (!bLastWord)
        {
            Result += ((i + 1) % WordsPerLine == 0) ? TEXT("\n") : TEXT(" ");
        }
    }
    return Result;
}

TArray<FString> UCaptionTextLibrary::PaginateCaption(const FString& WrappedText, int32 MaxLines)
{
    MaxLines = FMath::Max(MaxLines, 1);

    TArray<FString> Lines;
    WrappedText.ParseIntoArray(Lines, TEXT("\n"), false);

    TArray<FString> Pages;
    for (int32 i = 0; i < Lines.Num(); i += MaxLines)
    {
        const int32 PageLineCount = FMath::Min(MaxLines, Lines.Num() - i);
        FString Page = Lines[i];
        for (int32 j = 1; j < PageLineCount; ++j)
        {
            Page += TEXT("\n");
            Page += Lines[i + j];
        }
        Pages.Add(Page);
    }

    if (Pages.Num() == 0)
    {
        Pages.Add(FString());
    }
    return Pages;
}

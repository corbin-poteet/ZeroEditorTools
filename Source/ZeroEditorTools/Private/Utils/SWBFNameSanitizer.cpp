// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFNameSanitizer.h"
#include "Misc/Char.h"
#include "ObjectTools.h"

FString FSWBFNameSanitizer::SanitizeTextureName(const FString& RawName, const FString& LevelName)
{
	return SanitizeName(RawName, TEXT("T_"), LevelName);
}

FString FSWBFNameSanitizer::SanitizeMeshName(const FString& RawName, const FString& LevelName)
{
	return SanitizeName(RawName, TEXT("SM_"), LevelName);
}

FString FSWBFNameSanitizer::SanitizeMaterialName(const FString& RawName, const FString& LevelName)
{
	return SanitizeName(RawName, TEXT("MI_"), LevelName);
}

FString FSWBFNameSanitizer::SanitizeName(const FString& RawName, const FString& Prefix, const FString& LevelName)
{
	if (RawName.IsEmpty())
	{
		return Prefix + TEXT("Unknown");
	}

	// Split on underscores
	TArray<FString> Segments;
	RawName.ParseIntoArray(Segments, TEXT("_"), true);

	if (Segments.Num() == 0)
	{
		return Prefix + TEXT("Unknown");
	}

	TArray<FString> ResultSegments;

	// Check if the first segment is a level prefix that matches LevelName
	bool bHasLevelPrefix = false;
	int32 StartIdx = 0;

	if (Segments.Num() > 0 && IsLevelPrefix(Segments[0]))
	{
		bHasLevelPrefix = true;
		ResultSegments.Add(Segments[0].ToUpper());
		StartIdx = 1;
	}

	// Insert level name if not already present as the first segment
	if (!bHasLevelPrefix && !LevelName.IsEmpty())
	{
		ResultSegments.Add(LevelName.ToUpper());
	}

	// Track whether the final segment ends up being a number
	FString LastNumericSuffix;

	for (int32 i = StartIdx; i < Segments.Num(); ++i)
	{
		FString Segment = Segments[i];

		// PascalCase the segment, then separate trailing numbers
		FString Processed = SeparateTrailingNumbers(Segment);

		// SeparateTrailingNumbers may produce segments with underscores
		TArray<FString> SubSegments;
		Processed.ParseIntoArray(SubSegments, TEXT("_"), true);

		for (const FString& Sub : SubSegments)
		{
			// If the sub-segment is purely numeric, keep as-is (it's the separated number)
			bool bAllDigits = true;
			for (int32 c = 0; c < Sub.Len(); ++c)
			{
				if (!FChar::IsDigit(Sub[c]))
				{
					bAllDigits = false;
					break;
				}
			}

			if (bAllDigits)
			{
				ResultSegments.Add(Sub);
			}
			else
			{
				ResultSegments.Add(PascalCaseSegment(Sub));
			}
		}
	}

	// Determine if the last segment is numeric
	bool bEndsWithNumber = false;
	if (ResultSegments.Num() > 0)
	{
		const FString& LastSeg = ResultSegments.Last();
		bEndsWithNumber = true;
		for (int32 c = 0; c < LastSeg.Len(); ++c)
		{
			if (!FChar::IsDigit(LastSeg[c]))
			{
				bEndsWithNumber = false;
				break;
			}
		}
	}

	if (bEndsWithNumber)
	{
		// Pad existing number suffix to 2 digits
		FString& NumSeg = ResultSegments.Last();
		int32 NumVal = FCString::Atoi(*NumSeg);
		NumSeg = FString::Printf(TEXT("%02d"), NumVal);
	}
	else
	{
		// No trailing number — append 01
		ResultSegments.Add(TEXT("01"));
	}

	FString Combined = Prefix + FString::Join(ResultSegments, TEXT("_"));

	// Final pass: ensure name is valid for UE asset system
	Combined = ObjectTools::SanitizeObjectName(Combined);

	return Combined;
}

FString FSWBFNameSanitizer::PascalCaseSegment(const FString& Segment)
{
	if (Segment.IsEmpty())
	{
		return Segment;
	}

	FString Result = Segment.ToLower();
	Result[0] = FChar::ToUpper(Result[0]);
	return Result;
}

FString FSWBFNameSanitizer::SeparateTrailingNumbers(const FString& Segment)
{
	if (Segment.IsEmpty())
	{
		return Segment;
	}

	// Find where trailing digits start
	int32 NumStart = Segment.Len();
	while (NumStart > 0 && FChar::IsDigit(Segment[NumStart - 1]))
	{
		--NumStart;
	}

	// No trailing numbers, or entire segment is numbers
	if (NumStart == 0 || NumStart == Segment.Len())
	{
		return Segment;
	}

	// Split into alpha part and numeric part
	FString AlphaPart = Segment.Left(NumStart);
	FString NumPart = Segment.Mid(NumStart);

	return AlphaPart + TEXT("_") + NumPart;
}

bool FSWBFNameSanitizer::IsLevelPrefix(const FString& Segment)
{
	if (Segment.Len() < 4 || Segment.Len() > 5)
	{
		return false;
	}

	// Pattern: 3-4 alpha characters followed by 1+ digit
	int32 AlphaCount = 0;
	int32 Idx = 0;

	// Count leading alpha characters
	while (Idx < Segment.Len() && FChar::IsAlpha(Segment[Idx]))
	{
		++AlphaCount;
		++Idx;
	}

	if (AlphaCount < 3 || AlphaCount > 4)
	{
		return false;
	}

	// Must have at least one digit remaining
	if (Idx >= Segment.Len())
	{
		return false;
	}

	// All remaining characters must be digits
	while (Idx < Segment.Len())
	{
		if (!FChar::IsDigit(Segment[Idx]))
		{
			return false;
		}
		++Idx;
	}

	return true;
}

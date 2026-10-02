#include "DinoRegistry.h"

#include "DinoInfoData.h"

UDinoInfoData* UDinoRegistry::FindByMarker(const FString& MarkerCode) const
{
	if (MarkerCode.IsEmpty())
	{
		return nullptr;
	}

	for (const TObjectPtr<UDinoInfoData>& Entry : Species)
	{
		if (Entry != nullptr && !Entry->MarkerCode.IsEmpty()
			&& Entry->MarkerCode.Equals(MarkerCode, ESearchCase::IgnoreCase))
		{
			return Entry;
		}
	}

	return nullptr;
}

bool UDinoRegistry::HasAnyMarker() const
{
	for (const TObjectPtr<UDinoInfoData>& Entry : Species)
	{
		if (Entry != nullptr && !Entry->MarkerCode.IsEmpty())
		{
			return true;
		}
	}

	return false;
}

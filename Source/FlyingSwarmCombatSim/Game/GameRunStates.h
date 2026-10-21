#pragma once

#include "CoreMinimal.h"
#include "GameRunStates.generated.h"

UENUM(BlueprintType)
enum class EGameRunState : uint8
{
	Starting,
	Playing,
	Spectating,
	Restarting
};


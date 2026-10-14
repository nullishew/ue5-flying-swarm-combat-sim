#pragma once

#include "CoreMinimal.h"
#include "BoidHealthTypes.generated.h"

UDELEGATE()
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBoidHealthChangedDelegate,
	float, CurrentHealth, float, MaxHealth);
UDELEGATE()
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoidDeathDelegate);

#include "Modules/ModuleManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "NavigationVisualizer.h"
#include "NavigationWaypoint.h"
#include "Misc/CoreDelegates.h"

class FBoidEditorToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (GUnrealEd) RegisterVisualizer();
		else PostEngineInit = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FBoidEditorToolsModule::RegisterVisualizer);
	}
	void RegisterVisualizer()
	{
		if (GUnrealEd && !Visualizer)
		{
			Visualizer = MakeShared<FNavigationVisualizer>();
			GUnrealEd->RegisterComponentVisualizer(UNavigationMarkerComponent::StaticClass()->GetFName(), Visualizer);
			Visualizer->OnRegister();
		}
	}
	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInit);
		if (GUnrealEd && Visualizer) GUnrealEd->UnregisterComponentVisualizer(UNavigationMarkerComponent::StaticClass()->GetFName());
		Visualizer.Reset();
	}
private:
	TSharedPtr<FNavigationVisualizer> Visualizer;
	FDelegateHandle PostEngineInit;
};

IMPLEMENT_MODULE(FBoidEditorToolsModule, FlyingSwarmCombatSimEditorTools)

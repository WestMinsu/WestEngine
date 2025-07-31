#include "Engine.h"
#include "MainGameState.h" 
#include <memory>

int main(void)
{
	WestEngine& engine = WestEngine::GetInstance();
	engine.Init();
	engine.GetStateManager().PushState(std::make_unique<MainGameState>());
	engine.Run();
	engine.Shutdown();
}
#include "Engine.h"
#include "TestScene.h" 
#include <memory>

int main(void)
{
	WestEngine& engine = WestEngine::GetInstance();
	engine.Init();
	engine.GetStateManager().PushState(std::make_unique<TestScene>());
	engine.Run();
	engine.Shutdown();
	return 0;
}
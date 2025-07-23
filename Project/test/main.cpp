#include "WestEngine.h"

int main(void)
{
	WestEngine::GetInstance().Init();
	WestEngine::GetInstance().Run();
	WestEngine::GetInstance().Shutdown();

	return 0;
}
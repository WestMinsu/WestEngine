#include "WestEngine.h"
#include "WindowManager.h"

WestEngine& WestEngine::GetInstance()
{
	static WestEngine instance;
	return instance;
}

WestEngine::WestEngine()
{
	m_windowManager = std::make_unique<WindowManager>();
}

WestEngine::~WestEngine()
{
}

void WestEngine::Init()
{
	m_windowManager->Init(1600, 900, "Hello World!");
}

void WestEngine::Run()
{
	while (m_isRunning)
	{
		m_windowManager->PollEvents();
		m_windowManager->SwapBuffers();
		if (m_windowManager->ShouldClose())
			m_isRunning = false;
	}
}

void WestEngine::Shutdown()
{
	m_windowManager->Shutdown();
}

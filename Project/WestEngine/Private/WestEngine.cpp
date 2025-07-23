#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"

#include "Engine.h"
#include <iostream>

WestEngine& WestEngine::GetInstance()
{
	static WestEngine instance;
	return instance;
}

WestEngine::WestEngine()
{
	m_windowManager = std::make_unique<WindowManager>();
	m_inputManager = std::make_unique<InputManager>();
}

WestEngine::~WestEngine()
{
}

void WestEngine::Init()
{
	m_windowManager->Init(1600, 900, "Hello World!");
	m_inputManager->Init(m_windowManager->GetWindowHandle());
}

void WestEngine::Run()
{
	while (m_isRunning)
	{
		m_windowManager->PollEvents();
	
		if (m_inputManager->IsKeyTriggered(GLFW_KEY_SPACE))
		{
			std::cout << "Space key triggered!" << std::endl;
		}
		if (m_inputManager->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT))
		{
			double x, y;
			m_inputManager->GetMousePosition(x, y);
			std::cout << "Left mouse button pressed at: " << x << ", " << y << std::endl;
		}

		m_inputManager->Update();

		m_windowManager->Clear();
		m_windowManager->SwapBuffers();

		if (m_windowManager->ShouldClose())
			m_isRunning = false;
	}
}

void WestEngine::Shutdown()
{
	m_windowManager->Shutdown();
}

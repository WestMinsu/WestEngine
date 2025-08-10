#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include "Engine.h"
#include <iostream>
#include <GLFW/glfw3.h>

#include "GameObject.h"
#include "SpriteRendererComponent.h"
#include "Texture.h"
#include <glm/gtc/matrix_transform.hpp>

WestEngine& WestEngine::GetInstance()
{
	static WestEngine instance;
	return instance;
}

WestEngine::WestEngine()
{
	m_windowManager = std::make_unique<WindowManager>();
	m_inputManager = std::make_unique<InputManager>();
	m_renderManager = std::make_unique<RenderManager>();
	m_spriteRenderer = std::make_unique<SpriteRenderer>();
	m_stateManager = std::make_unique<StateManager>();
	m_soundManager = std::make_unique<SoundManager>();
	m_physicsManager = std::make_unique<PhysicsManager>();
}

WestEngine::~WestEngine() {}

void WestEngine::Init()
{
	m_windowManager->Init(1600, 900, "WestEngine");
	m_inputManager->Init(m_windowManager->GetWindowHandle());
	m_renderManager->Init();
	m_renderManager->LoadShader("sprite", "Shaders/sprite.vert", "Shaders/sprite.frag");
	m_spriteRenderer->Init(m_renderManager->GetShader("sprite"));
	m_soundManager->Init();
}

void WestEngine::Run()
{
	while (m_isRunning)
	{	
		static float lastTime = 0.f;
		float currentTime = static_cast<float>(glfwGetTime());
		float deltaTime = currentTime - lastTime;
		lastTime = currentTime;

		m_windowManager->PollEvents();
		m_inputManager->Update();

		m_stateManager->Update(deltaTime);
		m_physicsManager->CheckCollisions();
		m_stateManager->Draw();
	
		m_windowManager->SwapBuffers();

		if (m_windowManager->ShouldClose())
			m_isRunning = false;
	}
}

void WestEngine::Shutdown()
{
	m_spriteRenderer->Shutdown();
	m_renderManager->Shutdown();
	m_windowManager->Shutdown();
}
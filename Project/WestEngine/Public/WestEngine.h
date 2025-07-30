#pragma once
#include <memory>

class WindowManager;
class InputManager;
class RenderManager;
class SpriteRenderer;
class StateManager;

class WestEngine
{
public:
	static WestEngine& GetInstance();

	WestEngine(const WestEngine&) = delete;
	void operator=(const WestEngine&) = delete;
	~WestEngine();

	void Init();
	void Run();
	void Shutdown();

	WindowManager& GetWindowManager() { return *m_windowManager; }
	InputManager& GetInputManager() { return *m_inputManager; }
	RenderManager& GetRenderManager() { return *m_renderManager; }
	SpriteRenderer& GetSpriteRenderer() { return *m_spriteRenderer; }
	StateManager& GetStateManager() { return *m_stateManager; }

private:
	WestEngine();

	bool m_isRunning = true;

	std::unique_ptr<WindowManager>   m_windowManager;
	std::unique_ptr<InputManager>    m_inputManager;
	std::unique_ptr<RenderManager>   m_renderManager;
	std::unique_ptr<SpriteRenderer>  m_spriteRenderer;
	std::unique_ptr<StateManager>    m_stateManager;
};
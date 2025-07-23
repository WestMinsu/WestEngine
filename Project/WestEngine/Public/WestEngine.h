#pragma once
#include <memory>

class WindowManager;
class InputManager;

class WestEngine
{
public:
	static WestEngine& GetInstance();

	WestEngine(const WestEngine&) = delete;
	void operator=(const WestEngine&) = delete;

	void Init();
	void Run();
	void Shutdown();

	WindowManager& GetWindowManager() { return *m_windowManager; }
	InputManager& GetInputManager() { return *m_inputManager; }

private:
	WestEngine();
	~WestEngine();

	bool m_isRunning = true;

	std::unique_ptr<WindowManager> m_windowManager;
	std::unique_ptr<InputManager> m_inputManager;
};
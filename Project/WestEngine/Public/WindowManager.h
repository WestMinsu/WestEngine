#pragma once
#include <GLFW/glfw3.h>
#include <string>

class WindowManager
{
public:
	WindowManager();
	~WindowManager();

	bool Init(int width, int height, const std::string& title);

	void Shutdown();

	void PollEvents();
	void SwapBuffers();
	void Clear();

	bool ShouldClose() const;

	GLFWwindow* GetWindowHandle() const { return m_pWindow; }

private:
	GLFWwindow* m_pWindow;
};
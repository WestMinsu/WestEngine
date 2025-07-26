#pragma once
#include <string>

struct GLFWwindow;

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
	static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
	GLFWwindow* m_pWindow;
};
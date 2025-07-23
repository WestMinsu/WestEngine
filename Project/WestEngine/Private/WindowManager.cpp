#include "WindowManager.h"
#include <iostream> 

WindowManager::WindowManager()
{
	m_pWindow = nullptr;
}

WindowManager::~WindowManager()
{
	Shutdown();
}

bool WindowManager::Init(int width, int height, const std::string& title)
{
	if (!glfwInit())
	{
		std::cerr << "Error: Failed to initialize GLFW!" << std::endl;
		return false;
	}

	m_pWindow = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
	if (!m_pWindow)
	{
		std::cerr << "Error: Failed to create GLFW window!" << std::endl;
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(m_pWindow);

	glfwSetWindowUserPointer(m_pWindow, this);

	return true;
}

void WindowManager::Shutdown()
{
	if (m_pWindow)
	{
		glfwDestroyWindow(m_pWindow);
		m_pWindow = nullptr;
	}
	glfwTerminate();
}

void WindowManager::PollEvents()
{
	glfwPollEvents();
}

void WindowManager::SwapBuffers()
{
	glfwSwapBuffers(m_pWindow);
}

void WindowManager::Clear()
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

bool WindowManager::ShouldClose() const
{
	return glfwWindowShouldClose(m_pWindow);
}
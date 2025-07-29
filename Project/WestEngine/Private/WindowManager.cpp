#include "WindowManager.h"

#include <Debug.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

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
		WEST_ERR("Failed to initialize GLFW!");
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	m_pWindow = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
	if (!m_pWindow)
	{
		WEST_ERR("Failed to create GLFW window!");
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(m_pWindow);

	if (!gladLoadGL(glfwGetProcAddress))
	{
		WEST_ERR("Failed to initialize GLAD!");
		return false;
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glViewport(0, 0, width, height);
	glfwSetWindowUserPointer(m_pWindow, this);
	glfwSetFramebufferSizeCallback(m_pWindow, FramebufferSizeCallback);

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

void WindowManager::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	WEST_INFO("framebuffer size changed: (" << width << " * " << height << ")");
	glViewport(0, 0, width, height);
}
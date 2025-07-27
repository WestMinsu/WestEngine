#include "InputManager.h"
#include "Debug.h"

#include <GLFW/glfw3.h>

InputManager* InputManager::s_pInstance = nullptr;

InputManager::InputManager()
{
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		m_currKeyState[i] = false;
		m_prevKeyState[i] = false;
	}
	for (int i = 0; i < NUM_MOUSE_BUTTONS; ++i)
	{
		m_currMouseButtonState[i] = false;
		m_prevMouseButtonState[i] = false;
	}
	m_cursorPosX = 0.0;
	m_cursorPosY = 0.0;
}

InputManager::~InputManager()
{
}

void InputManager::Init(GLFWwindow* pWindow)
{
	s_pInstance = this;

	glfwSetKeyCallback(pWindow, KeyCallback);
	glfwSetMouseButtonCallback(pWindow, MouseButtonCallback);
	glfwSetCursorPosCallback(pWindow, CursorPosCallback);
}

void InputManager::Update()
{
	for (int i = 0; i < NUM_KEYS; ++i)
	{
		m_prevKeyState[i] = m_currKeyState[i];
	}
	for (int i = 0; i < NUM_MOUSE_BUTTONS; ++i)
	{
		m_prevMouseButtonState[i] = m_currMouseButtonState[i];
	}
}

bool InputManager::IsKeyPressed(int key)
{
	return m_currKeyState[key];
}

bool InputManager::IsKeyTriggered(int key)
{
	return m_currKeyState[key] && !m_prevKeyState[key];
}

bool InputManager::IsKeyReleased(int key)
{
	return !m_currKeyState[key] && m_prevKeyState[key];
}

bool InputManager::IsMouseButtonPressed(int button)
{
	return m_currMouseButtonState[button];
}

bool InputManager::IsMouseButtonTriggered(int button)
{
	return m_currMouseButtonState[button] && !m_prevMouseButtonState[button];
}

bool InputManager::IsMouseButtonReleased(int button)
{
	return !m_currMouseButtonState[button] && m_prevMouseButtonState[button];
}

void InputManager::GetMousePosition(double& x, double& y)
{
	x = m_cursorPosX;
	y = m_cursorPosY;
}

void InputManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	WEST_INFO
	(
		"key: " << key << ", " <<

		"scancode: " << scancode << ", " <<

		"action: " << (action == GLFW_PRESS ? "Pressed" :
			action == GLFW_RELEASE ? "Released" :
			action == GLFW_REPEAT ? "Repeat" : "Unknown") << ", " <<

		"mods: " << (mods & GLFW_MOD_CONTROL ? "C" : "-") <<
		(mods & GLFW_MOD_SHIFT ? "S" : "-") <<
		(mods & GLFW_MOD_ALT ? "A" : "-")
	);

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (key >= 0 && key < NUM_KEYS)
	{
		if (action == GLFW_PRESS)
		{
			s_pInstance->m_currKeyState[key] = true;
		}
		else if (action == GLFW_RELEASE)
		{
			s_pInstance->m_currKeyState[key] = false;
		}
	}
}

void InputManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (button >= 0 && button < NUM_MOUSE_BUTTONS)
	{
		if (action == GLFW_PRESS)
		{
			s_pInstance->m_currMouseButtonState[button] = true;
		}
		else if (action == GLFW_RELEASE)
		{
			s_pInstance->m_currMouseButtonState[button] = false;
		}
	}
}

void InputManager::CursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
	s_pInstance->m_cursorPosX = xpos;
	s_pInstance->m_cursorPosY = ypos;
}

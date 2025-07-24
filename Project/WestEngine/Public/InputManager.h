#pragma once
struct GLFWwindow;

constexpr int NUM_KEYS = 350; 
constexpr int NUM_MOUSE_BUTTONS = 8; 

class InputManager
{
public:
	InputManager();
	~InputManager();

	void Init(GLFWwindow* pWindow);

	void Update();

	bool IsKeyPressed(int key);        
	bool IsKeyTriggered(int key);   
	bool IsKeyReleased(int key);   

	bool IsMouseButtonPressed(int button);
	bool IsMouseButtonTriggered(int button);
	bool IsMouseButtonReleased(int button);

	void GetMousePosition(double& x, double& y);

private:
	static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);

	static InputManager* s_pInstance;

	bool m_currKeyState[NUM_KEYS];
	bool m_prevKeyState[NUM_KEYS];
	bool m_currMouseButtonState[NUM_MOUSE_BUTTONS];
	bool m_prevMouseButtonState[NUM_MOUSE_BUTTONS];

	double m_cursorPosX;
	double m_cursorPosY;
};
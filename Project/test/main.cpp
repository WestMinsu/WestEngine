#include <glad/gl.h>
#include <iostream>
#include "Engine.h"
#include <GLFW/include/GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Texture.h"
#include "IndexBuffer.h"
#include "SpriteRenderer.h"

int main(void)
{
	WindowManager windowManager;
	InputManager inputManager;
	RenderManager renderManager;

	if (!windowManager.Init(1280, 720, "WestEngine")) 
		return -1;

	inputManager.Init(windowManager.GetWindowHandle());
	renderManager.Init();

	if (!renderManager.LoadShader("sprite", "Shaders/sprite.vert", "Shaders/sprite.frag")) 
		return -1;

	Texture myTexture;
	if (!myTexture.Load("Assets/my_texture.png")) return -1;

	SpriteRenderer spriteRenderer;
	spriteRenderer.Init(renderManager.GetShader("sprite"));

	glm::mat4 projection = glm::ortho(0.0f, 1280.0f, 0.0f, 720.0f, -1.0f, 1.0f);
	glm::vec2 objectPosition = glm::vec2(600.f, 300.f);
	float moveSpeed = 5.0f;

	while (!windowManager.ShouldClose())
	{
		windowManager.PollEvents();

		if (inputManager.IsKeyPressed(GLFW_KEY_W)) objectPosition.y += moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_S)) objectPosition.y -= moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_A)) objectPosition.x -= moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_D)) objectPosition.x += moveSpeed;

		renderManager.BeginFrame();
		renderManager.Clear(0.1f, 0.1f, 0.3f, 1.0f);

		Shader* spriteShader = renderManager.GetShader("sprite");
		spriteShader->Bind();
		spriteShader->SetUniformMat4("projection", projection);

		spriteRenderer.DrawSprite(myTexture, objectPosition, glm::vec2(200.f, 200.f), 0.f); 

		renderManager.EndFrame();
		windowManager.SwapBuffers();
		inputManager.Update();
	}

	renderManager.Shutdown();
	return 0;
}
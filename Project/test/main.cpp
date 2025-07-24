#include <glad/gl.h>
#include <iostream>
#include "Engine.h"
#include <GLFW/include/GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

int main(void)
{
	//WestEngine::GetInstance().Init();
	//WestEngine::GetInstance().Run();
	//WestEngine::GetInstance().Shutdown();

	WindowManager windowManager;
	InputManager inputManager;
	RenderManager renderManager;

	if (!windowManager.Init(1280, 720, "WestEngine"))
	{
		return -1;
	}

	inputManager.Init(windowManager.GetWindowHandle());
	renderManager.Init();

	if (!renderManager.LoadShader("simple", "Shaders/simple.vert", "Shaders/simple.frag"))
	{
		return -1;
	}

	std::vector<float> vertices = {
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.0f,  0.5f, 0.0f
	};

	auto vertexBuffer = std::make_shared<VertexBuffer>();
	vertexBuffer->Init(vertices);
	auto vertexArray = std::make_shared<VertexArray>();
	vertexArray->Init();
	vertexArray->AddVertexBuffer(vertexBuffer);

	glm::mat4 projection = glm::ortho(0.0f, 1280.0f, 0.0f, 720.0f, -1.0f, 1.0f);

	glm::vec3 trianglePosition = glm::vec3(640.f, 360.f, 0.f);
	float moveSpeed = 5.0f;

	while (!windowManager.ShouldClose())
	{
		windowManager.PollEvents();

		if (inputManager.IsKeyPressed(GLFW_KEY_W))
			trianglePosition.y += moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_S))
			trianglePosition.y -= moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_A))
			trianglePosition.x -= moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_D))
			trianglePosition.x += moveSpeed;

		renderManager.BeginFrame();
		renderManager.Clear(0.1f, 0.1f, 0.3f, 1.0f);

		Shader* simpleShader = renderManager.GetShader("simple");
		if (simpleShader)
		{
			simpleShader->Bind();

			glm::mat4 model = glm::mat4(1.0f);

			model = glm::translate(model, trianglePosition);
			model = glm::scale(model, glm::vec3(100.f, 100.f, 1.f)); 

			simpleShader->SetUniformMat4("projection", projection);
			simpleShader->SetUniformMat4("model", model);

			vertexArray->Bind();
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}

		renderManager.EndFrame();
		windowManager.SwapBuffers();
		inputManager.Update();
	}

	renderManager.Shutdown();
	return 0;
}
#include <glad/gl.h>
#include <iostream>
#include "Engine.h"
#include <GLFW/include/GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Texture.h"
#include <IndexBuffer.h>

int main(void)
{
	WindowManager windowManager;
	InputManager inputManager;
	RenderManager renderManager;

	if (!windowManager.Init(1280, 720, "WestEngine")) return -1;

	inputManager.Init(windowManager.GetWindowHandle());
	renderManager.Init();

	if (!renderManager.LoadShader("simple", "Shaders/simple.vert", "Shaders/simple.frag")) return -1;

	std::vector<float> vertices = {
		-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
		 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
		-0.5f,  0.5f, 0.0f,  0.0f, 1.0f
	};
	std::vector<unsigned int> indices = { 0, 1, 2, 2, 3, 0 };

	auto vbo = std::make_shared<VertexBuffer>();
	vbo->Init(vertices);

	auto ibo = std::make_shared<IndexBuffer>();
	ibo->Init(indices);

	auto vao = std::make_shared<VertexArray>();
	vao->Init();
	vao->AddVertexBuffer(vbo);
	vao->SetIndexBuffer(ibo);

	Texture myTexture;
	if (!myTexture.Load("Assets/Battlemage Idle.png")) 
	{
		return -1;
	}

	glm::mat4 projection = glm::ortho(0.0f, 1280.0f, 0.0f, 720.0f, -1.0f, 1.0f);
	glm::vec3 objectPosition = glm::vec3(640.f, 360.f, 0.f);
	float moveSpeed = 5.0f;

	while (!windowManager.ShouldClose())
	{
		windowManager.PollEvents();

		if (inputManager.IsKeyPressed(GLFW_KEY_W))
			objectPosition.y += moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_S))
			objectPosition.y -= moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_A))
			objectPosition.x -= moveSpeed;
		if (inputManager.IsKeyPressed(GLFW_KEY_D))
			objectPosition.x += moveSpeed;

		renderManager.BeginFrame();
		renderManager.Clear(0.1f, 0.1f, 0.3f, 1.0f);

		Shader* simpleShader = renderManager.GetShader("simple");
		if (simpleShader)
		{
			simpleShader->Bind();

			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, objectPosition);
			model = glm::scale(model, glm::vec3(200.f, 200.f, 1.f));

			simpleShader->SetUniformMat4("projection", projection);
			simpleShader->SetUniformMat4("model", model);

			myTexture.Bind(0);

			vao->Bind();
			glDrawElements(GL_TRIANGLES, vao->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, nullptr);
		}

		renderManager.EndFrame();
		windowManager.SwapBuffers();
		inputManager.Update();
	}

	renderManager.Shutdown();
	return 0;
}
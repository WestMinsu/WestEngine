#include <glad/gl.h>
#include <iostream>
#include "Engine.h"

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
		std::cerr << "Failed to load simple shader!" << std::endl;
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

	while (!windowManager.ShouldClose())
	{
		windowManager.PollEvents();

		renderManager.BeginFrame();
		renderManager.Clear(0.0f, 1.0f, 0.0f, 1.0f);

		Shader* simpleShader = renderManager.GetShader("simple");
		if (simpleShader)
		{
			simpleShader->Bind();

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
#include "RenderManager.h"
#include <GLFW/glfw3.h>
#include <iostream>

RenderManager::RenderManager()
{
}

RenderManager::~RenderManager()
{
}

void RenderManager::Init()
{
}

void RenderManager::Shutdown()
{
	m_shaders.clear(); 
}

void RenderManager::BeginFrame()
{
}

void RenderManager::EndFrame()
{
}

void RenderManager::Clear(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);
}

bool RenderManager::LoadShader(const std::string& tag, const std::string& vertPath, const std::string& fragPath)
{
	auto shader = std::make_unique<Shader>();

	if (shader->Load(vertPath, fragPath))
	{
		m_shaders[tag] = std::move(shader);
		return true;
	}
	return false;
}

Shader* RenderManager::GetShader(const std::string& tag)
{
	if (m_shaders.count(tag))
	{
		return m_shaders[tag].get();
	}
	return nullptr;
}
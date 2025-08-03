#pragma once
#include <string>
#include <map>
#include <memory>
#include "Shader.h"
#include "Camera2D.h"

class RenderManager
{
public:
	RenderManager();
	~RenderManager();

	void Init();
	void Shutdown();

	void BeginFrame(Camera2D& camera);
	void EndFrame();
	void Clear(float r, float g, float b, float a);

	bool LoadShader(const std::string& tag, const std::string& vertPath, const std::string& fragPath);
	Shader* GetShader(const std::string& tag);

private:
	std::map<std::string, std::unique_ptr<Shader>> m_shaders;
};
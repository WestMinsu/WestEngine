#pragma once
#include "Shader.h"
#include "Texture.h"
#include "VertexArray.h"
#include "Animation.h"

#include <memory>

class SpriteRenderer
{
public:
	SpriteRenderer();
	~SpriteRenderer();

	void Init(Shader* shader);
	void Shutdown();

	void DrawSprite(Texture& texture,
		const glm::vec2& position,
		const glm::vec2& size = glm::vec2(10.0f, 10.0f),
		float rotate = 0.0f,
		const glm::vec4& color = glm::vec4(1.0f));

	void DrawAnimatedSprite(Texture& texture, Animation& animation,
		const glm::vec2& position,
		const glm::vec2& size = glm::vec2(10.0f, 10.0f),
		float rotate = 0.0f,
		const glm::vec4& color = glm::vec4(1.0f));
private:
	Shader* m_shader;
	std::shared_ptr<VertexArray> m_quadVAO;
};
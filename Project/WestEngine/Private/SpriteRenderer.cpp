#include "SpriteRenderer.h"
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include "IndexBuffer.h"
#include "glad/gl.h"

SpriteRenderer::SpriteRenderer()
{
	m_shader = nullptr;
}

SpriteRenderer::~SpriteRenderer()
{
}

void SpriteRenderer::Init(Shader* shader)
{
	m_shader = shader;

	std::vector<float> vertices = {
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
		-0.5f,  0.5f, 0.0f, 0.0f, 1.0f
	};
	std::vector<unsigned int> indices = { 0, 1, 2, 2, 3, 0 };

	auto vbo = std::make_shared<VertexBuffer>();
	vbo->Init(vertices);
	auto ibo = std::make_shared<IndexBuffer>();
	ibo->Init(indices);

	m_quadVAO = std::make_shared<VertexArray>();
	m_quadVAO->Init();
	m_quadVAO->AddVertexBuffer(vbo);
	m_quadVAO->SetIndexBuffer(ibo);
}

void SpriteRenderer::Shutdown()
{
}

void SpriteRenderer::DrawSprite(Texture& texture, const glm::vec2& position, const glm::vec2& size, float rotate, const glm::vec4& color)
{
	m_shader->Bind();

	glm::mat4 model = glm::mat4(1.0f);

	model = glm::translate(model, glm::vec3(position, 0.0f));
	model = glm::translate(model, glm::vec3(0.5f * size.x, 0.5f * size.y, 0.0f));
	model = glm::rotate(model, glm::radians(rotate), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::translate(model, glm::vec3(-0.5f * size.x, -0.5f * size.y, 0.0f));
	model = glm::scale(model, glm::vec3(size, 1.0f));

	m_shader->SetUniformMat4("model", model);

	glUniform1i(glGetUniformLocation(m_shader->GetProgramID(), "u_Texture"), 0);

	m_quadVAO->Bind();
	glDrawElements(GL_TRIANGLES, m_quadVAO->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, 0);
	m_quadVAO->Unbind();
}

void SpriteRenderer::DrawAnimatedSprite(Texture& texture, Animation& animation, const glm::vec2& position, const glm::vec2& size, float rotate, const glm::vec4& color)
{
	m_shader->Bind();

	glm::mat4 model = glm::mat4(1.0f);

	model = glm::translate(model, glm::vec3(position, 0.0f));
	model = glm::translate(model, glm::vec3(0.5f * size.x, 0.5f * size.y, 0.0f));
	model = glm::rotate(model, glm::radians(rotate), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::translate(model, glm::vec3(-0.5f * size.x, -0.5f * size.y, 0.0f));
	model = glm::scale(model, glm::vec3(size, 1.0f));

	m_shader->SetUniformVec2("uvOffset", animation.GetUVOffset());
	m_shader->SetUniformVec2("uvScale", animation.GetUVScale());

	m_shader->SetUniformMat4("model", model);

	texture.Bind(0);
	glUniform1i(glGetUniformLocation(m_shader->GetProgramID(), "u_Texture"), 0);

	m_quadVAO->Bind();
	glDrawElements(GL_TRIANGLES, m_quadVAO->GetIndexBuffer()->GetCount(), GL_UNSIGNED_INT, 0);
	m_quadVAO->Unbind();
}
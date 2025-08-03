#pragma once
#include "IComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Transform : public IComponent
{
public:
	Transform(GameObject* owner);

	void SetPosition(const glm::vec2& pos) { m_position = pos; }
	const glm::vec2& GetPosition() const { return m_position; }

	void SetRotation(float rot) { m_rotation = rot; }
	float GetRotation() const { return m_rotation; }

	void SetScale(const glm::vec2& scale) { m_scale = scale; }
	const glm::vec2& GetScale() const { return m_scale; }

	glm::mat4 GetModelMatrix() const;

private:
	glm::vec2 m_position;
	float m_rotation; 
	glm::vec2 m_scale;
};
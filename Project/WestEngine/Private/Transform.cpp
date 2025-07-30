#include "Transform.h"

Transform::Transform(GameObject* owner)
	: IComponent(owner), m_position(0.0f), m_rotation(0.0f), m_scale(1.0f)
{
}

glm::mat4 Transform::GetModelMatrix() const
{
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, glm::vec3(m_position, 0.0f));
	model = glm::rotate(model, glm::radians(m_rotation), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::scale(model, glm::vec3(m_scale, 1.0f));
	return model;
}
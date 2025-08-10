#pragma once
#include "IComponent.h"
#include <glm/glm.hpp>

class RigidbodyComponent : public IComponent
{
public:
	RigidbodyComponent(GameObject* owner);
	~RigidbodyComponent();

	void Update(float dt) override;
	void SetVelocity(glm::vec2 newVelocity)
	{
		m_velocity = newVelocity;
	};
	const glm::vec2& GetVelocity() const 
	{ 
		return m_velocity; 
	}
private:
	glm::vec2 m_velocity;
	float m_gravity;
};
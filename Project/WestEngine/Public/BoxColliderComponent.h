#pragma once
#include "IComponent.h"
#include "EngineTypes.h"
#include <glm/glm.hpp>

class BoxColliderComponent : public IComponent
{
public:
	BoxColliderComponent(GameObject* owner);
	~BoxColliderComponent();

	void Init(const glm::vec2& size, const glm::vec2& offset = { 0.f, 0.f });

	AttackHitbox GetHitbox() const;

private:
	glm::vec2 m_size;
	glm::vec2 m_offset;
};
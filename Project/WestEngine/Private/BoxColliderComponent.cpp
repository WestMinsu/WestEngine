#include "BoxColliderComponent.h"
#include "GameObject.h"

BoxColliderComponent::BoxColliderComponent(GameObject* owner)
	: IComponent(owner)
{
	m_size = { 1.f, 1.f };
	m_offset = { 0.f, 0.f };
}

BoxColliderComponent::~BoxColliderComponent()
{
}

void BoxColliderComponent::Init(const glm::vec2& size, const glm::vec2& offset)
{
	m_size = size;
	m_offset = offset;
}

AttackHitbox BoxColliderComponent::GetHitbox() const
{
	glm::vec2 ownerPos = m_owner->GetTransform().GetPosition();
	glm::vec2 finalPos;
	finalPos.x = ownerPos.x + m_offset.x;
	finalPos.y = ownerPos.y + m_offset.y;

	return { finalPos, m_size, 0 };
}
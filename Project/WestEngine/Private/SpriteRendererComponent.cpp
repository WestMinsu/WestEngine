#include "SpriteRendererComponent.h"
#include "GameObject.h"
#include "WestEngine.h"
#include "RenderManager.h"
#include "SpriteRenderer.h"

SpriteRendererComponent::SpriteRendererComponent(GameObject* owner)
	: IComponent(owner), m_texture(nullptr)
{
}

SpriteRendererComponent::~SpriteRendererComponent()
{
}

void SpriteRendererComponent::Draw()
{
	if (m_texture)
	{
		SpriteRenderer& renderer = WestEngine::GetInstance().GetSpriteRenderer();
		Transform& transform = m_owner->GetTransform();

		renderer.DrawSprite(
			*m_texture,
			transform.GetPosition(),
			transform.GetScale(),
			transform.GetRotation()
		);
	}
}

void SpriteRendererComponent::SetTexture(Texture* texture)
{
	m_texture = texture;
}
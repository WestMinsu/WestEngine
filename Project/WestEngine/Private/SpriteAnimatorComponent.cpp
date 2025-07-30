#include "SpriteAnimatorComponent.h"
#include "GameObject.h"
#include "WestEngine.h"
#include "RenderManager.h"
#include "SpriteRenderer.h"

SpriteAnimatorComponent::SpriteAnimatorComponent(GameObject* owner)
	: IComponent(owner), m_currentClip(nullptr)
{
}

SpriteAnimatorComponent::~SpriteAnimatorComponent()
{
}

void SpriteAnimatorComponent::Update(float dt)
{
	if (m_currentClip)
	{
		m_animation.Update(dt);
	}
}

void SpriteAnimatorComponent::Draw()
{
	if (m_currentClip && m_currentClip->pTexture)
	{
		SpriteRenderer& renderer = WestEngine::GetInstance().GetSpriteRenderer();
		Transform& transform = m_owner->GetTransform();

		renderer.DrawAnimatedSprite(
			*m_currentClip->pTexture,
			m_animation,
			transform.GetPosition(),
			transform.GetScale(),
			transform.GetRotation()
		);
	}
}

void SpriteAnimatorComponent::AddClip(const std::string& name, const AnimData& data)
{
	m_clips[name] = data;
}

void SpriteAnimatorComponent::Play(const std::string& name)
{
	if (m_clips.count(name))
	{
		m_currentClip = &m_clips.at(name);
		m_animation.Init(m_currentClip->frameCount, m_currentClip->frameCount * m_currentClip->frameDuration,
			m_currentClip->orientation == SpriteSheetOrientation::VERTICAL, m_currentClip->loop);
	}
}
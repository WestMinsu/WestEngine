#include "AudioSourceComponent.h"
#include "WestEngine.h"
#include "SoundManager.h"
#include "GameObject.h"

AudioSourceComponent::AudioSourceComponent(GameObject* owner) : IComponent(owner)
{
}

AudioSourceComponent::~AudioSourceComponent()
{
}

void AudioSourceComponent::AddSound(const std::string& tag, const std::string& filePath)
{
	m_clips[tag] = WestEngine::GetInstance().GetSoundManager().LoadSound(filePath);
}

void AudioSourceComponent::Play(const std::string& tag, float volume)
{
	if (m_clips.count(tag))
	{
		WestEngine::GetInstance().GetSoundManager().PlaySound(m_clips[tag], volume);
	}
}
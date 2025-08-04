#pragma once
#include <fmod.hpp>
#include "IComponent.h"
#include <string>
#include <map>

class AudioSourceComponent : public IComponent
{
public:
	AudioSourceComponent(GameObject* owner);
	~AudioSourceComponent();

	void AddSound(const std::string& tag, const std::string& filePath);
	void Play(const std::string& tag, float volume = 1.0f);

private:
	std::map<std::string, FMOD::Sound*> m_clips;
};
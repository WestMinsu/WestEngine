#pragma once
#include "fmod.hpp"
#include <string>
#include <map>
#include <glm/glm.hpp>

class SoundManager
{
public:
	SoundManager();
	~SoundManager();

	void Init();
	void Update();
	void Shutdown();

	FMOD::Sound* LoadSound(const std::string& filePath);

	void PlaySound(FMOD::Sound* sound, float volume = 1.0f);

private:
	FMOD::System* m_system;
	std::map<std::string, FMOD::Sound*> m_sounds;
};
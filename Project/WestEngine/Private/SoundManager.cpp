#include "SoundManager.h"
#include <iostream>

SoundManager::SoundManager() : m_system(nullptr) {}
SoundManager::~SoundManager() {}

void SoundManager::Init()
{
	FMOD_RESULT result = FMOD::System_Create(&m_system);
	if (result != FMOD_OK)
	{
		std::cerr << "FMOD Error! FMOD::System_Create failed." << std::endl;
		return;
	}

	result = m_system->init(32, FMOD_INIT_NORMAL, nullptr);
	if (result != FMOD_OK)
	{
		std::cerr << "FMOD Error! FMOD::System::init failed." << std::endl;
		return;
	}
}

void SoundManager::Update()
{
	if (m_system)
	{
		m_system->update();
	}
}

void SoundManager::Shutdown()
{
	if (m_system)
	{
		for (auto const& [key, val] : m_sounds)
		{
			val->release();
		}
		m_sounds.clear();
		m_system->close();
		m_system->release();
	}
}

FMOD::Sound* SoundManager::LoadSound(const std::string& filePath)
{
	if (m_sounds.count(filePath))
	{
		return m_sounds[filePath];
	}

	FMOD::Sound* sound = nullptr;

	// <<< 핵심: FMOD_3D 모드를 제거하고, FMOD_DEFAULT로 단순화 >>>
	FMOD_RESULT result = m_system->createSound(filePath.c_str(), FMOD_DEFAULT, nullptr, &sound);
	if (result != FMOD_OK)
	{
		std::cerr << "FMOD Error! Could not load sound file: " << filePath << std::endl;
		return nullptr;
	}

	m_sounds[filePath] = sound;
	return sound;
}

void SoundManager::PlaySound(FMOD::Sound* sound, float volume)
{
	if (!sound) return;

	FMOD::Channel* channel = nullptr;
	m_system->playSound(sound, nullptr, false, &channel);
	if (channel)
	{
		channel->setVolume(volume);
	}
}
#pragma once
#include "IComponent.h"
#include "Animation.h"
#include "AnimationTypes.h" 
#include <map>
#include <string>

class SpriteAnimatorComponent : public IComponent
{
public:
	SpriteAnimatorComponent(GameObject* owner);
	~SpriteAnimatorComponent();

	void Update(float dt) override;
	void Draw() override;

	void AddClip(const std::string& name, const AnimData& data);
	void Play(const std::string& name);

private:
	std::map<std::string, AnimData> m_clips;
	Animation m_animation;
	AnimData* m_currentClip;
};
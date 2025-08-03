#pragma once
#include <string>
#include <glm/glm.hpp>

class Texture;
enum class ElementType
{
	NONE,
	FIRE,
	ICE,
	DARK,
};

enum class DamageType
{
	NONE,
	FIRE,
	ICE,
	LIGHTNING
};

enum class CharacterAnimationState
{
	IDLE,
	WALK,
	JUMP,
	CROUCH,
	MELEE_ATTACK,
	MELEE_ATTACK_2,
	MELEE_ATTACK_3,
	RANGED_ATTACK,
	DASH,
	DEATH,
	HURT,

	//for boss
	APPEARANCE,
	GLOWING,
	LASER_CAST,
	LASER_SHEET,
	BUFF
};

enum class CharacterDirection
{
	LEFT,
	RIGHT
};

enum class SpriteSheetOrientation
{
	HORIZONTAL, VERTICAL
};

struct AnimData
{
	std::string texturePath;
	Texture* pTexture = nullptr;
	int frameCount = 0;
	SpriteSheetOrientation orientation = SpriteSheetOrientation::VERTICAL;
	float frameDuration = 0.1f;
	bool loop = true;
};

//struct ProjectileData
//{
//	DamageType type;
//	float speed;
//	int damage;
//	AEVec2 size;
//	AnimData animData;
//};
//
//struct AttackHitbox
//{
//	AEVec2 offset;
//	AEVec2 size;
//};

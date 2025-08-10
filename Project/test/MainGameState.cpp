#include "MainGameState.h"
#include "WestEngine.h"
#include "Engine.h"
#include "PlayerControllerComponent.h"
#include <glm/gtc/matrix_transform.hpp>

MainGameState::MainGameState()
{
	m_playerObject = nullptr;
	m_playerIdleTexture = nullptr;
	m_camera = nullptr;
}

MainGameState::~MainGameState()
{
}

void MainGameState::Init()
{
	m_camera = std::make_unique<Camera2D>(1600.0f, 900.0f);

	m_playerIdleTexture = std::make_unique<Texture>();
	m_playerIdleTexture->Load("Assets/Battlemage Idle.png");
	m_testObjectTexture = std::make_unique<Texture>();
	m_testObjectTexture->Load("Assets/stone.png");

	m_playerObject = std::make_unique<GameObject>();
	m_playerObject->GetTransform().SetPosition({ 0.f, 0.f });
	m_playerObject->GetTransform().SetScale({ 200.f, 200.f });

	auto* animator = m_playerObject->AddComponent<SpriteAnimatorComponent>();
	AnimData idleClip;
	idleClip.pTexture = m_playerIdleTexture.get();
	idleClip.frameCount = 8;
	idleClip.orientation = SpriteSheetOrientation::VERTICAL;
	idleClip.frameDuration = 0.1f;
	idleClip.loop = true;
	animator->AddClip("idle", idleClip);
	animator->Play("idle");

	auto* audioSource = m_playerObject->AddComponent<AudioSourceComponent>();
	audioSource->AddSound("footstep", "Assets/die_boss.wav");

	m_playerObject->AddComponent<RigidbodyComponent>();
	m_playerObject->AddComponent<PlayerControllerComponent>();

	glm::vec2 playerHitboxSize = { 100.f, 180.f };
	auto* playerCollider = m_playerObject->AddComponent<BoxColliderComponent>(playerHitboxSize, glm::vec2{ 0.f, 0.f });
	WestEngine::GetInstance().GetPhysicsManager().AddCollider(playerCollider);

	m_stoneObject = std::make_unique<GameObject>();
	m_stoneObject->GetTransform().SetPosition({ 300.f, 0.f });
	m_stoneObject->GetTransform().SetScale({ 100.f, 100.f });

	auto* spriteComp = m_stoneObject->AddComponent<SpriteRendererComponent>();
	spriteComp->SetTexture(m_testObjectTexture.get());

	glm::vec2 stoneHitboxSize = { 100.f, 100.f };
	auto* stoneCollider = m_stoneObject->AddComponent<BoxColliderComponent>(stoneHitboxSize, glm::vec2{ 0.f, 0.f });
	WestEngine::GetInstance().GetPhysicsManager().AddCollider(stoneCollider);
}

void MainGameState::Update(float dt)
{
	m_playerObject->Update(dt);
	m_stoneObject->Update(dt);
	m_camera->SetPosition(m_playerObject->GetTransform().GetPosition());
}

void MainGameState::Draw()
{
	RenderManager& renderer = WestEngine::GetInstance().GetRenderManager();

	renderer.BeginFrame(*m_camera);
	
	renderer.Clear(0.1f, 0.1f, 0.3f, 1.0f);

	m_stoneObject->Draw();

	m_playerObject->Draw();
}

void MainGameState::Exit()
{
}
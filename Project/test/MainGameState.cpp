#include "MainGameState.h"
#include "WestEngine.h"
#include "InputManager.h"
#include "RenderManager.h"
#include "SpriteAnimatorComponent.h" 
#include "Camera2D.h"
#include <glm/gtc/matrix_transform.hpp>
#include <SpriteRendererComponent.h>

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

	m_playerObject = std::make_unique<GameObject>();
	m_playerObject->GetTransform().SetPosition({ 640.f, 360.f });
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
}

void MainGameState::Update(float dt)
{
	InputManager& input = WestEngine::GetInstance().GetInputManager();

	float moveSpeed = 300.f * dt;
	Transform& playerTransform = m_playerObject->GetTransform();
	glm::vec2 currentPos = playerTransform.GetPosition();

	if (input.IsKeyPressed(KEY_W))
		currentPos.y += moveSpeed;
	if (input.IsKeyPressed(KEY_S))
		currentPos.y -= moveSpeed;
	if (input.IsKeyPressed(KEY_A))
		currentPos.x -= moveSpeed;
	if (input.IsKeyPressed(KEY_D))
		currentPos.x += moveSpeed;

	playerTransform.SetPosition(currentPos);
	m_playerObject->Update(dt);
	
	m_camera->SetPosition(playerTransform.GetPosition());

	m_testObjectTexture = std::make_unique<Texture>();
	m_testObjectTexture->Load("Assets/stone.png"); 

	m_staticTestObject = std::make_unique<GameObject>();
	m_staticTestObject->GetTransform().SetPosition({ 1000.f, 360.f }); 
	m_staticTestObject->GetTransform().SetScale({ 100.f, 100.f });

	auto* spriteComp = m_staticTestObject->AddComponent<SpriteRendererComponent>();
	spriteComp->SetTexture(m_testObjectTexture.get());
}

void MainGameState::Draw()
{
	RenderManager& renderer = WestEngine::GetInstance().GetRenderManager();

	renderer.BeginFrame(*m_camera);
	
	renderer.Clear(0.1f, 0.1f, 0.3f, 1.0f);

	m_staticTestObject->Draw();

	m_playerObject->Draw();
}

void MainGameState::Exit()
{
}
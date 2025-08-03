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

	m_playerObject->AddComponent<PlayerControllerComponent>();
}

void MainGameState::Update(float dt)
{


	//playerTransform.SetPosition(currentPos);
	m_playerObject->Update(dt);
	//
	//m_camera->SetPosition(playerTransform.GetPosition());

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
#include "MainGameState.h"
#include "WestEngine.h"
#include "InputManager.h"
#include "RenderManager.h"
#include "SpriteAnimatorComponent.h" 
#include <glm/gtc/matrix_transform.hpp>

MainGameState::MainGameState()
{
	m_playerObject = nullptr;
	m_playerIdleTexture = nullptr;
}

MainGameState::~MainGameState()
{
}

void MainGameState::Init()
{
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
}

void MainGameState::Draw()
{
	RenderManager& renderer = WestEngine::GetInstance().GetRenderManager();
	Shader* spriteShader = renderer.GetShader("sprite");
	if (spriteShader)
	{
		spriteShader->Bind();
		glm::mat4 projection = glm::ortho(0.0f, 1600.0f, 0.0f, 900.0f, -1.0f, 1.0f);
		spriteShader->SetUniformMat4("projection", projection);
	}

	m_playerObject->Draw();
}

void MainGameState::Exit()
{
}
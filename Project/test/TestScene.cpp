#include "TestScene.h"
#include "WestEngine.h"
#include "SpriteAnimatorComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include "InputManager.h"
#include <Engine.h>

TestScene::TestScene() : m_playerObject(nullptr)
{
}

TestScene::~TestScene()
{
}

void TestScene::Init()
{
	Scene::Init();

	m_playerTexture = std::make_unique<Texture>();
	m_playerTexture->Load("Assets/Battlemage Idle.png");

	m_playerObject = AddGameObject();
	m_playerObject->GetTransform().SetPosition({ 640.f, 360.f });
	m_playerObject->GetTransform().SetScale({ 200.f, 200.f });

	auto* animator = m_playerObject->AddComponent<SpriteAnimatorComponent>();
	AnimData idleClip;
	idleClip.pTexture = m_playerTexture.get();
	idleClip.frameCount = 8;
	idleClip.orientation = SpriteSheetOrientation::VERTICAL;
	idleClip.frameDuration = 0.1f;
	idleClip.loop = true;
	animator->AddClip("idle", idleClip);
	animator->Play("idle");
}

void TestScene::Update(float dt)
{
	Scene::Update(dt); 

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
}

void TestScene::Draw()
{
	RenderManager& renderer = WestEngine::GetInstance().GetRenderManager();
	Shader* spriteShader = renderer.GetShader("sprite");
	if (spriteShader)
	{
		spriteShader->Bind();
		glm::mat4 projection = glm::ortho(0.0f, 1600.0f, 0.0f, 900.0f, -1.0f, 1.0f);
		spriteShader->SetUniformMat4("projection", projection);
	}

	Scene::Draw(); 
}

void TestScene::Exit()
{
	Scene::Exit();
}
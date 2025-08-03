#pragma once
#include "IComponent.h"
#include "Texture.h"
//#include "SpriteRenderer.h"
//#include <memory>

class SpriteRendererComponent : public IComponent
{
public:
	SpriteRendererComponent(GameObject* owner);
	~SpriteRendererComponent();

	void Draw() override;

	void SetTexture(Texture* texture);

private:
	Texture* m_texture;
};
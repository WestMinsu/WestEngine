#pragma once
#include <string>

typedef unsigned int GLuint;
typedef unsigned int GLenum;

class Texture
{
public:
	Texture();
	~Texture();

	bool Load(const std::string& filePath);

	void Bind(unsigned int slot = 0) const;
	void Unbind() const;

private:
	GLuint m_textureID;
	int m_width, m_height, m_channels;
};
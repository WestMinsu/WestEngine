#include "Texture.h"
#include "glad/gl.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include "Debug.h"

Texture::Texture()
	: m_textureID(0), m_width(0), m_height(0), m_channels(0)
{
}

Texture::~Texture()
{
	if (m_textureID != 0)
	{
		glDeleteTextures(1, &m_textureID);
	}
}

bool Texture::Load(const std::string& filePath)
{
	stbi_set_flip_vertically_on_load(true);

	unsigned char* data = stbi_load(filePath.c_str(), &m_width, &m_height, &m_channels, 0);
	if (!data)
	{
		WEST_ERR("Failed to load texture");
		return false;
	}

	GLenum internalFormat = 0, dataFormat = 0;
	if (m_channels == 4)
	{
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
	}
	else if (m_channels == 3)
	{
		internalFormat = GL_RGB8;
		dataFormat = GL_RGB;
	}
	else
	{
		std::cerr << "Error: Texture format not supported: " << filePath << std::endl;
		stbi_image_free(data);
		return false;
	}

	glGenTextures(1, &m_textureID);
	glBindTexture(GL_TEXTURE_2D, m_textureID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_width, m_height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);

	stbi_image_free(data);

	return true;
}

void Texture::Bind(unsigned int slot) const
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_textureID);
}

void Texture::Unbind() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
}
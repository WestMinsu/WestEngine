#include "IndexBuffer.h"
#include "glad/gl.h"

IndexBuffer::IndexBuffer()
{
	m_bufferID = 0;
	m_count = 0;
}

IndexBuffer::~IndexBuffer()
{
	if (m_bufferID != 0)
	{
		glDeleteBuffers(1, &m_bufferID);
	}
}

void IndexBuffer::Init(const std::vector<unsigned int>& indices)
{
	m_count = indices.size();

	glGenBuffers(1, &m_bufferID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_bufferID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_count * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
}

void IndexBuffer::Bind() const
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_bufferID);
}

void IndexBuffer::Unbind() const
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
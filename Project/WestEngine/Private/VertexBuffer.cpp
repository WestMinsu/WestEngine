#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "VertexBuffer.h"

VertexBuffer::VertexBuffer()
{
	m_bufferID = 0;
}

VertexBuffer::~VertexBuffer()
{
	if (m_bufferID != 0)
	{
		glDeleteBuffers(1, &m_bufferID);
	}
}

void VertexBuffer::Init(const std::vector<float>& vertices)
{
	glGenBuffers(1, &m_bufferID);
	glBindBuffer(GL_ARRAY_BUFFER, m_bufferID);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
}

void VertexBuffer::Bind() const
{
	glBindBuffer(GL_ARRAY_BUFFER, m_bufferID);
}

void VertexBuffer::Unbind() const
{
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
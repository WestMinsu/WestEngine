#pragma once
#include <vector>

class VertexBuffer
{
public:
	VertexBuffer();
	~VertexBuffer();

	void Init(const std::vector<float>& vertices);

	void Bind() const;
	void Unbind() const;

private:
	GLuint m_bufferID;
};
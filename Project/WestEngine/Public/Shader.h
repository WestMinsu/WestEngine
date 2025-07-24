#pragma once
#include <string>
typedef unsigned int GLuint;
typedef unsigned int GLenum;

class Shader
{
public:
	Shader();
	~Shader();

	bool Load(const std::string& vertPath, const std::string& fragPath);

	void Bind() const;
	void Unbind() const;

private:
	std::string ReadFile(const std::string& filepath);
	GLuint CompileShader(GLenum type, const std::string& source);

	GLuint m_programID;
};
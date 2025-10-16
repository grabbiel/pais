
// Platform-specific OpenGL function loading
#ifdef _WIN32
#include <windows.h>

// OpenGL 3.3 function pointers for Windows
typedef void(APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei, GLuint *);
typedef void(APIENTRYP PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void(APIENTRYP PFNGLBUFFERDATAPROC)(GLenum, GLsizeiptr, const void *,
                                            GLenum);
typedef void(APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint *);
typedef void(APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef void(APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void(APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum,
                                                     GLboolean, GLsizei,
                                                     const void *);
typedef GLuint(APIENTRYP PFNGLCREATESHADERPROC)(GLenum);
typedef void(APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar **,
                                              const GLint *);
typedef void(APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint);
typedef GLuint(APIENTRYP PFNGLCREATEPROGRAMPROC)();
typedef void(APIENTRYP PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint);
typedef void(APIENTRYP PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint *);
typedef void(APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei *,
                                                  GLchar *);
typedef void(APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint *);
typedef void(APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei *,
                                                   GLchar *);
typedef GLint(APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar *);
typedef void(APIENTRYP PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRYP PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void(APIENTRYP PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void(APIENTRYP PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRYP PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat,
                                           GLfloat);
typedef void(APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean,
                                                  const GLfloat *);
typedef void(APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void(APIENTRYP PFNGLDELETESHADERPROC)(GLuint);
typedef void(APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void(APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint *);
typedef void(APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint *);

typedef void(APIENTRYP PFNGLTEXIMAGE3DPROC)(GLenum, GLint, GLint, GLsizei,
                                            GLsizei, GLsizei, GLint, GLenum,
                                            GLenum, const void *);
typedef void(APIENTRYP PFNGLTEXSUBIMAGE3DPROC)(GLenum, GLint, GLint, GLint,
                                               GLint, GLsizei, GLsizei, GLsizei,
                                               GLenum, GLenum, const void *);

// Global function pointers
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLTEXIMAGE3DPROC glTexImage3D = nullptr;
PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D = nullptr;

#define LOAD_GL_FUNC(name)                                                     \
  name = (decltype(name))glfwGetProcAddress(#name);                            \
  if (!name)                                                                   \
    throw std::runtime_error("Failed to load " #name);
#endif

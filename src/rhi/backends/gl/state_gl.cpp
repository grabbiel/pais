// src/rhi/backends/gl/state_gl.cpp
#include "device_gl.hpp"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#endif

namespace pixel::rhi::gl {

GLQuery::GLQuery() { glGenQueries(1, &query_id_); }

GLQuery::~GLQuery() { glDeleteQueries(1, &query_id_); }

void GLQuery::begin_time_elapsed() { glBeginQuery(GL_TIME_ELAPSED, query_id_); }

void GLQuery::end_time_elapsed() { glEndQuery(GL_TIME_ELAPSED); }

bool GLQuery::is_result_available() {
  GLint available = 0;
  glGetQueryObjectiv(query_id_, GL_QUERY_RESULT_AVAILABLE, &available);
  return available != 0;
}

uint64_t GLQuery::get_result() {
  uint64_t elapsed_time = 0;
  glGetQueryObjectui64v(query_id_, GL_QUERY_RESULT, &elapsed_time);
  return elapsed_time;
}

void GLQuery::timestamp() { glQueryCounter(query_id_, GL_TIMESTAMP); }

GLenum to_gl_compare(CompareOp op) {
  switch (op) {
  case CompareOp::Never:
    return GL_NEVER;
  case CompareOp::Less:
    return GL_LESS;
  case CompareOp::Equal:
    return GL_EQUAL;
  case CompareOp::LessEqual:
    return GL_LEQUAL;
  case CompareOp::Greater:
    return GL_GREATER;
  case CompareOp::NotEqual:
    return GL_NOTEQUAL;
  case CompareOp::GreaterEqual:
    return GL_GEQUAL;
  case CompareOp::Always:
  default:
    return GL_ALWAYS;
  }
}

GLenum to_gl_stencil_op(StencilOp op) {
  switch (op) {
  case StencilOp::Keep:
    return GL_KEEP;
  case StencilOp::Zero:
    return GL_ZERO;
  case StencilOp::Replace:
    return GL_REPLACE;
  case StencilOp::IncrementClamp:
    return GL_INCR;
  case StencilOp::DecrementClamp:
    return GL_DECR;
  case StencilOp::Invert:
    return GL_INVERT;
  case StencilOp::IncrementWrap:
    return GL_INCR_WRAP;
  case StencilOp::DecrementWrap:
    return GL_DECR_WRAP;
  default:
    return GL_KEEP;
  }
}

void GLCmdList::setDepthStencilState(const DepthStencilState &state) {
  if (depth_stencil_state_initialized_ && state == current_depth_stencil_state_)
    return;

  depth_stencil_state_initialized_ = true;
  current_depth_stencil_state_ = state;

  if (state.depthTestEnable) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }

  glDepthMask(state.depthWriteEnable ? GL_TRUE : GL_FALSE);
  glDepthFunc(to_gl_compare(state.depthCompare));

  if (state.stencilEnable) {
    glEnable(GL_STENCIL_TEST);
    glStencilMask(state.stencilWriteMask);
    glStencilFuncSeparate(GL_FRONT_AND_BACK, to_gl_compare(state.stencilCompare),
                          static_cast<GLint>(state.stencilReference),
                          state.stencilReadMask);
    const GLenum fail = to_gl_stencil_op(state.stencilFailOp);
    const GLenum depth_fail = to_gl_stencil_op(state.stencilDepthFailOp);
    const GLenum pass = to_gl_stencil_op(state.stencilPassOp);
    glStencilOpSeparate(GL_FRONT_AND_BACK, fail, depth_fail, pass);
  } else {
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
  }
}

void GLCmdList::setDepthBias(const DepthBiasState &state) {
  if (depth_bias_initialized_ && state.enable == current_depth_bias_state_.enable &&
      state.constantFactor == current_depth_bias_state_.constantFactor &&
      state.slopeFactor == current_depth_bias_state_.slopeFactor) {
    return;
  }

  depth_bias_initialized_ = true;
  current_depth_bias_state_ = state;

  if (state.enable) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(state.slopeFactor, state.constantFactor);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
}

} // namespace pixel::rhi::gl

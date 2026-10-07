// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

#include "soft_renderer.hpp"

#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <mutex>
#include <string>
#include <utility>

#include "events.hpp"

namespace render {

namespace {

constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;

struct Rect {
  int x = 0, y = 0, w = kCanvasWidth, h = kCanvasHeight;
};

// Mailbox between the network thread (producer) and the main thread.
struct Mailbox {
  std::mutex mutex;
  h264::YuvFrame frame;
  bool fresh = false;
};

struct SlotGl {
  GLuint tex[3] = {0, 0, 0};
  int width = 0;
  int height = 0;
  h264::YuvFrame frame;  // used only by the main thread
};

Mailbox g_mailbox[kMaxSlots];
SlotGl g_slot[kMaxSlots];
Rect g_rect[kMaxSlots];
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_gl = 0;
bool g_loop_started = false;

GLuint CompileShader(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    app::Log(-1, "gl", false, std::string("shader: ") + log);
  }
  return s;
}

GLuint NewTexture() {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

void Upload(GLuint tex, int unit, int w, int h, const uint8_t* data, bool resize) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (resize) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE,
                 GL_UNSIGNED_BYTE, data);
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    data);
  }
}

void DrawSlot(int slot) {
  SlotGl& s = g_slot[slot];
  const h264::YuvFrame& f = s.frame;
  if (f.width <= 0 || f.height <= 0) return;
  if (!s.tex[0]) {
    for (auto& t : s.tex) t = NewTexture();
  }
  bool resize = f.width != s.width || f.height != s.height;
  s.width = f.width;
  s.height = f.height;
  Upload(s.tex[0], 0, f.width, f.height, f.y.data(), resize);
  Upload(s.tex[1], 1, f.width / 2, f.height / 2, f.u.data(), resize);
  Upload(s.tex[2], 2, f.width / 2, f.height / 2, f.v.data(), resize);

  const Rect& r = g_rect[slot];
  glViewport(r.x, kCanvasHeight - r.y - r.h, r.w, r.h);  // GL counts y from the bottom
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

EM_BOOL OnAnimationFrame(double, void*) {
  for (int slot = 0; slot < kMaxSlots; ++slot) {
    Mailbox& m = g_mailbox[slot];
    {
      std::lock_guard<std::mutex> lock(m.mutex);
      if (!m.fresh) continue;
      std::swap(m.frame, g_slot[slot].frame);
      m.fresh = false;
    }
    DrawSlot(slot);
  }
  return EM_TRUE;
}

}  // namespace

bool Init() {
  if (g_gl) return true;
  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.alpha = false;
  attrs.depth = false;
  attrs.antialias = false;
  attrs.preserveDrawingBuffer = true;  // each camera redraws only its own tile
  g_gl = emscripten_webgl_create_context("#canvas", &attrs);
  if (g_gl <= 0) {
    app::Log(-1, "gl", false, "could not create the WebGL context on #canvas");
    g_gl = 0;
    return false;
  }
  emscripten_webgl_make_context_current(g_gl);

  static const char* kVertex =
      "attribute vec2 a_pos;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  v_uv = vec2((a_pos.x + 1.0) * 0.5, (1.0 - a_pos.y) * 0.5);\n"
      "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
      "}\n";
  // BT.601 limited range, the default of cameras and DVRs.
  static const char* kFragment =
      "precision mediump float;\n"
      "uniform sampler2D u_y;\n"
      "uniform sampler2D u_u;\n"
      "uniform sampler2D u_v;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  float y = 1.164 * (texture2D(u_y, v_uv).r - 0.0625);\n"
      "  float u = texture2D(u_u, v_uv).r - 0.5;\n"
      "  float v = texture2D(u_v, v_uv).r - 0.5;\n"
      "  gl_FragColor = vec4(y + 1.596 * v, y - 0.813 * v - 0.391 * u,\n"
      "                      y + 2.018 * u, 1.0);\n"
      "}\n";
  GLuint program = glCreateProgram();
  glAttachShader(program, CompileShader(GL_VERTEX_SHADER, kVertex));
  glAttachShader(program, CompileShader(GL_FRAGMENT_SHADER, kFragment));
  glBindAttribLocation(program, 0, "a_pos");
  glLinkProgram(program);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_y"), 0);
  glUniform1i(glGetUniformLocation(program, "u_u"), 1);
  glUniform1i(glGetUniformLocation(program, "u_v"), 2);

  static const GLfloat kQuad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  if (!g_loop_started) {
    g_loop_started = true;
    emscripten_request_animation_frame_loop(OnAnimationFrame, nullptr);
  }
  return true;
}

void SetRect(int slot, int x, int y, int w, int h) {
  if (slot < 0 || slot >= kMaxSlots) return;
  g_rect[slot] = Rect{x, y, w, h};
  // Redraws the last frame at the new position (layout change).
  if (g_gl) DrawSlot(slot);
}

void ClearAll() {
  if (!g_gl) return;
  glClear(GL_COLOR_BUFFER_BIT);
}

void ClearSlot(int slot) {
  if (!g_gl || slot < 0 || slot >= kMaxSlots) return;
  const Rect& r = g_rect[slot];
  glEnable(GL_SCISSOR_TEST);
  glScissor(r.x, kCanvasHeight - r.y - r.h, r.w, r.h);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  g_slot[slot].frame = h264::YuvFrame();
}

void Publish(int slot, h264::YuvFrame* frame) {
  Mailbox& m = g_mailbox[slot];
  std::lock_guard<std::mutex> lock(m.mutex);
  std::swap(m.frame, *frame);
  m.fresh = true;
}

void Drop(int slot) {
  Mailbox& m = g_mailbox[slot];
  std::lock_guard<std::mutex> lock(m.mutex);
  m.fresh = false;
}

}  // namespace render

#include "voxel_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#define VOXEL_HAVE_EGL 1
#endif

#ifdef VOXEL_HAVE_EGL

#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

struct VoxelGl {
  VoxelGlLogFn log;
  void *log_user;

  EGLDisplay dpy;
  EGLConfig cfg;
  EGLContext ctx;
  EGLSurface surf;

  EGLDisplay prev_dpy;
  EGLContext prev_ctx;
  EGLSurface prev_draw;
  EGLSurface prev_read;

  // SDL may be using GLX instead of EGL for its own window. Save/restore that
  // context explicitly so the plugin does not steal the engine's GL context.
  GLXContext prev_glx_ctx;
  GLXDrawable prev_glx_draw;
  GLXDrawable prev_glx_read;
  Display *prev_glx_dpy;

  bool ready;
  int width, height;
  int logical_width, logical_height;

  GLuint fbo;
  GLuint color_tex;
  GLuint depth_rb;
  GLuint atlas_tex;
  GLuint vbo;
  GLuint prog3d;
  GLuint prog2d;
  GLuint prog_shadow;

  GLint u3d_center;
  GLint u3d_size;
  GLint u3d_tilt;
  GLint u3d_persp;
  GLint u3d_zscale;
  GLint u3d_yaw;
  GLint u3d_zoom;
  GLint u3d_tex;

  GLint us_center;
  GLint us_size;
  GLint us_tilt;
  GLint us_persp;
  GLint us_zscale;
  GLint us_yaw;
  GLint us_zoom;
  GLint us_strength;

  GLint u2d_size;
  GLint u2d_tex;

  uint8_t *readback;
  size_t readback_size;
};

static void GlLog(VoxelGl *gl, int level, const char *message) {
  if (gl->log != NULL)
    gl->log(gl->log_user, level, message);
}

static const char kVertex3D[] =
  "#version 120\n"
  "attribute vec3 a_pos;\n"
  "attribute vec2 a_uv;\n"
  "attribute float a_shade;\n"
  "attribute float a_depth;\n"
  "uniform vec2 u_center;\n"
  "uniform vec2 u_size;\n"
  "uniform float u_tilt;\n"
  "uniform float u_persp;\n"
  "uniform float u_zscale;\n"
  "uniform float u_yaw;\n"
  "uniform float u_zoom;\n"
  "varying vec2 v_uv;\n"
  "varying float v_shade;\n"
  "void main() {\n"
  "  float dx = a_pos.x - u_center.x;\n"
  "  float dy = a_pos.y - u_center.y;\n"
  "  float c = cos(u_yaw);\n"
  "  float s = sin(u_yaw);\n"
  "  float rx = dx * c - dy * s;\n"
  "  float ry = dx * s + dy * c;\n"
  "  float w = 1.0 + u_persp * ry / u_size.y;\n"
  "  float n = 1.0 - u_persp * 0.5;\n"
  "  float zoom = 1.0 / max(u_tilt, 0.1);\n"
  "  float nx = rx / (u_size.x * 0.5) * n / w * u_zoom;\n"
  "  float ny = (-ry * u_tilt + a_pos.z * u_zscale)\n"
  "             * zoom / (u_size.y * 0.5) * n / w * u_zoom;\n"
  "  float nz = 1.0 - 2.0 * clamp((ry + u_center.y + a_depth) / u_size.y,\n"
  "                               0.0, 1.0);\n"
  "  gl_Position = vec4(nx, ny, nz, 1.0);\n"
  "  v_uv = a_uv;\n"
  "  v_shade = a_shade;\n"
  "}\n";

static const char kFragment3D[] =
  "#version 120\n"
  "uniform sampler2D u_tex;\n"
  "varying vec2 v_uv;\n"
  "varying float v_shade;\n"
  "void main() {\n"
  "  vec4 c = texture2D(u_tex, v_uv);\n"
  "  if (c.a < 0.5) discard;\n"
  "  gl_FragColor = vec4(c.rgb * v_shade, 1.0);\n"
  "}\n";

static const char kFragmentShadow[] =
  "#version 120\n"
  "uniform float u_strength;\n"
  "void main() {\n"
  "  gl_FragColor = vec4(0.0, 0.0, 0.0, u_strength);\n"
  "}\n";

static const char kVertex2D[] =
  "#version 120\n"
  "attribute vec3 a_pos;\n"
  "attribute vec2 a_uv;\n"
  "attribute float a_shade;\n"
  "uniform vec2 u_size;\n"
  "varying vec2 v_uv;\n"
  "varying float v_shade;\n"
  "void main() {\n"
  "  float nx = a_pos.x / u_size.x * 2.0 - 1.0;\n"
  "  float ny = 1.0 - a_pos.y / u_size.y * 2.0;\n"
  "  gl_Position = vec4(nx, ny, 0.0, 1.0);\n"
  "  v_uv = a_uv;\n"
  "  v_shade = a_shade;\n"
  "}\n";

static const char kFragment2D[] =
  "#version 120\n"
  "uniform sampler2D u_tex;\n"
  "varying vec2 v_uv;\n"
  "varying float v_shade;\n"
  "void main() {\n"
  "  vec4 c = texture2D(u_tex, v_uv);\n"
  "  if (c.a < 0.5) discard;\n"
  "  gl_FragColor = vec4(c.rgb * v_shade, 1.0);\n"
  "}\n";

static GLuint CompileShader(VoxelGl *gl, GLenum type, const char *source,
                            const char *name) {
  GLuint shader = glCreateShader(type);
  if (shader == 0)
    return 0;
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    GLsizei length = 0;
    glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &length, log);
    log[sizeof(log) - 1] = 0;
    char message[1200];
    snprintf(message, sizeof(message), "%s shader compile failed: %s", name,
             log);
    GlLog(gl, 2, message);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint LinkProgram(VoxelGl *gl, GLuint vs, GLuint fs, const char *name,
                          bool with_depth) {
  GLuint program = glCreateProgram();
  if (program == 0)
    return 0;
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "a_pos");
  glBindAttribLocation(program, 1, "a_uv");
  glBindAttribLocation(program, 2, "a_shade");
  if (with_depth)
    glBindAttribLocation(program, 3, "a_depth");
  glLinkProgram(program);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    GLsizei length = 0;
    glGetProgramInfoLog(program, (GLsizei)sizeof(log), &length, log);
    log[sizeof(log) - 1] = 0;
    char message[1200];
    snprintf(message, sizeof(message), "%s program link failed: %s", name,
             log);
    GlLog(gl, 2, message);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

static bool CreatePrograms(VoxelGl *gl) {
  GLuint vs3 = CompileShader(gl, GL_VERTEX_SHADER, kVertex3D, "voxel 3D vertex");
  GLuint fs3 = CompileShader(gl, GL_FRAGMENT_SHADER, kFragment3D,
                             "voxel 3D fragment");
  if (vs3 == 0 || fs3 == 0) {
    if (vs3) glDeleteShader(vs3);
    if (fs3) glDeleteShader(fs3);
    return false;
  }
  gl->prog3d = LinkProgram(gl, vs3, fs3, "voxel 3D", true);
  glDeleteShader(vs3);
  glDeleteShader(fs3);
  if (gl->prog3d == 0)
    return false;

  GLuint vs2 = CompileShader(gl, GL_VERTEX_SHADER, kVertex2D, "voxel 2D vertex");
  GLuint fs2 = CompileShader(gl, GL_FRAGMENT_SHADER, kFragment2D,
                             "voxel 2D fragment");
  if (vs2 == 0 || fs2 == 0) {
    if (vs2) glDeleteShader(vs2);
    if (fs2) glDeleteShader(fs2);
    return false;
  }
  gl->prog2d = LinkProgram(gl, vs2, fs2, "voxel 2D", false);
  glDeleteShader(vs2);
  glDeleteShader(fs2);
  if (gl->prog2d == 0)
    return false;

  // Shadow pass: same projection/vertex shader as 3D, constant alpha fragment.
  GLuint vs3b = CompileShader(gl, GL_VERTEX_SHADER, kVertex3D,
                              "voxel shadow vertex");
  GLuint fss = CompileShader(gl, GL_FRAGMENT_SHADER, kFragmentShadow,
                             "voxel shadow fragment");
  if (vs3b == 0 || fss == 0) {
    if (vs3b) glDeleteShader(vs3b);
    if (fss) glDeleteShader(fss);
    return false;
  }
  gl->prog_shadow = LinkProgram(gl, vs3b, fss, "voxel shadow", true);
  glDeleteShader(vs3b);
  glDeleteShader(fss);
  if (gl->prog_shadow == 0)
    return false;

  gl->u3d_center = glGetUniformLocation(gl->prog3d, "u_center");
  gl->u3d_size = glGetUniformLocation(gl->prog3d, "u_size");
  gl->u3d_tilt = glGetUniformLocation(gl->prog3d, "u_tilt");
  gl->u3d_persp = glGetUniformLocation(gl->prog3d, "u_persp");
  gl->u3d_zscale = glGetUniformLocation(gl->prog3d, "u_zscale");
  gl->u3d_yaw = glGetUniformLocation(gl->prog3d, "u_yaw");
  gl->u3d_zoom = glGetUniformLocation(gl->prog3d, "u_zoom");
  gl->u3d_tex = glGetUniformLocation(gl->prog3d, "u_tex");
  gl->us_center = glGetUniformLocation(gl->prog_shadow, "u_center");
  gl->us_size = glGetUniformLocation(gl->prog_shadow, "u_size");
  gl->us_tilt = glGetUniformLocation(gl->prog_shadow, "u_tilt");
  gl->us_persp = glGetUniformLocation(gl->prog_shadow, "u_persp");
  gl->us_zscale = glGetUniformLocation(gl->prog_shadow, "u_zscale");
  gl->us_yaw = glGetUniformLocation(gl->prog_shadow, "u_yaw");
  gl->us_zoom = glGetUniformLocation(gl->prog_shadow, "u_zoom");
  gl->us_strength = glGetUniformLocation(gl->prog_shadow, "u_strength");
  gl->u2d_size = glGetUniformLocation(gl->prog2d, "u_size");
  gl->u2d_tex = glGetUniformLocation(gl->prog2d, "u_tex");
  return true;
}

static bool EnsureFramebuffer(VoxelGl *gl, int width, int height) {
  if (gl->fbo != 0 && gl->width == width && gl->height == height)
    return true;

  if (gl->fbo != 0) {
    glDeleteFramebuffers(1, &gl->fbo);
    glDeleteTextures(1, &gl->color_tex);
    glDeleteRenderbuffers(1, &gl->depth_rb);
    gl->fbo = 0;
    gl->color_tex = 0;
    gl->depth_rb = 0;
  }

  glGenTextures(1, &gl->color_tex);
  glBindTexture(GL_TEXTURE_2D, gl->color_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenRenderbuffers(1, &gl->depth_rb);
  glBindRenderbuffer(GL_RENDERBUFFER, gl->depth_rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

  glGenFramebuffers(1, &gl->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         gl->color_tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, gl->depth_rb);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    char message[128];
    snprintf(message, sizeof(message), "framebuffer incomplete (0x%x)",
             (unsigned)status);
    GlLog(gl, 2, message);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }
  gl->width = width;
  gl->height = height;
  return true;
}

static void RestoreCurrent(VoxelGl *gl);

static bool MakeCurrent(VoxelGl *gl) {
  // SDL/GLX may already have its context current on this thread. Release it
  // before binding the EGL context; RestoreCurrent puts it back afterwards.
  if (gl->prev_glx_ctx != NULL && gl->prev_glx_dpy != NULL)
    glXMakeContextCurrent(gl->prev_glx_dpy, None, None, NULL);
  if (eglMakeCurrent(gl->dpy, gl->surf, gl->surf, gl->ctx))
    return true;
  if (gl->surf != EGL_NO_SURFACE &&
      eglMakeCurrent(gl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, gl->ctx))
    return true;
  RestoreCurrent(gl);
  return false;
}

static void RestoreCurrent(VoxelGl *gl) {
  // Release our EGL context first; GLX and EGL share the thread's current
  // context slot on some drivers.
  eglMakeCurrent(gl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (gl->prev_glx_ctx != NULL && gl->prev_glx_dpy != NULL) {
    if (!glXMakeContextCurrent(gl->prev_glx_dpy, gl->prev_glx_draw,
                               gl->prev_glx_read, gl->prev_glx_ctx))
      GlLog(gl, 1, "could not restore the previous GLX context");
    return;
  }
  if (gl->prev_ctx != EGL_NO_CONTEXT) {
    if (!eglMakeCurrent(gl->prev_dpy, gl->prev_draw, gl->prev_read,
                        gl->prev_ctx))
      GlLog(gl, 1, "could not restore the previous EGL context");
  }
}

VoxelGl *VoxelGl_Create(VoxelGlLogFn log, void *log_user) {
  VoxelGl *gl = (VoxelGl *)calloc(1, sizeof(*gl));
  if (gl == NULL)
    return NULL;
  gl->log = log;
  gl->log_user = log_user;
  gl->dpy = EGL_NO_DISPLAY;
  gl->ctx = EGL_NO_CONTEXT;
  gl->surf = EGL_NO_SURFACE;

  PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
          "eglGetPlatformDisplayEXT");
  if (get_platform_display != NULL)
    gl->dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA,
                                   EGL_DEFAULT_DISPLAY, NULL);
  if (gl->dpy == EGL_NO_DISPLAY)
    gl->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (gl->dpy == EGL_NO_DISPLAY) {
    GlLog(gl, 2, "no EGL display; falling back to the reference renderer");
    free(gl);
    return NULL;
  }

  EGLint major = 0, minor = 0;
  if (!eglInitialize(gl->dpy, &major, &minor)) {
    GlLog(gl, 2, "eglInitialize failed; falling back to the reference renderer");
    free(gl);
    return NULL;
  }

  EGLint config_attrs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_NONE
  };
  EGLint config_count = 0;
  if (!eglChooseConfig(gl->dpy, config_attrs, &gl->cfg, 1, &config_count) ||
      config_count == 0) {
    EGLint fallback_attrs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24,
      EGL_NONE
    };
    config_count = 0;
    if (!eglChooseConfig(gl->dpy, fallback_attrs, &gl->cfg, 1,
                         &config_count) ||
        config_count == 0) {
      GlLog(gl, 2, "no suitable EGL config; falling back to the reference renderer");
      eglTerminate(gl->dpy);
      free(gl);
      return NULL;
    }
  }

  if (!eglBindAPI(EGL_OPENGL_API)) {
    GlLog(gl, 2, "eglBindAPI(OpenGL) failed; falling back to the reference renderer");
    eglTerminate(gl->dpy);
    free(gl);
    return NULL;
  }

  EGLint pbuffer_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
  gl->surf = eglCreatePbufferSurface(gl->dpy, gl->cfg, pbuffer_attrs);

  EGLint context_attrs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 2,
    EGL_CONTEXT_MINOR_VERSION, 1,
    EGL_NONE
  };
  gl->ctx = eglCreateContext(gl->dpy, gl->cfg, EGL_NO_CONTEXT, context_attrs);
  if (gl->ctx == EGL_NO_CONTEXT)
    gl->ctx = eglCreateContext(gl->dpy, gl->cfg, EGL_NO_CONTEXT, NULL);
  if (gl->ctx == EGL_NO_CONTEXT) {
    GlLog(gl, 2, "eglCreateContext failed; falling back to the reference renderer");
    if (gl->surf != EGL_NO_SURFACE)
      eglDestroySurface(gl->dpy, gl->surf);
    eglTerminate(gl->dpy);
    free(gl);
    return NULL;
  }

  if (!MakeCurrent(gl)) {
    GlLog(gl, 2, "eglMakeCurrent failed; falling back to the reference renderer");
    eglDestroyContext(gl->dpy, gl->ctx);
    if (gl->surf != EGL_NO_SURFACE)
      eglDestroySurface(gl->dpy, gl->surf);
    eglTerminate(gl->dpy);
    free(gl);
    return NULL;
  }

  const char *version = (const char *)glGetString(GL_VERSION);
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  char message[512];
  snprintf(message, sizeof(message), "OpenGL %s (%s) via EGL %d.%d",
           version != NULL ? version : "?", renderer != NULL ? renderer : "?",
           major, minor);
  GlLog(gl, 0, message);

  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glGenBuffers(1, &gl->vbo);
  glGenTextures(1, &gl->atlas_tex);
  glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (!CreatePrograms(gl)) {
    GlLog(gl, 2, "shader setup failed; falling back to the reference renderer");
    VoxelGl_Destroy(gl);
    return NULL;
  }

  // Do not keep our context current on the main thread: the engine creates
  // its own SDL/GLX context after loading the plugin.
  eglMakeCurrent(gl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  gl->ready = true;
  return gl;
}

void VoxelGl_Destroy(VoxelGl *gl) {
  if (gl == NULL)
    return;
  if (gl->ready) {
    MakeCurrent(gl);
    if (gl->fbo != 0) {
      glDeleteFramebuffers(1, &gl->fbo);
      glDeleteTextures(1, &gl->color_tex);
      glDeleteRenderbuffers(1, &gl->depth_rb);
    }
    if (gl->vbo != 0)
      glDeleteBuffers(1, &gl->vbo);
    if (gl->atlas_tex != 0)
      glDeleteTextures(1, &gl->atlas_tex);
    if (gl->prog3d != 0)
      glDeleteProgram(gl->prog3d);
    if (gl->prog2d != 0)
      glDeleteProgram(gl->prog2d);
    if (gl->prog_shadow != 0)
      glDeleteProgram(gl->prog_shadow);
  }
  free(gl->readback);
  if (gl->ctx != EGL_NO_CONTEXT) {
    eglMakeCurrent(gl->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(gl->dpy, gl->ctx);
  }
  if (gl->surf != EGL_NO_SURFACE)
    eglDestroySurface(gl->dpy, gl->surf);
  if (gl->dpy != EGL_NO_DISPLAY)
    eglTerminate(gl->dpy);
  free(gl);
}

bool VoxelGl_IsReady(const VoxelGl *gl) {
  return gl != NULL && gl->ready;
}

bool VoxelGl_BeginFrame(VoxelGl *gl, int width, int height, uint32_t clear_rgb) {
  if (gl == NULL || !gl->ready || width <= 0 || height <= 0)
    return false;

  gl->prev_dpy = eglGetCurrentDisplay();
  gl->prev_ctx = eglGetCurrentContext();
  gl->prev_draw = eglGetCurrentSurface(EGL_DRAW);
  gl->prev_read = eglGetCurrentSurface(EGL_READ);
  gl->prev_glx_ctx = glXGetCurrentContext();
  gl->prev_glx_draw = glXGetCurrentDrawable();
  gl->prev_glx_read = glXGetCurrentReadDrawable();
  gl->prev_glx_dpy = glXGetCurrentDisplay();
  if (!MakeCurrent(gl)) {
    GlLog(gl, 2, "cannot make the voxel GL context current");
    return false;
  }
  while (glGetError() != GL_NO_ERROR) {
  }

  if (!EnsureFramebuffer(gl, width, height)) {
    RestoreCurrent(gl);
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glClearColor(((clear_rgb >> 16) & 0xff) / 255.0f,
               ((clear_rgb >> 8) & 0xff) / 255.0f,
               (clear_rgb & 0xff) / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);
  return true;
}

void VoxelGl_SetViewport(VoxelGl *gl, int x, int y, int width, int height,
                         int logical_width, int logical_height) {
  if (gl == NULL)
    return;
  gl->logical_width = logical_width;
  gl->logical_height = logical_height;
  glViewport(x, y, width, height);
  glScissor(x, y, width, height);
}

bool VoxelGl_UploadAtlas(VoxelGl *gl, const uint8_t *rgba, int width,
                         int height) {
  if (gl == NULL || !gl->ready || rgba == NULL || width <= 0 || height <= 0)
    return false;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, rgba);
  return glGetError() == GL_NO_ERROR;
}

static void SetVertexPointers(bool with_depth) {
  const GLsizei stride = (GLsizei)sizeof(VoxelGlVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                        (const void *)offsetof(VoxelGlVertex, x));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                        (const void *)offsetof(VoxelGlVertex, u));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                        (const void *)offsetof(VoxelGlVertex, shade));
  if (with_depth) {
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          (const void *)offsetof(VoxelGlVertex, depth));
  }
}

bool VoxelGl_Draw3D(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                    const VoxelGlCamera *camera) {
  if (gl == NULL || !gl->ready)
    return false;
  if (count <= 0)
    return true;
  if (vertices == NULL || camera == NULL)
    return false;
  glUseProgram(gl->prog3d);
  glUniform2f(gl->u3d_center, camera->center_x, camera->center_y);
  glUniform2f(gl->u3d_size, camera->width, camera->height);
  glUniform1f(gl->u3d_tilt, camera->tilt);
  glUniform1f(gl->u3d_persp, camera->persp);
  glUniform1f(gl->u3d_zscale, camera->zscale);
  glUniform1f(gl->u3d_yaw, camera->yaw);
  glUniform1f(gl->u3d_zoom, camera->zoom);
  glUniform1i(gl->u3d_tex, 0);
  glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * sizeof(*vertices)),
               vertices, GL_STREAM_DRAW);
  SetVertexPointers(true);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDrawArrays(GL_TRIANGLES, 0, count);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);
  return glGetError() == GL_NO_ERROR;
}

bool VoxelGl_DrawShadows(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         const VoxelGlCamera *camera, float strength) {
  if (gl == NULL || !gl->ready)
    return false;
  if (count <= 0 || strength <= 0.0f)
    return true;
  if (vertices == NULL || camera == NULL)
    return false;
  glUseProgram(gl->prog_shadow);
  glUniform2f(gl->us_center, camera->center_x, camera->center_y);
  glUniform2f(gl->us_size, camera->width, camera->height);
  glUniform1f(gl->us_tilt, camera->tilt);
  glUniform1f(gl->us_persp, camera->persp);
  glUniform1f(gl->us_zscale, camera->zscale);
  glUniform1f(gl->us_yaw, camera->yaw);
  glUniform1f(gl->us_zoom, camera->zoom);
  glUniform1f(gl->us_strength, strength > 1.0f ? 1.0f : strength);
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * sizeof(*vertices)),
               vertices, GL_STREAM_DRAW);
  SetVertexPointers(true);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawArrays(GL_TRIANGLES, 0, count);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);
  return glGetError() == GL_NO_ERROR;
}

bool VoxelGl_Draw2D(VoxelGl *gl, const VoxelGlVertex *vertices, int count) {
  if (gl == NULL || !gl->ready)
    return false;
  if (count <= 0)
    return true;
  if (vertices == NULL)
    return false;
  glUseProgram(gl->prog2d);
  glUniform2f(gl->u2d_size, (float)gl->logical_width,
              (float)gl->logical_height);
  glUniform1i(gl->u2d_tex, 0);
  glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * sizeof(*vertices)),
               vertices, GL_STREAM_DRAW);
  SetVertexPointers(false);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDrawArrays(GL_TRIANGLES, 0, count);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  return glGetError() == GL_NO_ERROR;
}

bool VoxelGl_EndFrame(VoxelGl *gl, uint8_t *pixels, size_t pitch) {
  if (gl == NULL || !gl->ready || pixels == NULL)
    return false;
  glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  size_t needed = (size_t)gl->width * (size_t)gl->height * 4;
  if (gl->readback_size < needed) {
    uint8_t *buffer = (uint8_t *)realloc(gl->readback, needed);
    if (buffer == NULL) {
      GlLog(gl, 2, "out of memory for the GL readback buffer");
      RestoreCurrent(gl);
      return false;
    }
    gl->readback = buffer;
    gl->readback_size = needed;
  }
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, gl->width, gl->height, GL_BGRA, GL_UNSIGNED_BYTE,
               gl->readback);
  if (glGetError() != GL_NO_ERROR) {
    GlLog(gl, 2, "glReadPixels failed");
    RestoreCurrent(gl);
    return false;
  }
  size_t row_bytes = (size_t)gl->width * 4;
  for (int y = 0; y < gl->height; y++) {
    const uint8_t *src = gl->readback +
        (size_t)(gl->height - 1 - y) * row_bytes;
    memcpy(pixels + (size_t)y * pitch, src, row_bytes);
  }

  RestoreCurrent(gl);
  return true;
}

#else  // !VOXEL_HAVE_EGL

struct VoxelGl {
  bool ready;
};

VoxelGl *VoxelGl_Create(VoxelGlLogFn log, void *log_user) {
  (void)log;
  (void)log_user;
  return NULL;
}
void VoxelGl_Destroy(VoxelGl *gl) { (void)gl; }
bool VoxelGl_IsReady(const VoxelGl *gl) { (void)gl; return false; }
bool VoxelGl_BeginFrame(VoxelGl *gl, int width, int height,
                        uint32_t clear_rgb) {
  (void)gl; (void)width; (void)height; (void)clear_rgb;
  return false;
}
void VoxelGl_SetViewport(VoxelGl *gl, int x, int y, int width, int height,
                         int logical_width, int logical_height) {
  (void)gl; (void)x; (void)y; (void)width; (void)height;
  (void)logical_width; (void)logical_height;
}
bool VoxelGl_UploadAtlas(VoxelGl *gl, const uint8_t *rgba, int width,
                         int height) {
  (void)gl; (void)rgba; (void)width; (void)height;
  return false;
}
bool VoxelGl_Draw3D(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                    const VoxelGlCamera *camera) {
  (void)gl; (void)vertices; (void)count; (void)camera;
  return false;
}
bool VoxelGl_Draw2D(VoxelGl *gl, const VoxelGlVertex *vertices, int count) {
  (void)gl; (void)vertices; (void)count;
  return false;
}
bool VoxelGl_DrawShadows(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         const VoxelGlCamera *camera, float strength) {
  (void)gl; (void)vertices; (void)count; (void)camera; (void)strength;
  return false;
}
bool VoxelGl_EndFrame(VoxelGl *gl, uint8_t *pixels, size_t pitch) {
  (void)gl; (void)pixels; (void)pitch;
  return false;
}

#endif  // VOXEL_HAVE_EGL

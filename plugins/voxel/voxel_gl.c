#include "voxel_gl.h"

#include <math.h>
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
  GLuint world_fbo;
  GLuint world_color_tex;
  GLuint world_depth_rb;
  GLuint atlas_tex;
  GLuint vbo;
  GLuint prog3d;
  GLuint prog2d;
  GLuint prog_shadow;
  GLuint prog_shadow_map;
  GLuint prog_tilt;
  GLuint shadow_fbo;
  GLuint shadow_tex;
  bool shadow_ready;
  float shadow_strength;
  float light_mvp[16];

  GLint u3d_center;
  GLint u3d_size;
  GLint u3d_yaw;
  GLint u3d_cos_elev;
  GLint u3d_sin_elev;
  GLint u3d_distance;
  GLint u3d_zoom;
  GLint u3d_zscale;
  GLint u3d_world_offset;
  GLint u3d_tex;
  GLint u3d_light_mvp;
  GLint u3d_shadow;
  GLint u3d_shadow_ready;
  GLint u3d_shadow_strength;

  GLint uso_mvp;
  GLint uso_zscale;
  GLint uso_world_offset;
  GLint uso_tex;

  GLint ut_world;
  GLint ut_size;
  GLint ut_rect;
  GLint ut_texel;
  GLint ut_strength;

  GLint us_center;
  GLint us_size;
  GLint us_yaw;
  GLint us_cos_elev;
  GLint us_sin_elev;
  GLint us_distance;
  GLint us_zoom;
  GLint us_zscale;
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
  "uniform vec2 u_center;\n"
  "uniform vec2 u_size;\n"
  "uniform float u_yaw;\n"
  "uniform float u_cos_elev;\n"
  "uniform float u_sin_elev;\n"
  "uniform float u_distance;\n"
  "uniform float u_zoom;\n"
  "uniform float u_zscale;\n"
  "uniform vec2 u_world_offset;\n"
  "uniform mat4 u_light_mvp;\n"
  "varying vec2 v_uv;\n"
  "varying float v_shade;\n"
  "varying vec4 v_shadow;\n"
  "void main() {\n"
  "  vec2 world = a_pos.xy + u_world_offset;\n"
  "  float dx = world.x - u_center.x;\n"
  "  float dy = world.y - u_center.y;\n"
  "  float c = cos(u_yaw);\n"
  "  float s = sin(u_yaw);\n"
  "  float rx = dx * c - dy * s;\n"
  "  float ry = dx * s + dy * c;\n"
  "  float z = a_pos.z * u_zscale;\n"
  // The camera sits south of the view centre and looks north/down. view_z
  // grows away from it, so the perspective divide is physically meaningful
  // and the ground becomes a real trapezoid.
  "  float view_z = u_distance - ry * u_cos_elev - z * u_sin_elev;\n"
  "  view_z = max(view_z, 1.0);\n"
  "  float up = -ry * u_sin_elev + z * u_cos_elev;\n"
  "  float scale = u_zoom * u_distance / view_z;\n"
  "  float nx = rx * scale / (u_size.x * 0.5);\n"
  "  float ny = up * scale / (u_size.y * 0.5);\n"
  "  float z_ndc = clamp((view_z - u_distance) / (u_distance * 0.5),\n"
  "                      -1.0, 1.0);\n"
  "  gl_Position = vec4(nx * view_z, ny * view_z, z_ndc * view_z, view_z);\n"
  "  v_uv = a_uv;\n"
  "  v_shade = a_shade;\n"
  "  v_shadow = u_light_mvp * vec4(world.x, world.y, a_pos.z * u_zscale,\n"
  "                                1.0);\n"
  "}\n";

static const char kFragment3D[] =
  "#version 120\n"
  "uniform sampler2D u_tex;\n"
  "uniform sampler2D u_shadow;\n"
  "uniform float u_shadow_ready;\n"
  "uniform float u_shadow_strength;\n"
  "varying vec2 v_uv;\n"
  "varying float v_shade;\n"
  "varying vec4 v_shadow;\n"
  "void main() {\n"
  "  vec4 c = texture2D(u_tex, v_uv);\n"
  "  if (c.a < 0.5) discard;\n"
  "  float lit = 1.0;\n"
  "  if (u_shadow_ready > 0.5) {\n"
  "    vec3 sc = v_shadow.xyz / v_shadow.w * 0.5 + 0.5;\n"
  "    if (sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 && sc.y < 1.0 &&\n"
  "        sc.z > 0.0 && sc.z < 1.0) {\n"
  "      float bias = 0.0015;\n"
  "      float sum = 0.0;\n"
  "      for (int y = -1; y <= 1; y++)\n"
  "        for (int x = -1; x <= 1; x++)\n"
  "          sum += texture2D(u_shadow, sc.xy + vec2(x, y) * 0.0012).r;\n"
  "      float map = sum / 9.0;\n"
  "      lit = mix(1.0, 0.72, u_shadow_strength *\n"
  "                step(sc.z - bias, map));\n"
  "    }\n"
  "  }\n"
  "  gl_FragColor = vec4(c.rgb * v_shade * lit, 1.0);\n"
  "}\n";

static const char kFragmentShadow[] =
  "#version 120\n"
  "uniform float u_strength;\n"
  "void main() {\n"
  "  gl_FragColor = vec4(0.0, 0.0, 0.0, u_strength);\n"
  "}\n";

// Depth-only pass from the sun. It shares the world position convention of
// the main shader, so the same mesh casts without a second representation.
static const char kVertexTilt[] =
  "#version 120\n"
  "attribute vec3 a_pos;\n"
  "uniform vec2 u_size;\n"
  "varying vec2 v_uv;\n"
  "void main() {\n"
  "  v_uv = a_pos.xy / u_size;\n"
  "  gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
  "}\n";

static const char kFragmentTilt[] =
  "#version 120\n"
  "uniform sampler2D u_world;\n"
  "uniform vec2 u_tex_size;\n"
  "uniform vec4 u_rect;\n"
  "uniform float u_strength;\n"
  "varying vec2 v_uv;\n"
  "void main() {\n"
  "  vec2 uv = (u_rect.xy + v_uv * u_rect.zw) / u_tex_size;\n"
  "  if (u_strength <= 0.0001) {\n"
  "    gl_FragColor = vec4(texture2D(u_world, uv).rgb, 1.0);\n"
  "    return;\n"
  "  }\n"
  "  float d = abs(v_uv.y - 0.5);\n"
  "  float blur = smoothstep(0.20, 0.50, d) * u_strength;\n"
  "  vec2 o = (1.0 / u_tex_size) * (1.0 + 4.0 * blur);\n"
  "  vec4 c = texture2D(u_world, uv) * 0.2270270270;\n"
  "  c += (texture2D(u_world, uv + vec2(o.x, 0.0)) +\n"
  "        texture2D(u_world, uv - vec2(o.x, 0.0))) * 0.1945945946;\n"
  "  c += (texture2D(u_world, uv + vec2(0.0, o.y)) +\n"
  "        texture2D(u_world, uv - vec2(0.0, o.y))) * 0.1945945946;\n"
  "  c += (texture2D(u_world, uv + o) +\n"
  "        texture2D(u_world, uv - o)) * 0.0540540541;\n"
  "  c += (texture2D(u_world, uv + vec2(o.x, -o.y)) +\n"
  "        texture2D(u_world, uv + vec2(-o.x, o.y))) * 0.0540540541;\n"
  "  float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
  "  c.rgb = mix(vec3(luma), c.rgb, 1.0 + 0.10 * blur);\n"
  "  gl_FragColor = vec4(c.rgb, 1.0);\n"
  "}\n";

static const char kVertexShadowMap[] =
  "#version 120\n"
  "attribute vec3 a_pos;\n"
  "attribute vec2 a_uv;\n"
  "uniform mat4 u_light_mvp;\n"
  "uniform float u_zscale;\n"
  "uniform vec2 u_world_offset;\n"
  "varying vec2 v_uv;\n"
  "void main() {\n"
  "  gl_Position = u_light_mvp * vec4(a_pos.x + u_world_offset.x,\n"
  "                                   a_pos.y + u_world_offset.y,\n"
  "                                   a_pos.z * u_zscale, 1.0);\n"
  "  v_uv = a_uv;\n"
  "}\n";

static const char kFragmentShadowMap[] =
  "#version 120\n"
  "uniform sampler2D u_tex;\n"
  "varying vec2 v_uv;\n"
  "void main() {\n"
  "  if (texture2D(u_tex, v_uv).a < 0.5) discard;\n"
  "  gl_FragColor = vec4(1.0);\n"
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

static void Mat4Multiply(float out[16], const float a[16], const float b[16]) {
  float r[16];
  for (int c = 0; c < 4; c++) {
    for (int row = 0; row < 4; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 4; k++)
        sum += a[k * 4 + row] * b[c * 4 + k];
      r[c * 4 + row] = sum;
    }
  }
  memcpy(out, r, sizeof(r));
}

static float Vec3Dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Vec3Cross(float out[3], const float a[3], const float b[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static bool Vec3Normalize(float v[3]) {
  float len = sqrtf(Vec3Dot(v, v));
  if (len < 1e-5f)
    return false;
  v[0] /= len;
  v[1] /= len;
  v[2] /= len;
  return true;
}

static void Mat4LookAt(float out[16], const float eye[3], const float at[3],
                       const float up[3]) {
  float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  Vec3Normalize(f);
  float s[3];
  Vec3Cross(s, f, up);
  Vec3Normalize(s);
  float u[3];
  Vec3Cross(u, s, f);
  memset(out, 0, sizeof(float) * 16);
  out[0] = s[0]; out[4] = s[1]; out[8] = s[2];
  out[1] = u[0]; out[5] = u[1]; out[9] = u[2];
  out[2] = -f[0]; out[6] = -f[1]; out[10] = -f[2];
  out[12] = -Vec3Dot(s, eye);
  out[13] = -Vec3Dot(u, eye);
  out[14] = Vec3Dot(f, eye);
  out[15] = 1.0f;
}

static void Mat4Ortho(float out[16], float l, float r, float b, float t,
                      float n, float f) {
  memset(out, 0, sizeof(float) * 16);
  out[0] = 2.0f / (r - l);
  out[5] = 2.0f / (t - b);
  out[10] = -2.0f / (f - n);
  out[12] = -(r + l) / (r - l);
  out[13] = -(t + b) / (t - b);
  out[14] = -(f + n) / (f - n);
  out[15] = 1.0f;
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

  GLuint vss = CompileShader(gl, GL_VERTEX_SHADER, kVertexShadowMap,
                             "voxel shadow map vertex");
  GLuint fssm = CompileShader(gl, GL_FRAGMENT_SHADER, kFragmentShadowMap,
                              "voxel shadow map fragment");
  if (vss == 0 || fssm == 0) {
    if (vss) glDeleteShader(vss);
    if (fssm) glDeleteShader(fssm);
    return false;
  }
  gl->prog_shadow_map = LinkProgram(gl, vss, fssm, "voxel shadow map", true);
  glDeleteShader(vss);
  glDeleteShader(fssm);
  if (gl->prog_shadow_map == 0)
    return false;

  GLuint vst = CompileShader(gl, GL_VERTEX_SHADER, kVertexTilt,
                             "voxel tilt-shift vertex");
  GLuint fst = CompileShader(gl, GL_FRAGMENT_SHADER, kFragmentTilt,
                             "voxel tilt-shift fragment");
  if (vst == 0 || fst == 0) {
    if (vst) glDeleteShader(vst);
    if (fst) glDeleteShader(fst);
    return false;
  }
  gl->prog_tilt = LinkProgram(gl, vst, fst, "voxel tilt-shift", false);
  glDeleteShader(vst);
  glDeleteShader(fst);
  if (gl->prog_tilt == 0)
    return false;

  gl->u3d_center = glGetUniformLocation(gl->prog3d, "u_center");
  gl->u3d_size = glGetUniformLocation(gl->prog3d, "u_size");
  gl->u3d_yaw = glGetUniformLocation(gl->prog3d, "u_yaw");
  gl->u3d_cos_elev = glGetUniformLocation(gl->prog3d, "u_cos_elev");
  gl->u3d_sin_elev = glGetUniformLocation(gl->prog3d, "u_sin_elev");
  gl->u3d_distance = glGetUniformLocation(gl->prog3d, "u_distance");
  gl->u3d_zoom = glGetUniformLocation(gl->prog3d, "u_zoom");
  gl->u3d_zscale = glGetUniformLocation(gl->prog3d, "u_zscale");
  gl->u3d_world_offset =
      glGetUniformLocation(gl->prog3d, "u_world_offset");
  gl->u3d_tex = glGetUniformLocation(gl->prog3d, "u_tex");
  gl->u3d_light_mvp = glGetUniformLocation(gl->prog3d, "u_light_mvp");
  gl->u3d_shadow = glGetUniformLocation(gl->prog3d, "u_shadow");
  gl->u3d_shadow_ready = glGetUniformLocation(gl->prog3d, "u_shadow_ready");
  gl->u3d_shadow_strength =
      glGetUniformLocation(gl->prog3d, "u_shadow_strength");
  gl->uso_mvp = glGetUniformLocation(gl->prog_shadow_map, "u_light_mvp");
  gl->uso_zscale = glGetUniformLocation(gl->prog_shadow_map, "u_zscale");
  gl->uso_world_offset =
      glGetUniformLocation(gl->prog_shadow_map, "u_world_offset");
  gl->uso_tex = glGetUniformLocation(gl->prog_shadow_map, "u_tex");
  gl->ut_world = glGetUniformLocation(gl->prog_tilt, "u_world");
  gl->ut_size = glGetUniformLocation(gl->prog_tilt, "u_size");
  gl->ut_rect = glGetUniformLocation(gl->prog_tilt, "u_rect");
  gl->ut_texel = glGetUniformLocation(gl->prog_tilt, "u_tex_size");
  gl->ut_strength = glGetUniformLocation(gl->prog_tilt, "u_strength");
  gl->us_center = glGetUniformLocation(gl->prog_shadow, "u_center");
  gl->us_size = glGetUniformLocation(gl->prog_shadow, "u_size");
  gl->us_yaw = glGetUniformLocation(gl->prog_shadow, "u_yaw");
  gl->us_cos_elev = glGetUniformLocation(gl->prog_shadow, "u_cos_elev");
  gl->us_sin_elev = glGetUniformLocation(gl->prog_shadow, "u_sin_elev");
  gl->us_distance = glGetUniformLocation(gl->prog_shadow, "u_distance");
  gl->us_zoom = glGetUniformLocation(gl->prog_shadow, "u_zoom");
  gl->us_zscale = glGetUniformLocation(gl->prog_shadow, "u_zscale");
  gl->us_strength = glGetUniformLocation(gl->prog_shadow, "u_strength");
  gl->u2d_size = glGetUniformLocation(gl->prog2d, "u_size");
  gl->u2d_tex = glGetUniformLocation(gl->prog2d, "u_tex");
  return true;
}

static bool CreateTarget(GLuint *fbo, GLuint *tex, GLuint *depth, int width,
                         int height, bool linear, const char *name) {
  glGenTextures(1, tex);
  glBindTexture(GL_TEXTURE_2D, *tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  GLint filter = linear ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenRenderbuffers(1, depth);
  glBindRenderbuffer(GL_RENDERBUFFER, *depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

  glGenFramebuffers(1, fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         *tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, *depth);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    char message[160];
    snprintf(message, sizeof(message), "%s framebuffer incomplete (0x%x)", name,
             (unsigned)status);
    fprintf(stderr, "render-plugin: error: %s\n", message);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }
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
  if (gl->world_fbo != 0) {
    glDeleteFramebuffers(1, &gl->world_fbo);
    glDeleteTextures(1, &gl->world_color_tex);
    glDeleteRenderbuffers(1, &gl->world_depth_rb);
    gl->world_fbo = 0;
    gl->world_color_tex = 0;
    gl->world_depth_rb = 0;
  }

  if (!CreateTarget(&gl->fbo, &gl->color_tex, &gl->depth_rb, width, height,
                    false, "voxel"))
    return false;
  if (!CreateTarget(&gl->world_fbo, &gl->world_color_tex,
                    &gl->world_depth_rb, width, height, true, "voxel world"))
    return false;
  gl->width = width;
  gl->height = height;
  return true;
}

static bool EnsureShadowMap(VoxelGl *gl) {
  if (gl->shadow_fbo != 0)
    return true;
  enum { kShadowSize = 1024 };
  glGenTextures(1, &gl->shadow_tex);
  glBindTexture(GL_TEXTURE_2D, gl->shadow_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize,
               kShadowSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

  glGenFramebuffers(1, &gl->shadow_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->shadow_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                         GL_TEXTURE_2D, gl->shadow_tex, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    char message[128];
    snprintf(message, sizeof(message), "shadow framebuffer incomplete (0x%x)",
             (unsigned)status);
    GlLog(gl, 2, message);
    glDeleteFramebuffers(1, &gl->shadow_fbo);
    glDeleteTextures(1, &gl->shadow_tex);
    gl->shadow_fbo = 0;
    gl->shadow_tex = 0;
    return false;
  }
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
  if (!EnsureShadowMap(gl))
    GlLog(gl, 1, "shadow map unavailable; shadows are disabled");

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
    if (gl->prog_shadow_map != 0)
      glDeleteProgram(gl->prog_shadow_map);
    if (gl->prog_tilt != 0)
      glDeleteProgram(gl->prog_tilt);
    if (gl->world_fbo != 0) {
      glDeleteFramebuffers(1, &gl->world_fbo);
      glDeleteTextures(1, &gl->world_color_tex);
      glDeleteRenderbuffers(1, &gl->world_depth_rb);
    }
    if (gl->shadow_fbo != 0) {
      glDeleteFramebuffers(1, &gl->shadow_fbo);
      glDeleteTextures(1, &gl->shadow_tex);
    }
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

  glDisable(GL_SCISSOR_TEST);
  glClearColor(((clear_rgb >> 16) & 0xff) / 255.0f,
               ((clear_rgb >> 8) & 0xff) / 255.0f,
               (clear_rgb & 0xff) / 255.0f, 1.0f);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
  glViewport(0, 0, width, height);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->world_fbo);
  glViewport(0, 0, width, height);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
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
  // Every pane's 3D pass goes to the world target; CompositeWorld then copies
  // the pane into the output and Draw2D overlays the HUD.
  if (gl->world_fbo != 0)
    glBindFramebuffer(GL_FRAMEBUFFER, gl->world_fbo);
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

bool VoxelGl_RenderShadowMap(VoxelGl *gl, const VoxelGlVertex *terrain,
                             int terrain_count, const VoxelGlVertex *sprites,
                             int sprite_count, const VoxelGlCamera *camera,
                             float light_x, float light_y, float light_z,
                             float strength, float terrain_offset_x,
                             float terrain_offset_y) {
  if (gl == NULL || !gl->ready || camera == NULL)
    return false;
  gl->shadow_ready = false;
  gl->shadow_strength = strength > 1.0f ? 1.0f : (strength < 0.0f ? 0.0f
                                                                    : strength);
  if (gl->shadow_strength <= 0.0f || gl->shadow_fbo == 0 ||
      gl->shadow_tex == 0 || gl->prog_shadow_map == 0)
    return true;

  float l[3] = {light_x, light_y, light_z};
  if (!Vec3Normalize(l))
    return true;
  float center[3] = {camera->center_x, camera->center_y, 0.0f};
  float eye[3] = {
    center[0] + l[0] * 700.0f,
    center[1] + l[1] * 700.0f,
    center[2] + l[2] * 700.0f,
  };
  float up[3] = {0.0f, 0.0f, 1.0f};
  if (fabsf(l[2]) > 0.92f) {
    up[0] = 0.0f;
    up[1] = 1.0f;
    up[2] = 0.0f;
  }
  float radius = (camera->width > camera->height ? camera->width
                                                 : camera->height) * 1.6f +
                 160.0f;
  float view[16], ortho[16];
  Mat4LookAt(view, eye, center, up);
  Mat4Ortho(ortho, -radius, radius, -radius, radius, 1.0f, 1400.0f);
  Mat4Multiply(gl->light_mvp, ortho, view);

  GLint vp[4], scissor[4], prev_fbo = 0;
  glGetIntegerv(GL_VIEWPORT, vp);
  glGetIntegerv(GL_SCISSOR_BOX, scissor);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->shadow_fbo);
  glViewport(0, 0, 1024, 1024);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(gl->prog_shadow_map);
  glUniformMatrix4fv(gl->uso_mvp, 1, GL_FALSE, gl->light_mvp);
  glUniform1f(gl->uso_zscale, camera->zscale);
  glUniform2f(gl->uso_world_offset, terrain_offset_x, terrain_offset_y);
  glUniform1i(gl->uso_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.0f, 4.0f);
  if (terrain_count > 0) {
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)terrain_count * sizeof(*terrain)),
                 terrain, GL_STREAM_DRAW);
    SetVertexPointers(true);
    glDrawArrays(GL_TRIANGLES, 0, terrain_count);
  }
  if (sprite_count > 0) {
    glUniform2f(gl->uso_world_offset, 0.0f, 0.0f);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)sprite_count * sizeof(*sprites)),
                 sprites, GL_STREAM_DRAW);
    SetVertexPointers(true);
    glDrawArrays(GL_TRIANGLES, 0, sprite_count);
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
  glEnable(GL_SCISSOR_TEST);
  bool ok = glGetError() == GL_NO_ERROR;
  gl->shadow_ready = ok;
  return ok;
}

bool VoxelGl_Draw3D(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                    const VoxelGlCamera *camera, float offset_x,
                    float offset_y) {
  if (gl == NULL || !gl->ready)
    return false;
  if (count <= 0)
    return true;
  if (vertices == NULL || camera == NULL)
    return false;
  glUseProgram(gl->prog3d);
  glUniform2f(gl->u3d_center, camera->center_x, camera->center_y);
  glUniform2f(gl->u3d_size, camera->width, camera->height);
  glUniform1f(gl->u3d_yaw, camera->yaw);
  glUniform1f(gl->u3d_cos_elev, camera->cos_elev);
  glUniform1f(gl->u3d_sin_elev, camera->sin_elev);
  glUniform1f(gl->u3d_distance, camera->distance);
  glUniform1f(gl->u3d_zoom, camera->zoom);
  glUniform1f(gl->u3d_zscale, camera->zscale);
  glUniform2f(gl->u3d_world_offset, offset_x, offset_y);
  glUniform1i(gl->u3d_tex, 0);
  glUniformMatrix4fv(gl->u3d_light_mvp, 1, GL_FALSE, gl->light_mvp);
  glUniform1f(gl->u3d_shadow_ready, gl->shadow_ready ? 1.0f : 0.0f);
  glUniform1f(gl->u3d_shadow_strength, gl->shadow_strength);
  glUniform1i(gl->u3d_shadow, 1);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, gl->shadow_tex);
  glActiveTexture(GL_TEXTURE0);
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
  glUniform1f(gl->us_yaw, camera->yaw);
  glUniform1f(gl->us_cos_elev, camera->cos_elev);
  glUniform1f(gl->us_sin_elev, camera->sin_elev);
  glUniform1f(gl->us_distance, camera->distance);
  glUniform1f(gl->us_zoom, camera->zoom);
  glUniform1f(gl->us_zscale, camera->zscale);
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

bool VoxelGl_CompositeWorld(VoxelGl *gl, float strength) {
  if (gl == NULL || !gl->ready || gl->prog_tilt == 0)
    return false;
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glUseProgram(gl->prog_tilt);
  glUniform2f(gl->ut_size, (float)gl->width, (float)gl->height);
  glUniform2f(gl->ut_texel, (float)gl->width, (float)gl->height);
  glUniform4f(gl->ut_rect, (float)vp[0], (float)vp[1], (float)vp[2],
              (float)vp[3]);
  glUniform1f(gl->ut_strength, strength > 1.0f ? 1.0f
                                  : (strength < 0.0f ? 0.0f : strength));
  glUniform1i(gl->ut_world, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl->world_color_tex);
  GLint filter = strength > 0.001f ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  VoxelGlVertex quad[6];
  memset(quad, 0, sizeof(quad));
  float w = (float)gl->width;
  float h = (float)gl->height;
  const float px[6] = {0, w, w, 0, w, 0};
  const float py[6] = {0, 0, h, 0, h, h};
  for (int i = 0; i < 6; i++) {
    quad[i].x = px[i];
    quad[i].y = py[i];
    quad[i].shade = 1.0f;
  }
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
  SetVertexPointers(false);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  return glGetError() == GL_NO_ERROR;
}

static bool Draw2DTarget(VoxelGl *gl, const VoxelGlVertex *vertices,
                         int count, GLuint fbo) {
  if (gl == NULL || !gl->ready)
    return false;
  if (count <= 0)
    return true;
  if (vertices == NULL)
    return false;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
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

bool VoxelGl_DrawBackdrop(VoxelGl *gl, const VoxelGlVertex *vertices,
                          int count) {
  if (gl == NULL || !gl->ready)
    return false;
  return Draw2DTarget(gl, vertices, count, gl->world_fbo);
}

bool VoxelGl_DrawOverlay(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         bool subtract, bool half) {
  if (gl == NULL || !gl->ready)
    return false;
  if (count <= 0)
    return true;
  if (vertices == NULL)
    return false;
  glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
  glUseProgram(gl->prog2d);
  glUniform2f(gl->u2d_size, (float)gl->logical_width,
              (float)gl->logical_height);
  glUniform1i(gl->u2d_tex, 0);
  glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
  glBindBuffer(GL_ARRAY_BUFFER, gl->vbo);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)((size_t)count * sizeof(*vertices)), vertices,
               GL_STREAM_DRAW);
  SetVertexPointers(false);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendColor(0.0f, 0.0f, 0.0f, 0.5f);
  if (subtract) {
    glBlendFunc(half ? GL_CONSTANT_ALPHA : GL_ONE,
                half ? GL_CONSTANT_ALPHA : GL_ONE);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
  } else if (half) {
    glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
  } else {
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);
  }
  glDrawArrays(GL_TRIANGLES, 0, count);
  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_BLEND);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  return glGetError() == GL_NO_ERROR;
}

bool VoxelGl_Draw2D(VoxelGl *gl, const VoxelGlVertex *vertices, int count) {
  if (gl == NULL || !gl->ready)
    return false;
  return Draw2DTarget(gl, vertices, count, gl->fbo);
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
                    const VoxelGlCamera *camera, float offset_x,
                    float offset_y) {
  (void)gl; (void)vertices; (void)count; (void)camera; (void)offset_x;
  (void)offset_y;
  return false;
}
bool VoxelGl_CompositeWorld(VoxelGl *gl, float strength) {
  (void)gl; (void)strength;
  return false;
}
bool VoxelGl_DrawBackdrop(VoxelGl *gl, const VoxelGlVertex *vertices,
                          int count) {
  (void)gl; (void)vertices; (void)count;
  return false;
}
bool VoxelGl_DrawOverlay(VoxelGl *gl, const VoxelGlVertex *vertices, int count,
                         bool subtract, bool half) {
  (void)gl; (void)vertices; (void)count; (void)subtract; (void)half;
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
bool VoxelGl_RenderShadowMap(VoxelGl *gl, const VoxelGlVertex *terrain,
                             int terrain_count, const VoxelGlVertex *sprites,
                             int sprite_count, const VoxelGlCamera *camera,
                             float light_x, float light_y, float light_z,
                             float strength, float terrain_offset_x,
                             float terrain_offset_y) {
  (void)gl; (void)terrain; (void)terrain_count; (void)sprites;
  (void)sprite_count; (void)camera; (void)light_x; (void)light_y;
  (void)light_z; (void)strength; (void)terrain_offset_x;
  (void)terrain_offset_y;
  return false;
}
bool VoxelGl_EndFrame(VoxelGl *gl, uint8_t *pixels, size_t pitch) {
  (void)gl; (void)pixels; (void)pitch;
  return false;
}

#endif  // VOXEL_HAVE_EGL

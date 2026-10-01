// OpenGL ES 3 backend of the view renderer (view_gpu.h).
//
// One offscreen EGL context (pbuffer 1x1, the picture goes to a framebuffer
// object) shared by every thread that renders: draw() takes the lock, makes
// the context current, draws the frame Core prepared, resolves the MSAA
// target, reads the pixels back and releases the context again. Textures and
// the room geometry stay on the GPU between frames; the posed model is
// uploaded every frame. Nothing here decides what a triangle looks like —
// those rules come from Core's GpuViewFrame — the shaders only evaluate them
// per pixel.
#include "gles_view_backend.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dmcresource/view_gpu.h"

namespace dmcviewer {
namespace {

using dmcresource::GpuBatch;
using dmcresource::GpuBatchKind;
using dmcresource::GpuScreenVertex;
using dmcresource::GpuVertex;
using dmcresource::GpuViewFrame;
using dmcresource::ImagePreview;
using dmcresource::RgbaImage;

constexpr const char* kMeshVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec3 a_normal;
layout(location = 4) in float a_light;
uniform mat4 u_view;
uniform vec4 u_proj;
uniform float u_cd;
uniform int u_room;
uniform vec2 u_light;
uniform vec2 u_scroll;
uniform float u_depth_bias;
out vec2 v_uv;
out vec4 v_color;
out float v_light;
out float v_z;
void main() {
    vec3 c = (u_view * vec4(a_pos, 1.0)).xyz;
    float term = a_light;
    if (u_room == 1) {
        vec3 n = mat3(u_view) * a_normal;
        float nl = length(n);
        float pl = length(c);
        term = (nl > 0.0 && pl > 0.0) ? abs(dot(n, c)) / (nl * pl) : 1.0;
    }
    v_light = u_light.x + u_light.y * term;
    v_uv = a_uv + u_scroll;
    v_color = a_color * (255.0 / 128.0);
    v_z = c.z - u_cd;
    gl_Position = vec4(u_proj.x * c.x, u_proj.y * c.y, u_proj.z * c.z + u_proj.w, c.z);
    if (u_depth_bias != 0.0) {
        float zb = max(c.z - u_depth_bias, 1.0e-4);
        gl_Position.z = (u_proj.z + u_proj.w / zb) * c.z;
    }
}
)";

// u_mode: 0 model, 1 model additive / subtractive, 2 room opaque texels,
// 3 room soft texels, 4 room additive / subtractive, 5 solid colour, 6 shadow.
constexpr const char* kMeshFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec4 v_color;
in float v_light;
in float v_z;
uniform sampler2D u_tex;
uniform int u_textured;
uniform int u_mode;
uniform int u_colored;
uniform int u_translucent;
uniform float u_radius;
uniform vec4 u_solid;
out vec4 o;
void main() {
    if (u_mode == 5) { o = u_solid; return; }
    if (u_mode == 6) { o = vec4(0.45, 0.45, 0.45, 1.0); return; }
    if (u_mode <= 1) {
        if (u_textured == 0) {
            float zn = 0.5 + 0.5 * tanh(-v_z / u_radius);
            float s = floor(145.0 + 80.0 * zn) / 255.0;
            o = vec4(s, s, min(1.0, s + 10.0 / 255.0), 1.0);
            return;
        }
        vec4 t = texture(u_tex, v_uv);
        if (u_colored == 1) t = min(t * v_color, vec4(1.0));
        t.rgb = min(t.rgb * v_light, vec3(1.0));
        if (t.a < 0.5 / 255.0) discard;
        o = t;
        return;
    }
    vec4 t = u_textured == 1 ? texture(u_tex, v_uv) : vec4(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 1.0);
    if (t.a < 7.5 / 255.0) discard;
    bool opaque = u_mode == 2 || u_mode == 3 ? (t.a >= 239.5 / 255.0 || u_translucent == 0) : false;
    if (u_mode == 2) {
        if (!opaque) discard;
        if (u_translucent == 0 && t.a < 31.5 / 255.0) discard;
    } else if (opaque) {
        discard;
    }
    vec3 c = t.rgb;
    if (u_colored == 1) c *= v_color.rgb;
    c *= v_light;
    if (u_mode == 4) {
        float k = t.a;
        if (u_colored == 1) k *= min(1.0, v_color.a);
        o = vec4(c * k, 1.0);
        return;
    }
    o = vec4(min(c, vec3(1.0)), u_mode == 3 ? t.a : 1.0);
}
)";

// Effect quads: image-space positions (affine, like the software pass).
constexpr const char* kScreenVertex = R"(#version 300 es
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_size;
uniform vec3 u_depth;
out vec2 v_uv;
out vec4 v_color;
void main() {
    float cz = a_pos.z + u_depth.z;
    gl_Position = vec4(a_pos.x * 2.0 / u_size.x - 1.0, a_pos.y * 2.0 / u_size.y - 1.0,
                       u_depth.x + u_depth.y / cz, 1.0);
    v_uv = a_uv;
    v_color = a_color / 255.0;
}
)";

constexpr const char* kScreenFragment = R"(#version 300 es
precision highp float;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
uniform int u_textured;
out vec4 o;
void main() {
    vec4 t = u_textured == 1 ? texture(u_tex, v_uv) : vec4(1.0);
    bool tinted = any(lessThan(v_color, vec4(254.5 / 255.0)));
    if (tinted) t *= v_color;
    if (t.a < (tinted ? 1.5 : 7.5) / 255.0) discard;
    o = t;
}
)";

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteShader(shader);
        return 0U;
    }
    return shader;
}

GLuint link(const char* vertex, const char* fragment) {
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (vs == 0U || fs == 0U) {
        if (vs != 0U) glDeleteShader(vs);
        if (fs != 0U) glDeleteShader(fs);
        return 0U;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteProgram(program);
        return 0U;
    }
    return program;
}

// FNV-1a over a few texels: a texture at a reused address is re-uploaded.
std::uint64_t texel_signature(const ImagePreview& image) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto n = image.rgba8.size();
    for (std::size_t k = 0U; k < 32U && n != 0U; ++k) {
        h ^= image.rgba8[k * (n - 1U) / 31U];
        h *= 0x100000001B3ULL;
    }
    return h ^ (static_cast<std::uint64_t>(image.width) << 32U) ^ image.height;
}

class GlesViewBackend final : public dmcresource::GpuViewBackend {
public:
    bool draw(const GpuViewFrame& frame, RgbaImage& image) override {
        const std::lock_guard lock{mutex_};
        if (!initialize() || !make_current()) return false;
        const bool ok = draw_current(frame, image);
        release_current();
        if (!ok && ++failures_ >= 8U) failed_ = true;  // stop retrying a broken driver
        if (ok) failures_ = 0U;
        return ok;
    }

    std::string describe() override {
        const std::lock_guard lock{mutex_};
        if (!initialize()) return {};
        return description_;
    }

private:
    struct Texture final {
        GLuint id{};
        const void* data{};
        std::uint64_t signature{};
        std::uint64_t last_frame{};
        std::size_t bytes{};
    };

    struct Uniforms final {
        GLint view{}, proj{}, cd{}, room{}, light{}, scroll{}, depth_bias{};
        GLint tex{}, textured{}, mode{}, colored{}, translucent{}, radius{}, solid{};
    };

    bool initialize() {
        if (failed_) return false;
        if (context_ != EGL_NO_CONTEXT) return true;
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
            failed_ = true;
            return false;
        }
        eglBindAPI(EGL_OPENGL_ES_API);
        const EGLint config_attributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (eglChooseConfig(display_, config_attributes, &config, 1, &count) != EGL_TRUE || count < 1) {
            failed_ = true;
            return false;
        }
        const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
        const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, surface_attributes);
        if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE || !make_current()) {
            failed_ = true;
            return false;
        }
        const bool ready = create_resources();
        release_current();
        if (!ready) failed_ = true;
        return ready;
    }

    bool make_current() {
        return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
    }

    void release_current() {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    bool create_resources() {
        const auto text = [](GLenum name) {
            const auto* value = reinterpret_cast<const char*>(glGetString(name));
            return std::string(value != nullptr ? value : "");
        };
        auto version = text(GL_VERSION);
        // "OpenGL ES 3.2 V@0800.0 ..." -> "OpenGL ES 3.2"
        if (version.size() > 13U) version.resize(13U);
        description_ = version + " / " + text(GL_RENDERER);

        mesh_program_ = link(kMeshVertex, kMeshFragment);
        screen_program_ = link(kScreenVertex, kScreenFragment);
        if (mesh_program_ == 0U || screen_program_ == 0U) return false;
        const auto at = [this](const char* name) { return glGetUniformLocation(mesh_program_, name); };
        u_.view = at("u_view");
        u_.proj = at("u_proj");
        u_.cd = at("u_cd");
        u_.room = at("u_room");
        u_.light = at("u_light");
        u_.scroll = at("u_scroll");
        u_.depth_bias = at("u_depth_bias");
        u_.tex = at("u_tex");
        u_.textured = at("u_textured");
        u_.mode = at("u_mode");
        u_.colored = at("u_colored");
        u_.translucent = at("u_translucent");
        u_.radius = at("u_radius");
        u_.solid = at("u_solid");
        screen_size_ = glGetUniformLocation(screen_program_, "u_size");
        screen_depth_ = glGetUniformLocation(screen_program_, "u_depth");
        screen_tex_ = glGetUniformLocation(screen_program_, "u_tex");
        screen_textured_ = glGetUniformLocation(screen_program_, "u_textured");

        glGenBuffers(1, &model_vbo_);
        glGenBuffers(1, &room_vbo_);
        glGenBuffers(1, &aux_vbo_);
        glGenBuffers(1, &screen_vbo_);
        glGenVertexArrays(1, &model_vao_);
        glGenVertexArrays(1, &room_vao_);
        glGenVertexArrays(1, &aux_vao_);
        glGenVertexArrays(1, &screen_vao_);
        mesh_layout(model_vao_, model_vbo_);
        mesh_layout(room_vao_, room_vbo_);
        mesh_layout(aux_vao_, aux_vbo_);
        glBindVertexArray(screen_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
        constexpr auto stride = static_cast<GLsizei>(sizeof(GpuScreenVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuScreenVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuScreenVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuScreenVertex, rgba)));
        glBindVertexArray(0);

        // Pixel (original) look and the smooth one: trilinear + anisotropic.
        glGenSamplers(1, &nearest_);
        glSamplerParameteri(nearest_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(nearest_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(nearest_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(nearest_, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenSamplers(1, &smooth_);
        glSamplerParameteri(smooth_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(smooth_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(smooth_, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(smooth_, GL_TEXTURE_WRAP_T, GL_REPEAT);
        const std::string extensions = text(GL_EXTENSIONS);
        if (extensions.find("GL_EXT_texture_filter_anisotropic") != std::string::npos) {
            GLfloat most = 1.0F;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
            glSamplerParameterf(smooth_, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0F, most));
        }
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples_);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_target_);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_);
        return glGetError() == GL_NO_ERROR;
    }

    static void mesh_layout(GLuint vao, GLuint vbo) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        constexpr auto stride = static_cast<GLsizei>(sizeof(GpuVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuVertex, rgba)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(GpuVertex, nx)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(GpuVertex, light)));
        glBindVertexArray(0);
    }

    // Colour + depth/stencil target, multisampled when it fits; a plain
    // target the samples resolve into and the pixels are read from.
    bool ensure_target(int width, int height) {
        if (width > max_target_ || height > max_target_) return false;
        const auto pixels = static_cast<long long>(width) * height;
        int samples = pixels <= 2'600'000LL ? 4 : (pixels <= 9'000'000LL ? 2 : 0);
        samples = std::min(samples, static_cast<int>(max_samples_));
        if (width == target_width_ && height == target_height_ && samples == target_samples_ && fbo_ != 0U) {
            return true;
        }
        destroy_target();
        for (;; samples = samples > 1 ? samples / 2 : 0) {
            if (build_target(width, height, samples)) return true;
            destroy_target();
            if (samples == 0) return false;
        }
    }

    bool build_target(int width, int height, int samples) {
        glGenFramebuffers(1, &fbo_);
        glGenRenderbuffers(1, &color_);
        glGenRenderbuffers(1, &depth_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, color_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR) {
            return false;
        }
        if (samples > 0) {
            glGenFramebuffers(1, &resolve_fbo_);
            glGenRenderbuffers(1, &resolve_color_);
            glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_);
            glBindRenderbuffer(GL_RENDERBUFFER, resolve_color_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve_color_);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
                glGetError() != GL_NO_ERROR) {
                return false;
            }
        }
        target_width_ = width;
        target_height_ = height;
        target_samples_ = samples;
        return true;
    }

    void destroy_target() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        const GLuint fbos[2] = {fbo_, resolve_fbo_};
        const GLuint rbs[3] = {color_, depth_, resolve_color_};
        glDeleteFramebuffers(2, fbos);
        glDeleteRenderbuffers(3, rbs);
        fbo_ = resolve_fbo_ = color_ = depth_ = resolve_color_ = 0U;
        target_width_ = target_height_ = 0;
        target_samples_ = -1;
        glGetError();
    }

    // The GL texture of an image, uploaded with mipmaps on first use.
    GLuint texture(const ImagePreview* image) {
        if (image == nullptr || !image->available()) return 0U;
        if (image->width > static_cast<std::uint32_t>(max_texture_) ||
            image->height > static_cast<std::uint32_t>(max_texture_)) {
            return 0U;
        }
        auto& entry = textures_[image];
        const auto signature = texel_signature(*image);
        if (entry.id == 0U || entry.data != image->rgba8.data() || entry.signature != signature) {
            if (entry.id == 0U) glGenTextures(1, &entry.id);
            glBindTexture(GL_TEXTURE_2D, entry.id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image->width),
                         static_cast<GLsizei>(image->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image->rgba8.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            texture_bytes_ -= entry.bytes;
            entry.bytes = image->rgba8.size() * 4U / 3U;
            texture_bytes_ += entry.bytes;
            entry.data = image->rgba8.data();
            entry.signature = signature;
        }
        entry.last_frame = frame_number_;
        return entry.id;
    }

    // Textures unused for a while, or the oldest ones past the budget.
    void trim_textures() {
        constexpr std::size_t kBudget = 768U * 1024U * 1024U;
        for (auto it = textures_.begin(); it != textures_.end();) {
            const bool stale = frame_number_ - it->second.last_frame > 900U;
            const bool over = texture_bytes_ > kBudget && it->second.last_frame != frame_number_;
            if (stale || over) {
                glDeleteTextures(1, &it->second.id);
                texture_bytes_ -= it->second.bytes;
                it = textures_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void bind_texture(GLint textured_uniform, const ImagePreview* image, bool smooth) {
        const GLuint id = texture(image);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, id);
        glBindSampler(0U, smooth ? smooth_ : nearest_);
        glUniform1i(textured_uniform, id != 0U ? 1 : 0);
    }

    void set_blend(GpuBatchKind kind, bool soft_alpha) {
        const bool additive = kind == GpuBatchKind::model_additive || kind == GpuBatchKind::room_additive;
        const bool subtractive = kind == GpuBatchKind::model_subtractive || kind == GpuBatchKind::room_subtractive;
        if (!additive && !subtractive && !soft_alpha) {
            glDisable(GL_BLEND);
            return;
        }
        glEnable(GL_BLEND);
        glBlendEquation(subtractive ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        const bool room = kind == GpuBatchKind::room_additive || kind == GpuBatchKind::room_subtractive;
        if (additive || subtractive) {
            // Model: src * a; room: the shader already weighted it.
            glBlendFunc(room ? GL_ONE : GL_SRC_ALPHA, GL_ONE);
        } else {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    void draw_batch(const GpuBatch& batch, int mode, bool smooth, const GpuViewFrame& frame) {
        bind_texture(u_.textured, batch.texture, smooth);
        glUniform1i(u_.mode, mode);
        glUniform1i(u_.colored, batch.colored ? 1 : 0);
        glUniform1i(u_.translucent, batch.translucent ? 1 : 0);
        glUniform2f(u_.light, batch.light_base, batch.light_gain);
        float su = 0.0F, sv = 0.0F;
        if (batch.scroll_slot != dmcresource::kNoTextureSlot) {
            for (const auto& s : frame.room_scrolls) {
                if (s.slot == batch.scroll_slot) {
                    su = s.u;
                    sv = s.v;
                    break;
                }
            }
        }
        glUniform2f(u_.scroll, su, sv);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.first), static_cast<GLsizei>(batch.count));
    }

    void upload_aux(const std::vector<dmcresource::Vec3>& points) {
        std::vector<GpuVertex> vertices(points.size());
        for (std::size_t i = 0U; i < points.size(); ++i) {
            vertices[i].x = points[i].x;
            vertices[i].y = points[i].y;
            vertices[i].z = points[i].z;
        }
        glBindBuffer(GL_ARRAY_BUFFER, aux_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuVertex)), vertices.data(),
                     GL_STREAM_DRAW);
    }

    bool draw_current(const GpuViewFrame& frame, RgbaImage& image) {
        ++frame_number_;
        glGetError();
        if (!ensure_target(frame.width, frame.height)) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, frame.width, frame.height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFU);
        glClearColor(frame.background[0] / 255.0F, frame.background[1] / 255.0F, frame.background[2] / 255.0F, 1.0F);
        glClearDepthf(1.0F);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glUseProgram(mesh_program_);
        glUniform4f(u_.proj, frame.projection[0], frame.projection[1], frame.projection[2], frame.projection[3]);
        glUniform1f(u_.cd, frame.camera_distance);
        glUniform1f(u_.radius, std::max(frame.radius, 1.0e-4F));
        glUniform1i(u_.tex, 0);
        glUniform1f(u_.depth_bias, 0.0F);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Room geometry: rebuilt only when the room changed.
        if (frame.room) {
            if (!room_cached_ || !(room_key_ == frame.room_key)) {
                const auto geometry = frame.build_room();
                glBindBuffer(GL_ARRAY_BUFFER, room_vbo_);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.vertices.size() * sizeof(GpuVertex)),
                             geometry.vertices.data(), GL_STATIC_DRAW);
                room_batches_ = geometry.batches;
                room_key_ = frame.room_key;
                room_cached_ = true;
            }
        }

        // 1. Opaque room texels.
        if (frame.room) {
            glBindVertexArray(room_vao_);
            glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.room_view.data());
            glUniform1i(u_.room, 1);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            for (const auto& batch : room_batches_) {
                if (batch.kind == GpuBatchKind::room) draw_batch(batch, 2, frame.smooth_room, frame);
            }
        }

        // 2. Plain floor.
        glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.model_view.data());
        glUniform1i(u_.room, 0);
        glUniform2f(u_.light, 1.0F, 0.0F);
        glUniform2f(u_.scroll, 0.0F, 0.0F);
        if (!frame.floor.empty()) {
            upload_aux(frame.floor);
            glBindVertexArray(aux_vao_);
            glDisable(GL_BLEND);
            glUniform1i(u_.mode, 5);
            glUniform4f(u_.solid, frame.floor_rgb[0] / 255.0F, frame.floor_rgb[1] / 255.0F,
                        frame.floor_rgb[2] / 255.0F, 1.0F);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.floor.size()));
        }

        // 3. The model: blended-over batches writing depth, then additive /
        // subtractive ones on top without it.
        if (!frame.model.vertices.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, model_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(frame.model.vertices.size() * sizeof(GpuVertex)),
                         frame.model.vertices.data(), GL_STREAM_DRAW);
            glBindVertexArray(model_vao_);
            for (const auto& batch : frame.model.batches) {
                const bool layered = batch.kind != GpuBatchKind::model;
                glDepthMask(layered ? GL_FALSE : GL_TRUE);
                set_blend(batch.kind, !layered);
                draw_batch(batch, layered ? 1 : 0, frame.smooth_model, frame);
            }
        }

        // 4. Soft room texels, then light shafts.
        if (frame.room) {
            glBindVertexArray(room_vao_);
            glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.room_view.data());
            glUniform1i(u_.room, 1);
            glDepthMask(GL_FALSE);
            for (const auto& batch : room_batches_) {
                if (batch.kind != GpuBatchKind::room || !batch.translucent) continue;
                set_blend(batch.kind, true);
                draw_batch(batch, 3, frame.smooth_room, frame);
            }
            for (const auto& batch : room_batches_) {
                if (batch.kind == GpuBatchKind::room) continue;
                set_blend(batch.kind, true);
                draw_batch(batch, 4, frame.smooth_room, frame);
            }
        }

        // 5. Shadow footprint: darkens what is visible there once per pixel.
        if (!frame.shadow.empty()) {
            upload_aux(frame.shadow);
            glBindVertexArray(aux_vao_);
            glUniformMatrix4fv(u_.view, 1, GL_FALSE, frame.model_view.data());
            glUniform1i(u_.room, 0);
            glUniform1i(u_.mode, 6);
            glUniform1f(u_.depth_bias, frame.radius * 0.01F);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, 0, 0xFFU);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(frame.shadow.size()));
            glDisable(GL_STENCIL_TEST);
            glUniform1f(u_.depth_bias, 0.0F);
            glDepthFunc(GL_LESS);
        }

        // 6. Effect quads (image space).
        if (!frame.effects.empty()) {
            std::vector<GpuScreenVertex> vertices;
            vertices.reserve(frame.effects.size() * 6U);
            for (const auto& quad : frame.effects) {
                for (const int k : {0, 1, 2, 0, 2, 3}) vertices.push_back(quad.vertices[static_cast<std::size_t>(k)]);
            }
            glUseProgram(screen_program_);
            glUniform2f(screen_size_, static_cast<float>(frame.width), static_cast<float>(frame.height));
            glUniform3f(screen_depth_, frame.projection[2], frame.projection[3], frame.camera_distance);
            glUniform1i(screen_tex_, 0);
            glBindVertexArray(screen_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, screen_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuScreenVertex)),
                         vertices.data(), GL_STREAM_DRAW);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            for (std::size_t i = 0U; i < frame.effects.size(); ++i) {
                const auto& quad = frame.effects[i];
                glBlendFunc(GL_SRC_ALPHA, quad.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                bind_texture(screen_textured_, quad.texture, true);
                glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i * 6U), 6);
            }
        }
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        // Resolve and read back: GL row 0 is image row 0 (the projection
        // flips y), so the rows land top-down as RgbaImage stores them.
        GLuint read_fbo = fbo_;
        if (target_samples_ > 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_);
            glBlitFramebuffer(0, 0, frame.width, frame.height, 0, 0, frame.width, frame.height,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            read_fbo = resolve_fbo_;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        image.width = frame.width;
        image.height = frame.height;
        image.pixels.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U);
        glReadPixels(0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        for (std::size_t i = 3U; i < image.pixels.size(); i += 4U) image.pixels[i] = 255U;
        trim_textures();
        return glGetError() == GL_NO_ERROR;
    }

    std::mutex mutex_;
    bool failed_{false};
    unsigned failures_{0U};
    std::string description_;
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLContext context_{EGL_NO_CONTEXT};
    EGLSurface surface_{EGL_NO_SURFACE};

    GLuint mesh_program_{}, screen_program_{};
    Uniforms u_{};
    GLint screen_size_{}, screen_depth_{}, screen_tex_{}, screen_textured_{};
    GLuint model_vbo_{}, room_vbo_{}, aux_vbo_{}, screen_vbo_{};
    GLuint model_vao_{}, room_vao_{}, aux_vao_{}, screen_vao_{};
    GLuint nearest_{}, smooth_{};
    GLint max_samples_{0}, max_target_{4096}, max_texture_{4096};

    GLuint fbo_{}, color_{}, depth_{}, resolve_fbo_{}, resolve_color_{};
    int target_width_{}, target_height_{}, target_samples_{-1};

    bool room_cached_{false};
    dmcresource::GpuRoomKey room_key_{};
    std::vector<GpuBatch> room_batches_;

    std::unordered_map<const ImagePreview*, Texture> textures_;
    std::size_t texture_bytes_{};
    std::uint64_t frame_number_{};
};

}  // namespace

void install_gles_view_backend() {
    static GlesViewBackend backend;
    dmcresource::set_gpu_view_backend(&backend);
}

}  // namespace dmcviewer

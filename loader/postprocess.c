#include "postprocess.h"

extern void game_log(const char *fmt, ...);

#ifdef POSTPROCESS_SHADER

#include <vitaGL.h>

// Sharpen (unsharp mask, 4 vecinos) sobre el blit del compositor 400x240 ->
// pantalla completa. NO se toca el motor ni sus asset/buffers -- solo se
// reemplaza, para ese unico draw call por frame, el fixed-function texturing
// (GL_REPLACE) por este programa. Ver port_progress.md Backlog B.1 para el
// analisis de por que reemplazar assets no serviria (el motor compone todo a
// software en un buffer fijo 400x240) y por que un shader de post-proceso en
// el blit final es la unica via.
//
// v2 (2026-07-11): la v1 reusaba los vertex/texcoord arrays legacy que arma
// el motor (glVertexPointer/glTexCoordPointer) confiando en que vitaGL los
// "bridgea" automaticamente a los atributos POSITION/TEXCOORD0 de un shader
// custom. Resultado en consola: pantalla negra desde el menu en adelante. La
// hipotesis mas probable: el mapeo semantics->ATTR de Cg sigue la convencion
// estandar ARB_vertex_program (POSITION=ATTR0, pero TEXCOORD0=ATTR8, NO
// ATTR1), que no necesariamente coincide con el indice al que vitaGL bridgea
// el GL_TEXTURE_COORD_ARRAY legacy -- si no coincide, el shader lee texcoord
// basura/cero y muestrea siempre el mismo texel (podria leerse como "negro"
// si ese texel puntual es oscuro), aun con la textura y la geometria bien.
//
// v2 evita todo ese mapeo implicito: en vez de reusar los arrays del motor,
// dibuja SU PROPIO quad de pantalla completa con glVertexAttribPointer +
// glBindAttribLocation (API estandar, sin ambiguedad) usando geometria/UVs
// que YA CONOCEMOS son constantes (el compositor es siempre pantalla
// completa, con el sub-rect 400x240 fijo dentro de la textura POT -- ver
// pp_src_w/h). Esto reemplaza por completo, en vez de envolver, el
// glDrawArrays original del motor para ese unico draw call.
static const char *VERT_SRC =
    "void main(\n"
    "    float2 position : POSITION,\n"
    "    float2 texcoord : TEXCOORD0,\n"
    "    out float4 oPosition : POSITION,\n"
    "    out float2 oTexcoord : TEXCOORD0)\n"
    "{\n"
    "    oPosition = float4(position.x, position.y, 0.0, 1.0);\n"
    "    oTexcoord = texcoord;\n"
    "}\n";

static const char *FRAG_SRC =
    "void main(\n"
    "    float2 texcoord : TEXCOORD0,\n"
    "    out float4 color : COLOR,\n"
    "    uniform sampler2D tex,\n"
    "    uniform float2 texel,\n"
    "    uniform float strength)\n"
    "{\n"
    "    float4 center = tex2D(tex, texcoord);\n"
    "    float4 n = tex2D(tex, texcoord + float2(0.0, -texel.y));\n"
    "    float4 s = tex2D(tex, texcoord + float2(0.0, texel.y));\n"
    "    float4 w = tex2D(tex, texcoord + float2(-texel.x, 0.0));\n"
    "    float4 e = tex2D(tex, texcoord + float2(texel.x, 0.0));\n"
    "    float4 blur = (n + s + w + e) * 0.25;\n"
    "    color = saturate(center + (center - blur) * strength);\n"
    "    color.a = center.a;\n"
    "}\n";

#define SHARPEN_STRENGTH 0.6f
#define ATTRIB_POSITION 0
#define ATTRIB_TEXCOORD 1

static GLuint pp_program = 0;
static GLint pp_loc_tex = -1;
static GLint pp_loc_texel = -1;
static GLint pp_loc_strength = -1;

static int pp_src_w = 512, pp_src_h = 512;
static int pp_next_draw_marked = 0;

// Quad de pantalla completa en NDC (-1..1): bottom-left, bottom-right,
// top-left, top-right, para GL_TRIANGLE_STRIP.
static const GLfloat QUAD_POS[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f
};
// UV correspondiente, recalculado cuando se conoce el tamaño real de la
// textura POT (postprocess_set_source_size). Orden confirmado contra el log
// de arranque (vertices/UVs del propio draw del motor): la fila 0 de la
// textura (V=0) corresponde a la parte de ARRIBA de la pantalla, V=240/alto
// a la de ABAJO.
static GLfloat quad_uv[8];

static void update_uv(void) {
    float u = 400.0f / (float)pp_src_w;
    float v = 240.0f / (float)pp_src_h;
    quad_uv[0] = 0.0f; quad_uv[1] = v;   // bottom-left
    quad_uv[2] = u;    quad_uv[3] = v;   // bottom-right
    quad_uv[4] = 0.0f; quad_uv[5] = 0.0f; // top-left
    quad_uv[6] = u;    quad_uv[7] = 0.0f; // top-right
}

static GLuint compile_shader(GLenum type, const char *src, const char *label) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(log), &len, log);
        game_log("[POSTPROCESS] %s compile FAILED: %s\n", label, log);
        glDeleteShader(shader);
        return 0;
    }
    game_log("[POSTPROCESS] %s compiled OK\n", label);
    return shader;
}

void postprocess_init(void) {
    update_uv();

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VERT_SRC, "vertex shader");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC, "fragment shader");
    if (!vs || !fs) {
        game_log("[POSTPROCESS] init abortado (shader invalido)\n");
        return;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Bindear nosotros los indices de atributo ANTES de linkear, en vez de
    // confiar en el mapeo automatico semantics->ATTR de Cg (ver nota v2
    // arriba) -- alimentamos estos dos atributos nosotros mismos via
    // glVertexAttribPointer, así que elegimos los indices.
    glBindAttribLocation(prog, ATTRIB_POSITION, "position");
    glBindAttribLocation(prog, ATTRIB_TEXCOORD, "texcoord");
    glLinkProgram(prog);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei len = 0;
        glGetProgramInfoLog(prog, sizeof(log), &len, log);
        game_log("[POSTPROCESS] link FAILED: %s\n", log);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(prog);
        return;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    pp_program = prog;
    pp_loc_tex = glGetUniformLocation(prog, "tex");
    pp_loc_texel = glGetUniformLocation(prog, "texel");
    pp_loc_strength = glGetUniformLocation(prog, "strength");
    game_log("[POSTPROCESS] listo: program=%u loc_tex=%d loc_texel=%d loc_strength=%d\n",
             pp_program, pp_loc_tex, pp_loc_texel, pp_loc_strength);
}

void postprocess_set_source_size(int tex_w, int tex_h) {
    pp_src_w = tex_w;
    pp_src_h = tex_h;
    update_uv();
}

void postprocess_mark_next_draw(void) {
    pp_next_draw_marked = 1;
}

int postprocess_try_draw(void) {
    if (!pp_next_draw_marked) return 0;
    pp_next_draw_marked = 0;
    if (!pp_program) return 0;

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(pp_program);
    if (pp_loc_tex >= 0) glUniform1i(pp_loc_tex, 0);
    if (pp_loc_texel >= 0) glUniform2f(pp_loc_texel, 1.0f / pp_src_w, 1.0f / pp_src_h);
    if (pp_loc_strength >= 0) glUniform1f(pp_loc_strength, SHARPEN_STRENGTH);

    glVertexAttribPointer(ATTRIB_POSITION, 2, GL_FLOAT, GL_FALSE, 0, QUAD_POS);
    glEnableVertexAttribArray(ATTRIB_POSITION);
    glVertexAttribPointer(ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 0, quad_uv);
    glEnableVertexAttribArray(ATTRIB_TEXCOORD);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(ATTRIB_POSITION);
    glDisableVertexAttribArray(ATTRIB_TEXCOORD);
    glUseProgram(0);

    static int logged = 0;
    if (!logged) {
        game_log("[POSTPROCESS] primer draw con shader (src=%dx%d uv_max=%.3f,%.3f)\n",
                 pp_src_w, pp_src_h, quad_uv[2], quad_uv[1]);
        logged = 1;
    }
    return 1;
}

#else // !POSTPROCESS_SHADER

void postprocess_init(void) {}
void postprocess_set_source_size(int tex_w, int tex_h) { (void)tex_w; (void)tex_h; }
void postprocess_mark_next_draw(void) {}
int postprocess_try_draw(void) { return 0; }

#endif

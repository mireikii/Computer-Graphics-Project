#define GLUT_DISABLE_ATEXIT_HACK
#include <GL/glut.h>

// ============================================================================
//  A DAY IN BLOOM  -  2D art in a real 3D camera (OpenGL / freeglut), one file
//
//  Every character is a flat, hand-drawn 2D sprite (a "billboard"), but the
//  scene is laid out in metres and rendered through a real perspective camera,
//  so sizes are realistic: flower << pedestrian << car << bus.
//
//  Computer-graphics topics demonstrated (see the info panel, key H):
//    - perspective projection + camera           gluPerspective / gluLookAt
//    - billboards and depth sorting              painter's algorithm
//    - planar projected shadows                  shadow matrix + stencil buffer
//    - lighting model: ambient colour + sun direction, changing with time of day
//    - atmospheric perspective                   distance haze
//    - level of detail (LOD) and view culling    flowers
//    - alpha blending                            glass, glows, shadows, water
//    - Bezier curves                             petals, leaves, stems
//    - hierarchical transforms                   limbs of people, wheels, bike
//    - Gouraud shading                           gradients from vertex colours
//    - double buffering + timer animation
//
//  Controls:
//    1-6        jump to sunrise / morning / midday / afternoon / sunset / night
//    SPACE      pause / resume            + / -   speed up / slow down the day
//    ARROWS     left/right: move camera sideways, up/down: camera height (angle)
//    0          reset camera              C       city life on/off (traffic, people)
//    S          shadows on/off            F       distance haze on/off
//    G          wireframe on/off          H       rendering info panel
//    R          replay the blooming intro ESC     quit
// ============================================================================
#include <GL/freeglut.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <functional>

static const float PI = 3.14159265358979323846f;

struct Color { float r, g, b; };
struct Point { float x, y; };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float f) { return a + (b - a) * f; }
static float sstep(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// deterministic pseudo random number in [0,1)
static float hash1(int n) {
    float h = std::sin(n * 12.9898f) * 43758.5453f;
    return h - std::floor(h);
}
static Color mix(Color a, Color b, float f) {
    return { lerpf(a.r, b.r, f), lerpf(a.g, b.g, f), lerpf(a.b, b.b, f) };
}
static Color scaleC(Color c, float s) { return { c.r * s, c.g * s, c.b * s }; }

static Point bezier(Point p0, Point p1, Point p2, Point p3, float t) {
    float u = 1.0f - t;
    return {
        u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
        u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y
    };
}

// ----------------------------------------------------------------------------
//  Global state
// ----------------------------------------------------------------------------
static float aspect      = 1000.0f / 600.0f;  // window aspect ratio
static int   viewW = 1000, viewH = 600;
static float hourNow     = 8.0f;              // time of day, 0..24
static float animTime    = 0.0f;              // seconds of animation (stops when paused)
static float dayLength   = 120.0f;            // real seconds for one full 24h cycle
static float speedMul    = 1.0f;
static bool  paused      = false;
static bool  showCommute = true;
static int   lastMs      = 0;

// camera: a real perspective camera, units are metres, +z points into the scene
static const float FOVY   = 50.0f;
static const float LOOK_Y = 1.0f, LOOK_Z = 33.0f;     // the point the camera looks at
static float camX = 0.0f, camH = 4.5f;                // camera sideways position and height
static float horizonY = 0.2f;                         // where the horizon lands on screen
static const float tanHalfFov = 0.46631f;             // tan(FOVY / 2)

// render-technique toggles (handy for the CG demo / report)
static bool showShadows = true, showFog = true, wireframe = false, showInfo = false;

// per-draw render state used by the colour helper
static bool  gShadowPass  = false;                    // drawing a shadow silhouette?
static float gShadowAlpha = 0.35f;
static float gFogK        = 0.0f;                     // distance haze amount for what is being drawn
static Color gFogC        = {0.7f, 0.8f, 0.9f};
static float gWheelRate   = 140.0f;                   // wheel spin, degrees per second
static float gAlphaMul    = 1.0f;                     // fades whatever is being drawn (people fade out at night)
static float gPeopleFade  = 1.0f;                     // 1 = people visible, 0 = gone (set once per frame)
static bool  easterEggs   = false;                    // secret surprise, key E

// every colour in the program goes through here: shadows are black, far things get hazy
static void gl4(float r, float g, float b, float a) {
    if (gShadowPass) { glColor4f(0.0f, 0.0f, 0.0f, gShadowAlpha * gAlphaMul); return; }
    if (gFogK > 0.0f) {
        r += (gFogC.r - r) * gFogK; g += (gFogC.g - g) * gFogK; b += (gFogC.b - b) * gFogK;
    }
    glColor4f(r, g, b, a * gAlphaMul);
}

// ----------------------------------------------------------------------------
//  Sky / lighting engine
//  Every key has: hour, sky top, sky horizon, ambient light tint, daylight 0..1
// ----------------------------------------------------------------------------
struct SkyKey { float hour; Color top, horizon, amb; float day; };

static const int NKEYS = 11;
static const SkyKey KEYS[NKEYS] = {
    {  0.0f, {0.02f,0.03f,0.10f}, {0.06f,0.07f,0.18f}, {0.22f,0.26f,0.48f}, 0.0f },
    {  5.0f, {0.03f,0.04f,0.13f}, {0.12f,0.10f,0.25f}, {0.24f,0.27f,0.48f}, 0.0f },
    {  6.3f, {0.20f,0.25f,0.52f}, {1.00f,0.60f,0.40f}, {1.00f,0.80f,0.70f}, 0.5f },
    {  8.0f, {0.30f,0.55f,0.88f}, {1.00f,0.84f,0.66f}, {1.00f,0.96f,0.90f}, 0.9f },
    { 12.0f, {0.16f,0.46f,0.90f}, {0.66f,0.84f,0.98f}, {1.00f,1.00f,1.00f}, 1.0f },
    { 15.5f, {0.25f,0.47f,0.80f}, {0.98f,0.82f,0.58f}, {1.00f,0.95f,0.86f}, 0.9f },
    { 18.0f, {0.28f,0.26f,0.55f}, {1.00f,0.50f,0.30f}, {1.00f,0.68f,0.52f}, 0.5f },
    { 19.2f, {0.12f,0.10f,0.30f}, {0.75f,0.30f,0.35f}, {0.55f,0.42f,0.58f}, 0.2f },
    { 20.5f, {0.04f,0.04f,0.15f}, {0.14f,0.10f,0.25f}, {0.28f,0.30f,0.52f}, 0.0f },
    { 22.0f, {0.02f,0.03f,0.10f}, {0.06f,0.07f,0.18f}, {0.22f,0.26f,0.48f}, 0.0f },
    { 24.0f, {0.02f,0.03f,0.10f}, {0.06f,0.07f,0.18f}, {0.22f,0.26f,0.48f}, 0.0f }
};

struct SkyState { Color top, horizon, amb; float day; };
static SkyState sky = { {0,0,0}, {0,0,0}, {1,1,1}, 1.0f };

static void updateSky() {
    int i = 0;
    while (i < NKEYS - 2 && hourNow >= KEYS[i + 1].hour) i++;
    const SkyKey& a = KEYS[i];
    const SkyKey& b = KEYS[i + 1];
    float f = clampf((hourNow - a.hour) / (b.hour - a.hour), 0.0f, 1.0f);
    sky.top     = mix(a.top, b.top, f);
    sky.horizon = mix(a.horizon, b.horizon, f);
    sky.amb     = mix(a.amb, b.amb, f);
    sky.day     = lerpf(a.day, b.day, f);
}
static float nightness() { return clampf(1.0f - sky.day * 2.2f, 0.0f, 1.0f); }

// multiply a colour by the current ambient light (so everything darkens at night
// and turns orange at sunset automatically)
static Color tint(Color c) { return { c.r * sky.amb.r, c.g * sky.amb.g, c.b * sky.amb.b }; }
static void setColor(Color c, float a = 1.0f) { gl4(c.r, c.g, c.b, a); }

// ----------------------------------------------------------------------------
//  Drawing primitives
// ----------------------------------------------------------------------------
static void rect(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
        glVertex2f(x0, y0); glVertex2f(x1, y0);
        glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

static void disc(float x, float y, float rx, float ry, int seg = 32) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= seg; i++) {
        float a = 2.0f * PI * i / seg;
        glVertex2f(x + rx * std::cos(a), y + ry * std::sin(a));
    }
    glEnd();
}

// soft additive glow (sun halo, lamps, fireflies, headlights)
static void glowDisc(float x, float y, float r, Color c, float alpha) {
    if (gShadowPass || alpha <= 0.003f) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLE_FAN);
    gl4(c.r, c.g, c.b, alpha);
    glVertex2f(x, y);
    gl4(c.r, c.g, c.b, 0.0f);
    for (int i = 0; i <= 40; i++) {
        float a = 2.0f * PI * i / 40;
        glVertex2f(x + r * std::cos(a), y + r * std::sin(a));
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void drawText(float x, float y, void* font, const char* s, Color c) {
    glColor3f(c.r, c.g, c.b);
    glRasterPos2f(x, y);
    for (const char* p = s; *p; ++p) glutBitmapCharacter(font, *p);
}

// ----------------------------------------------------------------------------
//  Sky, stars, sun, moon, clouds
// ----------------------------------------------------------------------------
struct Star { float x, y, ph, size; };
static std::vector<Star> stars;

static void drawSky() {
    const float W = 6.0f;
    float h = horizonY;
    Color mid = mix(sky.horizon, sky.top, 0.55f);
    glBegin(GL_QUADS);
        setColor(sky.horizon); glVertex2f(-W, h - 0.15f); glVertex2f(W, h - 0.15f);
        setColor(mid);         glVertex2f(W, h + 0.45f);  glVertex2f(-W, h + 0.45f);
        setColor(mid);         glVertex2f(-W, h + 0.45f); glVertex2f(W, h + 0.45f);
        setColor(sky.top);     glVertex2f(W, 1.2f);       glVertex2f(-W, 1.2f);
    glEnd();
}

static void drawStars() {
    float vis = clampf(1.0f - sky.day * 3.0f, 0.0f, 1.0f);
    if (vis < 0.01f) return;
    for (size_t i = 0; i < stars.size(); i++) {
        const Star& s = stars[i];
        float tw = 0.55f + 0.45f * std::sin(animTime * 2.0f + s.ph);
        glPointSize(s.size);
        glBegin(GL_POINTS);
        gl4(1.0f, 1.0f, 0.92f, vis * tw);
        glVertex2f(s.x, s.y);
        glEnd();
    }
}

static void drawSunMoon() {
    const float X = 1.35f;
    // ---- sun: rises on the left at ~6h, sets on the right at ~18h
    if (hourNow > 4.5f && hourNow < 19.5f) {
        float a = (hourNow - 6.0f) / 12.0f * PI;
        float sx = -std::cos(a) * X;
        float sy = horizonY - 0.20f + std::sin(a) * 0.95f;
        float h = clampf(std::sin(a), 0.0f, 1.0f);
        Color c = mix(Color{1.0f, 0.55f, 0.25f}, Color{1.0f, 0.97f, 0.82f}, clampf(h * 2.5f, 0.0f, 1.0f));
        glowDisc(sx, sy, 0.70f, c, 0.22f);
        glowDisc(sx, sy, 0.28f, c, 0.45f);
        setColor(c);
        disc(sx, sy, 0.065f, 0.065f, 48);
    }
    // ---- moon: same arc, 12 hours later
    float hm = hourNow < 12.0f ? hourNow + 24.0f : hourNow;
    if (hm > 17.0f && hm < 31.5f) {
        float a = (hm - 18.0f) / 12.0f * PI;
        float mx = -std::cos(a) * X;
        float my = horizonY - 0.20f + std::sin(a) * 0.85f;
        float vis = clampf(1.3f - sky.day * 1.6f, 0.0f, 1.0f);
        glowDisc(mx, my, 0.40f, Color{0.65f, 0.75f, 1.0f}, 0.22f * vis);
        setColor(Color{0.93f, 0.94f, 0.88f}, vis);
        disc(mx, my, 0.052f, 0.052f, 48);
        setColor(Color{0.78f, 0.80f, 0.78f}, vis);
        disc(mx - 0.016f, my + 0.012f, 0.012f, 0.012f, 16);
        disc(mx + 0.015f, my - 0.014f, 0.016f, 0.016f, 16);
        disc(mx + 0.020f, my + 0.022f, 0.008f, 0.008f, 12);
    }
}

struct Cloud { float x, y, s, speed; };
static const Cloud CLOUDS[6] = {
    {-2.5f, 0.62f, 0.22f, 0.020f}, {-1.0f, 0.80f, 0.30f, 0.015f},
    { 0.2f, 0.55f, 0.18f, 0.025f}, { 1.4f, 0.78f, 0.26f, 0.018f},
    { 2.4f, 0.60f, 0.20f, 0.022f}, {-0.3f, 0.45f, 0.15f, 0.028f}
};
static const float PUFFS[7][3] = {
    { 0.00f,  0.00f, 0.50f}, {-0.45f, -0.05f, 0.38f}, { 0.45f, -0.07f, 0.40f},
    {-0.20f,  0.18f, 0.38f}, { 0.22f,  0.15f, 0.35f}, { 0.70f, -0.10f, 0.28f},
    {-0.70f, -0.10f, 0.27f}
};

static void drawClouds() {
    for (int c = 0; c < 6; c++) {
        float cx = std::fmod(CLOUDS[c].x + animTime * CLOUDS[c].speed + 3.5f, 7.0f) - 3.5f;
        float cy = CLOUDS[c].y, s = CLOUDS[c].s;
        // shaded underside
        setColor(tint(Color{0.74f, 0.79f, 0.90f}), 0.9f);
        for (int p = 0; p < 7; p++)
            disc(cx + PUFFS[p][0] * s, cy + PUFFS[p][1] * s - 0.03f * s, PUFFS[p][2] * s, PUFFS[p][2] * s * 0.8f, 24);
        // bright top
        setColor(tint(Color{1.0f, 1.0f, 1.0f}), 0.92f);
        for (int p = 0; p < 7; p++)
            disc(cx + PUFFS[p][0] * s, cy + PUFFS[p][1] * s + 0.01f * s, PUFFS[p][2] * s * 0.95f, PUFFS[p][2] * s * 0.72f, 24);
    }
}

// ----------------------------------------------------------------------------
//  Hills, trees, meadow
// ----------------------------------------------------------------------------
struct Hill { float base, amp, f1, f2, ph; bool ridged; };
static const Hill farH  = { 0.20f, 0.11f, 1.10f, 2.70f, 0.5f, true  };
static const Hill midH  = { 0.02f, 0.07f, 1.60f, 3.90f, 2.0f, false };

static float hillY(const Hill& h, float x) {
    if (h.ridged) {
        float v = 0.65f * (1.0f - std::fabs(std::sin(h.f1 * x + h.ph)))
                + 0.35f * (1.0f - std::fabs(std::sin(h.f2 * x + h.ph * 1.7f)));
        return h.base + h.amp * (v - 0.5f) * 2.0f;
    }
    return h.base + h.amp * (0.65f * std::sin(h.f1 * x + h.ph) + 0.35f * std::sin(h.f2 * x + h.ph * 1.7f));
}

static void drawHill(const Hill& h, Color top, Color bottom, float bottomY) {
    const int N = 200;
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++) {
        float x = -5.0f + 10.0f * i / N;
        setColor(top);    glVertex2f(x, hillY(h, x));
        setColor(bottom); glVertex2f(x, bottomY);
    }
    glEnd();
}

static void drawTree(float x, float y, float s) {
    // trunk
    setColor(tint(Color{0.33f, 0.20f, 0.09f}));
    glBegin(GL_QUADS);
        glVertex2f(x - 0.012f * s, y);           glVertex2f(x + 0.012f * s, y);
        glVertex2f(x + 0.008f * s, y + 0.12f * s); glVertex2f(x - 0.008f * s, y + 0.12f * s);
    glEnd();
    // foliage: dark base -> light highlights
    float cy = y + 0.17f * s;
    setColor(tint(Color{0.08f, 0.30f, 0.12f}));
    disc(x, cy, 0.085f * s, 0.08f * s, 28);
    disc(x - 0.055f * s, cy - 0.03f * s, 0.06f * s, 0.055f * s, 24);
    disc(x + 0.055f * s, cy - 0.03f * s, 0.06f * s, 0.055f * s, 24);
    setColor(tint(Color{0.14f, 0.42f, 0.17f}));
    disc(x - 0.010f * s, cy + 0.015f * s, 0.065f * s, 0.060f * s, 24);
    disc(x + 0.040f * s, cy - 0.020f * s, 0.045f * s, 0.040f * s, 20);
    setColor(tint(Color{0.24f, 0.56f, 0.22f}));
    disc(x - 0.020f * s, cy + 0.035f * s, 0.040f * s, 0.035f * s, 20);
}

// ---------------------------------------------------------------------------
//  3D-style city: every building is a box seen in oblique projection
//  (front face + shaded side face + lit roof) in five architectural styles
// ---------------------------------------------------------------------------
static void quad4(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
    glBegin(GL_QUADS);
        glVertex2f(ax, ay); glVertex2f(bx, by); glVertex2f(cx, cy); glVertex2f(dx, dy);
    glEnd();
}

static void drawBox(float x, float y0, float w, float h, float ox, float oy, Color front, Color side, Color roof) {
    if (ox > 0.0f || oy > 0.0f) {
        setColor(side); quad4(x + w, y0, x + w + ox, y0 + oy, x + w + ox, y0 + h + oy, x + w, y0 + h);
        setColor(roof); quad4(x, y0 + h, x + w, y0 + h, x + w + ox, y0 + h + oy, x + ox, y0 + h + oy);
    }
    glBegin(GL_QUADS);
        setColor(scaleC(front, 0.88f));        glVertex2f(x, y0);     glVertex2f(x + w, y0);
        setColor(mix(front, roof, 0.25f));     glVertex2f(x + w, y0 + h); glVertex2f(x, y0 + h);
    glEnd();
}

static void drawWindowGrid(float x, float y0, float w, float h, float cw, float ch, float ww, float wh,
                           int seed, Color body, bool glass, float haze) {
    int cols = (int)((w - 0.010f) / cw), rows = (int)((h - 0.014f) / ch);
    if (cols < 1 || rows < 1) return;
    float startX = x + (w - cols * cw) * 0.5f + (cw - ww) * 0.5f;
    float n = nightness();
    glBegin(GL_QUADS);
    for (int r = 0; r < rows; r++) {
        float wy = y0 + 0.010f + r * ch;
        Color dayC = glass ? mix(mix(sky.horizon, sky.top, clampf((wy + 0.1f) / 0.8f, 0.0f, 1.0f)), body, 0.25f)
                           : scaleC(mix(body, Color{0.78f, 0.88f, 1.0f}, 0.35f), 0.85f);
        dayC = mix(dayC, sky.horizon, haze * 0.4f);
        for (int c = 0; c < cols; c++) {
            float wx = startX + c * cw;
            bool on = hash1(seed * 977 + r * 31 + c * 7) < 0.5f;
            Color col = on ? mix(dayC, Color{1.0f, 0.85f, 0.45f}, n) : mix(dayC, scaleC(body, 0.55f), n);
            setColor(col);
            glVertex2f(wx, wy);          glVertex2f(wx + ww, wy);
            glVertex2f(wx + ww, wy + wh); glVertex2f(wx, wy + wh);
        }
    }
    glEnd();
}

static void drawBuilding(float x, float y0, float w, float h, float d, int style, int seed, float haze) {
    if (x > aspect + 0.3f || x + w + d < -aspect - 0.3f) return;          // off screen
    static const Color BASE[5] = {
        {0.30f, 0.52f, 0.68f},   // glass tower
        {0.62f, 0.36f, 0.28f},   // brick
        {0.68f, 0.67f, 0.62f},   // concrete, stepped
        {0.42f, 0.46f, 0.60f},   // steel blue with spire
        {0.82f, 0.74f, 0.56f}    // sandstone with dome
    };
    static const float CW[5] = {0.017f, 0.024f, 0.022f, 0.020f, 0.026f};
    static const float CH[5] = {0.022f, 0.034f, 0.030f, 0.028f, 0.036f};
    static const float WW[5] = {0.013f, 0.013f, 0.012f, 0.013f, 0.012f};
    static const float WH[5] = {0.016f, 0.020f, 0.018f, 0.019f, 0.022f};

    float n = nightness();
    float sh = 0.9f + 0.2f * hash1(seed * 3 + 1);
    Color front = mix(tint(scaleC(BASE[style], sh)), sky.horizon, haze);
    Color side  = scaleC(front, 0.70f);
    Color roof  = mix(front, Color{1.0f, 1.0f, 1.0f}, 0.30f);
    float ox = 0.0f, oy = 0.0f;                         // flat 2D: no side/roof faces
    float yTop = y0 + h;
    float cxr = x + w * 0.5f + ox * 0.5f;

    if (style == 2) {                                   // stepped tower: wide base + narrower upper tier
        float h1 = h * 0.62f;
        drawBox(x, y0, w, h1, ox, oy, front, side, roof);
        drawWindowGrid(x, y0, w, h1, CW[style], CH[style], WW[style], WH[style], seed, front, false, haze);
        float h2 = h - h1;
        float x2 = x + w * 0.18f + ox * 0.35f, w2 = w * 0.64f, y2 = y0 + h1 + oy * 0.35f;
        drawBox(x2, y2, w2, h2, ox * 0.6f, oy * 0.6f, front, side, roof);
        drawWindowGrid(x2, y2, w2, h2, CW[style], CH[style], WW[style], WH[style], seed + 50, front, false, haze);
        return;
    }

    drawBox(x, y0, w, h, ox, oy, front, side, roof);
    drawWindowGrid(x, y0, w, h, CW[style], CH[style], WW[style], WH[style], seed, front, style == 0, haze);
    // flat "cel shading": darker band on the right, lighter strip on the left, cornice on top
    setColor(scaleC(front, 0.82f), 0.55f); rect(x + w * 0.84f, y0, x + w, yTop);
    setColor(mix(front, Color{1, 1, 1}, 0.35f), 0.35f); rect(x, y0, x + w * 0.06f, yTop);
    setColor(scaleC(front, 0.78f)); rect(x - 0.004f, yTop, x + w + 0.004f, yTop + 0.007f);

    if (style == 0) {                                   // diagonal glass reflection
        setColor(Color{1, 1, 1}, 0.10f * (0.4f + 0.6f * sky.day));
        quad4(x + w * 0.15f, y0, x + w * 0.40f, y0, x + w * 0.85f, yTop, x + w * 0.60f, yTop);
    } else if (style == 1) {                            // rooftop water tank
        float tx = x + w * 0.25f + ox * 0.5f, ty = yTop + oy * 0.4f;
        setColor(tint(Color{0.45f, 0.30f, 0.18f}));
        rect(tx, ty, tx + 0.022f, ty + 0.028f);
        glBegin(GL_TRIANGLES);
            glVertex2f(tx - 0.003f, ty + 0.028f); glVertex2f(tx + 0.025f, ty + 0.028f); glVertex2f(tx + 0.011f, ty + 0.042f);
        glEnd();
    } else if (style == 3) {                            // spire with blinking light
        float by = yTop + oy * 0.5f;
        setColor(scaleC(roof, 0.8f));
        rect(cxr - 0.003f, by, cxr + 0.003f, by + 0.10f);
        glBegin(GL_TRIANGLES);
            glVertex2f(cxr - 0.015f, by); glVertex2f(cxr + 0.015f, by); glVertex2f(cxr, by + 0.05f);
        glEnd();
        glowDisc(cxr, by + 0.10f, 0.03f, Color{1.0f, 0.2f, 0.2f}, (0.3f + 0.7f * n) * (0.5f + 0.5f * std::sin(animTime * 3.0f + seed)));
    } else if (style == 4) {                            // dome
        float by = yTop + oy * 0.5f;
        glBegin(GL_TRIANGLE_FAN);
        setColor(mix(roof, Color{1, 1, 1}, 0.2f)); glVertex2f(cxr, by);
        setColor(scaleC(roof, 0.8f));
        for (int i = 0; i <= 20; i++) {
            float a = PI * i / 20.0f;
            glVertex2f(cxr + w * 0.30f * std::cos(a), by + w * 0.22f * std::sin(a));
        }
        glEnd();
    }
}

// two layers of skyline: a hazy back row and a taller front row with a downtown cluster
static void drawCity() {
    for (int layer = 0; layer < 2; layer++) {
        float x = -3.4f + layer * 0.07f;
        int i = 0;
        while (x < 3.4f) {
            int seed = layer * 100 + i;
            float w = (layer == 0 ? 0.08f : 0.09f) + 0.07f * hash1(seed * 5 + 301);
            float downtown = std::exp(-(x * x) / 1.2f);
            float h = (layer == 0 ? 0.10f : 0.14f) + 0.20f * hash1(seed * 5 + 302)
                    + (layer == 0 ? 0.20f : 0.26f) * downtown * hash1(seed * 5 + 305);
            int style = (int)(hash1(seed * 5 + 306) * 5.0f) % 5;
            float d = 0.04f + 0.05f * hash1(seed * 5 + 307);
            drawBuilding(x, -0.06f, w, h, d, style, seed, layer == 0 ? 0.50f : 0.22f);
            x += w + d * 0.6f + 0.008f + 0.025f * hash1(seed * 5 + 304);
            i++;
        }
    }
}

static void drawBackdrop() {          // far mountains, city and middle hills (screen space, behind the 3D world)
    Color farC = mix(tint(Color{0.38f, 0.50f, 0.62f}), sky.horizon, 0.45f);
    drawHill(farH, farC, mix(farC, sky.horizon, 0.4f), -0.4f);
    drawCity();
    Color midTop = mix(tint(Color{0.38f, 0.62f, 0.30f}), sky.horizon, 0.15f);
    Color midBot = mix(tint(Color{0.28f, 0.50f, 0.24f}), sky.horizon, 0.15f);
    drawHill(midH, midTop, midBot, -0.4f);
}

// ----------------------------------------------------------------------------
//  Rendering helpers: haze, level of detail, culling, billboards, sun + shadow matrix
// ----------------------------------------------------------------------------
static float zBottom = 7.0f;                          // nearest ground distance still visible on screen

static float hazeAt(float z) { return showFog ? clampf(1.0f - std::exp(-0.0065f * (z - 8.0f)), 0.0f, 0.9f) : 0.0f; }
static float pxPerMetre(float z) { return (viewH * 0.5f) / (std::max(z, 0.5f) * tanHalfFov); }
static bool visibleX(float x, float z, float halfW) { return std::fabs(x - camX) <= z * tanHalfFov * aspect + halfW; }

// place a flat sprite standing on the ground at (x, z); k = metres per sprite unit
static void beginSprite(float x, float z, float k, bool flip = false, float y = 0.0f) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(flip ? -k : k, k, 1.0f);
    gFogK = hazeAt(z);
}
static void endSprite() { glPopMatrix(); gFogK = 0.0f; }

// sun (or moon) direction -> planar shadow projection matrix onto the ground plane y = 0
static float shadowM[16];
static float sunElevDeg = 60.0f, shadowStretch = 0.3f;

static void updateLight() {
    float a = (hourNow - 6.0f) / 12.0f * PI;
    float sunUp = std::sin(a);
    float hm = hourNow < 12.0f ? hourNow + 24.0f : hourNow;
    float am = (hm - 18.0f) / 12.0f * PI;
    float moonUp = std::sin(am);
    float elev, alpha, az;
    if (hourNow > 5.0f && hourNow < 19.0f && sunUp > 0.02f) {
        elev = 8.0f + 67.0f * sunUp;
        alpha = 0.38f * sstep(0.0f, 0.25f, sunUp);
        az = -std::cos(a);                              // morning: light from the left
    } else {
        float mu = std::max(moonUp, 0.0f);
        elev = 12.0f + 45.0f * mu;
        alpha = 0.14f * nightness() * sstep(0.0f, 0.25f, mu);
        az = -std::cos(am);
    }
    sunElevDeg = elev;
    gShadowAlpha = alpha;
    float e = elev * PI / 180.0f;
    float hx = az * 0.9f, hz = 0.6f;                    // the sun sits behind the scene -> shadows fall towards us
    float hl = std::sqrt(hx * hx + hz * hz); hx /= hl; hz /= hl;
    float te = std::tan(e);
    float kx = hx / te, kz = hz / te;                   // ground offset per metre of height
    float len = std::sqrt(kx * kx + kz * kz);
    if (len > 5.0f) { kx *= 5.0f / len; kz *= 5.0f / len; len = 5.0f; }
    shadowStretch = len;
    for (int i = 0; i < 16; i++) shadowM[i] = 0.0f;
    shadowM[0] = 1.0f; shadowM[4] = -kx; shadowM[6] = -kz; shadowM[10] = 1.0f; shadowM[15] = 1.0f;
}

// additive glow in world space (fireflies)
static void glow3(float x, float y, float z, float r, Color c, float alpha) {
    if (gShadowPass || alpha <= 0.003f) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLE_FAN);
    gl4(c.r, c.g, c.b, alpha);
    glVertex3f(x, y, z);
    gl4(c.r, c.g, c.b, 0.0f);
    for (int i = 0; i <= 24; i++) {
        float a = 2.0f * PI * i / 24;
        glVertex3f(x + r * std::cos(a), y + r * std::sin(a), z);
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ----------------------------------------------------------------------------
//  Flowers (cosmos)
// ----------------------------------------------------------------------------
struct Palette { Color base, tip; };
static const Palette PAL[5] = {
    { {0.55f, 0.05f, 0.32f}, {0.93f, 0.34f, 0.66f} },   // magenta
    { {0.78f, 0.28f, 0.50f}, {1.00f, 0.72f, 0.85f} },   // light pink
    { {0.82f, 0.85f, 0.55f}, {0.99f, 0.98f, 0.98f} },   // white
    { {0.85f, 0.30f, 0.02f}, {1.00f, 0.64f, 0.14f} },   // orange
    { {0.50f, 0.02f, 0.12f}, {0.86f, 0.14f, 0.32f} }    // deep red
};

static std::vector<Point> petalPts, leafPts, florets;

static void buildShapes() {
    // petal: wide, rounded sides, three little teeth at the tip
    Point base = {0.0f, 0.02f};
    for (int i = 0; i <= 8; i++)
        petalPts.push_back(bezier(base, {-0.34f, 0.22f}, {-0.30f, 0.62f}, {-0.17f, 0.80f}, i / 8.0f));
    petalPts.push_back({-0.12f, 0.90f}); petalPts.push_back({-0.06f, 0.81f});
    petalPts.push_back({ 0.00f, 0.90f});
    petalPts.push_back({ 0.06f, 0.81f}); petalPts.push_back({ 0.12f, 0.90f});
    for (int i = 0; i <= 8; i++)
        petalPts.push_back(bezier({0.17f, 0.80f}, {0.30f, 0.62f}, {0.34f, 0.22f}, base, i / 8.0f));

    // slim leaf
    Point lb = {0.0f, 0.0f}, lt = {0.0f, 0.45f};
    for (int i = 0; i <= 10; i++) leafPts.push_back(bezier(lb, {-0.12f, 0.15f}, {-0.08f, 0.32f}, lt, i / 10.0f));
    for (int i = 0; i <= 10; i++) leafPts.push_back(bezier(lt, {0.08f, 0.30f}, {0.12f, 0.14f}, lb, i / 10.0f));

    // flower centre: sunflower-style golden angle spiral
    const float GOLDEN = 137.507764f * (PI / 180.0f);
    for (int i = 0; i < 55; i++) {
        float r = std::sqrt((float)i / 55.0f) * 0.12f;
        florets.push_back({r * std::cos(i * GOLDEN), r * std::sin(i * GOLDEN)});
    }
}

static void drawPetal(const Palette& p, float shade) {
    Color b = tint(scaleC(p.base, shade));
    Color t = tint(scaleC(p.tip, shade));
    glBegin(GL_TRIANGLE_FAN);
    setColor(scaleC(b, 0.8f));
    glVertex2f(0.0f, 0.0f);
    for (size_t i = 0; i < petalPts.size(); i++) {
        float k = clampf(petalPts[i].y / 0.9f, 0.0f, 1.0f);
        setColor(mix(b, t, k));
        glVertex2f(petalPts[i].x, petalPts[i].y);
    }
    glEnd();
    // faint veins
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    gl4(b.r * 0.6f, b.g * 0.6f, b.b * 0.6f, 0.35f);
    for (int k = -1; k <= 1; k++) { glVertex2f(0.0f, 0.08f); glVertex2f(k * 0.07f, 0.72f); }
    glEnd();
}

static void drawLeaf() {
    glBegin(GL_TRIANGLE_FAN);
    setColor(tint(Color{0.08f, 0.30f, 0.07f}));
    glVertex2f(0.0f, 0.0f);
    for (size_t i = 0; i < leafPts.size(); i++) {
        float k = clampf(leafPts[i].y / 0.45f, 0.0f, 1.0f);
        setColor(tint(mix(Color{0.08f, 0.32f, 0.07f}, Color{0.28f, 0.58f, 0.18f}, k)));
        glVertex2f(leafPts[i].x, leafPts[i].y);
    }
    glEnd();
}

// ---- the meadow in metres: every flower is a Plant standing on the ground ----
struct Plant { float x, z, h, headD, phase, delay; int type, variant, seed; };   // type 0 flower, 1 grass tuft
static std::vector<Plant> plants;                     // sorted far -> near (painter's algorithm)
struct Dot { float x, z; int var; };
static std::vector<Dot> farDots;                      // distant flowers, drawn as coloured dots

static const float POND_X = 4.8f, POND_Z = 9.8f, POND_RX = 2.6f, POND_RZ = 1.25f;
static const float PIC_X = -4.6f, PIC_Z = 9.6f;
static const float KID_X = 0.0f,  KID_Z = 9.3f;

static bool inEllipse(float x, float z, float cx, float cz, float a, float b) {
    float u = (x - cx) / a, v = (z - cz) / b;
    return u * u + v * v < 1.0f;
}
static bool inClearing(float x, float z) {            // keep flowers out of the pond, picnic and play areas
    return inEllipse(x, z, PIC_X, PIC_Z, 2.3f, 1.5f) || inEllipse(x, z, KID_X, KID_Z, 2.0f, 1.2f)
        || inEllipse(x, z, POND_X, POND_Z, POND_RX + 0.7f, POND_RZ + 0.5f);
}
static bool plantFarther(const Plant& a, const Plant& b) { return a.z > b.z; }

static void initPlants() {
    int id = 0;
    for (float z = 4.5f; z < 14.6f; z += 0.36f) {
        for (float x = -14.0f; x < 14.0f; x += 0.42f) {
            id++;
            float px = x + (hash1(id * 3 + 1) - 0.5f) * 0.38f;
            float pz = z + (hash1(id * 3 + 2) - 0.5f) * 0.30f;
            if (pz > 14.55f || inClearing(px, pz)) continue;
            Plant p;
            p.x = px; p.z = pz;
            p.type = (hash1(id * 7 + 11) < 0.18f) ? 1 : 0;
            p.h = p.type ? 0.18f + 0.18f * hash1(id * 7 + 12) : 0.40f + 0.35f * hash1(id * 7 + 12);
            p.headD = 0.18f + 0.10f * hash1(id * 7 + 13);     // (a little larger than life so they stay readable)
            p.phase = hash1(id * 7 + 5) * 6.28f;
            p.delay = hash1(id * 7 + 6) * 6.0f;
            float cl = hash1((int)std::floor(px / 2.2f) * 31 + (int)std::floor(pz / 1.8f) * 17 + 5);
            float u = hash1(id * 7 + 7);
            p.variant = (u < 0.25f) ? (int)(hash1(id * 7 + 8) * 5.0f) % 5 : (int)(cl * 5.0f) % 5;
            p.seed = id;
            plants.push_back(p);
        }
    }
    std::sort(plants.begin(), plants.end(), plantFarther);

    for (float z = 22.8f; z < 34.0f; z += 0.75f)
        for (float x = -34.0f; x < 34.0f; x += 0.8f) {
            id++;
            if (hash1(id * 5 + 31) < 0.35f) continue;
            Dot d;
            d.x = x + (hash1(id * 5 + 32) - 0.5f) * 0.7f;
            d.z = z + (hash1(id * 5 + 33) - 0.5f) * 0.6f;
            d.var = (int)(hash1(id * 5 + 34) * 5.0f) % 5;
            farDots.push_back(d);
        }
}

// cosmos head, radius ~0.9 sprite units. Level of detail depends on its size on screen (px)
static void drawFlowerHead(int variant, int seed, float px) {
    float open = 0.78f + 0.22f * sstep(0.2f, 0.7f, sky.day);   // petals close up a little at night
    const Palette& pl = PAL[variant];
    if (px < 3.5f) {                                     // LOD 0: a coloured speck
        setColor(tint(pl.tip));
        disc(0.0f, 0.0f, 0.9f * open, 0.9f * open, 6);
        return;
    }
    if (px < 13.0f) {                                    // LOD 1: star-shaped fan + centre
        glBegin(GL_TRIANGLE_FAN);
        setColor(tint(mix(pl.base, pl.tip, 0.35f)));
        glVertex2f(0.0f, 0.0f);
        for (int i = 0; i <= 16; i++) {
            float a = 2.0f * PI * i / 16;
            float r = ((i % 2) == 0 ? 0.95f : 0.60f) * open;
            setColor(tint((i % 2) == 0 ? pl.tip : mix(pl.base, pl.tip, 0.6f)));
            glVertex2f(r * std::cos(a), r * std::sin(a));
        }
        glEnd();
        setColor(tint(Color{0.95f, 0.72f, 0.12f}));
        disc(0.0f, 0.0f, 0.20f, 0.20f, 8);
        return;
    }
    int rings = (px < 24.0f) ? 1 : 2;                    // LOD 2: full bezier petals
    for (int r = rings - 1; r >= 0; --r) {
        for (int i = 0; i < 8; i++) {
            int idx = seed * 16 + r * 8 + i;
            float j1 = hash1(idx), j2 = hash1(idx + 101);
            glPushMatrix();
            glRotatef(i * 45.0f + (r == 1 ? 22.5f : 0.0f) + (j1 - 0.5f) * 10.0f, 0, 0, 1);
            glScalef((0.95f + 0.15f * j2) * (0.85f + 0.15f * open),
                     (0.85f + 0.20f * j2) * open * (r == 1 ? 0.92f : 1.0f), 1.0f);
            drawPetal(pl, r == 1 ? 0.85f : 1.0f);
            glPopMatrix();
        }
    }
    glBegin(GL_TRIANGLE_FAN);
    setColor(tint(Color{0.30f, 0.14f, 0.02f}));
    glVertex2f(0.0f, 0.0f);
    setColor(tint(Color{0.92f, 0.66f, 0.08f}));
    for (int i = 0; i <= 24; i++) {
        float a = 2.0f * PI * i / 24;
        glVertex2f(0.13f * std::cos(a), 0.13f * std::sin(a));
    }
    glEnd();
    if (px >= 30.0f) {
        glPointSize(2.0f);
        glBegin(GL_POINTS);
        for (size_t i = 0; i < florets.size(); i++) {
            float r = std::sqrt(florets[i].x * florets[i].x + florets[i].y * florets[i].y) / 0.12f;
            setColor(tint(mix(Color{0.25f, 0.10f, 0.01f}, Color{1.0f, 0.82f, 0.20f}, r)));
            glVertex2f(florets[i].x, florets[i].y);
        }
        glEnd();
    }
}

static void drawFlowerPlant(const Plant& p) {
    float tt = animTime - p.delay;
    float stemG = sstep(0.0f, 2.0f, tt);                 // stem grows first
    if (stemG <= 0.0f) return;
    float headG = sstep(0.0f, 3.0f, tt - 1.5f);          // then the head blooms
    float ppm = pxPerMetre(p.z);
    float H = p.h * stemG;
    float wind = std::sin(animTime * 0.9f + p.x * 0.8f) * 0.6f + std::sin(animTime * 2.1f + p.phase) * 0.4f;
    float off = wind * 0.07f * H;
    Point p0 = {0.0f, 0.0f}, p1 = {0.0f, H * 0.45f}, p2 = {off * 0.5f, H * 0.8f}, p3 = {off, H};

    glPushMatrix();
    glTranslatef(p.x, 0.0f, p.z);
    gFogK = hazeAt(p.z);

    int steps = ppm < 45.0f ? 3 : 8;
    float w = std::max(0.012f, 1.5f / ppm);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= steps; i++) {
        float t = i / (float)steps;
        Point q = bezier(p0, p1, p2, p3, t);
        setColor(tint(mix(Color{0.12f, 0.40f, 0.10f}, Color{0.30f, 0.60f, 0.20f}, t)));
        glVertex2f(q.x - w * 0.5f, q.y);
        glVertex2f(q.x + w * 0.5f, q.y);
    }
    glEnd();

    if (ppm * H > 16.0f) {                               // leaves only when they would be visible
        for (int k = 0; k < 2; k++) {
            float side = (k == 0) ? 1.0f : -1.0f;
            Point lp = bezier(p0, p1, p2, p3, 0.28f + 0.25f * k);
            float ls = (0.22f + 0.30f * p.h) * stemG;
            glPushMatrix();
            glTranslatef(lp.x, lp.y, 0.0f);
            glRotatef(-side * 55.0f + wind * 5.0f, 0, 0, 1);
            glScalef(ls, ls, 1.0f);
            drawLeaf();
            glPopMatrix();
        }
    }
    float headPx = p.headD * ppm * headG;
    if (headPx > 0.8f) {
        float tilt = (H > 0.001f) ? -off / H * 40.0f : 0.0f;
        float sc = p.headD * 0.5f / 0.9f * headG;
        glPushMatrix();
        glTranslatef(p3.x, p3.y, 0.0f);
        glRotatef(tilt + (p.phase - 3.14f) * 6.0f, 0, 0, 1);
        glScalef(sc, sc, 1.0f);
        drawFlowerHead(p.variant, p.seed, headPx);
        glPopMatrix();
    }
    glPopMatrix();
    gFogK = 0.0f;
}

static void drawTuft(const Plant& p) {
    float sw = std::sin(animTime * 1.7f + p.x * 2.5f + p.phase) * 0.35f;
    glPushMatrix();
    glTranslatef(p.x, 0.0f, p.z);
    gFogK = hazeAt(p.z);
    glBegin(GL_TRIANGLES);
    for (int b = 0; b < 5; b++) {
        float bx = (b - 2) * 0.035f;
        float h = p.h * (0.7f + 0.3f * hash1(p.seed * 5 + b));
        float lean = (b - 2) * 0.04f + sw * h * 0.5f;
        setColor(tint(Color{0.10f, 0.34f, 0.08f}));
        glVertex2f(bx - 0.012f, 0.0f); glVertex2f(bx + 0.012f, 0.0f);
        setColor(tint(Color{0.30f, 0.62f, 0.20f}));
        glVertex2f(bx + lean, h);
    }
    glEnd();
    glPopMatrix();
    gFogK = 0.0f;
}

static void drawFarDots() {                              // distant meadow: one coloured point per flower
    gFogK = 0.0f;
    Color farCol = mix(tint(Color{0.38f, 0.62f, 0.30f}), sky.horizon, 0.15f);
    glPointSize(4.0f);
    glBegin(GL_POINTS);
    for (size_t i = 0; i < farDots.size(); i++) {
        const Dot& d = farDots[i];
        if (!visibleX(d.x, d.z, 0.5f)) continue;
        setColor(mix(tint(PAL[d.var].tip), farCol, hazeAt(d.z)));
        glVertex3f(d.x, 0.45f, d.z);
    }
    glEnd();
}

struct Butterfly { float ph, speed, ax, cy, ay, size; Color c1, c2; };
static const Butterfly BFLY[3] = {
    { 0.0f, 0.10f, 1.30f, -0.12f, 0.12f, 0.26f, {0.98f, 0.60f, 0.12f}, {0.20f, 0.10f, 0.05f} },
    { 2.0f, 0.08f, 1.20f, -0.30f, 0.10f, 0.22f, {0.55f, 0.75f, 1.00f}, {0.10f, 0.15f, 0.35f} },
    { 4.1f, 0.12f, 1.40f, -0.02f, 0.10f, 0.24f, {1.00f, 0.90f, 0.30f}, {0.30f, 0.20f, 0.02f} }
};
static const Point UPPER[5] = {{0.01f,0.01f},{0.10f,0.13f},{0.22f,0.17f},{0.25f,0.09f},{0.17f,0.01f}};
static const Point LOWER[5] = {{0.01f,0.00f},{0.16f,-0.01f},{0.15f,-0.11f},{0.07f,-0.14f},{0.01f,-0.06f}};

static void drawWingShape(const Point* p, int n, Color fill, Color edge, float a) {
    glBegin(GL_TRIANGLE_FAN);
    setColor(fill, a);
    for (int i = 0; i < n; i++) glVertex2f(p[i].x, p[i].y);
    glEnd();
    glLineWidth(1.2f);
    glBegin(GL_LINE_LOOP);
    setColor(edge, a);
    for (int i = 0; i < n; i++) glVertex2f(p[i].x, p[i].y);
    glEnd();
}

// one butterfly in local sprite units (wing span ~0.5)
static void drawButterflyLocal(const Butterfly& b, float flap, float alpha) {
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix();
        glScalef(side * flap, 1.0f, 1.0f);
        drawWingShape(UPPER, 5, tint(b.c1), tint(b.c2), alpha);
        drawWingShape(LOWER, 5, tint(mix(b.c1, b.c2, 0.25f)), tint(b.c2), alpha);
        setColor(tint(b.c2), alpha);
        disc(0.16f, 0.09f, 0.03f, 0.03f, 12);
        glPopMatrix();
    }
    setColor(tint(Color{0.12f, 0.08f, 0.05f}), alpha);
    disc(0.0f, 0.0f, 0.018f, 0.085f, 14);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
        glVertex2f(0.0f, 0.08f); glVertex2f(-0.05f, 0.16f);
        glVertex2f(0.0f, 0.08f); glVertex2f( 0.05f, 0.16f);
    glEnd();
}

struct Fly { float x, y, z, ph; };
static std::vector<Fly> flies;

static void drawFireflies() {
    float n = nightness();
    if (n < 0.02f) return;
    for (size_t i = 0; i < flies.size(); i++) {
        const Fly& f = flies[i];
        float x = f.x + std::sin(animTime * 0.5f + f.ph) * 0.4f;
        float y = f.y + std::cos(animTime * 0.7f + f.ph * 1.3f) * 0.25f;
        float z = f.z + std::sin(animTime * 0.4f + f.ph) * 0.3f;
        if (!visibleX(x, z, 0.5f)) continue;
        float pulse = 0.5f + 0.5f * std::sin(animTime * 2.5f + f.ph * 3.0f);
        float a = n * pulse;
        glow3(x, y, z, 0.20f, Color{0.9f, 1.0f, 0.35f}, 0.85f * a);
        glow3(x, y, z, 0.03f, Color{1.0f, 1.0f, 0.7f}, a);
    }
}

static void drawLamp(float x, float yBase, float night) {      // local units: pole ~0.32 high
    setColor(tint(Color{0.25f, 0.26f, 0.28f}));
    rect(x - 0.006f, yBase, x + 0.006f, yBase + 0.32f);
    rect(x, yBase + 0.31f, x + 0.06f, yBase + 0.322f);
    setColor(mix(tint(Color{0.55f, 0.55f, 0.55f}), Color{1.0f, 0.88f, 0.55f}, night));
    rect(x + 0.045f, yBase + 0.30f, x + 0.085f, yBase + 0.312f);
    glowDisc(x + 0.065f, yBase + 0.30f, 0.24f, Color{1.0f, 0.85f, 0.5f}, 0.45f * night);
}

static void headlightCone(float x, float y, float len, float spread, float a) {
    if (gShadowPass || a <= 0.003f) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLES);
        gl4(1.0f, 0.95f, 0.7f, a); glVertex2f(x, y);
        gl4(1.0f, 0.95f, 0.7f, 0.0f);
        glVertex2f(x + len, y + spread);
        glVertex2f(x + len, y - spread * 0.7f);
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ---- helpers for the glossy "cartoon-3D" bus -------------------------------
static void rrPoints(Point* pts, float x0, float y0, float x1, float y1, float r) {
    float cx[4] = {x1 - r, x1 - r, x0 + r, x0 + r};
    float cy[4] = {y0 + r, y1 - r, y1 - r, y0 + r};
    int n = 0;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i <= 6; i++) {
            float a = (-90.0f + c * 90.0f + 90.0f * i / 6.0f) * PI / 180.0f;
            pts[n++] = { cx[c] + r * std::cos(a), cy[c] + r * std::sin(a) };
        }
}

// rounded rectangle filled with a vertical gradient
static void roundRect(float x0, float y0, float x1, float y1, float r, Color bot, Color top) {
    Point pts[28];
    rrPoints(pts, x0, y0, x1, y1, r);
    glBegin(GL_TRIANGLE_FAN);
    setColor(mix(bot, top, 0.5f));
    glVertex2f((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    for (int i = 0; i <= 28; i++) {
        const Point& q = pts[i % 28];
        setColor(mix(bot, top, clampf((q.y - y0) / (y1 - y0), 0.0f, 1.0f)));
        glVertex2f(q.x, q.y);
    }
    glEnd();
}

static void roundRectEdge(float x0, float y0, float x1, float y1, float r, Color c, float width) {
    Point pts[28];
    rrPoints(pts, x0, y0, x1, y1, r);
    glLineWidth(width);
    glBegin(GL_LINE_LOOP);
    setColor(c);
    for (int i = 0; i < 28; i++) glVertex2f(pts[i].x, pts[i].y);
    glEnd();
}

// ---- people: simple, friendly cartoon figures --------------------------------
static const Color SKINS[5]  = {{0.98f,0.80f,0.66f},{0.90f,0.68f,0.50f},{0.72f,0.50f,0.34f},{0.55f,0.36f,0.24f},{0.99f,0.86f,0.74f}};
static const Color HAIRS[5]  = {{0.12f,0.08f,0.05f},{0.40f,0.22f,0.08f},{0.85f,0.65f,0.25f},{0.60f,0.12f,0.08f},{0.20f,0.20f,0.22f}};
static const Color SHIRTS[7] = {{0.90f,0.25f,0.30f},{0.20f,0.55f,0.90f},{0.95f,0.75f,0.15f},{0.30f,0.75f,0.45f},
                                {0.70f,0.35f,0.85f},{0.95f,0.50f,0.15f},{0.95f,0.95f,0.95f}};
static const Color PANTS[4]  = {{0.20f,0.25f,0.45f},{0.25f,0.25f,0.28f},{0.55f,0.40f,0.25f},{0.15f,0.30f,0.35f}};

// head + shoulders, for people seen through windows. cy = bottom of the window
static void drawPassenger(float cx, float cy, int id, float s = 1.0f) {
    Color skin = SKINS[id % 5], hair = HAIRS[(id * 3 + 1) % 5], shirt = SHIRTS[(id * 5 + 2) % 7];
    setColor(tint(shirt)); disc(cx, cy + 0.004f * s, 0.017f * s, 0.014f * s, 12);
    setColor(tint(skin));  disc(cx, cy + 0.024f * s, 0.0085f * s, 0.0095f * s, 12);
    setColor(tint(hair));  disc(cx, cy + 0.030f * s, 0.0090f * s, 0.0055f * s, 12);
}

// a person about 1.0 tall, feet at y=0, facing +x. phase/amp = walking swing, armLift = raise both arms (deg)
static void drawPerson(int id, float phase, float amp, float armLift) {
    Color skin = SKINS[id % 5], hair = HAIRS[(id * 3 + 1) % 5];
    Color shirt = SHIRTS[(id * 5 + 2) % 7], pants = PANTS[(id * 7 + 3) % 4];
    float sw = std::sin(phase) * amp;
    if (id % 3 == 0) { setColor(tint(hair)); rect(-0.095f, 0.74f, -0.02f, 0.92f); }   // long hair behind
    for (int k = -1; k <= 1; k += 2) {                                                   // legs
        glPushMatrix();
        glTranslatef(0.0f, 0.46f, 0.0f);
        glRotatef(k * sw, 0, 0, 1);
        setColor(tint(scaleC(pants, k < 0 ? 0.8f : 1.0f)));
        rect(-0.04f, -0.46f, 0.04f, 0.0f);
        setColor(tint(Color{0.12f, 0.10f, 0.10f}));
        rect(-0.045f, -0.46f, 0.075f, -0.415f);
        glPopMatrix();
    }
    if (id % 4 == 1) {                                                                   // dress
        setColor(tint(scaleC(shirt, 0.9f)));
        quad4(-0.10f, 0.48f, 0.10f, 0.48f, 0.15f, 0.25f, -0.15f, 0.25f);
    }
    roundRect(-0.115f, 0.44f, 0.115f, 0.77f, 0.05f, tint(scaleC(shirt, 0.8f)), tint(shirt));
    for (int k = -1; k <= 1; k += 2) {                                                   // arms
        glPushMatrix();
        glTranslatef(0.0f, 0.72f, 0.0f);
        glRotatef(-k * sw * 0.9f + k * armLift, 0, 0, 1);
        setColor(tint(shirt)); rect(-0.03f, -0.26f, 0.03f, 0.0f);
        setColor(tint(skin));  rect(-0.028f, -0.32f, 0.028f, -0.26f);
        glPopMatrix();
    }
    setColor(tint(skin)); disc(0.0f, 0.87f, 0.085f, 0.09f, 14);                          // head
    setColor(tint(hair)); disc(0.0f, 0.92f, 0.09f, 0.05f, 14);
    setColor(tint(Color{0.1f, 0.1f, 0.1f})); disc(0.035f, 0.875f, 0.011f, 0.011f, 8);   // eye
}

// sitting on the ground, legs stretched forward (+x)
static void drawSitting(int id, float armDeg) {
    Color skin = SKINS[id % 5], hair = HAIRS[(id * 3 + 1) % 5];
    Color shirt = SHIRTS[(id * 5 + 2) % 7], pants = PANTS[(id * 7 + 3) % 4];
    setColor(tint(pants)); rect(-0.02f, 0.04f, 0.34f, 0.14f);
    setColor(tint(Color{0.12f, 0.10f, 0.10f})); rect(0.31f, 0.03f, 0.40f, 0.15f);
    roundRect(-0.11f, 0.10f, 0.11f, 0.46f, 0.05f, tint(scaleC(shirt, 0.8f)), tint(shirt));
    glPushMatrix();
    glTranslatef(0.0f, 0.42f, 0.0f);
    glRotatef(armDeg, 0, 0, 1);
    setColor(tint(shirt)); rect(-0.03f, -0.26f, 0.03f, 0.0f);
    setColor(tint(skin));  rect(-0.028f, -0.32f, 0.028f, -0.26f);
    glPopMatrix();
    setColor(tint(skin)); disc(0.0f, 0.58f, 0.085f, 0.09f, 14);
    setColor(tint(hair)); disc(0.0f, 0.63f, 0.09f, 0.05f, 14);
    setColor(tint(Color{0.1f, 0.1f, 0.1f})); disc(0.035f, 0.585f, 0.011f, 0.011f, 8);
}

// bicycle with rider (about 1.0 tall, facing +x)
static void drawCyclist(int id, float wheelDeg, float crank) {
    Color skin = SKINS[id % 5];
    Color shirt = SHIRTS[(id * 5 + 2) % 7], pants = PANTS[(id * 7 + 3) % 4];
    Color frame = tint(Color{0.85f, 0.15f, 0.20f});
    for (int k = -1; k <= 1; k += 2) {                                         // wheels
        glPushMatrix();
        glTranslatef(k * 0.40f, 0.22f, 0.0f);
        glLineWidth(2.5f);
        setColor(tint(Color{0.10f, 0.10f, 0.12f}));
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 24; i++) { float a = 2.0f * PI * i / 24; glVertex2f(0.22f * std::cos(a), 0.22f * std::sin(a)); }
        glEnd();
        glRotatef(wheelDeg, 0, 0, 1);
        glLineWidth(1.0f);
        setColor(tint(Color{0.65f, 0.65f, 0.70f}));
        glBegin(GL_LINES);
        for (int i = 0; i < 6; i++) { float a = PI * i / 6.0f; glVertex2f(0.21f * std::cos(a), 0.21f * std::sin(a)); glVertex2f(-0.21f * std::cos(a), -0.21f * std::sin(a)); }
        glEnd();
        glPopMatrix();
    }
    glLineWidth(3.0f);                                                         // frame
    setColor(frame);
    glBegin(GL_LINES);
        glVertex2f(-0.40f, 0.22f); glVertex2f(0.00f, 0.20f);
        glVertex2f(-0.40f, 0.22f); glVertex2f(-0.12f, 0.50f);
        glVertex2f( 0.00f, 0.20f); glVertex2f(-0.12f, 0.50f);
        glVertex2f(-0.12f, 0.50f); glVertex2f( 0.30f, 0.50f);
        glVertex2f( 0.00f, 0.20f); glVertex2f( 0.30f, 0.50f);
        glVertex2f( 0.30f, 0.50f); glVertex2f( 0.40f, 0.22f);
        glVertex2f( 0.30f, 0.50f); glVertex2f( 0.26f, 0.58f);
        glVertex2f(-0.17f, 0.52f); glVertex2f(-0.07f, 0.52f);
    glEnd();
    float px = 0.09f * std::cos(crank), py = 0.20f + 0.09f * std::sin(crank);
    float qx = -px, qy = 0.40f - py;
    glLineWidth(6.0f);                                                         // far leg, then body, then near leg
    setColor(tint(scaleC(pants, 0.8f)));
    glBegin(GL_LINES); glVertex2f(-0.12f, 0.54f); glVertex2f(qx, qy); glEnd();
    glLineWidth(9.0f);
    setColor(tint(shirt));
    glBegin(GL_LINES); glVertex2f(-0.12f, 0.56f); glVertex2f(0.10f, 0.80f); glEnd();
    glLineWidth(6.0f);
    setColor(tint(pants));
    glBegin(GL_LINES); glVertex2f(-0.12f, 0.54f); glVertex2f(px, py); glEnd();
    glLineWidth(3.0f);
    setColor(tint(skin));
    glBegin(GL_LINES); glVertex2f(0.10f, 0.78f); glVertex2f(0.26f, 0.58f); glEnd();
    setColor(tint(Color{0.12f, 0.10f, 0.10f})); disc(px + 0.02f, py, 0.03f, 0.014f, 8);
    setColor(tint(skin)); disc(0.15f, 0.91f, 0.08f, 0.085f, 14);
    setColor(tint(Color{0.85f, 0.15f, 0.20f})); disc(0.15f, 0.96f, 0.085f, 0.05f, 14);   // helmet
    setColor(tint(Color{0.1f, 0.1f, 0.1f})); disc(0.185f, 0.915f, 0.01f, 0.01f, 8);
}

// dog, ~0.5 long, facing +x
static void drawDog(float phase) {
    Color fur = tint(Color{0.72f, 0.48f, 0.24f}), dark = tint(Color{0.45f, 0.28f, 0.12f});
    for (int k = 0; k < 4; k++) {
        float sw = std::sin(phase + (k % 2) * PI + (k / 2) * 0.6f) * 28.0f;
        glPushMatrix();
        glTranslatef(k < 2 ? -0.14f : 0.14f, 0.14f, 0.0f);
        glRotatef(sw, 0, 0, 1);
        setColor(dark); rect(-0.022f, -0.14f, 0.022f, 0.0f);
        glPopMatrix();
    }
    glPushMatrix();                                                              // wagging tail
    glTranslatef(-0.19f, 0.23f, 0.0f);
    glRotatef(40.0f + std::sin(phase * 1.5f) * 25.0f, 0, 0, 1);
    setColor(fur); rect(-0.015f, 0.0f, 0.015f, 0.16f);
    glPopMatrix();
    setColor(fur);  disc(0.0f, 0.19f, 0.20f, 0.09f, 16);                          // body
    disc(0.22f, 0.27f, 0.075f, 0.07f, 12);                                       // head
    disc(0.30f, 0.25f, 0.045f, 0.035f, 10);                                      // snout
    setColor(dark); disc(0.19f, 0.33f, 0.03f, 0.05f, 10);                        // ear
    setColor(tint(Color{0.05f, 0.05f, 0.05f}));
    disc(0.345f, 0.255f, 0.012f, 0.012f, 8);
    disc(0.245f, 0.285f, 0.010f, 0.010f, 8);
}

static void drawBusWheel(float x) {
    setColor(tint(Color{0.10f, 0.05f, 0.14f}));  disc(x, 0.036f, 0.044f, 0.044f, 20);   // wheel arch
    setColor(tint(Color{0.05f, 0.05f, 0.06f}));  disc(x, 0.032f, 0.034f, 0.034f, 20);   // tyre
    glBegin(GL_TRIANGLE_FAN);                                                            // rim
    setColor(tint(Color{0.95f, 0.95f, 1.00f})); glVertex2f(x, 0.032f);
    setColor(tint(Color{0.55f, 0.55f, 0.65f}));
    for (int i = 0; i <= 20; i++) {
        float a = 2.0f * PI * i / 20;
        glVertex2f(x + 0.022f * std::cos(a), 0.032f + 0.022f * std::sin(a));
    }
    glEnd();
    glPushMatrix();                                                                      // spinning spokes
    glTranslatef(x, 0.032f, 0.0f);
    glRotatef(-animTime * gWheelRate, 0, 0, 1);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    setColor(tint(Color{0.30f, 0.30f, 0.38f}));
    for (int k = 0; k < 5; k++) {
        float a = 2.0f * PI * k / 5.0f;
        glVertex2f(0, 0); glVertex2f(0.020f * std::cos(a), 0.020f * std::sin(a));
    }
    glEnd();
    glPopMatrix();
    setColor(tint(Color{0.35f, 0.35f, 0.42f}));   disc(x, 0.032f, 0.006f, 0.006f, 10);   // hub
}

static void drawBus(float night) {
    const float w = 0.25f;
    float bob = std::sin(animTime * 9.0f) * 0.0020f;           // tiny suspension bounce
    Color outline = tint(Color{0.10f, 0.12f, 0.18f});
    float gloss = 0.16f * (0.4f + 0.6f * sky.day);


    glPushMatrix();
    glTranslatef(0.0f, bob, 0.0f);

    setColor(tint(Color{0.14f, 0.14f, 0.17f}));                // underbody + wheels
    rect(-w + 0.02f, 0.02f, w - 0.02f, 0.05f);
    drawBusWheel(-0.15f);
    drawBusWheel( 0.15f);

    // body: white with a blue stripe and a red accent line
    roundRect(-w, 0.035f, w, 0.168f, 0.020f, tint(Color{0.80f, 0.82f, 0.87f}), tint(Color{0.99f, 0.99f, 1.00f}));
    roundRect(-w, 0.035f, w, 0.078f, 0.016f, tint(Color{0.07f, 0.26f, 0.62f}), tint(Color{0.14f, 0.42f, 0.84f}));
    setColor(tint(Color{0.85f, 0.15f, 0.15f})); rect(-w + 0.004f, 0.078f, w - 0.004f, 0.083f);
    roundRect(-w + 0.012f, 0.160f, w - 0.012f, 0.176f, 0.007f, tint(Color{0.70f, 0.72f, 0.78f}), tint(Color{0.90f, 0.92f, 0.96f}));  // roof
    roundRect(-0.12f, 0.175f, 0.03f, 0.190f, 0.005f, tint(Color{0.55f, 0.57f, 0.63f}), tint(Color{0.82f, 0.84f, 0.90f}));         // roof AC
    setColor(tint(Color{0.45f, 0.47f, 0.52f}));
    rect(0.06f, 0.175f, 0.09f, 0.182f); rect(0.11f, 0.175f, 0.14f, 0.182f);

    // windows with passengers
    Color wTop = mix(tint(Color{0.78f, 0.92f, 1.00f}), Color{1.0f, 0.92f, 0.60f}, night);
    Color wBot = mix(tint(Color{0.34f, 0.62f, 0.84f}), Color{1.0f, 0.70f, 0.30f}, night);
    for (int i = 0; i < 5; i++) {
        float x0 = -0.225f + i * 0.064f;
        roundRect(x0, 0.095f, x0 + 0.05f, 0.150f, 0.007f, wBot, wTop);
        if (hash1(i * 7 + 3) < 0.85f) drawPassenger(x0 + 0.025f, 0.095f, i * 3 + 1);
        if (hash1(i * 7 + 9) < 0.35f) drawPassenger(x0 + 0.040f, 0.095f, i * 5 + 2, 0.8f);
        roundRectEdge(x0, 0.095f, x0 + 0.05f, 0.150f, 0.007f, outline, 1.5f);
        setColor(Color{1, 1, 1}, 0.25f);                       // soft glass reflection
        glBegin(GL_QUADS);
            glVertex2f(x0 + 0.008f, 0.098f); glVertex2f(x0 + 0.018f, 0.098f);
            glVertex2f(x0 + 0.032f, 0.147f); glVertex2f(x0 + 0.022f, 0.147f);
        glEnd();
    }
    for (int k = 0; k < 2; k++) {                              // double door
        float x0 = 0.100f + k * 0.027f;
        roundRect(x0, 0.048f, x0 + 0.023f, 0.150f, 0.004f, wBot, wTop);
        roundRectEdge(x0, 0.048f, x0 + 0.023f, 0.150f, 0.004f, outline, 1.5f);
    }
    roundRect(0.170f, 0.062f, 0.236f, 0.150f, 0.010f, wBot, wTop);        // windscreen
    drawPassenger(0.200f, 0.074f, 17);                                    // driver
    roundRectEdge(0.170f, 0.062f, 0.236f, 0.150f, 0.010f, outline, 1.5f);
    setColor(tint(Color{0.15f, 0.12f, 0.20f})); glLineWidth(2.0f);        // steering wheel
    glBegin(GL_LINES); glVertex2f(0.222f, 0.086f); glVertex2f(0.230f, 0.102f); glEnd();

    setColor(tint(Color{0.05f, 0.05f, 0.07f}));                           // destination sign
    rect(0.170f, 0.152f, 0.238f, 0.165f);
    setColor(Color{1.0f, 0.70f, 0.15f});
    for (int i = 0; i < 6; i++) rect(0.174f + i * 0.010f, 0.155f, 0.180f + i * 0.010f, 0.162f);

    glBegin(GL_QUADS);                                         // subtle gloss band
        gl4(1, 1, 1, 0.0f);   glVertex2f(-w + 0.02f, 0.125f); glVertex2f(w - 0.02f, 0.125f);
        gl4(1, 1, 1, gloss);  glVertex2f(w - 0.02f, 0.158f);  glVertex2f(-w + 0.02f, 0.158f);
    glEnd();
    roundRectEdge(-w, 0.035f, w, 0.168f, 0.020f, outline, 2.0f);

    setColor(tint(Color{0.20f, 0.18f, 0.25f}));                           // bumpers, lights, mirror
    rect(w - 0.005f, 0.030f, w + 0.010f, 0.050f);
    rect(-w - 0.010f, 0.030f, -w + 0.005f, 0.050f);
    roundRect(w - 0.012f, 0.056f, w + 0.006f, 0.074f, 0.004f, Color{1.0f, 0.9f, 0.5f}, Color{1.0f, 1.0f, 0.9f});
    setColor(Color{0.95f, 0.10f, 0.15f}); rect(-w - 0.006f, 0.056f, -w + 0.008f, 0.076f);
    setColor(tint(Color{0.15f, 0.10f, 0.20f})); disc(w + 0.006f, 0.135f, 0.006f, 0.011f, 10);
    glowDisc(w, 0.065f, 0.05f, Color{1.0f, 0.9f, 0.55f}, 0.10f + 0.40f * night);
    glowDisc(-w, 0.066f, 0.05f, Color{1.0f, 0.1f, 0.1f}, 0.10f + 0.40f * night);
    glPopMatrix();
}

// ---- cars of different types, each with a driver (and passengers) -----------
// 0 sedan, 1 hatchback, 2 van, 3 taxi, 4 sports car. Faces +x. Returns x of the front bumper.
static float drawCarType(int type, Color col, float n) {
    float L = 0.11f, top = 0.065f, cTop = 0.105f, x0 = -0.07f, x1 = -0.045f, x2 = 0.035f, x3 = 0.065f, wr = 0.02f;
    if (type == 1)      { L = 0.095f; cTop = 0.108f; x0 = -0.085f; x1 = -0.07f;  x2 = 0.03f; x3 = 0.06f; }
    else if (type == 2) { L = 0.12f; top = 0.075f; cTop = 0.128f; x0 = -0.115f; x1 = -0.115f; x2 = 0.06f; x3 = 0.095f; wr = 0.022f; }
    else if (type == 4) { L = 0.115f; top = 0.05f; cTop = 0.085f; x0 = -0.05f; x1 = -0.015f; x2 = 0.03f; x3 = 0.075f; }

    Color bodyB = tint(scaleC(col, 0.70f)), bodyT = tint(mix(col, Color{1, 1, 1}, 0.25f));
    Color outline = tint(scaleC(col, 0.30f));

    glBegin(GL_QUADS);                                                                 // cabin
        setColor(bodyB); glVertex2f(x0, top); glVertex2f(x3, top);
        setColor(bodyT); glVertex2f(x2, cTop); glVertex2f(x1, cTop);
    glEnd();
    Color gTop = mix(tint(Color{0.80f, 0.94f, 1.00f}), Color{1.0f, 0.92f, 0.60f}, n);
    Color gBot = mix(tint(Color{0.35f, 0.62f, 0.82f}), Color{1.0f, 0.70f, 0.30f}, n);
    glBegin(GL_QUADS);                                                                 // glass
        setColor(gBot); glVertex2f(x0 + 0.012f, top); glVertex2f(x3 - 0.012f, top);
        setColor(gTop); glVertex2f(x2 - 0.006f, cTop - 0.008f); glVertex2f(x1 + 0.010f, cTop - 0.008f);
    glEnd();
    drawPassenger(x2 - 0.012f, top, type * 5 + 1, 0.75f);                              // driver
    int pid = type * 5 + 4;
    for (float px = x1 + 0.028f; px < x2 - 0.035f; px += 0.05f) drawPassenger(px, top, pid++, 0.75f);

    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
        setColor(outline);
        glVertex2f(x0, top); glVertex2f(x1, cTop); glVertex2f(x2, cTop); glVertex2f(x3, top);
    glEnd();

    for (int k = -1; k <= 1; k += 2) {                                                 // wheels
        float x = k * L * 0.58f;
        setColor(tint(Color{0.08f, 0.06f, 0.10f}));  disc(x, wr + 0.003f, wr + 0.006f, wr + 0.006f, 14);
        setColor(tint(Color{0.04f, 0.04f, 0.05f}));  disc(x, wr, wr, wr, 14);
        setColor(tint(Color{0.75f, 0.75f, 0.82f}));  disc(x, wr, wr * 0.6f, wr * 0.6f, 12);
        setColor(tint(Color{0.30f, 0.30f, 0.38f}));  disc(x, wr, wr * 0.2f, wr * 0.2f, 8);
    }
    roundRect(-L, 0.02f, L, top, 0.014f, bodyB, bodyT);                                // body
    setColor(Color{1, 1, 1}, 0.18f * (0.4f + 0.6f * sky.day));                         // gloss
    rect(-L + 0.010f, top - 0.014f, L - 0.010f, top - 0.006f);
    roundRectEdge(-L, 0.02f, L, top, 0.014f, outline, 1.5f);

    if (type == 3) {                                                                   // taxi: checker stripe + roof sign
        setColor(Color{0.05f, 0.05f, 0.05f}); rect(-L + 0.008f, 0.036f, L - 0.008f, 0.040f);
        roundRect(-0.014f, cTop, 0.014f, cTop + 0.012f, 0.003f, Color{1.0f, 0.9f, 0.4f}, Color{1.0f, 1.0f, 0.8f});
        glowDisc(0.0f, cTop + 0.006f, 0.04f, Color{1.0f, 0.9f, 0.4f}, 0.15f + 0.35f * n);
    }
    setColor(Color{1.0f, 0.97f, 0.75f}); rect(L - 0.010f, top - 0.024f, L + 0.002f, top - 0.010f);
    setColor(Color{0.95f, 0.10f, 0.15f}); rect(-L - 0.002f, top - 0.024f, -L + 0.010f, top - 0.010f);
    glowDisc(L, top - 0.017f, 0.035f, Color{1.0f, 0.9f, 0.55f}, 0.12f + 0.30f * n);
    glowDisc(-L, top - 0.017f, 0.035f, Color{1.0f, 0.1f, 0.1f}, 0.12f + 0.30f * n);
    return L + 0.002f;
}

// ----------------------------------------------------------------------------
//  The ground plane (y = 0): meadow, path, road, far lawn, pond, picnic blanket
// ----------------------------------------------------------------------------
static Color farGround() { return mix(tint(Color{0.38f, 0.62f, 0.30f}), sky.horizon, 0.15f); }   // = colour of the hills behind
static Color gcol(Color base, float z) { return mix(tint(base), farGround(), hazeAt(z)); }
static void gv(float x, float z, Color c) { setColor(c); glVertex3f(x, 0.0f, z); }

// full-width strip between two distances, vertex colours blend (Gouraud) and fade into haze
static void gband(float z0, float z1, Color c0, Color c1) {
    const float xe = 400.0f;
    glBegin(GL_QUADS);
        gv(-xe, z0, gcol(c0, z0)); gv(xe, z0, gcol(c0, z0));
        gv( xe, z1, gcol(c1, z1)); gv(-xe, z1, gcol(c1, z1));
    glEnd();
}

static void gEllipseFan(float cx, float cz, float rx, float rz, Color cen, Color edge, int seg = 48) {
    glBegin(GL_TRIANGLE_FAN);
    setColor(cen); glVertex3f(cx, 0.0f, cz);
    setColor(edge);
    for (int i = 0; i <= seg; i++) {
        float a = 2.0f * PI * i / seg;
        glVertex3f(cx + rx * std::cos(a), 0.0f, cz + rz * std::sin(a));
    }
    glEnd();
}

static void gDisc(float cx, float cz, float r, Color c, float alpha = 1.0f) {
    glBegin(GL_TRIANGLE_FAN);
    setColor(c, alpha); glVertex3f(cx, 0.0f, cz);
    for (int i = 0; i <= 16; i++) {
        float a = 2.0f * PI * i / 16;
        glVertex3f(cx + r * std::cos(a), 0.0f, cz + r * std::sin(a));
    }
    glEnd();
}

static void glowGround(float cx, float cz, float r, Color c, float alpha) {
    if (alpha <= 0.003f) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLE_FAN);
    setColor(c, alpha); glVertex3f(cx, 0.0f, cz);
    setColor(c, 0.0f);
    for (int i = 0; i <= 32; i++) {
        float a = 2.0f * PI * i / 32;
        glVertex3f(cx + r * std::cos(a), 0.0f, cz + r * std::sin(a));
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void drawPondGround() {
    const float cx = POND_X, cz = POND_Z, rx = POND_RX, rz = POND_RZ;
    float t = animTime, dayK = sky.day;
    Color bank = tint(Color{0.34f, 0.50f, 0.20f}), mud = tint(Color{0.52f, 0.44f, 0.28f});
    gEllipseFan(cx, cz, rx + 0.55f, rz + 0.30f, bank, bank);
    gEllipseFan(cx, cz, rx + 0.22f, rz + 0.12f, mud, mud);
    Color deep = mix(tint(Color{0.10f, 0.32f, 0.48f}), sky.top, 0.30f);
    Color shallow = mix(tint(Color{0.25f, 0.58f, 0.62f}), sky.horizon, 0.50f);
    gEllipseFan(cx, cz, rx, rz, deep, shallow);            // water reflects the sky

    glLineWidth(1.5f);                                      // shimmer
    glBegin(GL_LINES);
    for (int k = 0; k < 10; k++) {
        float v = (hash1(k + 600) - 0.5f) * 1.5f;
        float hw = rx * std::sqrt(1.0f - v * v);
        float mx = cx + (hash1(k + 620) - 0.5f) * hw * 1.2f + std::sin(t * 0.8f + k) * 0.2f;
        float al = (0.18f + 0.15f * std::sin(t * 1.6f + k * 1.7f)) * (0.3f + 0.7f * dayK);
        gl4(1.0f, 1.0f, 1.0f, al);
        glVertex3f(mx - 0.3f, 0.0f, cz + v * rz); glVertex3f(mx + 0.3f, 0.0f, cz + v * rz);
    }
    glEnd();
    for (int k = 0; k < 3; k++) {                           // ripples: circles on the ground look like ellipses in perspective
        float p = std::fmod(t * 0.35f + k * 0.33f, 1.0f);
        float rr = 0.08f + p * 0.7f;
        float rcx = cx - 0.9f + k * 1.0f, rcz = cz + 0.2f * std::sin(k * 2.0f);
        glLineWidth(1.2f);
        glBegin(GL_LINE_LOOP);
        gl4(1.0f, 1.0f, 1.0f, (1.0f - p) * 0.45f);
        for (int i = 0; i < 24; i++) { float a = 2.0f * PI * i / 24; glVertex3f(rcx + rr * std::cos(a), 0.0f, rcz + rr * std::sin(a)); }
        glEnd();
    }
    if (dayK > 0.4f) glowGround(cx + 0.7f, cz + 0.1f, 1.2f, Color{1.0f, 1.0f, 0.9f}, 0.14f * dayK);
    float nn = nightness();
    if (nn > 0.1f) glowGround(cx - 0.3f, cz + 0.2f, 0.9f, Color{0.8f, 0.9f, 1.0f}, 0.20f * nn);

    static const float PADS[4][2] = {{-1.2f, 0.35f}, {-0.3f, -0.45f}, {1.3f, 0.40f}, {1.9f, -0.30f}};
    for (int k = 0; k < 4; k++) {                           // lily pads
        gDisc(cx + PADS[k][0], cz + PADS[k][1], 0.32f, tint(Color{0.14f, 0.45f, 0.18f}));
        gDisc(cx + PADS[k][0], cz + PADS[k][1], 0.24f, tint(Color{0.24f, 0.60f, 0.24f}));
        if (k == 2) {
            gDisc(cx + PADS[k][0], cz + PADS[k][1], 0.12f, tint(Color{1.0f, 0.65f, 0.80f}));
            gDisc(cx + PADS[k][0], cz + PADS[k][1], 0.04f, tint(Color{1.0f, 0.90f, 0.30f}));
        }
    }
    for (int k = 0; k < 9; k++) {                           // stones on the rim
        float a = k * 0.7f;
        gDisc(cx + (rx + 0.25f) * std::cos(a), cz + (rz + 0.10f) * std::sin(a), 0.10f, tint(Color{0.55f, 0.55f, 0.55f}));
    }
}

static void drawBlanketGround() {
    const float cx = PIC_X, cz = PIC_Z, hw = 1.3f, hd = 0.9f;
    Color red = tint(Color{0.85f, 0.15f, 0.18f}), white = tint(Color{0.96f, 0.96f, 0.96f});
    const int N = 12;
    glBegin(GL_QUADS);
    for (int i = 0; i < N; i++) {
        float x0 = cx - hw + 2.0f * hw * i / N, x1 = cx - hw + 2.0f * hw * (i + 1) / N;
        setColor((i % 2) ? white : red);
        glVertex3f(x0, 0.0f, cz - hd); glVertex3f(x1, 0.0f, cz - hd);
        glVertex3f(x1, 0.0f, cz + hd); glVertex3f(x0, 0.0f, cz + hd);
    }
    glEnd();
    glBegin(GL_QUADS);                                      // cross stripes -> gingham look
    for (int k = 1; k <= 3; k++) {
        float z = cz - hd + 2.0f * hd * k / 4.0f;
        setColor(Color{0.55f, 0.05f, 0.10f}, 0.22f);
        glVertex3f(cx - hw, 0.0f, z - 0.07f); glVertex3f(cx + hw, 0.0f, z - 0.07f);
        glVertex3f(cx + hw, 0.0f, z + 0.07f); glVertex3f(cx - hw, 0.0f, z + 0.07f);
    }
    glEnd();
}

static void drawGround() {
    gFogK = 0.0f;
    for (int i = 0; i < 14; i++) {                          // meadow with mown stripes
        float z0 = 1.0f + i * (13.6f / 14.0f), z1 = 1.0f + (i + 1) * (13.6f / 14.0f);
        Color c = (i % 2) ? Color{0.33f, 0.66f, 0.22f} : Color{0.28f, 0.60f, 0.19f};
        gband(z0, z1, c, c);
    }
    Color edge = {0.45f, 0.42f, 0.38f}, curb = {0.62f, 0.62f, 0.60f};
    Color pathA = {0.72f, 0.70f, 0.66f}, pathB = {0.80f, 0.78f, 0.74f};
    gband(14.6f, 14.8f, edge, edge);
    gband(14.8f, 16.0f, pathA, pathB);                      // footpath
    gband(16.0f, 16.2f, curb, curb);
    gband(16.2f, 20.8f, Color{0.21f, 0.22f, 0.25f}, Color{0.27f, 0.28f, 0.31f});   // road
    gband(20.8f, 21.0f, curb, curb);
    gband(21.0f, 22.4f, pathA, pathB);                      // far pavement
    static const float FZ[10] = {22.4f, 26.0f, 32.0f, 40.0f, 52.0f, 70.0f, 100.0f, 150.0f, 220.0f, 400.0f};
    for (int i = 0; i < 9; i++) {                           // far lawn, fading into haze
        Color c = (i % 2) ? Color{0.27f, 0.57f, 0.20f} : Color{0.31f, 0.62f, 0.22f};
        gband(FZ[i], FZ[i + 1], c, c);
    }

    setColor(tint(Color{0.95f, 0.90f, 0.50f}));             // dashed centre line
    glBegin(GL_QUADS);
    for (float x = -60.0f; x < 60.0f; x += 3.0f) {
        glVertex3f(x, 0.0f, 18.43f);        glVertex3f(x + 1.6f, 0.0f, 18.43f);
        glVertex3f(x + 1.6f, 0.0f, 18.57f); glVertex3f(x, 0.0f, 18.57f);
    }
    glEnd();
    setColor(tint(Color{0.92f, 0.92f, 0.92f}));             // solid edge lines
    glBegin(GL_QUADS);
        glVertex3f(-60.0f, 0.0f, 16.45f); glVertex3f(60.0f, 0.0f, 16.45f);
        glVertex3f( 60.0f, 0.0f, 16.55f); glVertex3f(-60.0f, 0.0f, 16.55f);
        glVertex3f(-60.0f, 0.0f, 20.45f); glVertex3f(60.0f, 0.0f, 20.45f);
        glVertex3f( 60.0f, 0.0f, 20.55f); glVertex3f(-60.0f, 0.0f, 20.55f);
    glEnd();
    glLineWidth(1.0f);                                      // paving joints
    glBegin(GL_LINES);
    setColor(tint(Color{0.45f, 0.43f, 0.40f}), 0.5f);
    for (float x = -60.0f; x < 60.0f; x += 1.6f) {
        glVertex3f(x, 0.0f, 14.8f); glVertex3f(x, 0.0f, 16.0f);
        glVertex3f(x, 0.0f, 21.0f); glVertex3f(x, 0.0f, 22.4f);
    }
    glEnd();
    drawPondGround();
    drawBlanketGround();
}

static void drawBirds() {
    float a = sstep(0.3f, 0.7f, sky.day);
    if (a < 0.02f) return;
    glLineWidth(2.0f);
    for (int i = 0; i < 6; i++) {
        float spd = 0.05f + 0.02f * hash1(i + 400);
        float x = std::fmod(animTime * spd + i * 1.7f, 8.0f) - 4.0f;
        float y = 0.55f + 0.25f * hash1(i + 410) + 0.03f * std::sin(animTime * 0.8f + i);
        float flap = std::sin(animTime * 7.0f + i * 1.3f);
        float s = 0.025f + 0.01f * hash1(i + 420);
        glBegin(GL_LINE_STRIP);
        setColor(Color{0.12f, 0.12f, 0.15f}, a * 0.8f);
        glVertex2f(x - s, y + 0.6f * s * flap);
        glVertex2f(x - 0.45f * s, y + 0.25f * s * flap);
        glVertex2f(x, y);
        glVertex2f(x + 0.45f * s, y + 0.25f * s * flap);
        glVertex2f(x + s, y + 0.6f * s * flap);
        glEnd();
    }
}

static const char* periodName() {
    if (hourNow < 5.0f)  return "Night";
    if (hourNow < 7.0f)  return "Sunrise";
    if (hourNow < 11.0f) return "Morning";
    if (hourNow < 14.0f) return "Midday";
    if (hourNow < 17.0f) return "Afternoon";
    if (hourNow < 19.5f) return "Sunset";
    if (hourNow < 21.0f) return "Dusk";
    return "Night";
}

// ----------------------------------------------------------------------------
//  Park props drawn in metres (sprite unit = 1 m)
// ----------------------------------------------------------------------------
static void drawParasolM() {
    setColor(tint(Color{0.55f, 0.42f, 0.28f})); rect(-0.02f, 0.0f, 0.02f, 2.15f);
    Color red = tint(Color{0.85f, 0.15f, 0.18f}), white = tint(Color{0.96f, 0.96f, 0.96f});
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 6; i++) {
        float a0 = PI * i / 6.0f, a1 = PI * (i + 1) / 6.0f;
        setColor((i % 2) ? white : red);
        glVertex2f(0.0f, 1.95f);
        glVertex2f(1.15f * std::cos(a0), 1.95f + 0.55f * std::sin(a0));
        glVertex2f(1.15f * std::cos(a1), 1.95f + 0.55f * std::sin(a1));
    }
    glEnd();
    setColor(red); disc(0.0f, 2.52f, 0.04f, 0.04f, 8);
}

static void drawBasketM() {
    setColor(tint(Color{0.62f, 0.42f, 0.20f})); rect(-0.22f, 0.0f, 0.22f, 0.30f);
    setColor(tint(Color{0.45f, 0.28f, 0.12f})); rect(-0.22f, 0.12f, 0.22f, 0.15f);
    glLineWidth(2.0f);
    setColor(tint(Color{0.45f, 0.28f, 0.12f}));
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 10; i++) { float a = PI * i / 10.0f; glVertex2f(0.20f * std::cos(a), 0.30f + 0.22f * std::sin(a)); }
    glEnd();
    setColor(tint(Color{0.96f, 0.96f, 0.96f})); rect(-0.20f, 0.30f, 0.20f, 0.36f);
}

static void drawDuckM(int d, float bob) {                  // faces +x
    Color bodyC = (d == 0) ? Color{0.97f, 0.97f, 0.95f} : Color{0.60f, 0.42f, 0.25f};
    setColor(tint(bodyC)); disc(0.0f, 0.09f + bob, 0.17f, 0.085f, 16);
    glBegin(GL_TRIANGLES);
        glVertex2f(-0.15f, 0.09f + bob); glVertex2f(-0.25f, 0.20f + bob); glVertex2f(-0.08f, 0.14f + bob);
    glEnd();
    setColor(tint(scaleC(bodyC, 0.85f))); disc(-0.02f, 0.10f + bob, 0.09f, 0.04f, 10);
    setColor(tint(Color{0.10f, 0.45f, 0.25f})); disc(0.15f, 0.20f + bob, 0.055f, 0.055f, 12);
    setColor(tint(Color{1.0f, 0.65f, 0.10f}));
    glBegin(GL_TRIANGLES);
        glVertex2f(0.19f, 0.215f + bob); glVertex2f(0.27f, 0.19f + bob); glVertex2f(0.19f, 0.175f + bob);
    glEnd();
    setColor(Color{0.05f, 0.05f, 0.05f}); disc(0.17f, 0.22f + bob, 0.01f, 0.01f, 6);
}

static void drawReedM(float h, float sway, bool cattail) {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    setColor(tint(Color{0.20f, 0.45f, 0.15f})); glVertex2f(0.0f, 0.0f);
    setColor(tint(Color{0.45f, 0.65f, 0.25f})); glVertex2f(sway, h);
    glEnd();
    if (cattail) { setColor(tint(Color{0.40f, 0.25f, 0.12f})); disc(sway, h, 0.025f, 0.09f, 8); }
}

static void drawBallM() {
    setColor(tint(Color{0.95f, 0.30f, 0.25f})); disc(0.0f, 0.11f, 0.11f, 0.11f, 12);
    setColor(tint(Color{1.0f, 1.0f, 1.0f}), 0.7f); disc(-0.03f, 0.15f, 0.035f, 0.035f, 8);
}

static void drawKiteM(float t) {
    Color kr = tint(Color{0.95f, 0.25f, 0.30f}), ky = tint(Color{1.0f, 0.85f, 0.15f});
    glBegin(GL_TRIANGLES);
        setColor(kr); glVertex2f(0, 0.35f);  glVertex2f(-0.22f, 0); glVertex2f(0, 0);
        setColor(ky); glVertex2f(0, 0.35f);  glVertex2f(0.22f, 0);  glVertex2f(0, 0);
        setColor(ky); glVertex2f(0, -0.45f); glVertex2f(-0.22f, 0); glVertex2f(0, 0);
        setColor(kr); glVertex2f(0, -0.45f); glVertex2f(0.22f, 0);  glVertex2f(0, 0);
    glEnd();
    glLineWidth(1.5f);
    setColor(kr);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 8; i++) glVertex2f(0.07f * std::sin(t * 5.0f + i * 0.8f), -0.45f - i * 0.08f);
    glEnd();
    for (int i = 2; i <= 6; i += 2) {
        setColor((i % 4 == 0) ? ky : kr);
        disc(0.07f * std::sin(t * 5.0f + i * 0.8f), -0.45f - i * 0.08f, 0.04f, 0.025f, 8);
    }
}

// ----------------------------------------------------------------------------
//  Cars, people and animals: movement data
// ----------------------------------------------------------------------------
struct Vehicle { int type; Color col; float speed; float offset; };      // type -1 = bus; speed in m/s
static const Vehicle LANE_NEAR[3] = {                                    // near lane drives left -> right
    {-1, {0.0f, 0.0f, 0.0f}, 5.0f, 0.0f}, {2, {0.92f, 0.92f, 0.95f}, 5.0f, 20.0f}, {1, {0.25f, 0.65f, 0.40f}, 5.0f, 40.0f}
};
static const Vehicle LANE_FAR[3] = {                                     // far lane drives right -> left
    {0, {0.15f, 0.35f, 0.80f}, 8.0f, 5.0f}, {3, {1.00f, 0.78f, 0.10f}, 8.0f, 25.0f}, {4, {0.85f, 0.12f, 0.15f}, 8.0f, 45.0f}
};
struct Walker { float x0, speed; int dir; int id; float z, h; };         // speed m/s, h = height in metres
static const Walker WALKERS[9] = {
    {-8.0f, 1.30f,  1,  1, 15.20f, 1.72f}, { 6.0f, 1.10f, -1,  4, 15.60f, 1.68f},
    { 0.5f, 1.45f,  1,  7, 15.40f, 1.80f}, {-3.0f, 1.00f, -1, 10, 15.70f, 1.65f},
    {11.0f, 1.20f, -1, 13, 15.30f, 1.66f}, {10.0f, 1.20f, -1, 16, 15.35f, 1.05f},     // mother + child
    {-14.0f, 1.35f, 1, 19, 21.70f, 1.75f}, { 3.0f, 1.15f, -1, 22, 21.55f, 1.70f}, {16.0f, 1.40f, -1, 25, 21.80f, 1.72f}
};
static const Walker CYCLISTS[2] = {
    {-10.0f, 4.5f, 1, 2, 15.05f, 1.55f}, {12.0f, 3.5f, -1, 5, 15.80f, 1.55f}
};

// ----------------------------------------------------------------------------
//  Per-frame list of standing sprites, depth sorted (painter's algorithm),
//  and the stencil-buffer planar shadow pass
// ----------------------------------------------------------------------------
struct Item { float z; bool shadow; std::function<void()> fn; };
static std::vector<Item> items;
static int gItemsDrawn = 0, gPlantsDrawn = 0;

static void addItem(float z, bool shadow, std::function<void()> fn) {
    Item it; it.z = z; it.shadow = shadow; it.fn = fn;
    items.push_back(it);
}
static bool itemFarther(const Item& a, const Item& b) { return a.z > b.z; }

static float loopX(float x0, float speed, int dir) {            // a 60 m long loop of street
    float x = std::fmod(x0 + 30.0f + dir * speed * animTime, 60.0f);
    if (x < 0.0f) x += 60.0f;
    return x - 30.0f;
}

// people (not vehicles) fade out as night falls and fade back in at dawn
static void addFaded(float z, bool shadow, std::function<void()> fn) {
    float pa = gPeopleFade;
    if (pa < 0.02f) return;
    addItem(z, shadow, [=]() { gAlphaMul = pa; fn(); gAlphaMul = 1.0f; });
}

// SECRET (key E): one little figure standing by the trees
static void drawLittleFigureM(float night, float ppm) {                  // sprite unit = metres
    float s = 3.2f / 2.4f;                                    // height in metres / 2.4 (the drawing is 2.4 units tall)
    glPushMatrix();
    glScalef(s, s, 1.0f);
    setColor(Color{0.01f, 0.01f, 0.02f}, 0.92f);
    disc(0.0f, 2.15f, 0.11f, 0.14f, 12);
    quad4(-0.16f, 1.45f, 0.16f, 1.45f, 0.10f, 2.00f, -0.10f, 2.00f);
    rect(-0.10f, 0.0f, -0.03f, 1.50f); rect(0.03f, 0.0f, 0.10f, 1.50f);
    quad4(-0.16f, 1.95f, -0.11f, 1.95f, -0.20f, 0.75f, -0.26f, 0.75f);
    quad4( 0.11f, 1.95f,  0.16f, 1.95f,  0.26f, 0.75f,  0.20f, 0.75f);
    gFogK = 0.0f;
    if (night > 0.3f) {                                       // at night its eyes glow (kept at least ~7 px wide so they show from far away)
        float gr = std::max(0.45f, 7.0f / ppm) / s;
        glowDisc(-0.06f, 2.17f, gr * 0.6f, Color{1.0f, 0.1f, 0.05f}, 0.9f * night);
        glowDisc( 0.06f, 2.17f, gr * 0.6f, Color{1.0f, 0.1f, 0.05f}, 0.9f * night);
        glowDisc(0.0f, 2.17f, gr, Color{1.0f, 0.1f, 0.05f}, 0.5f * night);
        setColor(Color{1.0f, 0.35f, 0.2f}, night);
        disc(-0.06f, 2.17f, 0.05f, 0.03f, 8); disc(0.06f, 2.17f, 0.05f, 0.03f, 8);
    }
    glPopMatrix();
}

static void buildItems() {
    items.clear();
    float n = nightness();
    float t = animTime;
    float zMin = zBottom - 1.5f;
    gPeopleFade = 1.0f - sstep(0.15f, 0.85f, n);               // 1 by day, fading to 0 in the deep night

    // ---- trees on the far lawn
    static const float TR[14][3] = {
        {-22, 28, 30}, {-17, 34, 36}, {-12, 27, 26}, {-6, 31, 34}, {-1, 38, 40}, {5, 29, 28}, {11, 35, 38},
        {17, 27, 30}, {22, 33, 34}, {-30, 45, 40}, {28, 50, 42}, {-12, 52, 38}, {10, 60, 44}, {0, 70, 46}
    };
    for (int i = 0; i < 14; i++) {
        float x = TR[i][0], z = TR[i][1], k = TR[i][2];
        if (!visibleX(x, z, k * 0.15f)) continue;
        addItem(z, true, [=]() { beginSprite(x, z, k); drawTree(0.0f, 0.0f, 1.0f); endSprite(); });
    }

    if (easterEggs) {                                          // SECRET (key E): tall figures peeking from behind trees on both sides
        static const float FIG[2][2] = {{-12.25f, 29.0f}, {17.6f, 28.0f}};     // x, z: behind the trees at the front of the far lawn
        for (int i = 0; i < 2; i++) {
            float fx = FIG[i][0], fz = FIG[i][1];
            if (visibleX(fx, fz, 1.0f))
                addItem(fz, true, [=]() { beginSprite(fx, fz, 1.0f); drawLittleFigureM(n, pxPerMetre(fz)); endSprite(); });
        }
    }

    // ---- butterflies, ducks, reeds
    float bAlpha = sstep(0.25f, 0.6f, sky.day);
    if (bAlpha > 0.02f) {
        for (int i = 0; i < 3; i++) {
            Butterfly b = BFLY[i];
            float a1 = t * b.speed * 1.4f + b.ph;
            float x = b.ax * 3.0f * std::sin(a1);
            float y = 1.1f + 0.45f * std::sin(a1 * 1.9f + b.ph * 2.0f) + 0.05f * std::sin(t * 7.0f + b.ph);
            float z = 10.5f + 3.0f * std::sin(a1 * 0.6f + b.ph * 3.0f);
            float flap = std::fabs(std::cos(t * (9.0f + b.ph))) * 0.8f + 0.2f;
            float sc = b.size * 0.85f;
            if (!visibleX(x, z, 0.3f)) continue;
            addItem(z, true, [=]() {
                glPushMatrix();
                glTranslatef(x, y, z);
                glScalef(sc, sc, 1.0f);
                gFogK = hazeAt(z);
                drawButterflyLocal(b, flap, bAlpha);
                glPopMatrix();
                gFogK = 0.0f;
            });
        }
    }
    for (int d = 0; d < 2; d++) {
        float ang = t * 0.18f + d * 2.4f;
        float x = POND_X + POND_RX * 0.55f * std::sin(ang);
        float z = POND_Z + (d == 0 ? 0.40f : -0.45f);
        bool flip = std::cos(ang) < 0.0f;
        float bob = std::sin(t * 2.0f + d) * 0.01f;
        if (!visibleX(x, z, 0.4f)) continue;
        addItem(z, false, [=]() { beginSprite(x, z, 1.0f, flip); drawDuckM(d, bob); endSprite(); });
    }
    for (int k = 0; k < 12; k++) {
        float side = (k < 6) ? -1.0f : 1.0f;
        float x = POND_X + side * (POND_RX + 0.1f + 0.5f * hash1(k + 500));
        float z = POND_Z + (hash1(k + 510) - 0.5f) * 1.4f;
        float h = 0.9f + 0.7f * hash1(k + 520);
        float sway = std::sin(t * 1.4f + k) * 0.08f;
        bool cat = (k % 2) == 0;
        if (!visibleX(x, z, 0.5f)) continue;
        addItem(z, false, [=]() { beginSprite(x, z, 1.0f); drawReedM(h, sway, cat); endSprite(); });
    }

    // ---- picnic
    addItem(PIC_Z - 0.20f, true, [=]() { beginSprite(PIC_X - 1.05f, PIC_Z - 0.20f, 1.0f); drawParasolM(); endSprite(); });
    addItem(PIC_Z + 0.35f, true, [=]() { beginSprite(PIC_X + 0.3f, PIC_Z + 0.35f, 1.0f); drawBasketM(); endSprite(); });
    addFaded(PIC_Z - 0.10f, true, [=]() { beginSprite(PIC_X - 0.7f, PIC_Z - 0.10f, 1.9f); drawSitting(3, 15.0f); endSprite(); });
    addFaded(PIC_Z + 0.15f, true, [=]() { beginSprite(PIC_X + 1.0f, PIC_Z + 0.15f, 1.9f, true); drawSitting(6, 160.0f + 12.0f * std::sin(t * 4.0f)); endSprite(); });
    addFaded(PIC_Z - 0.45f, true, [=]() { beginSprite(PIC_X + 0.1f, PIC_Z - 0.45f, 1.3f); drawSitting(9, 150.0f + 20.0f * std::sin(t * 6.0f)); endSprite(); });

    // ---- children, dog, ball, kite
    {
        float kx = KID_X - 1.3f, kz = KID_Z + 0.6f;                   // kite flyer
        float kiteX = kx + 3.0f + 1.0f * std::sin(t * 0.7f), kiteY = 6.0f + 0.5f * std::sin(t * 1.3f);
        float hx = kx + 0.18f, hy = 1.1f;
        float mx = (hx + kiteX) * 0.5f, my = (hy + kiteY) * 0.5f - 0.8f;
        addFaded(kz, true, [=]() { beginSprite(kx, kz, 1.15f); drawPerson(11, 0.0f, 0.0f, 150.0f + 12.0f * std::sin(t * 3.0f)); endSprite(); });
        addFaded(kz + 0.01f, false, [=]() {
            gFogK = hazeAt(kz);
            glLineWidth(1.0f);
            glBegin(GL_LINE_STRIP);
            setColor(tint(Color{0.95f, 0.95f, 0.95f}), 0.8f);
            for (int i = 0; i <= 24; i++) {                            // sagging string (quadratic Bezier)
                float u = i / 24.0f;
                float ax = lerpf(hx, mx, u), ay = lerpf(hy, my, u), bx = lerpf(mx, kiteX, u), by = lerpf(my, kiteY, u);
                glVertex3f(lerpf(ax, bx, u), lerpf(ay, by, u), kz);
            }
            glEnd();
            glPushMatrix();
            glTranslatef(kiteX, kiteY, kz);
            glRotatef(12.0f * std::sin(t * 1.1f), 0, 0, 1);
            drawKiteM(t);
            glPopMatrix();
            gFogK = 0.0f;
        });
    }
    {
        float jx = KID_X + 1.2f, jz = KID_Z - 0.6f;                   // jumping child
        float j = std::fabs(std::sin(t * 4.5f)) * 0.35f;
        addFaded(jz, true, [=]() { beginSprite(jx, jz, 1.1f, false, j); drawPerson(14, t * 9.0f, 25.0f * (j / 0.35f), 140.0f + 20.0f * std::sin(t * 9.0f)); endSprite(); });
    }
    {
        float tr = t * 1.5f;                                           // child chased by a dog, with a ball
        float rx = KID_X - 0.2f + 1.5f * std::sin(tr), rz = KID_Z + 0.1f;
        bool right = std::cos(tr) >= 0.0f;
        float dirK = right ? 1.0f : -1.0f;
        float ph = t * 18.0f;
        float hop = std::fabs(std::sin(ph)) * 0.04f;
        float bx = rx + dirK * 1.4f, by = std::fabs(std::sin(t * 7.0f)) * 0.5f;
        float dx = rx - dirK * 1.9f;
        addFaded(rz + 0.02f, true, [=]() { beginSprite(dx, rz + 0.02f, 1.5f, !right, hop); drawDog(ph + 1.0f); endSprite(); });
        addFaded(rz, true, [=]() { beginSprite(rx, rz, 1.1f, !right, hop); drawPerson(8, ph, 42.0f, 25.0f); endSprite(); });
        addFaded(rz - 0.01f, false, [=]() { beginSprite(bx, rz, 1.0f, false, by); drawBallM(); endSprite(); });
    }

    // ---- city life: lamps, pedestrians, cyclists, traffic
    if (showCommute) {
        for (int i = 0; i < 4; i++) {
            float x = -18.0f + 12.0f * i, z = 21.7f;
            if (!visibleX(x, z, 3.0f)) continue;
            addItem(z, true, [=]() { beginSprite(x, z, 14.0f); drawLamp(0.0f, 0.0f, n); endSprite(); });
        }
        for (int i = 0; i < 9; i++) {
            Walker w = WALKERS[i];
            float x = loopX(w.x0, w.speed, w.dir);
            if (!visibleX(x, w.z, 1.0f) || w.z < zMin) continue;
            float phase = t * w.speed * 4.5f * (1.7f / w.h);
            float bounce = std::fabs(std::sin(phase)) * 0.012f;
            addFaded(w.z, true, [=]() {
                beginSprite(x, w.z, w.h, w.dir < 0);
                glTranslatef(0.0f, bounce, 0.0f);
                drawPerson(w.id, phase, 28.0f, 0.0f);
                endSprite();
            });
        }
        for (int i = 0; i < 2; i++) {
            Walker w = CYCLISTS[i];
            float x = loopX(w.x0, w.speed, w.dir);
            if (!visibleX(x, w.z, 1.5f)) continue;
            float wheelDeg = -(w.speed * t / (0.22f * w.h)) * 57.2958f;
            addFaded(w.z, true, [=]() { beginSprite(x, w.z, w.h, w.dir < 0); drawCyclist(w.id, wheelDeg, t * 7.0f); endSprite(); });
        }
        for (int lane = 0; lane < 2; lane++) {
            for (int i = 0; i < 3; i++) {
                Vehicle v = lane == 0 ? LANE_NEAR[i] : LANE_FAR[i];
                bool right = (lane == 0);
                float z = right ? 17.5f : 19.5f;
                float x = loopX(v.offset - 30.0f, v.speed, right ? 1 : -1);
                float k = (v.type < 0) ? 17.0f : 16.0f;
                if (!visibleX(x, z, 6.0f)) continue;
                addItem(z, true, [=]() {
                    beginSprite(x, z, k, !right);
                    gWheelRate = v.speed / (0.034f * k) * 57.2958f;
                    if (v.type < 0) {
                        drawBus(n);
                        headlightCone(0.25f, 0.065f, 0.45f, 0.06f, 0.45f * n);
                    } else {
                        float front = drawCarType(v.type, v.col, n);
                        headlightCone(front, 0.048f, 0.30f, 0.04f, 0.45f * n);
                    }
                    endSprite();
                });
            }
        }
    }
}

// shadows: every sprite is flattened onto the ground with the sun's shadow matrix.
// The stencil buffer lets each ground pixel be darkened only once, so overlapping
// shadows (and overlapping parts of one sprite) do not double up.
static void shadowPass() {
    if (!showShadows || gShadowAlpha < 0.01f) return;
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    gShadowPass = true;
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].shadow) continue;
        glPushMatrix();
        glMultMatrixf(shadowM);
        items[i].fn();
        glPopMatrix();
    }
    gShadowPass = false;
    glDisable(GL_STENCIL_TEST);
}

// ----------------------------------------------------------------------------
//  HUD and rendering info panel
// ----------------------------------------------------------------------------
static void drawHUD() {
    int h = (int)hourNow;
    int m = (int)((hourNow - h) * 60.0f);
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%02d:%02d %s - %s", h12, m, h < 12 ? "AM" : "PM", periodName());

    Color txt = sky.day > 0.35f ? Color{0.08f, 0.08f, 0.10f} : Color{1.0f, 1.0f, 1.0f};
    Color shadow = sky.day > 0.35f ? Color{1.0f, 1.0f, 1.0f} : Color{0.0f, 0.0f, 0.0f};
    float x = -aspect + 0.06f;
    drawText(x + 0.004f, 0.896f, GLUT_BITMAP_HELVETICA_18, buf, shadow);
    drawText(x, 0.90f, GLUT_BITMAP_HELVETICA_18, buf, txt);
    drawText(x, 0.84f, GLUT_BITMAP_HELVETICA_12,
             "[1-6] time  [Space] pause  [+/-] speed  [Arrows] camera  [C] city life  [S] shadows  [F] haze  [G] wireframe  [H] info", txt);

    if (!showInfo) return;
    float px = -aspect + 0.04f, py = 0.78f, ph = 0.60f, pw = 1.55f;
    setColor(Color{0.0f, 0.0f, 0.0f}, 0.55f);
    rect(px, py - ph, px + pw, py);
    Color w = {1.0f, 1.0f, 1.0f};
    float pitchDeg = std::atan2(camH - LOOK_Y, LOOK_Z) * 180.0f / PI;
    float ly = py - 0.06f, dy = 0.05f;
    std::snprintf(buf, sizeof(buf), "RENDERING INFO");                                      drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, Color{1.0f, 0.9f, 0.4f}); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Perspective camera: height %.1f m, pitch %.1f deg, FOV %.0f deg", camH, pitchDeg, FOVY);
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Light: sun elevation %.0f deg -> shadow length x%.1f of object height", sunElevDeg, shadowStretch);
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Ambient light colour (r,g,b): %.2f %.2f %.2f  (tints every colour)", sky.amb.r, sky.amb.g, sky.amb.b);
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Shadows [S]: %s   planar projection matrix + stencil buffer", showShadows ? "ON" : "OFF");
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Distance haze [F]: %s   (atmospheric perspective)", showFog ? "ON" : "OFF");
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Wireframe [G]: %s", wireframe ? "ON" : "OFF");
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    std::snprintf(buf, sizeof(buf), "Depth-sorted sprites drawn: %d   plants drawn (with LOD): %d", gItemsDrawn, gPlantsDrawn);
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, buf, w); ly -= dy;
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, "Also: billboards, painter's algorithm, LOD + culling, alpha blending,", w); ly -= dy;
    drawText(px + 0.03f, ly, GLUT_BITMAP_HELVETICA_12, "Bezier curves, hierarchical transforms, Gouraud shading, double buffering.", w);
}

// ----------------------------------------------------------------------------
//  Scene setup
// ----------------------------------------------------------------------------
static void initScene() {
    buildShapes();
    for (int i = 0; i < 140; i++) {
        Star s;
        s.x = -4.0f + 8.0f * hash1(i * 3 + 1);
        s.y = 0.10f + 0.90f * hash1(i * 3 + 2);
        s.ph = hash1(i * 3 + 3) * 6.28f;
        s.size = 1.0f + 1.6f * hash1(i * 5 + 7);
        stars.push_back(s);
    }
    initPlants();
    for (int k = 0; k < 28; k++) {
        Fly f;
        f.x = -7.0f + 14.0f * hash1(k * 4 + 21);
        f.y = 0.3f + 1.3f * hash1(k * 4 + 22);
        f.z = 8.0f + 6.0f * hash1(k * 4 + 23);
        f.ph = hash1(k * 4 + 24) * 6.28f;
        flies.push_back(f);
    }
}

// ----------------------------------------------------------------------------
//  Display / animation loop
// ----------------------------------------------------------------------------
static void display() {
    updateSky();
    updateLight();
    float pitch = std::atan2(camH - LOOK_Y, LOOK_Z);
    horizonY = std::tan(pitch) / tanHalfFov;
    zBottom = camH / std::tan(FOVY * 0.5f * PI / 180.0f + pitch);
    gFogC = sky.horizon;

    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // pass 1: the far backdrop (sky, sun, clouds, mountains, city) in 2D screen space
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(-aspect, aspect, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    drawSky();
    drawStars();
    drawSunMoon();
    drawClouds();
    drawBirds();
    glPushMatrix();
    glTranslatef(-camX * 0.01f, horizonY + 0.0425f, 0.0f);
    glScalef(1.0f, 0.85f, 1.0f);
    drawBackdrop();
    glPopMatrix();

    // pass 2: the world, through a perspective camera
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(FOVY, aspect, 0.3, 600.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    gluLookAt(camX, camH, 0.0, camX, LOOK_Y, LOOK_Z, 0.0, 1.0, 0.0);

    drawGround();
    drawFarDots();
    buildItems();
    shadowPass();
    std::sort(items.begin(), items.end(), itemFarther);

    // far -> near: merge the pre-sorted plants with the sorted sprites
    float zMin = zBottom - 0.8f;
    size_t ip = 0, ii = 0;
    gItemsDrawn = 0; gPlantsDrawn = 0;
    while (ip < plants.size() || ii < items.size()) {
        bool takePlant = (ii >= items.size()) || (ip < plants.size() && plants[ip].z >= items[ii].z);
        if (takePlant) {
            const Plant& p = plants[ip++];
            if (p.z < zMin) { ip = plants.size(); continue; }
            if (!visibleX(p.x, p.z, 0.4f)) continue;
            if (p.type) drawTuft(p); else drawFlowerPlant(p);
            gPlantsDrawn++;
        } else {
            items[ii++].fn();
            gItemsDrawn++;
        }
    }
    drawFireflies();

    // pass 3: HUD
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(-aspect, aspect, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
    drawHUD();

    glutSwapBuffers();
}

static void tick(int) {
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;
    if (dt > 0.1f) dt = 0.1f;
    if (!paused) {
        animTime += dt;
        hourNow = std::fmod(hourNow + dt * speedMul * 24.0f / dayLength, 24.0f);
    }
    glutPostRedisplay();
    glutTimerFunc(16, tick, 0);   // ~60 FPS
}

static void reshape(int w, int h) {
    if (h == 0) h = 1;
    viewW = w; viewH = h;
    aspect = (float)w / (float)h;
    glViewport(0, 0, w, h);
}

static void keyboard(unsigned char key, int, int) {
    static const float JUMP[6] = {6.3f, 9.0f, 12.0f, 15.5f, 18.5f, 22.0f};
    if (key >= '1' && key <= '6') hourNow = JUMP[key - '1'];
    else if (key == ' ') paused = !paused;
    else if (key == '+' || key == '=') speedMul = std::min(speedMul * 1.5f, 30.0f);
    else if (key == '-' || key == '_') speedMul = std::max(speedMul / 1.5f, 0.1f);
    else if (key == 'c' || key == 'C') showCommute = !showCommute;
    else if (key == 'r' || key == 'R') animTime = 0.0f;
    else if (key == 's' || key == 'S') showShadows = !showShadows;
    else if (key == 'f' || key == 'F') showFog = !showFog;
    else if (key == 'g' || key == 'G') wireframe = !wireframe;
    else if (key == 'h' || key == 'H') showInfo = !showInfo;
    else if (key == 'e' || key == 'E') easterEggs = !easterEggs;      // secret key
    else if (key == '0') { camX = 0.0f; camH = 4.5f; }
    else if (key == 27) std::exit(0);
}

static void special(int key, int, int) {
    if (key == GLUT_KEY_LEFT)       camX = std::max(camX - 0.5f, -10.0f);
    else if (key == GLUT_KEY_RIGHT) camX = std::min(camX + 0.5f, 10.0f);
    else if (key == GLUT_KEY_UP)    camH = std::min(camH + 0.3f, 9.0f);
    else if (key == GLUT_KEY_DOWN)  camH = std::max(camH - 0.3f, 2.0f);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_MULTISAMPLE | GLUT_STENCIL);
    glutInitWindowPosition(60, 20);
    glutInitWindowSize(1000, 600);
    glutCreateWindow("A Day in Bloom - Field of Flowers & Time Cycles");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

    initScene();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    lastMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(0, tick, 0);

    glutMainLoop();
    return 0;
}

// ============================================================================
//  A DAY IN BLOOM  -  2D OpenGL (freeglut) scene, single file
//
//  Controls:
//    1-6      jump to sunrise / morning / midday / afternoon / sunset / night
//    SPACE    pause / resume
//    + / -    speed up / slow down the day
//    C        show / hide the commuter road (bus, car, street lamps)
//    R        replay the blooming intro
//    ESC      quit
// ============================================================================
#include <GL/freeglut.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

static const float PI = 3.14159265358979323846f;

// ----------------------------------------------------------------------------
//  Basic types & helpers
// ----------------------------------------------------------------------------
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
static float aspect      = 1100.0f / 680.0f;  // window aspect ratio
static float hourNow     = 8.0f;              // time of day, 0..24
static float animTime    = 0.0f;              // seconds of animation (stops when paused)
static float dayLength   = 120.0f;            // real seconds for one full 24h cycle
static float speedMul    = 1.0f;
static bool  paused      = false;
static bool  showCommute = true;
static int   lastMs      = 0;

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
static void setColor(Color c, float a = 1.0f) { glColor4f(c.r, c.g, c.b, a); }

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
    if (alpha <= 0.003f) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(c.r, c.g, c.b, alpha);
    glVertex2f(x, y);
    glColor4f(c.r, c.g, c.b, 0.0f);
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
    const float W = 5.0f;
    Color mid = mix(sky.horizon, sky.top, 0.55f);
    glBegin(GL_QUADS);
        setColor(sky.horizon); glVertex2f(-W, -0.3f); glVertex2f(W, -0.3f);
        setColor(mid);         glVertex2f(W, 0.45f);  glVertex2f(-W, 0.45f);
        setColor(mid);         glVertex2f(-W, 0.45f); glVertex2f(W, 0.45f);
        setColor(sky.top);     glVertex2f(W, 1.1f);   glVertex2f(-W, 1.1f);
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
        glColor4f(1.0f, 1.0f, 0.92f, vis * tw);
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
        float sy = -0.20f + std::sin(a) * 1.05f;
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
        float my = -0.20f + std::sin(a) * 0.95f;
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
static const Hill nearH = {-0.14f, 0.035f, 1.20f, 2.90f, 4.0f, false };

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
    setColor(Color{0, 0, 0}, 0.15f);
    disc(x + 0.02f * s, y, 0.09f * s, 0.014f * s, 20);
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

struct TreeDef { float x, s; };
static const TreeDef TREES[8] = {
    {-2.30f, 1.00f}, {-1.80f, 0.70f}, {-1.35f, 1.10f}, {-0.95f, 0.65f},
    { 1.00f, 0.75f}, { 1.40f, 1.15f}, { 1.85f, 0.70f}, { 2.30f, 1.00f}
};

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
    setColor(side); quad4(x + w, y0, x + w + ox, y0 + oy, x + w + ox, y0 + h + oy, x + w, y0 + h);
    setColor(roof); quad4(x, y0 + h, x + w, y0 + h, x + w + ox, y0 + h + oy, x + ox, y0 + h + oy);
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
    float ox = d * 0.55f, oy = d * 0.28f;
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

static void drawLandscape() {
    // far mountains: hazy, blended with the sky colour (aerial perspective)
    Color farC = mix(tint(Color{0.38f, 0.50f, 0.62f}), sky.horizon, 0.45f);
    drawHill(farH, farC, mix(farC, sky.horizon, 0.4f), -0.4f);
    drawCity();

    // middle hills
    Color midTop = mix(tint(Color{0.38f, 0.62f, 0.30f}), sky.horizon, 0.15f);
    Color midBot = mix(tint(Color{0.28f, 0.50f, 0.24f}), sky.horizon, 0.15f);
    drawHill(midH, midTop, midBot, -0.4f);

    for (int i = 0; i < 8; i++)
        drawTree(TREES[i].x, hillY(midH, TREES[i].x) - 0.015f, TREES[i].s);

    // meadow
    drawHill(nearH, tint(Color{0.40f, 0.68f, 0.26f}), tint(Color{0.16f, 0.42f, 0.14f}), -1.2f);
}

// ----------------------------------------------------------------------------
//  Grass blades
// ----------------------------------------------------------------------------
struct Blade { float x, y, h, w, ph, shade; };
static std::vector<Blade> backGrass, frontGrass;

static void drawBlades(const std::vector<Blade>& v) {
    glBegin(GL_TRIANGLES);
    for (size_t i = 0; i < v.size(); i++) {
        const Blade& b = v[i];
        float sw = std::sin(animTime * 1.7f + b.x * 2.5f + b.ph) * 0.35f * b.h;
        setColor(tint(Color{0.10f * b.shade, 0.34f * b.shade, 0.08f * b.shade}));
        glVertex2f(b.x - b.w, b.y);
        glVertex2f(b.x + b.w, b.y);
        setColor(tint(Color{0.30f * b.shade, 0.62f * b.shade, 0.20f * b.shade}));
        glVertex2f(b.x + sw, b.y + b.h);
    }
    glEnd();
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
    glColor4f(b.r * 0.6f, b.g * 0.6f, b.b * 0.6f, 0.35f);
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

struct Flower { float x, gy, scale, phase, delay; int variant, seed; };
static std::vector<Flower> flowers;

static void drawFlowerHead(const Flower& f) {
    // petals close up a little at night
    float open = 0.78f + 0.22f * sstep(0.2f, 0.7f, sky.day);
    int rings = (f.scale < 0.035f) ? 1 : 2;
    const Palette& pl = PAL[f.variant];

    for (int r = rings - 1; r >= 0; --r) {          // back ring first
        for (int i = 0; i < 8; i++) {
            int idx = f.seed * 16 + r * 8 + i;
            float j1 = hash1(idx), j2 = hash1(idx + 101);
            glPushMatrix();
            glRotatef(i * 45.0f + (r == 1 ? 22.5f : 0.0f) + (j1 - 0.5f) * 10.0f, 0, 0, 1);
            glScalef((0.95f + 0.15f * j2) * (0.85f + 0.15f * open),
                     (0.85f + 0.20f * j2) * open * (r == 1 ? 0.92f : 1.0f), 1.0f);
            drawPetal(pl, r == 1 ? 0.85f : 1.0f);
            glPopMatrix();
        }
    }

    // centre disc
    glBegin(GL_TRIANGLE_FAN);
    setColor(tint(Color{0.30f, 0.14f, 0.02f}));
    glVertex2f(0.0f, 0.0f);
    setColor(tint(Color{0.92f, 0.66f, 0.08f}));
    for (int i = 0; i <= 24; i++) {
        float a = 2.0f * PI * i / 24;
        glVertex2f(0.13f * std::cos(a), 0.13f * std::sin(a));
    }
    glEnd();

    if (f.scale >= 0.04f) {
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

static void drawFlower(const Flower& f) {
    if (std::fabs(f.x) > aspect + 0.2f) return;               // off screen
    float tt = animTime - f.delay;
    float stemG = sstep(0.0f, 2.0f, tt);            // stem grows first
    if (stemG <= 0.0f) return;
    float headG = sstep(0.0f, 3.0f, tt - 1.5f);     // then the head blooms

    float stemLen = (0.06f + 2.2f * f.scale) * stemG;
    float wind = std::sin(animTime * 0.9f + f.x * 0.8f) * 0.6f + std::sin(animTime * 2.1f + f.phase) * 0.4f;
    float off = wind * 0.10f * stemLen;

    Point p0 = {f.x, f.gy};
    Point p1 = {f.x, f.gy + stemLen * 0.45f};
    Point p2 = {f.x + off * 0.5f, f.gy + stemLen * 0.80f};
    Point p3 = {f.x + off, f.gy + stemLen};

    // stem
    float w = 0.002f + 0.04f * f.scale;
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 14; i++) {
        float t = i / 14.0f;
        Point p  = bezier(p0, p1, p2, p3, t);
        Point qa = bezier(p0, p1, p2, p3, std::max(t - 0.02f, 0.0f));
        Point qb = bezier(p0, p1, p2, p3, std::min(t + 0.02f, 1.0f));
        float dx = qb.x - qa.x, dy = qb.y - qa.y;
        float len = std::sqrt(dx * dx + dy * dy);
        float nx = 1.0f, ny = 0.0f;
        if (len > 1e-6f) { nx = -dy / len; ny = dx / len; }
        setColor(tint(mix(Color{0.12f, 0.40f, 0.10f}, Color{0.30f, 0.60f, 0.20f}, t)));
        glVertex2f(p.x - nx * w * 0.5f, p.y - ny * w * 0.5f);
        glVertex2f(p.x + nx * w * 0.5f, p.y + ny * w * 0.5f);
    }
    glEnd();

    // two leaves
    for (int k = 0; k < 2; k++) {
        float side = (k == 0) ? 1.0f : -1.0f;
        Point lp = bezier(p0, p1, p2, p3, 0.28f + 0.25f * k);
        float ls = (0.07f + 1.2f * f.scale) * stemG;
        glPushMatrix();
        glTranslatef(lp.x, lp.y, 0.0f);
        glRotatef(-side * 55.0f + wind * 5.0f, 0, 0, 1);
        glScalef(ls, ls, 1.0f);
        drawLeaf();
        glPopMatrix();
    }

    // head
    if (headG > 0.01f) {
        float tilt = (stemLen > 0.001f) ? -off / stemLen * 40.0f : 0.0f;
        float sc = f.scale * headG;
        glPushMatrix();
        glTranslatef(p3.x, p3.y, 0.0f);
        glRotatef(tilt + (f.phase - 3.14f) * 6.0f, 0, 0, 1);
        glScalef(sc, sc, 1.0f);
        drawFlowerHead(f);
        glPopMatrix();
    }
}

static bool flowerFarther(const Flower& a, const Flower& b) { return a.gy > b.gy; }

// ----------------------------------------------------------------------------
//  Butterflies & fireflies
// ----------------------------------------------------------------------------
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

static void drawButterflies() {
    float alpha = sstep(0.25f, 0.6f, sky.day);
    if (alpha < 0.02f) return;
    for (int i = 0; i < 3; i++) {
        const Butterfly& b = BFLY[i];
        float a1 = animTime * b.speed + b.ph;
        float x  = b.ax * std::sin(a1);
        float y  = b.cy + b.ay * std::sin(a1 * 1.9f + b.ph * 2.0f) + 0.015f * std::sin(animTime * 7.0f + b.ph);
        float dx = b.ax * b.speed * std::cos(a1);
        float flap = std::fabs(std::cos(animTime * (9.0f + b.ph))) * 0.8f + 0.2f;

        glPushMatrix();
        glTranslatef(x, y, 0.0f);
        glRotatef(clampf(-dx * 120.0f, -25.0f, 25.0f), 0, 0, 1);
        glScalef(b.size, b.size, 1.0f);
        for (int side = -1; side <= 1; side += 2) {
            glPushMatrix();
            glScalef(side * flap, 1.0f, 1.0f);
            drawWingShape(UPPER, 5, tint(b.c1), tint(b.c2), alpha);
            drawWingShape(LOWER, 5, tint(mix(b.c1, b.c2, 0.25f)), tint(b.c2), alpha);
            setColor(tint(b.c2), alpha);
            disc(0.16f, 0.09f, 0.03f, 0.03f, 12);
            glPopMatrix();
        }
        // body + antennae
        setColor(tint(Color{0.12f, 0.08f, 0.05f}), alpha);
        disc(0.0f, 0.0f, 0.018f, 0.085f, 14);
        glLineWidth(1.0f);
        glBegin(GL_LINES);
            glVertex2f(0.0f, 0.08f); glVertex2f(-0.05f, 0.16f);
            glVertex2f(0.0f, 0.08f); glVertex2f( 0.05f, 0.16f);
        glEnd();
        glPopMatrix();
    }
}

struct Fly { float x, y, ph; };
static std::vector<Fly> flies;

static void drawFireflies() {
    float n = nightness();
    if (n < 0.02f) return;
    for (size_t i = 0; i < flies.size(); i++) {
        const Fly& f = flies[i];
        float x = f.x + std::sin(animTime * 0.5f + f.ph) * 0.08f;
        float y = f.y + std::cos(animTime * 0.7f + f.ph * 1.3f) * 0.06f;
        float pulse = 0.5f + 0.5f * std::sin(animTime * 2.5f + f.ph * 3.0f);
        float a = n * pulse;
        glowDisc(x, y, 0.045f, Color{0.9f, 1.0f, 0.35f}, 0.85f * a);
        setColor(Color{1.0f, 1.0f, 0.7f}, a);
        disc(x, y, 0.004f, 0.004f, 8);
    }
}

// ----------------------------------------------------------------------------
//  Commuter road: street lamps, bus, car
// ----------------------------------------------------------------------------
static const float ROAD_Y0 = -0.96f, ROAD_Y1 = -0.66f;
static const float VEH_SCALE = 1.9f;   // car and lamps are drawn this much bigger
static const float BUS_SCALE = 2.6f;   // the bus is the star of the show

static void drawRoad() {
    // curbs
    setColor(tint(Color{0.62f, 0.62f, 0.60f}));
    rect(-5.0f, ROAD_Y1, 5.0f, ROAD_Y1 + 0.012f);
    rect(-5.0f, ROAD_Y0 - 0.012f, 5.0f, ROAD_Y0);
    // asphalt
    glBegin(GL_QUADS);
        setColor(tint(Color{0.20f, 0.21f, 0.24f})); glVertex2f(-5.0f, ROAD_Y0); glVertex2f(5.0f, ROAD_Y0);
        setColor(tint(Color{0.27f, 0.28f, 0.31f})); glVertex2f(5.0f, ROAD_Y1);  glVertex2f(-5.0f, ROAD_Y1);
    glEnd();
    // dashed centre line
    float cy = (ROAD_Y0 + ROAD_Y1) * 0.5f;
    setColor(tint(Color{0.95f, 0.90f, 0.50f}));
    for (float x = -5.0f; x < 5.0f; x += 0.50f) rect(x, cy - 0.006f, x + 0.20f, cy + 0.006f);
}

static void drawLamp(float x, float yBase, float night) {
    glPushMatrix();
    glTranslatef(x, yBase, 0.0f);
    glScalef(VEH_SCALE, VEH_SCALE, 1.0f);
    glTranslatef(-x, -yBase, 0.0f);
    setColor(tint(Color{0.25f, 0.26f, 0.28f}));
    rect(x - 0.006f, yBase, x + 0.006f, yBase + 0.32f);
    rect(x, yBase + 0.31f, x + 0.06f, yBase + 0.322f);
    setColor(mix(tint(Color{0.55f, 0.55f, 0.55f}), Color{1.0f, 0.88f, 0.55f}, night));
    rect(x + 0.045f, yBase + 0.30f, x + 0.085f, yBase + 0.312f);
    glowDisc(x + 0.065f, yBase + 0.30f, 0.24f, Color{1.0f, 0.85f, 0.5f}, 0.45f * night);
    glPopMatrix();
}

static void headlightCone(float x, float y, float len, float spread, float a) {
    if (a <= 0.003f) return;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLES);
        glColor4f(1.0f, 0.95f, 0.7f, a); glVertex2f(x, y);
        glColor4f(1.0f, 0.95f, 0.7f, 0.0f);
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

static void drawStar(float cx, float cy, float r1, float r2) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 10; i++) {
        float r = (i % 2 == 0) ? r1 : r2;
        float a = PI / 2.0f + i * PI / 5.0f;
        glVertex2f(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    glEnd();
}

static void drawSparkle(float x, float y, float s, float a) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(1.0f, 1.0f, 1.0f, a);
    glVertex2f(x, y);
    glColor4f(1.0f, 0.8f, 1.0f, 0.0f);
    glVertex2f(x, y + s);           glVertex2f(x + s * 0.18f, y + s * 0.18f);
    glVertex2f(x + s, y);           glVertex2f(x + s * 0.18f, y - s * 0.18f);
    glVertex2f(x, y - s);           glVertex2f(x - s * 0.18f, y - s * 0.18f);
    glVertex2f(x - s, y);           glVertex2f(x - s * 0.18f, y + s * 0.18f);
    glVertex2f(x, y + s);
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
    setColor(Color{0, 0, 0}, 0.18f);
    disc(0.0f, 0.0f, 0.16f, 0.03f, 12);
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
    Color skin = SKINS[id % 5], hair = HAIRS[(id * 3 + 1) % 5];
    Color shirt = SHIRTS[(id * 5 + 2) % 7], pants = PANTS[(id * 7 + 3) % 4];
    Color frame = tint(Color{0.85f, 0.15f, 0.20f});
    setColor(Color{0, 0, 0}, 0.18f); disc(0.0f, 0.0f, 0.5f, 0.03f, 16);
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

// ---- oblique "3D" helpers for the bus -------------------------------------
static void frontQuad(float w, float ox, float oy, float u0, float v0, float u1, float v1) {
    quad4(w + ox * u0, 0.040f + oy * u0 + 0.128f * v0, w + ox * u1, 0.040f + oy * u1 + 0.128f * v0,
          w + ox * u1, 0.040f + oy * u1 + 0.128f * v1, w + ox * u0, 0.040f + oy * u0 + 0.128f * v1);
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
    glRotatef(-animTime * 140.0f, 0, 0, 1);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    setColor(tint(Color{0.30f, 0.30f, 0.38f}));
    for (int k = 0; k < 5; k++) {
        float a = 2.0f * PI * k / 5.0f;
        glVertex2f(0, 0); glVertex2f(0.020f * std::cos(a), 0.020f * std::sin(a));
    }
    glEnd();
    glPopMatrix();
    setColor(tint(Color{1.0f, 0.85f, 0.30f}));   disc(x, 0.032f, 0.006f, 0.006f, 10);   // gold hub
}

static void drawBus(float night) {
    const float w = 0.25f;
    float bob = std::sin(animTime * 9.0f) * 0.0025f;       // little suspension bounce
    Color outline = tint(Color{0.30f, 0.06f, 0.38f});
    Color gold = tint(Color{1.0f, 0.85f, 0.30f});
    float gloss = 0.28f * (0.4f + 0.6f * sky.day);

    setColor(Color{0, 0, 0}, 0.28f);                        // ground shadow
    disc(0.0f, 0.004f, 0.28f, 0.018f, 24);

    glPushMatrix();
    glTranslatef(0.0f, bob, 0.0f);

    // underbody + wheels behind the body
    setColor(tint(Color{0.15f, 0.10f, 0.20f}));
    rect(-w + 0.02f, 0.02f, w - 0.02f, 0.05f);
    drawBusWheel(-0.15f);
    drawBusWheel( 0.15f);

    // 3D look: roof face + front face drawn in oblique projection
    const float ox = 0.034f, oy = 0.022f;
    setColor(tint(Color{0.97f, 0.90f, 1.00f}));
    quad4(-w + 0.010f, 0.168f, w - 0.004f, 0.168f, w + ox, 0.168f + oy, -w + 0.010f + ox, 0.168f + oy);
    setColor(tint(Color{0.66f, 0.12f, 0.48f}));
    quad4(w, 0.040f, w + ox, 0.040f + oy, w + ox, 0.168f + oy, w, 0.168f);
    setColor(mix(tint(Color{0.55f, 0.78f, 0.95f}), Color{1.0f, 0.9f, 0.55f}, night));
    frontQuad(w, ox, oy, 0.12f, 0.45f, 0.88f, 0.92f);                    // windscreen seen from the front
    setColor(Color{1.0f, 0.97f, 0.75f});
    frontQuad(w, ox, oy, 0.14f, 0.10f, 0.34f, 0.26f);                    // headlights
    frontQuad(w, ox, oy, 0.66f, 0.10f, 0.86f, 0.26f);

    // body: pink gradient, purple skirt, gold trim, roof
    roundRect(-w, 0.035f, w, 0.168f, 0.022f, tint(Color{0.82f, 0.18f, 0.58f}), tint(Color{1.00f, 0.50f, 0.80f}));
    roundRect(-w, 0.035f, w, 0.072f, 0.018f, tint(Color{0.35f, 0.15f, 0.65f}), tint(Color{0.55f, 0.30f, 0.85f}));
    setColor(gold); rect(-w + 0.004f, 0.072f, w - 0.004f, 0.076f);
    roundRect(-w + 0.015f, 0.160f, w - 0.015f, 0.179f, 0.008f, tint(Color{0.80f, 0.70f, 0.95f}), tint(Color{1.00f, 0.95f, 1.00f}));
    roundRect(-0.10f, 0.178f, 0.05f, 0.194f, 0.006f, tint(Color{0.55f, 0.55f, 0.65f}), tint(Color{0.85f, 0.85f, 0.92f}));   // roof AC

    // star decals on the skirt
    setColor(gold);
    for (int i = 0; i < 4; i++) drawStar(-0.17f + i * 0.10f, 0.054f, 0.013f, 0.0055f);

    // windows (with passengers!)
    Color wTop = mix(tint(Color{0.78f, 0.94f, 1.00f}), Color{1.0f, 0.92f, 0.60f}, night);
    Color wBot = mix(tint(Color{0.30f, 0.66f, 0.88f}), Color{1.0f, 0.70f, 0.30f}, night);
    for (int i = 0; i < 5; i++) {
        float x0 = -0.225f + i * 0.064f;
        roundRect(x0, 0.092f, x0 + 0.05f, 0.148f, 0.008f, wBot, wTop);
        if (hash1(i * 7 + 3) < 0.85f) drawPassenger(x0 + 0.025f, 0.092f, i * 3 + 1);
        if (hash1(i * 7 + 9) < 0.35f) drawPassenger(x0 + 0.040f, 0.092f, i * 5 + 2, 0.8f);   // a second, smaller passenger
        roundRectEdge(x0, 0.092f, x0 + 0.05f, 0.148f, 0.008f, outline, 1.5f);
        setColor(Color{1, 1, 1}, 0.30f);                     // reflection streak
        glBegin(GL_QUADS);
            glVertex2f(x0 + 0.008f, 0.095f); glVertex2f(x0 + 0.020f, 0.095f);
            glVertex2f(x0 + 0.034f, 0.145f); glVertex2f(x0 + 0.022f, 0.145f);
        glEnd();
    }
    // double door
    for (int k = 0; k < 2; k++) {
        float x0 = 0.100f + k * 0.027f;
        roundRect(x0, 0.045f, x0 + 0.023f, 0.148f, 0.004f, wBot, wTop);
        roundRectEdge(x0, 0.045f, x0 + 0.023f, 0.148f, 0.004f, outline, 1.5f);
    }
    // driver's windscreen
    roundRect(0.170f, 0.060f, 0.236f, 0.155f, 0.012f, wBot, wTop);
    drawPassenger(0.200f, 0.072f, 17);                                   // the driver
    setColor(tint(Color{0.15f, 0.12f, 0.20f})); glLineWidth(2.0f);
    glBegin(GL_LINES); glVertex2f(0.222f, 0.085f); glVertex2f(0.230f, 0.102f); glEnd();
    roundRectEdge(0.170f, 0.060f, 0.236f, 0.155f, 0.012f, outline, 1.5f);
    setColor(Color{1, 1, 1}, 0.30f);
    glBegin(GL_QUADS);
        glVertex2f(0.178f, 0.066f); glVertex2f(0.194f, 0.066f);
        glVertex2f(0.214f, 0.150f); glVertex2f(0.198f, 0.150f);
    glEnd();

    // glossy highlight band + outline
    glBegin(GL_QUADS);
        glColor4f(1, 1, 1, 0.0f);   glVertex2f(-w + 0.02f, 0.120f); glVertex2f(w - 0.02f, 0.120f);
        glColor4f(1, 1, 1, gloss);  glVertex2f(w - 0.02f, 0.158f);  glVertex2f(-w + 0.02f, 0.158f);
    glEnd();
    roundRectEdge(-w, 0.035f, w, 0.168f, 0.022f, outline, 2.0f);

    // lights, bumpers, mirror
    setColor(tint(Color{0.20f, 0.18f, 0.25f}));
    rect(w - 0.005f, 0.030f, w + 0.010f, 0.050f);
    rect(-w - 0.010f, 0.030f, -w + 0.005f, 0.050f);
    setColor(Color{1.0f, 0.97f, 0.75f}); roundRect(w - 0.012f, 0.056f, w + 0.006f, 0.074f, 0.004f, Color{1.0f, 0.9f, 0.5f}, Color{1.0f, 1.0f, 0.9f});
    setColor(Color{0.95f, 0.10f, 0.15f}); rect(-w - 0.006f, 0.056f, -w + 0.008f, 0.076f);
    setColor(tint(Color{0.15f, 0.10f, 0.20f})); disc(w + 0.006f, 0.135f, 0.006f, 0.011f, 10);
    glowDisc(w, 0.065f, 0.05f, Color{1.0f, 0.9f, 0.55f}, 0.20f + 0.35f * night);
    glowDisc(-w, 0.066f, 0.05f, Color{1.0f, 0.1f, 0.1f}, 0.20f + 0.35f * night);

    // magical sparkles around the bus
    static const float SP[4][3] = {{-0.20f, 0.23f, 0.0f}, {0.10f, 0.25f, 1.7f}, {0.30f, 0.14f, 3.1f}, {-0.30f, 0.10f, 4.6f}};
    for (int i = 0; i < 4; i++) {
        float a = 0.5f + 0.5f * std::sin(animTime * 4.0f + SP[i][2]);
        drawSparkle(SP[i][0], SP[i][1], 0.035f * (0.6f + 0.6f * a), 0.4f + 0.6f * a);
    }
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
    setColor(Color{0, 0, 0}, 0.25f); disc(0.0f, 0.003f, L * 1.15f, 0.012f, 18);       // shadow

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

struct Vehicle { int type; Color col; float speed; float offset; };   // type -1 = bus
static const Vehicle LANE_UP[3] = {                                   // upper lane drives right -> left
    {0, {0.15f, 0.35f, 0.80f}, 0.28f, 0.0f}, {3, {1.00f, 0.78f, 0.10f}, 0.28f, 2.4f}, {4, {0.85f, 0.12f, 0.15f}, 0.28f, 4.8f}
};
static const Vehicle LANE_DOWN[3] = {                                 // lower lane drives left -> right
    {-1, {0, 0, 0}, 0.18f, 1.0f}, {2, {0.92f, 0.92f, 0.95f}, 0.18f, 3.4f}, {1, {0.25f, 0.65f, 0.40f}, 0.18f, 5.4f}
};

static void drawVehicle(const Vehicle& v, bool rightMoving, float baseY, float n) {
    float run = std::fmod(animTime * v.speed + v.offset, 7.0f);
    float x = rightMoving ? run - 3.5f : 3.5f - run;
    if (std::fabs(x) > aspect + 1.8f) return;
    float sc = (v.type < 0) ? BUS_SCALE : VEH_SCALE;
    float dir = rightMoving ? 1.0f : -1.0f;
    glPushMatrix();
    glTranslatef(x, baseY, 0.0f);
    glScalef(dir * sc, sc, 1.0f);
    if (v.type < 0) {
        drawBus(n);
        headlightCone(0.25f, 0.065f, 0.45f, 0.06f, 0.45f * n);
    } else {
        float front = drawCarType(v.type, v.col, n);
        headlightCone(front, 0.048f, 0.30f, 0.04f, 0.45f * n);
    }
    glPopMatrix();
}

// ---- sidewalk with walkers and cyclists ----------------------------------------
static const float SW_Y0 = -0.648f, SW_Y1 = -0.600f;

static void drawSidewalk() {
    glBegin(GL_QUADS);
        setColor(tint(Color{0.66f, 0.65f, 0.62f})); glVertex2f(-5.0f, SW_Y0); glVertex2f(5.0f, SW_Y0);
        setColor(tint(Color{0.78f, 0.77f, 0.74f})); glVertex2f(5.0f, SW_Y1);  glVertex2f(-5.0f, SW_Y1);
    glEnd();
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    setColor(tint(Color{0.50f, 0.50f, 0.48f}), 0.6f);
    for (float x = -5.0f; x < 5.0f; x += 0.35f) { glVertex2f(x, SW_Y0); glVertex2f(x + 0.04f, SW_Y1); }
    glEnd();
}

struct Walker { float x0, speed; int dir; int id; float y, scale; };
static const Walker WALKERS[6] = {
    {-2.0f, 0.10f,  1,  1, -0.630f, 0.17f}, { 1.5f, 0.07f, -1,  4, -0.640f, 0.17f},
    { 0.2f, 0.12f,  1,  7, -0.625f, 0.16f}, {-0.9f, 0.06f, -1, 10, -0.635f, 0.17f},
    { 2.6f, 0.08f, -1, 13, -0.630f, 0.17f}, { 2.46f, 0.08f, -1, 16, -0.628f, 0.11f}   // mother + child
};
static const Walker CYCLISTS[2] = {
    {-1.0f, 0.22f, 1, 2, -0.620f, 0.20f}, { 2.0f, 0.16f, -1, 5, -0.640f, 0.20f}
};

static void drawCommuters() {
    float n = nightness();
    drawSidewalk();

    static const float LAMPX[4] = {-1.6f, -0.35f, 0.55f, 1.75f};
    for (int i = 0; i < 4; i++) drawLamp(LAMPX[i], SW_Y0 + 0.02f, n);

    for (int i = 0; i < 6; i++) {                                         // pedestrians
        const Walker& w = WALKERS[i];
        float x = std::fmod(w.x0 + 3.5f + w.dir * w.speed * animTime, 7.0f);
        if (x < 0.0f) x += 7.0f;
        x -= 3.5f;
        if (std::fabs(x) > aspect + 0.3f) continue;
        float phase = animTime * w.speed * 52.0f * (0.17f / w.scale);
        float bounce = std::fabs(std::sin(phase)) * 0.008f * (w.scale / 0.17f);
        glPushMatrix();
        glTranslatef(x, w.y + bounce, 0.0f);
        glScalef(w.dir * w.scale, w.scale, 1.0f);
        drawPerson(w.id, phase, 28.0f, 0.0f);
        glPopMatrix();
    }
    for (int i = 0; i < 2; i++) {                                         // cyclists
        const Walker& w = CYCLISTS[i];
        float x = std::fmod(w.x0 + 3.5f + w.dir * w.speed * animTime, 7.0f);
        if (x < 0.0f) x += 7.0f;
        x -= 3.5f;
        if (std::fabs(x) > aspect + 0.4f) continue;
        float wheelDeg = -(w.speed * animTime / (0.22f * w.scale)) * 57.2958f;
        glPushMatrix();
        glTranslatef(x, w.y, 0.0f);
        glScalef(w.dir * w.scale, w.scale, 1.0f);
        drawCyclist(w.id, wheelDeg, animTime * 7.0f);
        glPopMatrix();
    }

    drawRoad();
    for (int i = 0; i < 3; i++) drawVehicle(LANE_UP[i], false, -0.80f, n);
    for (int i = 0; i < 3; i++) drawVehicle(LANE_DOWN[i], true, -0.93f, n);
}

// ---- park life: pond, picnic, kids, dog, kite, birds -----------------------------
static void drawPond() {
    const float cx = 1.12f, cy = -0.51f, rx = 0.40f, ry = 0.075f;
    if (cx - rx - 0.1f > aspect) return;
    float t = animTime, dayK = sky.day;

    setColor(tint(Color{0.34f, 0.50f, 0.20f})); disc(cx, cy - 0.004f, rx + 0.055f, ry + 0.024f, 48);   // grassy bank
    setColor(tint(Color{0.52f, 0.44f, 0.28f})); disc(cx, cy - 0.002f, rx + 0.024f, ry + 0.011f, 48);   // muddy rim

    Color deep = mix(tint(Color{0.10f, 0.32f, 0.48f}), sky.top, 0.30f);
    Color shallow = mix(tint(Color{0.25f, 0.58f, 0.62f}), sky.horizon, 0.50f);
    glBegin(GL_TRIANGLE_FAN);                                                                          // water
    setColor(deep); glVertex2f(cx, cy - 0.012f);
    setColor(shallow);
    for (int i = 0; i <= 48; i++) { float a = 2.0f * PI * i / 48; glVertex2f(cx + rx * std::cos(a), cy + ry * std::sin(a)); }
    glEnd();

    glLineWidth(1.5f);                                                                                 // shimmer
    glBegin(GL_LINES);
    for (int k = 0; k < 10; k++) {
        float v = (hash1(k + 600) - 0.5f) * 1.5f;
        float hw = rx * std::sqrt(1.0f - v * v);
        float mx = cx + (hash1(k + 620) - 0.5f) * hw * 1.2f + std::sin(t * 0.8f + k) * 0.03f;
        float yy = cy + v * ry;
        float al = (0.18f + 0.15f * std::sin(t * 1.6f + k * 1.7f)) * (0.3f + 0.7f * dayK);
        glColor4f(1.0f, 1.0f, 1.0f, al);
        glVertex2f(mx - 0.035f, yy); glVertex2f(mx + 0.035f, yy);
    }
    glEnd();

    for (int k = 0; k < 3; k++) {                                                                      // ripples
        float p = std::fmod(t * 0.35f + k * 0.33f, 1.0f);
        float rr = 0.01f + p * 0.07f;
        float rcx = cx - 0.12f + k * 0.16f, rcy = cy + 0.012f * std::sin(k * 2.0f);
        glLineWidth(1.2f);
        glBegin(GL_LINE_LOOP);
        glColor4f(1.0f, 1.0f, 1.0f, (1.0f - p) * 0.45f);
        for (int i = 0; i < 24; i++) { float a = 2.0f * PI * i / 24; glVertex2f(rcx + rr * std::cos(a), rcy + rr * 0.35f * std::sin(a)); }
        glEnd();
    }
    if (dayK > 0.4f) glowDisc(cx + 0.10f, cy + 0.01f, 0.09f, Color{1.0f, 1.0f, 0.9f}, 0.14f * dayK);     // sun glint
    float nn = nightness();
    if (nn > 0.1f) glowDisc(cx - 0.05f, cy + 0.02f, 0.07f, Color{0.8f, 0.9f, 1.0f}, 0.20f * nn);         // moon glint

    static const float PADS[4][2] = {{-0.18f, 0.020f}, {-0.05f, -0.030f}, {0.20f, 0.025f}, {0.28f, -0.020f}};
    for (int k = 0; k < 4; k++) {                                                                      // lily pads
        float px = cx + PADS[k][0], py = cy + PADS[k][1];
        setColor(tint(Color{0.14f, 0.45f, 0.18f})); disc(px, py, 0.034f, 0.012f, 16);
        setColor(tint(Color{0.24f, 0.60f, 0.24f})); disc(px, py + 0.002f, 0.026f, 0.008f, 16);
        if (k == 2) {
            setColor(tint(Color{1.0f, 0.65f, 0.80f})); disc(px, py + 0.010f, 0.012f, 0.008f, 10);
            setColor(tint(Color{1.0f, 0.90f, 0.30f})); disc(px, py + 0.011f, 0.004f, 0.003f, 8);
        }
    }

    for (int d = 0; d < 2; d++) {                                                                      // ducks
        float ang = t * 0.18f + d * 2.4f;
        float dx = cx + rx * 0.55f * std::sin(ang);
        float dir = std::cos(ang) >= 0.0f ? 1.0f : -1.0f;
        float dy = cy + (d == 0 ? 0.018f : -0.022f);
        float bob = std::sin(t * 2.0f + d) * 0.0015f;
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        glColor4f(1.0f, 1.0f, 1.0f, 0.25f * (0.3f + 0.7f * dayK));
        glVertex2f(dx - dir * 0.02f, dy - 0.004f); glVertex2f(dx - dir * 0.06f, dy + 0.004f);
        glVertex2f(dx - dir * 0.02f, dy - 0.006f); glVertex2f(dx - dir * 0.06f, dy - 0.014f);
        glEnd();
        Color bodyC = (d == 0) ? Color{0.97f, 0.97f, 0.95f} : Color{0.60f, 0.42f, 0.25f};
        setColor(tint(bodyC)); disc(dx, dy + bob + 0.008f, 0.027f, 0.013f, 16);
        glBegin(GL_TRIANGLES);
            glVertex2f(dx - dir * 0.024f, dy + 0.008f + bob); glVertex2f(dx - dir * 0.036f, dy + 0.022f + bob); glVertex2f(dx - dir * 0.014f, dy + 0.014f + bob);
        glEnd();
        setColor(tint(Color{0.10f, 0.45f, 0.25f})); disc(dx + dir * 0.022f, dy + 0.024f + bob, 0.009f, 0.009f, 12);
        setColor(tint(Color{1.0f, 0.65f, 0.10f}));
        glBegin(GL_TRIANGLES);
            glVertex2f(dx + dir * 0.030f, dy + 0.026f + bob); glVertex2f(dx + dir * 0.041f, dy + 0.022f + bob); glVertex2f(dx + dir * 0.030f, dy + 0.020f + bob);
        glEnd();
        setColor(Color{0.05f, 0.05f, 0.05f}); disc(dx + dir * 0.025f, dy + 0.026f + bob, 0.0015f, 0.0015f, 6);
    }

    glLineWidth(2.0f);                                                                                 // reeds + cattails
    for (int k = 0; k < 12; k++) {
        float side = (k < 6) ? -1.0f : 1.0f;
        float bx = cx + side * (rx + 0.005f + 0.035f * hash1(k + 500));
        float by = cy + 0.02f * hash1(k + 510);
        float hgt = 0.09f + 0.07f * hash1(k + 520);
        float sway = std::sin(t * 1.4f + k) * 0.008f;
        glBegin(GL_LINES);
        setColor(tint(Color{0.20f, 0.45f, 0.15f})); glVertex2f(bx, by);
        setColor(tint(Color{0.45f, 0.65f, 0.25f})); glVertex2f(bx + sway - side * 0.01f, by + hgt);
        glEnd();
        if (k % 2 == 0) { setColor(tint(Color{0.40f, 0.25f, 0.12f})); disc(bx + sway - side * 0.01f, by + hgt, 0.005f, 0.014f, 8); }
    }
    for (int k = 0; k < 9; k++) {                                                                      // stones on the rim
        float a = PI + 0.3f + k * 0.3f;
        setColor(tint(Color{0.55f, 0.55f, 0.55f}));
        disc(cx + (rx + 0.02f) * std::cos(a), cy + (ry + 0.008f) * std::sin(a) - 0.004f, 0.011f, 0.007f, 8);
    }
}

static void drawPicnic(float bx, float by) {
    Color red = tint(Color{0.85f, 0.15f, 0.18f}), white = tint(Color{0.96f, 0.96f, 0.96f});

    float px = bx - 0.24f;                                                   // parasol
    setColor(tint(Color{0.55f, 0.42f, 0.28f})); rect(px - 0.003f, by, px + 0.003f, by + 0.28f);
    float pcx = px + 0.02f, pcy = by + 0.25f;
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 6; i++) {
        float a0 = PI * i / 6.0f, a1 = PI * (i + 1) / 6.0f;
        setColor((i % 2) ? white : red);
        glVertex2f(pcx, pcy);
        glVertex2f(pcx + 0.17f * std::cos(a0), pcy + 0.09f * std::sin(a0));
        glVertex2f(pcx + 0.17f * std::cos(a1), pcy + 0.09f * std::sin(a1));
    }
    glEnd();

    const int N = 10;                                                        // blanket (perspective trapezoid, striped)
    for (int i = 0; i < N; i++) {
        float u0 = i / (float)N, u1 = (i + 1) / (float)N;
        setColor((i % 2) ? white : red);
        quad4(lerpf(bx - 0.30f, bx + 0.30f, u0), by - 0.03f, lerpf(bx - 0.30f, bx + 0.30f, u1), by - 0.03f,
              lerpf(bx - 0.23f, bx + 0.23f, u1), by + 0.035f, lerpf(bx - 0.23f, bx + 0.23f, u0), by + 0.035f);
    }
    for (int k = 1; k <= 2; k++) {                                           // cross stripes -> gingham look
        float v = k / 3.0f;
        float yy = lerpf(by - 0.03f, by + 0.035f, v);
        float xl = lerpf(bx - 0.30f, bx - 0.23f, v), xr = lerpf(bx + 0.30f, bx + 0.23f, v);
        setColor(Color{0.55f, 0.05f, 0.10f}, 0.22f);
        quad4(xl, yy - 0.005f, xr, yy - 0.005f, xr, yy + 0.005f, xl, yy + 0.005f);
    }

    setColor(tint(Color{0.62f, 0.42f, 0.20f}));                               // basket + goodies
    rect(bx + 0.065f, by + 0.002f, bx + 0.125f, by + 0.040f);
    setColor(tint(Color{0.45f, 0.28f, 0.12f}));
    rect(bx + 0.065f, by + 0.018f, bx + 0.125f, by + 0.021f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 10; i++) { float a = PI * i / 10.0f; glVertex2f(bx + 0.095f + 0.03f * std::cos(a), by + 0.040f + 0.030f * std::sin(a)); }
    glEnd();
    setColor(white); rect(bx + 0.068f, by + 0.040f, bx + 0.122f, by + 0.046f);
    setColor(tint(Color{0.90f, 0.15f, 0.15f})); disc(bx - 0.045f, by + 0.004f, 0.009f, 0.007f, 8);       // apples
    setColor(tint(Color{0.50f, 0.80f, 0.20f})); disc(bx - 0.028f, by + 0.002f, 0.009f, 0.007f, 8);

    glPushMatrix(); glTranslatef(bx + 0.20f, by + 0.010f, 0.0f); glScalef(-0.15f, 0.15f, 1.0f); drawSitting(6, 160.0f + 12.0f * std::sin(animTime * 4.0f)); glPopMatrix();   // adult, waving
    glPushMatrix(); glTranslatef(bx - 0.16f, by + 0.000f, 0.0f); glScalef(0.15f, 0.15f, 1.0f);  drawSitting(3, 15.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(bx + 0.03f, by - 0.012f, 0.0f); glScalef(0.10f, 0.10f, 1.0f);  drawSitting(9, 150.0f + 20.0f * std::sin(animTime * 6.0f)); glPopMatrix(); // child waving
}

static void drawKiteKid(float cx, float cy) {
    float t = animTime;
    float kx = cx + 0.28f + 0.10f * std::sin(t * 0.7f);
    float ky = 0.34f + 0.05f * std::sin(t * 1.3f);
    float hx = cx + 0.05f, hy = cy + 0.105f;
    float mx = (hx + kx) * 0.5f, my = (hy + ky) * 0.5f - 0.10f;
    glLineWidth(1.0f);
    glBegin(GL_LINE_STRIP);
    setColor(tint(Color{0.95f, 0.95f, 0.95f}), 0.8f);
    for (int i = 0; i <= 24; i++) {                                           // sagging kite string
        float u = i / 24.0f;
        float ax = lerpf(hx, mx, u), ay = lerpf(hy, my, u), bx2 = lerpf(mx, kx, u), by2 = lerpf(my, ky, u);
        glVertex2f(lerpf(ax, bx2, u), lerpf(ay, by2, u));
    }
    glEnd();
    glPushMatrix();
    glTranslatef(kx, ky, 0.0f);
    glRotatef(12.0f * std::sin(t * 1.1f), 0, 0, 1);
    Color kr = tint(Color{0.95f, 0.25f, 0.30f}), ky2 = tint(Color{1.0f, 0.85f, 0.15f});
    glBegin(GL_TRIANGLES);
        setColor(kr);  glVertex2f(0, 0.07f);  glVertex2f(-0.045f, 0); glVertex2f(0, 0);
        setColor(ky2); glVertex2f(0, 0.07f);  glVertex2f(0.045f, 0);  glVertex2f(0, 0);
        setColor(ky2); glVertex2f(0, -0.09f); glVertex2f(-0.045f, 0); glVertex2f(0, 0);
        setColor(kr);  glVertex2f(0, -0.09f); glVertex2f(0.045f, 0);  glVertex2f(0, 0);
    glEnd();
    glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    setColor(kr);
    for (int i = 0; i <= 8; i++) glVertex2f(0.014f * std::sin(t * 5.0f + i * 0.8f), -0.09f - i * 0.016f);
    glEnd();
    for (int i = 2; i <= 6; i += 2) {
        setColor((i % 4 == 0) ? ky2 : kr);
        disc(0.014f * std::sin(t * 5.0f + i * 0.8f), -0.09f - i * 0.016f, 0.008f, 0.005f, 8);
    }
    glPopMatrix();
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glScalef(0.105f, 0.105f, 1.0f);
    drawPerson(11, 0.0f, 0.0f, 150.0f + 12.0f * std::sin(t * 3.0f));
    glPopMatrix();
}

static void drawJumper(float x, float y) {
    float j = std::fabs(std::sin(animTime * 4.5f));
    setColor(Color{0, 0, 0}, 0.20f * (1.0f - j * 0.5f)); disc(x, y, 0.04f, 0.008f, 12);
    glPushMatrix();
    glTranslatef(x, y + j * 0.05f, 0.0f);
    glScalef(0.105f, 0.105f, 1.0f);
    drawPerson(14, animTime * 9.0f, 25.0f * j, 140.0f + 20.0f * std::sin(animTime * 9.0f));
    glPopMatrix();
}

static void drawRunner(float y) {                                              // kid chasing a ball, dog chasing the kid
    float tr = animTime * 0.5f;
    float kx = -0.10f + 0.40f * std::sin(tr);
    float dirK = std::cos(tr) >= 0.0f ? 1.0f : -1.0f;
    float ph = animTime * 14.0f;
    glPushMatrix();
    glTranslatef(kx - dirK * 0.22f, y + std::fabs(std::sin(ph)) * 0.012f, 0.0f);
    glScalef(dirK * 0.28f, 0.28f, 1.0f);
    drawDog(ph + 1.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(kx, y + std::fabs(std::sin(ph)) * 0.01f, 0.0f);
    glScalef(dirK * 0.105f, 0.105f, 1.0f);
    drawPerson(8, ph, 42.0f, 25.0f);
    glPopMatrix();
    float bx = kx + dirK * 0.16f, by = y + 0.016f + std::fabs(std::sin(animTime * 7.0f)) * 0.05f;
    setColor(Color{0, 0, 0}, 0.2f); disc(bx, y, 0.014f, 0.004f, 8);
    setColor(tint(Color{0.95f, 0.30f, 0.25f})); disc(bx, by, 0.016f, 0.016f, 12);
    setColor(tint(Color{1.0f, 1.0f, 1.0f}), 0.7f); disc(bx - 0.004f, by + 0.005f, 0.005f, 0.005f, 8);
}

// drawn back-to-front so the kids overlap correctly
static void drawLawnLife() {
    drawKiteKid(-0.20f, -0.47f);
    drawJumper(0.50f, -0.50f);
    drawPicnic(-0.95f, -0.535f);
    drawRunner(-0.585f);
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

// ----------------------------------------------------------------------------
//  HUD
// ----------------------------------------------------------------------------
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

static void drawHUD() {
    int h = (int)hourNow;
    int m = (int)((hourNow - h) * 60.0f);
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d %s - %s", h12, m, h < 12 ? "AM" : "PM", periodName());

    Color txt = sky.day > 0.35f ? Color{0.08f, 0.08f, 0.10f} : Color{1.0f, 1.0f, 1.0f};
    Color shadow = sky.day > 0.35f ? Color{1.0f, 1.0f, 1.0f} : Color{0.0f, 0.0f, 0.0f};
    float x = -aspect + 0.06f;
    drawText(x + 0.004f, 0.896f, GLUT_BITMAP_HELVETICA_18, buf, shadow);
    drawText(x, 0.90f, GLUT_BITMAP_HELVETICA_18, buf, txt);
    drawText(x, 0.84f, GLUT_BITMAP_HELVETICA_12,
             "[1-6] time  [Space] pause  [+/-] speed  [C] city life  [R] replay bloom", txt);
}

// ----------------------------------------------------------------------------
//  Scene setup
// ----------------------------------------------------------------------------
static void initScene() {
    buildShapes();

    // stars
    for (int i = 0; i < 140; i++) {
        Star s;
        s.x = -4.0f + 8.0f * hash1(i * 3 + 1);
        s.y = 0.10f + 0.90f * hash1(i * 3 + 2);
        s.ph = hash1(i * 3 + 3) * 6.28f;
        s.size = 1.0f + 1.6f * hash1(i * 5 + 7);
        stars.push_back(s);
    }

    // flower field: 6 rows from far (small, high) to near (big, low)
    const int counts[6] = {18, 18, 20, 22, 24, 26};
    int id = 0;
    for (int r = 0; r < 6; r++) {
        float d = r / 5.0f;
        for (int i = 0; i < counts[r]; i++, id++) {
            Flower f;
            f.x = -2.5f + (i + 0.15f + 0.7f * hash1(id * 3 + 1)) * 5.0f / counts[r];
            f.gy = -0.14f - d * 0.28f + (hash1(id * 3 + 2) - 0.5f) * 0.05f;
            f.scale = (0.025f + d * 0.030f) * (0.88f + 0.28f * hash1(id * 3 + 3));
            f.phase = hash1(id * 7 + 5) * 6.28f;
            f.delay = hash1(id * 7 + 6) * 6.0f;
            float u = hash1(id * 7 + 7);
            f.variant = u < 0.35f ? 0 : u < 0.60f ? 1 : u < 0.78f ? 2 : u < 0.90f ? 3 : 4;
            f.seed = id;
            flowers.push_back(f);
        }
    }
    std::sort(flowers.begin(), flowers.end(), flowerFarther);   // far first, near last

    // grass
    for (int k = 0; k < 260; k++) {
        Blade b;
        b.x = -4.0f + 8.0f * hash1(k * 4 + 1);
        b.y = -0.16f - 0.60f * hash1(k * 4 + 2);
        b.h = 0.025f + 0.04f * (-b.y);
        b.w = 0.004f; b.ph = hash1(k * 4 + 3) * 6.28f; b.shade = 0.75f + 0.4f * hash1(k * 4 + 4);
        backGrass.push_back(b);
    }
    for (int k = 0; k < 200; k++) {
        Blade b;
        b.x = -4.0f + 8.0f * hash1(k * 6 + 11);
        b.y = -0.99f - 0.02f * hash1(k * 6 + 12);
        b.h = 0.03f + 0.03f * hash1(k * 6 + 13);
        b.w = 0.007f; b.ph = hash1(k * 6 + 14) * 6.28f; b.shade = 0.70f + 0.4f * hash1(k * 6 + 15);
        frontGrass.push_back(b);
    }

    // fireflies
    for (int k = 0; k < 28; k++) {
        Fly f;
        f.x = -2.2f + 4.4f * hash1(k * 3 + 21);
        f.y = -0.70f + 0.60f * hash1(k * 3 + 22);
        f.ph = hash1(k * 3 + 23) * 6.28f;
        flies.push_back(f);
    }
}

// ----------------------------------------------------------------------------
//  Display / animation loop
// ----------------------------------------------------------------------------
static void display() {
    updateSky();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    drawSky();
    drawStars();
    drawSunMoon();
    drawClouds();
    drawBirds();
    drawLandscape();
    drawBlades(backGrass);
    drawPond();

    for (size_t i = 0; i < flowers.size(); i++) drawFlower(flowers[i]);

    drawButterflies();
    drawLawnLife();
    if (showCommute) drawCommuters();
    drawBlades(frontGrass);
    drawFireflies();
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
    aspect = (float)w / (float)h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-aspect, aspect, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
}

static void keyboard(unsigned char key, int, int) {
    static const float JUMP[6] = {6.3f, 9.0f, 12.0f, 15.5f, 18.5f, 22.0f};
    if (key >= '1' && key <= '6') hourNow = JUMP[key - '1'];
    else if (key == ' ') paused = !paused;
    else if (key == '+' || key == '=') speedMul = std::min(speedMul * 1.5f, 30.0f);
    else if (key == '-' || key == '_') speedMul = std::max(speedMul / 1.5f, 0.1f);
    else if (key == 'c' || key == 'C') showCommute = !showCommute;
    else if (key == 'r' || key == 'R') animTime = 0.0f;
    else if (key == 27) std::exit(0);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_MULTISAMPLE);
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
    lastMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(0, tick, 0);

    glutMainLoop();
    return 0;
}

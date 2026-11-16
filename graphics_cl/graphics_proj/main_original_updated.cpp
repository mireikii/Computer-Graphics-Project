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

// distant city skyline (sits between the far mountains and the middle hills)
static void drawCity() {
    float n = nightness();
    float x = -1.25f;
    for (int i = 0; i < 16; i++) {
        float w = 0.07f + 0.07f * hash1(i * 5 + 301);
        float h = 0.15f + 0.35f * hash1(i * 5 + 302);
        if (i % 5 == 2) h += 0.12f;                       // a few taller towers
        float y0 = -0.05f, y1 = y0 + h;
        float shade = 0.85f + 0.3f * hash1(i * 5 + 303);
        Color body = mix(tint(Color{0.42f * shade, 0.47f * shade, 0.58f * shade}), sky.horizon, 0.28f);

        setColor(body);                rect(x, y0, x + w, y1);
        setColor(scaleC(body, 0.88f)); rect(x + w * 0.75f, y0, x + w, y1);          // shaded side
        setColor(scaleC(body, 0.80f)); rect(x - 0.004f, y1, x + w + 0.004f, y1 + 0.008f); // roof
        if (i % 3 == 0) rect(x + w * 0.5f - 0.002f, y1, x + w * 0.5f + 0.002f, y1 + 0.05f); // antenna

        // windows: warm and lit at night
        int cols = (int)(w / 0.022f), rows = (int)(h / 0.032f);
        glBegin(GL_QUADS);
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                float wx = x + 0.008f + c * 0.022f;
                float wy = y0 + 0.012f + r * 0.032f;
                if (wx + 0.012f > x + w - 0.004f || wy + 0.016f > y1 - 0.01f) continue;
                bool on = hash1(i * 1000 + r * 31 + c * 7) < 0.55f;
                Color glass = mix(body, Color{0.80f, 0.88f, 1.0f}, 0.25f);
                Color wc = on ? mix(glass, Color{1.0f, 0.85f, 0.45f}, n)
                              : mix(glass, scaleC(body, 0.7f), n);
                setColor(wc);
                glVertex2f(wx, wy);          glVertex2f(wx + 0.012f, wy);
                glVertex2f(wx + 0.012f, wy + 0.016f); glVertex2f(wx, wy + 0.016f);
            }
        }
        glEnd();
        x += w + 0.01f + 0.03f * hash1(i * 5 + 304);
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
        if (hash1(i * 7 + 3) < 0.75f) {                      // passenger silhouette
            setColor(tint(Color{0.25f, 0.10f, 0.35f}), 0.6f);
            disc(x0 + 0.025f, 0.113f, 0.0085f, 0.0095f, 12);
            disc(x0 + 0.025f, 0.094f, 0.016f, 0.013f, 14);
        }
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

static void drawCar(float night) {
    setColor(tint(Color{0.15f, 0.35f, 0.75f})); rect(-0.11f, 0.02f, 0.11f, 0.065f);
    glBegin(GL_QUADS);   // cabin
        glVertex2f(-0.06f, 0.065f); glVertex2f(0.07f, 0.065f);
        glVertex2f( 0.045f, 0.105f); glVertex2f(-0.04f, 0.105f);
    glEnd();
    setColor(mix(tint(Color{0.60f, 0.80f, 0.95f}), Color{1.0f, 0.86f, 0.45f}, night));
    glBegin(GL_QUADS);
        glVertex2f(-0.045f, 0.068f); glVertex2f(0.06f, 0.068f);
        glVertex2f( 0.040f, 0.098f); glVertex2f(-0.030f, 0.098f);
    glEnd();
    for (int k = -1; k <= 1; k += 2) {
        setColor(tint(Color{0.08f, 0.08f, 0.09f})); disc(k * 0.065f, 0.02f, 0.02f, 0.02f, 14);
        setColor(tint(Color{0.60f, 0.60f, 0.62f})); disc(k * 0.065f, 0.02f, 0.009f, 0.009f, 10);
    }
    setColor(Color{1.0f, 0.95f, 0.7f}); rect(0.10f, 0.035f, 0.11f, 0.048f);
    setColor(Color{0.9f, 0.1f, 0.1f});  rect(-0.11f, 0.035f, -0.10f, 0.048f);
}

static void drawCommuters() {
    float n = nightness();
    drawRoad();
    for (int i = 0; i < 5; i++) drawLamp(-2.4f + i * 1.2f, ROAD_Y1 + 0.012f, n);

    // car drives right -> left on the upper lane
    float cx = 3.5f - std::fmod(animTime * 0.28f + 2.0f, 7.0f);
    glPushMatrix();
    glTranslatef(cx, -0.80f, 0.0f);
    glScalef(-VEH_SCALE, VEH_SCALE, 1.0f);
    drawCar(n);
    headlightCone(0.11f, 0.042f, 0.30f, 0.04f, 0.45f * n);
    glPopMatrix();

    // bus drives left -> right on the lower lane
    float bx = std::fmod(animTime * 0.18f + 1.0f, 7.0f) - 3.5f;
    glPushMatrix();
    glTranslatef(bx, -0.93f, 0.0f);
    glScalef(BUS_SCALE, BUS_SCALE, 1.0f);
    drawBus(n);
    headlightCone(0.25f, 0.065f, 0.45f, 0.06f, 0.45f * n);
    glPopMatrix();
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
             "[1-6] time  [Space] pause  [+/-] speed  [C] commuters  [R] replay bloom", txt);
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
            f.gy = -0.18f - d * 0.36f + (hash1(id * 3 + 2) - 0.5f) * 0.05f;
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
    drawLandscape();
    drawBlades(backGrass);

    for (size_t i = 0; i < flowers.size(); i++) drawFlower(flowers[i]);

    drawButterflies();
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

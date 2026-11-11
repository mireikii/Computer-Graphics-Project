
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

struct Color { float r, g, b; };            // 0..255 floats so interpolation stays smooth
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

//FrameBuffer
struct Framebuffer {
    int w, h;
    std::vector<Color> px;
    Framebuffer(int w, int h) : w(w), h(h), px(w * h, Color{0, 0, 0}) {}
    void setPixel(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        px[y * w + x] = c;
    }
    void blendPixel(int x, int y, Color c, float a) {   //clouds
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        Color &d = px[y * w + x];
        d = {d.r + (c.r - d.r) * a, d.g + (c.g - d.g) * a, d.b + (c.b - d.b) * a};
    }
    void savePPM(const std::string &name) const {
        FILE *f = fopen(name.c_str(), "wb");
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (size_t i = 0; i < px.size(); i++) {
            const Color &c = px[i];
            unsigned char o[3] = {(unsigned char)clampf(c.r, 0.f, 255.f),
                                  (unsigned char)clampf(c.g, 0.f, 255.f),
                                  (unsigned char)clampf(c.b, 0.f, 255.f)};
            fwrite(o, 1, 3, f);
        }
        fclose(f);
    }
    void saveBMP(const std::string &name) const {
        FILE *f = fopen(name.c_str(), "wb");
        int pad = (4 - (w * 3) % 4) % 4;
        int size = 54 + (w * 3 + pad) * h;
        unsigned char hdr[54] = {0};
        hdr[0] = 'B'; hdr[1] = 'M';
        hdr[2] = size; hdr[3] = size >> 8; hdr[4] = size >> 16; hdr[5] = size >> 24;
        hdr[10] = 54; hdr[14] = 40;
        hdr[18] = w; hdr[19] = w >> 8; hdr[20] = w >> 16; hdr[21] = w >> 24;
        hdr[22] = h; hdr[23] = h >> 8; hdr[24] = h >> 16; hdr[25] = h >> 24;
        hdr[26] = 1; hdr[28] = 24;
        fwrite(hdr, 1, 54, f);
        for (int y = h - 1; y >= 0; y--) {              
            for (int x = 0; x < w; x++) {
                const Color &c = px[y * w + x];
                unsigned char bgr[3] = {(unsigned char)clampf(c.b, 0.f, 255.f),
                                        (unsigned char)clampf(c.g, 0.f, 255.f),
                                        (unsigned char)clampf(c.r, 0.f, 255.f)};
                fwrite(bgr, 1, 3, f);
            }
            for (int p = 0; p < pad; p++) fputc(0, f);
        }
        fclose(f);
    }
};

float lerp(float a, float b, float f) { return a + (b - a) * f; }
Color lerpColor(Color a, Color b, float f) {
    return {lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f)};
}

struct SkyKey { Color top, horizon; };
const SkyKey KEYS[5] = {
    {{40, 50, 120}, {255, 150, 100}},    // sunrise
    {{90, 150, 230}, {255, 220, 170}},   // morning
    {{40, 120, 230}, {160, 210, 250}},   // midday
    {{70, 120, 200}, {255, 200, 130}},   // afternoon
    {{50, 30, 90}, {255, 100, 70}},      // sunset
};

SkyKey skyAt(float t) {                 
    t = clampf(t, 0.f, 1.f);
    float s = t * 4.f;
    int i = std::min((int)s, 3);
    float f = s - i;
    return {lerpColor(KEYS[i].top, KEYS[i + 1].top, f),
            lerpColor(KEYS[i].horizon, KEYS[i + 1].horizon, f)};
}

void drawSky(Framebuffer &fb, float t, int horizonY) {
    SkyKey k = skyAt(t);
    for (int y = 0; y < fb.h; y++) {     
        float f = std::min(1.f, (float)y / horizonY);
        Color c = lerpColor(k.top, k.horizon, f);
        for (int x = 0; x < fb.w; x++) fb.setPixel(x, y, c);
    }
}

void hline(Framebuffer &fb, int x0, int x1, int y, Color c) {
    for (int x = x0; x <= x1; x++) fb.setPixel(x, y, c);
}
void fillCircle(Framebuffer &fb, int cx, int cy, int r, Color c) {
    int x = 0, y = r, d = 1 - r;
    while (x <= y) {
        hline(fb, cx - x, cx + x, cy + y, c);
        hline(fb, cx - x, cx + x, cy - y, c);
        hline(fb, cx - y, cx + y, cy + x, c);
        hline(fb, cx - y, cx + y, cy - x, c);
        if (d < 0) d += 2 * x + 3;
        else { d += 2 * (x - y) + 5; y--; }
        x++;
    }
}

struct SunState { int x, y; Color color; };
SunState sunAt(float t, int w, int horizonY) {
    float arc = std::max(0.f, std::sin(3.14159265f * t));      
    int x = (int)(w * 0.1f + w * 0.8f * t);                  
    int y = horizonY - (int)(arc * horizonY * 0.8f);
    Color c = lerpColor({255, 110, 40}, {255, 245, 200}, std::sqrt(arc));
    return {x, y, c};
}

void drawSun(Framebuffer &fb, float t, int horizonY) {
    SunState s = sunAt(t, fb.w, horizonY);
    const int R = 30, GLOW = 130;
    for (int dy = -GLOW; dy <= GLOW; dy++)                    
        for (int dx = -GLOW; dx <= GLOW; dx++) {
            float d = std::sqrt((float)(dx * dx + dy * dy));
            if (d > R && d < GLOW) {
                float a = std::pow(1.f - (d - R) / (GLOW - R), 2.f) * 0.55f;
                fb.blendPixel(s.x + dx, s.y + dy, s.color, a);
            }
        }
    fillCircle(fb, s.x, s.y, R, s.color);
}

void fillEllipseAlpha(Framebuffer &fb, int cx, int cy, int rx, int ry, Color c, float alpha) {
    for (int y = -ry; y <= ry; y++)
        for (int x = -rx; x <= rx; x++) {
            float e = (float)(x * x) / (rx * rx) + (float)(y * y) / (ry * ry);
            if (e <= 1.f) fb.blendPixel(cx + x, cy + y, c, alpha * (1.f - 0.5f * e));  
        }
}

void drawClouds(Framebuffer &fb, float t) {
    SkyKey k = skyAt(t);
    Color tint = lerpColor({255, 255, 255}, k.horizon, 0.45f); 
    struct Cloud { float x, y; float scale, speed; };
    const Cloud clouds[] = {{0.15f, 90, 1.0f, 0.10f}, {0.55f, 150, 1.4f, 0.06f}, {0.80f, 60, 0.8f, 0.14f}};
    for (const Cloud &c : clouds) {
        float fx = std::fmod(c.x + c.speed * t + 1.f, 1.f);    
        int cx = (int)(fx * fb.w), cy = (int)c.y;
        int s = (int)(40 * c.scale);
        fillEllipseAlpha(fb, cx, cy, s * 2, s / 2, tint, 0.8f);
        fillEllipseAlpha(fb, cx - s, cy + s / 4, s, s / 3, tint, 0.8f);
        fillEllipseAlpha(fb, cx + s, cy + s / 4, s * 3 / 2, s / 3, tint, 0.8f);
    }
}

Color sunlightTint(float t) {
    float arc = std::max(0.f, std::sin(3.14159265f * t));
    return lerpColor({255, 170, 120}, {255, 252, 240}, std::sqrt(arc));
}

void drawSkyLayer(Framebuffer &fb, float t, int horizonY) {
    drawSky(fb, t, horizonY);
    drawSun(fb, t, horizonY);
    drawClouds(fb, t);
}

int main() {
    const int W = 800, H = 600, HORIZON = 420;
    const float times[] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
    for (float t : times) {
        Framebuffer fb(W, H);
        drawSkyLayer(fb, t, HORIZON);
        char name[64];
        snprintf(name, sizeof name, "sky_%03d.ppm", (int)(t * 100));
        fb.savePPM(name);
        snprintf(name, sizeof name, "sky_%03d.bmp", (int)(t * 100));
        fb.saveBMP(name);
        printf("wrote %s\n", name);
    }
}

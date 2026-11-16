#include <GL/freeglut.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string>

const float PI = 3.14159265358979323846f;

struct Point { float x, y; };

Point bezier(Point p0, Point p1, Point p2, Point p3, float t) {
    float u = 1.0f - t;
    return {
        u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
        u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y
    };
}

// ==========================================
// 1. ANIMATED TIME-OF-DAY & SKY ENGINE
// ==========================================
struct Color { float r, g, b; };
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerp(float a, float b, float f) { return a + (b - a) * f; }
Color lerpColor(Color a, Color b, float f) {
    return {lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f)};
}

struct SkyKey { 
    Color top, horizon; 
    const char* timeLabel; 
};

// 6 Time-of-day states including Night and Stars
const SkyKey KEYS[6] = {
    {{0.15f, 0.20f, 0.47f}, {1.00f, 0.59f, 0.39f}, "06:00 AM - Sunrise"},
    {{0.35f, 0.59f, 0.90f}, {1.00f, 0.86f, 0.67f}, "09:00 AM - Morning"},
    {{0.15f, 0.47f, 0.90f}, {0.63f, 0.82f, 0.98f}, "12:00 PM - Midday"},
    {{0.27f, 0.47f, 0.78f}, {1.00f, 0.78f, 0.51f}, "03:00 PM - Afternoon"},
    {{0.20f, 0.12f, 0.35f}, {1.00f, 0.39f, 0.27f}, "06:00 PM - Sunset"},
    {{0.02f, 0.02f, 0.08f}, {0.10f, 0.08f, 0.20f}, "10:00 PM - Night"}
};

float globalTime = 0.0f; // Slower global animation timer

void drawText(float x, float y, void* font, const char* string, Color col) {
    glColor3f(col.r, col.g, col.b);
    glRasterPos2f(x, y);
    for (const char* c = string; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

void drawStars() {
    // Deterministic star field visible during night states
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 40; i++) {
        float sx = std::sin(i * 123.45f) * 1.5f;
        float sy = 0.2f + std::cos(i * 67.89f) * 1.3f;
        float twinkle = 0.5f + 0.5f * std::sin(globalTime * 0.1f + i);
        glColor3f(twinkle, twinkle, twinkle * 0.9f);
        glVertex2f(sx, sy);
    }
    glEnd();
}

void drawDynamicSky() {
    float scaledTime = std::fmod(globalTime * 0.015f, 6.0f); // Slower transition rate
    int i = (int)scaledTime;
    int nextI = (i + 1) % 6;
    float f = scaledTime - i;
    
    Color topColor = lerpColor(KEYS[i].top, KEYS[nextI].top, f);
    Color horizColor = lerpColor(KEYS[i].horizon, KEYS[nextI].horizon, f);
    
    // Background sky gradient quad
    glBegin(GL_QUADS);
        glColor3f(horizColor.r, horizColor.g, horizColor.b);
        glVertex2f(-1.6f, -1.6f);
        glVertex2f( 1.6f, -1.6f);
        glColor3f(topColor.r, topColor.g, topColor.b);
        glVertex2f( 1.6f,  1.6f);
        glVertex2f(-1.6f,  1.6f);
    glEnd();

    // Render stars if approaching night (index 4 to 5)
    if (i >= 4) {
        drawStars();
    }

    // Render timestamp text overlay
    Color textColor = (i == 5) ? Color{1.0f, 1.0f, 1.0f} : Color{0.1f, 0.1f, 0.1f};
    drawText(-1.45f, 1.40f, GLUT_BITMAP_HELVETICA_18, KEYS[i].timeLabel, textColor);
}

// ==========================================
// 2. LANDSCAPE, TREES & ENVIRONMENT
// ==========================================
void drawCircle(float x, float y, float radius) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 100; i++) {
        float angle = 2.0f * PI * i / 100;
        glVertex2f(x + radius * cos(angle), y + radius * sin(angle));
    }
    glEnd();
}

void drawTree(float x, float y, float scale) {
    // Trunk
    glColor3f(0.35f, 0.20f, 0.08f);
    glBegin(GL_QUADS);
        glVertex2f(x - 0.03f * scale, y);
        glVertex2f(x + 0.03f * scale, y);
        glVertex2f(x + 0.02f * scale, y + 0.35f * scale);
        glVertex2f(x - 0.02f * scale, y + 0.35f * scale);
    glEnd();

    // Foliage layers
    glColor3f(0.12f, 0.38f, 0.15f);
    drawCircle(x, y + 0.38f * scale, 0.18f * scale);
    glColor3f(0.16f, 0.45f, 0.18f);
    drawCircle(x - 0.08f * scale, y + 0.30f * scale, 0.13f * scale);
    drawCircle(x + 0.08f * scale, y + 0.30f * scale, 0.13f * scale);
}

void drawBackgroundHills() {
    glColor3f(0.50f, 0.70f, 0.40f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.15f); glVertex2f(-0.85f, 0.15f);
        glVertex2f(-0.55f, 0.35f); glVertex2f(-0.25f, 0.15f);
        glVertex2f(0.10f, 0.40f);  glVertex2f(0.45f, 0.20f);
        glVertex2f(0.75f, 0.38f);  glVertex2f(1.6f, 0.15f);
        glVertex2f(1.6f, -0.65f);  glVertex2f(-1.6f, -0.65f);
    glEnd();
}

void drawForegroundHills() {
    glColor3f(0.22f, 0.50f, 0.20f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.35f); glVertex2f(-0.75f, -0.10f);
        glVertex2f(-0.40f, -0.30f); glVertex2f(-0.10f, -0.12f);
        glVertex2f(0.25f, -0.32f);  glVertex2f(0.60f, -0.08f);
        glVertex2f(1.6f, -0.28f);   glVertex2f(1.6f, -0.85f);
        glVertex2f(-1.6f, -0.85f);
    glEnd();
}

void drawGround() {
    glColor3f(0.28f, 0.60f, 0.22f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.65f); glVertex2f(1.6f, -0.65f);
        glVertex2f(1.6f, -1.6f);   glVertex2f(-1.6f, -1.6f);
    glEnd();
}

void drawLandscape() {
    drawBackgroundHills();
    drawForegroundHills();
    
    // Add a few background trees
    drawTree(-1.2f, -0.25f, 0.9f);
    drawTree(-0.9f, -0.20f, 0.7f);
    drawTree(1.00f, -0.22f, 1.0f);
    drawTree(1.30f, -0.28f, 0.8f);

    drawGround();
}

// ==========================================
// 3. FLOWER FIELD & BUTTERFLIES
// ==========================================
struct PetalProps { float angleOffset, scaleW, scaleH, bend; };

PetalProps standardPetals[8] = {
    {  0.0f, 1.28f, 1.00f,  0.000f }, {  1.8f, 1.24f, 1.04f,  0.015f },
    { -1.2f, 1.30f, 0.97f, -0.010f }, {  2.2f, 1.25f, 1.02f,  0.020f },
    { -0.8f, 1.29f, 0.99f, -0.012f }, {  1.5f, 1.22f, 1.03f,  0.010f },
    { -1.9f, 1.27f, 0.96f, -0.018f }, {  0.7f, 1.31f, 1.01f,  0.008f }
};

void drawLeaf(Point origin, bool isRightSide, float scale) {
    glPushMatrix();
    glTranslatef(origin.x, origin.y, 0.0f);
    float sideMult = isRightSide ? 1.0f : -1.0f;
    glRotatef(sideMult * 35.0f, 0.0f, 0.0f, 1.0f);
    glScalef(scale * sideMult, scale, 1.0f);

    Point base = { 0.00f, 0.00f }, midL = {-0.10f, 0.20f }, tip = { 0.04f, 0.45f }, midR = { 0.11f, 0.22f };
    std::vector<Point> boundary;
    for (int i = 0; i <= 20; i++) boundary.push_back(bezier(base, midL, {-0.06f, 0.32f}, tip, i / 20.0f));
    for (int i = 0; i <= 20; i++) boundary.push_back(bezier(tip, {0.08f, 0.32f}, midR, base, i / 20.0f));

    glBegin(GL_TRIANGLE_FAN);
    glColor3f(0.12f, 0.42f, 0.08f);
    glVertex2f(base.x, base.y);
    for (const auto& p : boundary) glVertex2f(p.x, p.y);
    glEnd();
    glPopMatrix();
}

void drawStem(Point s0, Point s1, Point s2, Point s3, float stemWidth) {
    const int steps = 20;
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= steps; i++) {
        float t = i / (float)steps;
        Point p = bezier(s0, s1, s2, s3, t);
        Point pNext = bezier(s0, s1, s2, s3, t + 0.001f);
        float dx = pNext.x - p.x, dy = pNext.y - p.y;
        float len = std::sqrt(dx*dx + dy*dy);
        float nx = -dy / len, ny = dx / len;
        glColor3f(0.16f + t * 0.12f, 0.48f + t * 0.15f, 0.08f + t * 0.04f);
        glVertex2f(p.x - nx * stemWidth * 0.5f, p.y - ny * stemWidth * 0.5f);
        glVertex2f(p.x + nx * stemWidth * 0.5f, p.y + ny * stemWidth * 0.5f);
    }
    glEnd();
    drawLeaf(bezier(s0, s1, s2, s3, 0.4f), true, 0.65f);
}

void drawCosmosPetal(const PetalProps& props, bool isBackLayer) {
    Point base = { 0.00f, 0.05f };
    Point leftMid = {-0.24f * props.scaleW + props.bend, 0.42f * props.scaleH };
    Point tipLeft = {-0.16f * props.scaleW + props.bend, 0.78f * props.scaleH };
    Point notch1 = {-0.05f + props.bend, 0.77f * props.scaleH };
    Point tipMid = { 0.00f + props.bend, 0.81f * props.scaleH };
    Point notch2 = { 0.05f + props.bend, 0.77f * props.scaleH };
    Point tipRight = { 0.16f * props.scaleW + props.bend, 0.78f * props.scaleH };
    Point rightMid = { 0.24f * props.scaleW + props.bend, 0.42f * props.scaleH };

    std::vector<Point> boundary;
    for (int i = 0; i <= 30; i++) boundary.push_back(bezier(base, leftMid, {-0.18f + props.bend, 0.65f}, tipLeft, i / 30.0f));
    boundary.push_back(notch1); boundary.push_back(tipMid); boundary.push_back(notch2);
    for (int i = 0; i <= 30; i++) boundary.push_back(bezier(tipRight, {0.18f + props.bend, 0.65f}, rightMid, base, i / 30.0f));

    float dim = isBackLayer ? 0.92f : 1.0f;
    glBegin(GL_TRIANGLE_FAN);
    // Softer pink/magenta hue for prettier flower aesthetic
    glColor3f(0.68f * dim, 0.12f * dim, 0.45f * dim);
    glVertex2f(base.x, base.y);
    for (const auto& p : boundary) glVertex2f(p.x, p.y);
    glEnd();
}

void drawFlowerHead() {
    for (int i = 0; i < 8; i++) {
        glPushMatrix();
        glRotatef(i * 45.0f + standardPetals[i].angleOffset, 0.0f, 0.0f, 1.0f);
        drawCosmosPetal(standardPetals[i], false);
        glPopMatrix();
    }

    // Realistic Cosmos Yellow-Brown Center Disc
    const int FLORET_COUNT = 250;
    const float GOLDEN_ANGLE = 137.507764f * (PI / 180.0f);
    const float MAX_RADIUS = 0.11f;

    glBegin(GL_TRIANGLE_FAN);
    glColor3f(0.50f, 0.32f, 0.08f);
    glVertex2f(0.0f, 0.0f);
    for (int i = 0; i <= 36; i++) {
        float a = i * (2.0f * PI / 36.0f);
        glVertex2f(MAX_RADIUS * cos(a), MAX_RADIUS * sin(a));
    }
    glEnd();

    glEnable(GL_POINT_SMOOTH);
    for (int i = 0; i < FLORET_COUNT; i++) {
        float r = std::sqrt((float)i / FLORET_COUNT) * MAX_RADIUS;
        float theta = i * GOLDEN_ANGLE;
        float x = r * std::cos(theta);
        float y = r * std::sin(theta);

        glPointSize(1.8f + (r / MAX_RADIUS) * 2.0f);
        float normR = r / MAX_RADIUS;
        if (normR < 0.4f) {
            glColor3f(0.40f, 0.20f, 0.02f);
        } else {
            glColor3f(0.95f, 0.72f, 0.12f);
        }

        glBegin(GL_POINTS);
        glVertex2f(x, y);
        glEnd();
    }
}

void drawButterfly(float cx, float cy, float scale) {
    glPushMatrix();
    glTranslatef(cx, cy, 0.0f);
    glScalef(scale, scale, 1.0f);
    
    // Flapping wing angle based on globalTime
    float flap = std::sin(globalTime * 0.5f) * 0.4f + 0.6f;

    // Wings
    glColor3f(0.95f, 0.75f, 0.25f); // Amber / yellow butterfly wings
    glBegin(GL_TRIANGLES);
        // Left upper wing
        glVertex2f(0.0f, 0.0f);
        glVertex2f(-0.08f * flap, 0.08f);
        glVertex2f(-0.02f, -0.04f);
        // Right upper wing
        glVertex2f(0.0f, 0.0f);
        glVertex2f(0.08f * flap, 0.08f);
        glVertex2f(0.02f, -0.04f);
    glEnd();

    // Body
    glColor3f(0.15f, 0.10f, 0.05f);
    glBegin(GL_QUADS);
        glVertex2f(-0.012f, -0.04f);
        glVertex2f( 0.012f, -0.04f);
        glVertex2f( 0.012f,  0.04f);
        glVertex2f(-0.012f,  0.04f);
    glEnd();

    glPopMatrix();
}

// ==========================================
// 4. DISPLAY & ANIMATION LOOP
// ==========================================
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // 1. Sky & Background
    drawDynamicSky();
    drawLandscape();

    // 2. Blooming Animation progress (slower, smooth ramp-up)
    float bloomProgress = std::min(1.0f, globalTime * 0.025f);
    float sway = std::sin(globalTime * 0.05f) * 1.5f;

    // Render a field of smaller, varied flowers across the landscape
    struct FlowerPos { float x, y, scale; };
    FlowerPos fieldFlowers[7] = {
        {-0.85f, -0.30f, 0.28f},
        {-0.50f, -0.22f, 0.38f},
        {-0.15f, -0.18f, 0.44f},
        { 0.18f, -0.20f, 0.40f},
        { 0.52f, -0.25f, 0.35f},
        { 0.88f, -0.32f, 0.30f},
        { 0.00f, -0.12f, 0.48f} // Center hero
    };

    for (int i = 0; i < 7; i++) {
        float fx = fieldFlowers[i].x;
        float fy = fieldFlowers[i].y;
        float sc = fieldFlowers[i].scale * bloomProgress;

        // Draw corresponding stem down to the ground
        drawStem({fx, fy}, {fx - 0.01f, fy - 0.3f}, {fx - 0.02f, fy - 0.6f}, {fx - 0.03f, -0.85f}, 0.012f);

        glPushMatrix();
            glTranslatef(fx, fy, 0.0f);
            glRotatef(sway * (1.0f + i * 0.1f), 0.0f, 0.0f, 1.0f);
            glScalef(sc, sc, 1.0f);
            drawFlowerHead();
        glPopMatrix();
    }

    // 3. Animated Butterflies fluttering across the field
    float bx1 = std::fmod(globalTime * 0.003f + 0.2f, 2.2f) - 1.1f;
    float by1 = 0.05f + std::sin(globalTime * 0.08f) * 0.15f;
    drawButterfly(bx1, by1, 1.2f);

    float bx2 = std::fmod(-globalTime * 0.0025f + 1.5f, 2.2f) - 1.1f;
    float by2 = -0.15f + std::cos(globalTime * 0.07f) * 0.12f;
    drawButterfly(bx2, by2, 0.9f);

    glutSwapBuffers();
}

void update(int value) {
    globalTime += 1.0f; // Slower tick rate for gentle pacing
    glutPostRedisplay();
    glutTimerFunc(25, update, 0); // ~40 FPS with smoother transitions
}

void reshape(int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.6, 1.6, -1.6, 1.6);
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int x, int y) {
    if (key >= '1' && key <= '6') {
        globalTime = (key - '1') * 40.0f; // Jump to specific time of day state
        glutPostRedisplay();
    }
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_MULTISAMPLE);
    glutInitWindowSize(1000, 650);
    glutCreateWindow("A Day in Bloom - Field of Flowers & Time Cycles");

    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POLYGON_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(0, update, 0);
    
    glutMainLoop();
    return 0;
}
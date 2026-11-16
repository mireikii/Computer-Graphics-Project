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
// 1. DYNAMIC TIME-OF-DAY SKY ENGINE
// ==========================================
struct Color { float r, g, b; };
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerp(float a, float b, float f) { return a + (b - a) * f; }
Color lerpColor(Color a, Color b, float f) {
    return {lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f)};
}

struct SkyKey { Color top, horizon; };
const SkyKey KEYS[5] = {
    {{0.15f, 0.20f, 0.47f}, {1.00f, 0.59f, 0.39f}},    // 1: Sunrise
    {{0.35f, 0.59f, 0.90f}, {1.00f, 0.86f, 0.67f}},   // 2: Morning
    {{0.15f, 0.47f, 0.90f}, {0.63f, 0.82f, 0.98f}},   // 3: Midday
    {{0.27f, 0.47f, 0.78f}, {1.00f, 0.78f, 0.51f}},   // 4: Afternoon
    {{0.20f, 0.12f, 0.35f}, {1.00f, 0.39f, 0.27f}},      // 5: Sunset
};

// Current active sky state index (0 to 4)
int currentSkyState = 2; // Default to Midday

void drawDynamicSky() {
    SkyKey k = KEYS[currentSkyState];
    
    // Draw smooth gradient background quad covering the ortho projection
    glBegin(GL_QUADS);
        // Bottom horizon color
        glColor3f(k.horizon.r, k.horizon.g, k.horizon.b);
        glVertex2f(-1.6f, -1.6f);
        glVertex2f( 1.6f, -1.6f);
        // Top sky color
        glColor3f(k.top.r, k.top.g, k.top.b);
        glVertex2f( 1.6f,  1.6f);
        glVertex2f(-1.6f,  1.6f);
    glEnd();
}

// ==========================================
// 2. LANDSCAPE & ENVIRONMENT FUNCTIONS
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

void drawBackgroundHills() {
    glColor3f(0.55f, 0.75f, 0.45f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.15f); glVertex2f(-0.85f, 0.15f);
        glVertex2f(-0.55f, 0.35f); glVertex2f(-0.25f, 0.15f);
        glVertex2f(0.10f, 0.40f);  glVertex2f(0.45f, 0.20f);
        glVertex2f(0.75f, 0.38f);  glVertex2f(1.6f, 0.15f);
        glVertex2f(1.6f, -0.65f);  glVertex2f(-1.6f, -0.65f);
    glEnd();
}

void drawForegroundHills() {
    glColor3f(0.25f, 0.55f, 0.22f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.35f); glVertex2f(-0.75f, -0.10f);
        glVertex2f(-0.40f, -0.30f); glVertex2f(-0.10f, -0.12f);
        glVertex2f(0.25f, -0.32f);  glVertex2f(0.60f, -0.08f);
        glVertex2f(1.6f, -0.28f);   glVertex2f(1.6f, -0.85f);
        glVertex2f(-1.6f, -0.85f);
    glEnd();
}

void drawGround() {
    glColor3f(0.30f, 0.65f, 0.25f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.65f); glVertex2f(1.6f, -0.65f);
        glVertex2f(1.6f, -1.6f);   glVertex2f(-1.6f, -1.6f);
    glEnd();
}

void drawSoil() {
    glColor3f(0.45f, 0.25f, 0.12f);
    glBegin(GL_POLYGON);
        glVertex2f(-1.6f, -0.85f); glVertex2f(1.6f, -0.85f);
        glVertex2f(1.6f, -1.6f);   glVertex2f(-1.6f, -1.6f);
    glEnd();
    glColor3f(0.65f, 0.42f, 0.23f);
    drawCircle(-0.75f, -0.91f, 0.012f);
    drawCircle(-0.40f, -0.95f, 0.015f);
    drawCircle(0.10f, -0.90f, 0.012f);
    drawCircle(0.55f, -0.94f, 0.016f);
    drawCircle(0.82f, -0.89f, 0.012f);
}

void drawGrass() {
    glLineWidth(2.0f);
    glColor3f(0.05f, 0.35f, 0.08f);
    glBegin(GL_LINES);
        glVertex2f(-0.85f, -0.65f); glVertex2f(-0.90f, -0.55f);
        glVertex2f(-0.85f, -0.65f); glVertex2f(-0.83f, -0.53f);
        glVertex2f(-0.85f, -0.65f); glVertex2f(-0.78f, -0.57f);
        glVertex2f(-0.30f, -0.65f); glVertex2f(-0.35f, -0.54f);
        glVertex2f(-0.30f, -0.65f); glVertex2f(-0.28f, -0.52f);
        glVertex2f(-0.30f, -0.65f); glVertex2f(-0.23f, -0.56f);
        glVertex2f(0.40f, -0.65f);  glVertex2f(0.35f, -0.54f);
        glVertex2f(0.40f, -0.65f);  glVertex2f(0.42f, -0.52f);
        glVertex2f(0.40f, -0.65f);  glVertex2f(0.47f, -0.57f);
        glVertex2f(0.85f, -0.65f);  glVertex2f(0.80f, -0.54f);
        glVertex2f(0.85f, -0.65f);  glVertex2f(0.87f, -0.52f);
    glEnd();
}

void drawBushes() {
    glColor3f(0.10f, 0.40f, 0.12f);
    drawCircle(-0.75f, -0.40f, 0.12f);
    drawCircle(-0.62f, -0.40f, 0.15f);
    drawCircle(-0.50f, -0.42f, 0.10f);
    drawCircle(0.55f, -0.40f, 0.12f);
    drawCircle(0.68f, -0.38f, 0.16f);
    drawCircle(0.82f, -0.42f, 0.11f);
}

void drawSmallPlants() {
    glLineWidth(2.0f);
    glColor3f(0.05f, 0.30f, 0.05f);
    glBegin(GL_LINES);
        glVertex2f(-0.10f, -0.65f); glVertex2f(-0.10f, -0.48f);
        glVertex2f(0.15f, -0.65f);  glVertex2f(0.15f, -0.50f);
    glEnd();
    glColor3f(0.10f, 0.50f, 0.10f);
    glBegin(GL_TRIANGLES);
        glVertex2f(-0.10f, -0.55f); glVertex2f(-0.20f, -0.50f); glVertex2f(-0.10f, -0.49f);
        glVertex2f(-0.10f, -0.52f); glVertex2f(0.00f, -0.46f);  glVertex2f(-0.10f, -0.47f);
        glVertex2f(0.15f, -0.57f);  glVertex2f(0.05f, -0.52f);  glVertex2f(0.15f, -0.50f);
        glVertex2f(0.15f, -0.53f);  glVertex2f(0.25f, -0.48f);  glVertex2f(0.15f, -0.48f);
    glEnd();
}

void drawRocks() {
    glColor3f(0.40f, 0.40f, 0.38f);
    drawCircle(-0.45f, -0.72f, 0.035f);
    drawCircle(-0.40f, -0.72f, 0.025f);
    drawCircle(0.70f, -0.73f, 0.04f);
}

void drawLandscape() {
    drawBackgroundHills();
    drawForegroundHills();
    drawGround();
    drawSoil();
    drawBushes();
    drawSmallPlants();
    drawGrass();
    drawRocks();
}

// ==========================================
// 3. FLOWER GARDEN & PETAL STRUCTURES
// ==========================================
struct PetalProps { float angleOffset, scaleW, scaleH, bend; };

PetalProps petalDataHero[8] = {
    {  0.0f, 1.28f, 1.00f,  0.000f }, {  1.8f, 1.24f, 1.04f,  0.015f },
    { -1.2f, 1.30f, 0.97f, -0.010f }, {  2.2f, 1.25f, 1.02f,  0.020f },
    { -0.8f, 1.29f, 0.99f, -0.012f }, {  1.5f, 1.22f, 1.03f,  0.010f },
    { -1.9f, 1.27f, 0.96f, -0.018f }, {  0.7f, 1.31f, 1.01f,  0.008f }
};

PetalProps petalDataLeft[8] = {
    { -0.5f, 1.22f, 0.98f,  0.005f }, {  1.2f, 1.26f, 1.01f, -0.012f },
    { -1.8f, 1.20f, 0.95f,  0.018f }, {  0.9f, 1.28f, 1.03f, -0.010f },
    { -1.1f, 1.24f, 0.97f,  0.014f }, {  2.0f, 1.21f, 1.02f, -0.008f },
    { -0.6f, 1.25f, 0.99f,  0.012f }, {  1.4f, 1.23f, 1.00f, -0.015f }
};

PetalProps petalDataRight[8] = {
    {  1.1f, 1.25f, 1.02f, -0.010f }, { -0.9f, 1.21f, 0.96f,  0.014f },
    {  1.6f, 1.27f, 1.04f, -0.008f }, { -1.4f, 1.23f, 0.98f,  0.016f },
    {  0.8f, 1.26f, 1.01f, -0.012f }, { -1.7f, 1.20f, 0.97f,  0.010f },
    {  1.3f, 1.28f, 1.03f, -0.018f }, { -0.4f, 1.22f, 0.99f,  0.006f }
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
    const int steps = 30;
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
    drawLeaf(bezier(s0, s1, s2, s3, 0.38f), true, 0.75f);
    drawLeaf(bezier(s0, s1, s2, s3, 0.68f), false, 0.82f);
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
    glColor3f(0.58f * dim, 0.02f * dim, 0.32f * dim);
    glVertex2f(base.x, base.y);
    for (const auto& p : boundary) glVertex2f(p.x, p.y);
    glEnd();
}

void drawFlowerHead(const PetalProps data[8]) {
    for (int i = 0; i < 8; i++) {
        glPushMatrix();
        glRotatef(i * 45.0f + data[i].angleOffset, 0.0f, 0.0f, 1.0f);
        drawCosmosPetal(data[i], false);
        glPopMatrix();
    }
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(0.38f, 0.14f, 0.01f);
    glVertex2f(0.0f, 0.0f);
    for (int i = 0; i <= 36; i++) {
        float a = i * (2.0f * PI / 36.0f);
        glVertex2f(0.145f * cos(a), 0.145f * sin(a));
    }
    glEnd();
}

// ==========================================
// 4. MAIN DISPLAY & WINDOW CALLBACKS
// ==========================================
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // 1. Draw Dynamic Time-of-Day Sky
    drawDynamicSky();

    // 2. Draw Landscape & Hills (Properly scaled to fit screen width)
    drawLandscape();

    // 3. Draw Foreground Cosmos Garden Flowers (Scaled down and repositioned proportionally)
    Point s0_L = { -0.45f, -0.22f };
    drawStem(s0_L, { -0.42f, -0.50f }, { -0.38f, -0.80f }, { -0.35f, -1.15f }, 0.014f);
    glPushMatrix(); glTranslatef(s0_L.x, s0_L.y, 0.0f); glScalef(0.42f, 0.42f, 1.0f); drawFlowerHead(petalDataLeft); glPopMatrix();

    Point s0_R = {  0.45f, -0.26f };
    drawStem(s0_R, {  0.42f, -0.52f }, {  0.38f, -0.82f }, {  0.35f, -1.15f }, 0.014f);
    glPushMatrix(); glTranslatef(s0_R.x, s0_R.y, 0.0f); glScalef(0.44f, 0.44f, 1.0f); drawFlowerHead(petalDataRight); glPopMatrix();

    Point s0_C = {  0.00f, -0.15f };
    drawStem(s0_C, { -0.01f, -0.45f }, { -0.03f, -0.78f }, { -0.05f, -1.15f }, 0.016f);
    glPushMatrix(); glTranslatef(s0_C.x, s0_C.y, 0.0f); glScalef(0.50f, 0.50f, 1.0f); drawFlowerHead(petalDataHero); glPopMatrix();

    glutSwapBuffers();
}

void reshape(int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.6, 1.6, -1.6, 1.6);
    glMatrixMode(GL_MODELVIEW);
}

// Optional: Press keys '1' through '5' to change the sky state!
void keyboard(unsigned char key, int x, int y) {
    if (key >= '1' && key <= '5') {
        currentSkyState = key - '1';
        glutPostRedisplay(); // Refresh the window with the new sky
    }
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_MULTISAMPLE);
    glutInitWindowSize(1000, 650);
    glutCreateWindow("A Day in Bloom - Full Integrated Group Project");

    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POLYGON_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard); // Hook up keyboard keys 1-5 for sky changes
    
    glutMainLoop();
    return 0;
}
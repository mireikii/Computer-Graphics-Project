#include <GL/freeglut.h>
#include <cmath>
#include <vector>

const float PI = 3.14159265358979323846f;

struct Point { float x, y; };

Point bezier(Point p0, Point p1, Point p2, Point p3, float t) {
    float u = 1.0f - t;
    return {
        u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
        u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y
    };
}

struct PetalProps {
    float angleOffset, scaleW, scaleH, bend;
};

PetalProps petalDataHero[8] = {
    {  0.0f, 1.28f, 1.00f,  0.000f },
    {  1.8f, 1.24f, 1.04f,  0.015f },
    { -1.2f, 1.30f, 0.97f, -0.010f },
    {  2.2f, 1.25f, 1.02f,  0.020f },
    { -0.8f, 1.29f, 0.99f, -0.012f },
    {  1.5f, 1.22f, 1.03f,  0.010f },
    { -1.9f, 1.27f, 0.96f, -0.018f },
    {  0.7f, 1.31f, 1.01f,  0.008f }
};

PetalProps petalDataLeft[8] = {
    { -0.5f, 1.22f, 0.98f,  0.005f },
    {  1.2f, 1.26f, 1.01f, -0.012f },
    { -1.8f, 1.20f, 0.95f,  0.018f },
    {  0.9f, 1.28f, 1.03f, -0.010f },
    { -1.1f, 1.24f, 0.97f,  0.014f },
    {  2.0f, 1.21f, 1.02f, -0.008f },
    { -0.6f, 1.25f, 0.99f,  0.012f },
    {  1.4f, 1.23f, 1.00f, -0.015f }
};

PetalProps petalDataRight[8] = {
    {  1.1f, 1.25f, 1.02f, -0.010f },
    { -0.9f, 1.21f, 0.96f,  0.014f },
    {  1.6f, 1.27f, 1.04f, -0.008f },
    { -1.4f, 1.23f, 0.98f,  0.016f },
    {  0.8f, 1.26f, 1.01f, -0.012f },
    { -1.7f, 1.20f, 0.97f,  0.010f },
    {  1.3f, 1.28f, 1.03f, -0.018f },
    { -0.4f, 1.22f, 0.99f,  0.006f }
};

// --- 1. HORIZONTALLY ALIGNED BLURRED BACKGROUND BLOBS ---

struct BlurredFlower {
    float x, y, radius, r, g, b;
};

BlurredFlower bgBlobs[10] = {
    { -1.35f, -0.35f, 0.48f, 0.98f, 0.25f, 0.65f },
    { -1.05f, -0.30f, 0.52f, 1.00f, 0.35f, 0.62f },
    { -0.75f, -0.40f, 0.45f, 0.88f, 0.18f, 0.55f },
    { -0.45f, -0.32f, 0.50f, 1.00f, 0.45f, 0.72f },
    { -0.15f, -0.38f, 0.44f, 0.92f, 0.22f, 0.62f },
    {  0.15f, -0.38f, 0.44f, 0.96f, 0.28f, 0.68f },
    {  0.45f, -0.30f, 0.48f, 0.98f, 0.32f, 0.65f },
    {  0.75f, -0.40f, 0.45f, 0.85f, 0.15f, 0.52f },
    {  1.05f, -0.32f, 0.50f, 1.00f, 0.38f, 0.70f },
    {  1.35f, -0.36f, 0.46f, 0.90f, 0.20f, 0.58f }
};

void drawBlurredBlob(float cx, float cy, float maxRadius, float r, float g, float b) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int rings = 15;
    for (int ring = rings; ring >= 1; ring--) {
        float radius = (ring / (float)rings) * maxRadius;
        float alpha = 0.09f * (1.0f - (ring / (float)rings));

        glBegin(GL_TRIANGLE_FAN);
        glColor4f(r, g, b, alpha);
        glVertex2f(cx, cy);

        for (int i = 0; i <= 24; i++) {
            float a = i * (2.0f * PI / 24.0f);
            float var = 1.0f + 0.08f * std::sin(a * 8.0f);
            glVertex2f(cx + (radius * var) * std::cos(a), cy + (radius * var) * std::sin(a));
        }
        glEnd();
    }

    float centerRadius = maxRadius * 0.28f;
    for (int ring = 10; ring >= 1; ring--) {
        float radius = (ring / 10.0f) * centerRadius;
        float alpha = 0.14f * (1.0f - (ring / 10.0f));

        glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.00f, 0.85f, 0.05f, alpha);
        glVertex2f(cx, cy);

        for (int i = 0; i <= 20; i++) {
            float a = i * (2.0f * PI / 20.0f);
            glVertex2f(cx + radius * std::cos(a), cy + radius * std::sin(a));
        }
        glEnd();
    }
}

void drawBackgroundGarden() {
    for (int i = 0; i < 10; i++) {
        glEnable(GL_BLEND);
        glLineWidth(2.5f);
        glBegin(GL_LINES);
        glColor4f(0.28f, 0.62f, 0.18f, 0.25f);
        glVertex2f(bgBlobs[i].x, bgBlobs[i].y);
        glVertex2f(bgBlobs[i].x - 0.02f, -1.35f);
        glEnd();

        drawBlurredBlob(bgBlobs[i].x, bgBlobs[i].y, bgBlobs[i].radius, bgBlobs[i].r, bgBlobs[i].g, bgBlobs[i].b);
    }
}

// --- 2. 2D FLOWER ELEMENTS ---

void drawLeaf(Point origin, bool isRightSide, float scale) {
    glPushMatrix();
    glTranslatef(origin.x, origin.y, 0.0f);
    
    float sideMult = isRightSide ? 1.0f : -1.0f;
    glRotatef(sideMult * 35.0f, 0.0f, 0.0f, 1.0f);
    glScalef(scale * sideMult, scale, 1.0f);

    Point base  = { 0.00f, 0.00f };
    Point midL  = {-0.10f, 0.20f };
    Point tip   = { 0.04f, 0.45f };
    Point midR  = { 0.11f, 0.22f };

    std::vector<Point> boundary;

    for (int i = 0; i <= 20; i++) {
        float t = i / 20.0f;
        boundary.push_back(bezier(base, midL, {-0.06f, 0.32f}, tip, t));
    }
    for (int i = 0; i <= 20; i++) {
        float t = i / 20.0f;
        boundary.push_back(bezier(tip, {0.08f, 0.32f}, midR, base, t));
    }

    glBegin(GL_TRIANGLE_FAN);
    glColor3f(0.12f, 0.42f, 0.08f);
    glVertex2f(base.x, base.y);

    for (const auto& p : boundary) {
        float dist = std::sqrt(p.x * p.x + p.y * p.y);
        float t = dist / 0.45f;
        glColor3f(0.12f + t * 0.15f, 0.42f + t * 0.22f, 0.08f + t * 0.04f);
        glVertex2f(p.x, p.y);
    }
    glEnd();

    glLineWidth(1.0f);
    glBegin(GL_LINE_STRIP);
    glColor3f(0.20f, 0.55f, 0.12f);
    for (int i = 0; i <= 15; i++) {
        float t = i / 15.0f;
        Point vp = bezier(base, {0.0f, 0.20f}, {0.015f, 0.36f}, tip, t);
        glVertex2f(vp.x, vp.y);
    }
    glEnd();

    glPopMatrix();
}

void drawStem(Point s0, Point s1, Point s2, Point s3, float stemWidth) {
    const int steps = 30;

    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= steps; i++) {
        float t = i / (float)steps;
        Point p = bezier(s0, s1, s2, s3, t);

        float dt = 0.001f;
        Point pNext = bezier(s0, s1, s2, s3, t + dt);
        float dx = pNext.x - p.x;
        float dy = pNext.y - p.y;
        float len = std::sqrt(dx*dx + dy*dy);
        float nx = -dy / len;
        float ny =  dx / len;

        glColor3f(0.16f + t * 0.12f, 0.48f + t * 0.15f, 0.08f + t * 0.04f);
        glVertex2f(p.x - nx * stemWidth * 0.5f, p.y - ny * stemWidth * 0.5f);
        glVertex2f(p.x + nx * stemWidth * 0.5f, p.y + ny * stemWidth * 0.5f);
    }
    glEnd();

    Point leafAttach1 = bezier(s0, s1, s2, s3, 0.38f);
    Point leafAttach2 = bezier(s0, s1, s2, s3, 0.68f);

    drawLeaf(leafAttach1, true,  0.75f);
    drawLeaf(leafAttach1, false, 0.68f);
    drawLeaf(leafAttach2, true,  0.90f);
    drawLeaf(leafAttach2, false, 0.82f);

    glBegin(GL_TRIANGLES);
    glColor3f(0.16f, 0.45f, 0.08f);
    for (int i = 0; i < 7; i++) {
        float angle = i * (2.0f * PI / 7.0f) + 0.15f;
        float nextAngle = angle + 0.35f;
        glVertex2f(s0.x, s0.y);
        glVertex2f(s0.x + 0.11f * cos(angle), s0.y + 0.11f * sin(angle));
        glVertex2f(s0.x + 0.07f * cos(nextAngle), s0.y + 0.07f * sin(nextAngle));
    }
    glEnd();
}

void drawCosmosPetal(const PetalProps& props, bool isBackLayer) {
    Point base      = { 0.00f, 0.05f };
    Point leftMid   = {-0.24f * props.scaleW + props.bend, 0.42f * props.scaleH };
    Point tipLeft   = {-0.16f * props.scaleW + props.bend, 0.78f * props.scaleH };

    Point notch1    = {-0.05f + props.bend, 0.77f * props.scaleH };
    Point tipMid    = { 0.00f + props.bend, 0.81f * props.scaleH };
    Point notch2    = { 0.05f + props.bend, 0.77f * props.scaleH };

    Point tipRight  = { 0.16f * props.scaleW + props.bend, 0.78f * props.scaleH };
    Point rightMid  = { 0.24f * props.scaleW + props.bend, 0.42f * props.scaleH };

    std::vector<Point> boundary;

    for (int i = 0; i <= 30; i++) {
        float t = i / 30.0f;
        boundary.push_back(bezier(base, leftMid, {-0.18f + props.bend, 0.65f}, tipLeft, t));
    }
    boundary.push_back(notch1);
    boundary.push_back(tipMid);
    boundary.push_back(notch2);
    for (int i = 0; i <= 30; i++) {
        float t = i / 30.0f;
        boundary.push_back(bezier(tipRight, {0.18f + props.bend, 0.65f}, rightMid, base, t));
    }

    float dim = isBackLayer ? 0.92f : 1.0f;

    glBegin(GL_TRIANGLE_FAN);
    glColor3f(0.58f * dim, 0.02f * dim, 0.32f * dim);
    glVertex2f(base.x, base.y);

    for (const auto& p : boundary) {
        float dist = std::sqrt(p.x * p.x + p.y * p.y);
        float t = (dist - 0.05f) / 0.77f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        float r, g, b;
        if (t < 0.24f) {
            float factor = t / 0.24f;
            r = 0.68f + factor * 0.14f;
            g = 0.01f + factor * 0.04f;
            b = 0.30f + factor * 0.18f;
        } else {
            float factor = (t - 0.24f) / 0.76f;
            r = 0.82f + factor * 0.16f;
            g = 0.05f + factor * 0.35f;
            b = 0.48f + factor * 0.42f;
        }

        glColor3f(r * dim, g * dim, b * dim);
        glVertex2f(p.x, p.y);
    }
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int v = -3; v <= 3; v++) {
        if (v == 0) continue;
        glLineWidth(1.0f);
        glBegin(GL_LINE_STRIP);
        
        float offsetX = v * 0.022f * props.scaleW;
        for (int i = 0; i <= 25; i++) {
            float t = i / 25.0f;
            Point p0 = { 0.0f, 0.07f };
            Point p1 = { offsetX * 1.20f + props.bend, 0.42f };
            Point p2 = { offsetX * 0.80f + props.bend, 0.74f };
            Point vp = bezier(p0, p1, p1, p2, t);

            float alpha = 0.18f * (1.0f - t * 0.50f);
            glColor4f(0.52f * dim, 0.01f * dim, 0.28f * dim, alpha);
            glVertex2f(vp.x, vp.y);
        }
        glEnd();
    }
}

void drawPetalShadow(const PetalProps& props) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0.12f, 0.01f, 0.05f, 0.22f);
    glVertex2f(0.0f, 0.05f);
    
    glColor4f(0.12f, 0.01f, 0.05f, 0.0f);
    for (int i = 0; i <= 36; i++) {
        float a = i * (2.0f * PI / 36.0f);
        float r = 0.30f * props.scaleW;
        glVertex2f(r * std::cos(a), r * std::sin(a) + 0.08f);
    }
    glEnd();
}

void drawFlowerHead(const PetalProps data[8]) {
    const int PETAL_COUNT = 8;
    float baseAngles[8] = { 0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f };

    for (int i = 1; i < PETAL_COUNT; i += 2) {
        glPushMatrix();
        glRotatef(baseAngles[i] + data[i].angleOffset, 0.0f, 0.0f, 1.0f);
        drawCosmosPetal(data[i], true);
        glPopMatrix();
    }

    for (int i = 0; i < PETAL_COUNT; i += 2) {
        glPushMatrix();
        glRotatef(baseAngles[i] + data[i].angleOffset, 0.0f, 0.0f, 1.0f);
        drawPetalShadow(data[i]);
        glPopMatrix();
    }

    for (int i = 0; i < PETAL_COUNT; i += 2) {
        glPushMatrix();
        glRotatef(baseAngles[i] + data[i].angleOffset, 0.0f, 0.0f, 1.0f);
        drawCosmosPetal(data[i], false);
        glPopMatrix();
    }

    // Center Disc
    const int FLORET_COUNT = 480;
    const float GOLDEN_ANGLE = 137.507764f * (PI / 180.0f);
    const float MAX_RADIUS = 0.145f;

    glBegin(GL_TRIANGLE_FAN);
    glColor3f(0.38f, 0.14f, 0.01f);
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

        glPointSize(1.8f + (r / MAX_RADIUS) * 3.8f);

        float normR = r / MAX_RADIUS;
        if (normR < 0.28f) {
            glColor3f(0.52f, 0.18f, 0.02f);
        } else if (normR < 0.70f) {
            glColor3f(0.98f, 0.62f, 0.02f);
        } else {
            glColor3f(1.00f, 0.92f, 0.12f);
        }

        glBegin(GL_POINTS);
        glVertex2f(x, y);
        glEnd();
    }
}

// --- 3. SCENE DISPLAY ---

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // 1. Background Garden Blobs
    drawBackgroundGarden();

    // 2. Left Hero Flower
    Point s0_L = { -0.72f, -0.22f };
    Point s1_L = { -0.68f, -0.60f };
    Point s2_L = { -0.62f, -0.95f };
    Point s3_L = { -0.55f, -1.35f };
    drawStem(s0_L, s1_L, s2_L, s3_L, 0.018f);

    glPushMatrix();
    glTranslatef(s0_L.x, s0_L.y, 0.0f);
    glScalef(0.66f, 0.66f, 1.0f);
    glRotatef(8.5f, 0.0f, 0.0f, 1.0f);
    drawFlowerHead(petalDataLeft);
    glPopMatrix();

    // 3. Right Hero Flower
    Point s0_R = {  0.68f, -0.26f };
    Point s1_R = {  0.62f, -0.65f };
    Point s2_R = {  0.55f, -0.98f };
    Point s3_R = {  0.48f, -1.35f };
    drawStem(s0_R, s1_R, s2_R, s3_R, 0.018f);

    glPushMatrix();
    glTranslatef(s0_R.x, s0_R.y, 0.0f);
    glScalef(0.68f, 0.68f, 1.0f);
    glRotatef(-11.0f, 0.0f, 0.0f, 1.0f);
    drawFlowerHead(petalDataRight);
    glPopMatrix();

    // 4. Center Hero Flower
    Point s0_C = {  0.00f, -0.10f };
    Point s1_C = { -0.02f, -0.50f };
    Point s2_C = { -0.06f, -0.90f };
    Point s3_C = { -0.10f, -1.35f };
    drawStem(s0_C, s1_C, s2_C, s3_C, 0.020f);

    glPushMatrix();
    glTranslatef(s0_C.x, s0_C.y, 0.0f);
    glScalef(0.78f, 0.78f, 1.0f);
    glRotatef(-3.5f, 0.0f, 0.0f, 1.0f);
    drawFlowerHead(petalDataHero);
    glPopMatrix();

    glutSwapBuffers();
}

void init() {
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POLYGON_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.6, 1.6, -1.6, 1.6);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_MULTISAMPLE);
    glutInitWindowSize(800, 800);
    glutCreateWindow("2D Vibrant Cosmos Garden Trio");

    init();
    glutDisplayFunc(display);
    glutMainLoop();
    return 0;
}
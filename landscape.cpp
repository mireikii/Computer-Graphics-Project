#include <GL/freeglut.h>
#include <cmath>
#include "landscape.h"

// Helper function for drawing circles
void drawCircle(float x, float y, float radius) {
    glBegin(GL_TRIANGLE_FAN);

    glVertex2f(x, y);

    for (int i = 0; i <= 100; i++) {
        float angle = 2.0f * 3.14159f * i / 100;

        glVertex2f(
            x + radius * cos(angle),
            y + radius * sin(angle)
        );
    }

    glEnd();
}

// 1. Background hills
void drawBackgroundHills() {
    glColor3f(0.55f, 0.75f, 0.45f);

    glBegin(GL_POLYGON);
        glVertex2f(-1.0f, -0.15f);
        glVertex2f(-0.85f, 0.15f);
        glVertex2f(-0.55f, 0.35f);
        glVertex2f(-0.25f, 0.15f);
        glVertex2f(0.10f, 0.40f);
        glVertex2f(0.45f, 0.20f);
        glVertex2f(0.75f, 0.38f);
        glVertex2f(1.0f, 0.15f);
        glVertex2f(1.0f, -0.65f);
        glVertex2f(-1.0f, -0.65f);
    glEnd();
}

// 2. Foreground hills
void drawForegroundHills() {
    glColor3f(0.25f, 0.55f, 0.22f);

    glBegin(GL_POLYGON);
        glVertex2f(-1.0f, -0.35f);
        glVertex2f(-0.75f, -0.10f);
        glVertex2f(-0.40f, -0.30f);
        glVertex2f(-0.10f, -0.12f);
        glVertex2f(0.25f, -0.32f);
        glVertex2f(0.60f, -0.08f);
        glVertex2f(1.0f, -0.28f);
        glVertex2f(1.0f, -0.85f);
        glVertex2f(-1.0f, -0.85f);
    glEnd();
}

// 3. Ground
void drawGround() {
    glColor3f(0.30f, 0.65f, 0.25f);

    glBegin(GL_POLYGON);
        glVertex2f(-1.0f, -0.65f);
        glVertex2f(1.0f, -0.65f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(-1.0f, -1.0f);
    glEnd();
}

// 4. Soil
void drawSoil() {
    glColor3f(0.45f, 0.25f, 0.12f);

    glBegin(GL_POLYGON);
        glVertex2f(-1.0f, -0.85f);
        glVertex2f(1.0f, -0.85f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(-1.0f, -1.0f);
    glEnd();

    glColor3f(0.65f, 0.42f, 0.23f);

    drawCircle(-0.75f, -0.91f, 0.012f);
    drawCircle(-0.40f, -0.95f, 0.015f);
    drawCircle(0.10f, -0.90f, 0.012f);
    drawCircle(0.55f, -0.94f, 0.016f);
    drawCircle(0.82f, -0.89f, 0.012f);
}

// 5. Grass
void drawGrass() {
    glLineWidth(2.0f);
    glColor3f(0.05f, 0.35f, 0.08f);

    glBegin(GL_LINES);

        // Grass patch 1
        glVertex2f(-0.85f, -0.65f);
        glVertex2f(-0.90f, -0.55f);

        glVertex2f(-0.85f, -0.65f);
        glVertex2f(-0.83f, -0.53f);

        glVertex2f(-0.85f, -0.65f);
        glVertex2f(-0.78f, -0.57f);

        // Grass patch 2
        glVertex2f(-0.30f, -0.65f);
        glVertex2f(-0.35f, -0.54f);

        glVertex2f(-0.30f, -0.65f);
        glVertex2f(-0.28f, -0.52f);

        glVertex2f(-0.30f, -0.65f);
        glVertex2f(-0.23f, -0.56f);

        // Grass patch 3
        glVertex2f(0.40f, -0.65f);
        glVertex2f(0.35f, -0.54f);

        glVertex2f(0.40f, -0.65f);
        glVertex2f(0.42f, -0.52f);

        glVertex2f(0.40f, -0.65f);
        glVertex2f(0.47f, -0.57f);

        // Grass patch 4
        glVertex2f(0.85f, -0.65f);
        glVertex2f(0.80f, -0.54f);

        glVertex2f(0.85f, -0.65f);
        glVertex2f(0.87f, -0.52f);

    glEnd();
}

// 6. Bushes
void drawBushes() {
    glColor3f(0.10f, 0.40f, 0.12f);

    // Left bush
    drawCircle(-0.75f, -0.40f, 0.12f);
    drawCircle(-0.62f, -0.40f, 0.15f);
    drawCircle(-0.50f, -0.42f, 0.10f);

    // Right bush
    drawCircle(0.55f, -0.40f, 0.12f);
    drawCircle(0.68f, -0.38f, 0.16f);
    drawCircle(0.82f, -0.42f, 0.11f);
}

// 7. Small plants
void drawSmallPlants() {
    glLineWidth(2.0f);

    glColor3f(0.05f, 0.30f, 0.05f);

    glBegin(GL_LINES);
        glVertex2f(-0.10f, -0.65f);
        glVertex2f(-0.10f, -0.48f);

        glVertex2f(0.15f, -0.65f);
        glVertex2f(0.15f, -0.50f);
    glEnd();

    glColor3f(0.10f, 0.50f, 0.10f);

    glBegin(GL_TRIANGLES);
        glVertex2f(-0.10f, -0.55f);
        glVertex2f(-0.20f, -0.50f);
        glVertex2f(-0.10f, -0.49f);

        glVertex2f(-0.10f, -0.52f);
        glVertex2f(0.00f, -0.46f);
        glVertex2f(-0.10f, -0.47f);

        glVertex2f(0.15f, -0.57f);
        glVertex2f(0.05f, -0.52f);
        glVertex2f(0.15f, -0.50f);

        glVertex2f(0.15f, -0.53f);
        glVertex2f(0.25f, -0.48f);
        glVertex2f(0.15f, -0.48f);
    glEnd();
}

// 8. Rocks
void drawRocks() {
    glColor3f(0.40f, 0.40f, 0.38f);

    drawCircle(-0.45f, -0.72f, 0.035f);
    drawCircle(-0.40f, -0.72f, 0.025f);
    drawCircle(0.70f, -0.73f, 0.04f);
}

// 9. Draw the complete landscape
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

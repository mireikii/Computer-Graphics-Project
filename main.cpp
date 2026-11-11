//g++ main.cpp landscape.cpp -o landscape -lglut -lGL -lGLU
// ./landscape
#include <GL/freeglut.h>
#include "landscape.h"

// Display the landscape
void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    drawLandscape();

    glFlush();
}

// Adjust the graphics window
void reshape(int width, int height) {
    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int main(int argc, char** argv) {

    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);

    glutInitWindowSize(1000, 650);

    glutInitWindowPosition(100, 100);

    glutCreateWindow("A Day in Bloom - Michelle's Landscape");

    // Temporary sky colour
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);

    glutMainLoop();

    return 0;
}

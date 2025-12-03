#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// Global variables for cursor position
double cursorX = 0.0;
double cursorY = 0.0;

// Global variable for right mouse button state
int rightMousePressed = 0;

// Arrays to store square positions
#define MAX_SQUARES 100
double squareX[MAX_SQUARES];
double squareY[MAX_SQUARES];
int squareIsRed[MAX_SQUARES];
int squareCount = 0;

// Simple menu bar configuration (normalized device coordinates)
// Full-width bar at the top, with two small menu items: "Color" and "Exit"
const float menuTop = 1.0f;
const float menuBottom = 0.90f;
const float menuLeft = -1.0f;
const float menuRight = 1.0f;

// "Color" menu item area (smaller button on the left)
const float colorItemLeft = -0.95f;
const float colorItemRight = -0.55f;

// "Exit" menu item area (smaller button on the right)
const float exitItemLeft = 0.55f;
const float exitItemRight = 0.95f;

bool menuRedSquaresOn = false;

// Cursor position callback
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    // Convert screen coordinates to OpenGL coordinates (-1 to 1)
    cursorX = (xpos / width) * 2.0 - 1.0;
    cursorY = -((ypos / height) * 2.0 - 1.0);
}

// Utility to test if cursor is over the "Color" menu item
bool cursor_over_color_item(void) {
    return cursorX >= colorItemLeft && cursorX <= colorItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is over the "Exit" menu item
bool cursor_over_exit_item(void) {
    return cursorX >= exitItemLeft && cursorX <= exitItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (cursor_over_color_item()) {
            menuRedSquaresOn = !menuRedSquaresOn;
        } else if (cursor_over_exit_item()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            rightMousePressed = 1;
            // Store the position where the square should be created
            if (squareCount < MAX_SQUARES) {
                squareX[squareCount] = cursorX;
                squareY[squareCount] = cursorY;
                squareIsRed[squareCount] = menuRedSquaresOn ? 1 : 0;
                squareCount++;
            }
        } else if (action == GLFW_RELEASE) {
            rightMousePressed = 0;
        }
    }
}

void drawTriangle() {
    float size = 0.2f;
    
    glBegin(GL_TRIANGLES);
    
    // Red vertex (top)
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex2f(cursorX, cursorY + size);
    
    // Green vertex (bottom left)
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex2f(cursorX - size, cursorY - size);
    
    // Blue vertex (bottom right)
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex2f(cursorX + size, cursorY - size);
    
    glEnd();
}

void drawSquare() {
    float size = 0.15f;
    
    glBegin(GL_QUADS);
    
    // Blue square
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex2f(cursorX - size, cursorY + size);  // Top left
    glVertex2f(cursorX + size, cursorY + size);  // Top right
    glVertex2f(cursorX + size, cursorY - size);  // Bottom right
    glVertex2f(cursorX - size, cursorY - size);  // Bottom left
    
    glEnd();
}

void drawSquareAt(double x, double y, int isRed) {
    float size = 0.15f;
    
    glBegin(GL_QUADS);
    
    if (isRed) {
        glColor3f(1.0f, 0.0f, 0.0f);
    } else {
        glColor3f(0.0f, 0.0f, 1.0f);
    }
    glVertex2f(x - size, y + size);  // Top left
    glVertex2f(x + size, y + size);  // Top right
    glVertex2f(x + size, y - size);  // Bottom right
    glVertex2f(x - size, y - size);  // Bottom left
    
    glEnd();
}

void drawMenu() {
    // Draw full-width menu bar background
    glBegin(GL_QUADS);
    glColor3f(0.15f, 0.15f, 0.15f);
    glVertex2f(menuLeft, menuTop);
    glVertex2f(menuRight, menuTop);
    glVertex2f(menuRight, menuBottom);
    glVertex2f(menuLeft, menuBottom);
    glEnd();

    // Draw small outlines for the "Color" and "Exit" buttons (no big filled rectangles)
    glColor3f(0.7f, 0.7f, 0.7f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(colorItemLeft, menuTop);
    glVertex2f(colorItemRight, menuTop);
    glVertex2f(colorItemRight, menuBottom);
    glVertex2f(colorItemLeft, menuBottom);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex2f(exitItemLeft, menuTop);
    glVertex2f(exitItemRight, menuTop);
    glVertex2f(exitItemRight, menuBottom);
    glVertex2f(exitItemLeft, menuBottom);
    glEnd();

    // Draw "Color" label — only the font color changes with the toggle
    if (menuRedSquaresOn) {
        glColor3f(1.0f, 0.2f, 0.2f);  // red when ON
    } else {
        glColor3f(0.2f, 0.4f, 1.0f);  // blue when OFF
    }
    float textY = (menuTop + menuBottom) * 0.5f;
    float x = colorItemLeft + 0.02f;
    float h = (menuTop - menuBottom) * 0.5f;
    float w = h * 0.6f;

    glBegin(GL_LINES);
    // "C"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    x += w * 1.3f;

    // "O"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    x += w * 1.3f;

    // "L"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.1f;

    // "O"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    x += w * 1.3f;

    // "R"
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w,       textY);
    glVertex2f(x,           textY);
    glVertex2f(x,           textY);
    glVertex2f(x + w,       textY - h * 0.5f);
    glEnd();

    // Draw "Exit" label in the right button
    glColor3f(1.0f, 1.0f, 1.0f);
    textY = (menuTop + menuBottom) * 0.5f;
    x = exitItemLeft + 0.05f;
    h = (menuTop - menuBottom) * 0.5f;
    w = h * 0.6f;

    glBegin(GL_LINES);
    // "E"
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY);
    glVertex2f(x + w * 0.8f,textY);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.3f;

    // "X"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    x += w * 1.3f;

    // "I"
    glVertex2f(x + w * 0.5f,textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY - h * 0.5f);
    x += w * 0.9f;

    // "T"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY - h * 0.5f);
    glEnd();
}

int main(void) {
    GLFWwindow* window;

    // Initialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    // Create a windowed mode window and its OpenGL context
    window = glfwCreateWindow(800, 600, "RGB Triangle Cursor Tracker", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(window);
    
    // Set the cursor position callback
    glfwSetCursorPosCallback(window, cursor_position_callback);
    
    // Set the mouse button callback
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Clear the screen
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Draw menu bar first so it stays on top visually
        drawMenu();
        
        // Draw all previously created squares
        for (int i = 0; i < squareCount; i++) {
            drawSquareAt(squareX[i], squareY[i], squareIsRed[i]);
        }
        
        // Draw the triangle (always follows cursor)
        drawTriangle();

        // Swap front and back buffers
        glfwSwapBuffers(window);

        // Poll for and process events
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
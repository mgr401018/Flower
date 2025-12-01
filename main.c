#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>

// Global variables for cursor position
double cursorX = 0.0;
double cursorY = 0.0;

// Global variable for right mouse button state
int rightMousePressed = 0;

// Arrays to store square positions
#define MAX_SQUARES 100
double squareX[MAX_SQUARES];
double squareY[MAX_SQUARES];
int squareCount = 0;

// Cursor position callback
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    // Convert screen coordinates to OpenGL coordinates (-1 to 1)
    cursorX = (xpos / width) * 2.0 - 1.0;
    cursorY = -((ypos / height) * 2.0 - 1.0);
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            rightMousePressed = 1;
            // Store the position where the square should be created
            if (squareCount < MAX_SQUARES) {
                squareX[squareCount] = cursorX;
                squareY[squareCount] = cursorY;
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

void drawSquareAt(double x, double y) {
    float size = 0.15f;
    
    glBegin(GL_QUADS);
    
    // Blue square
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex2f(x - size, y + size);  // Top left
    glVertex2f(x + size, y + size);  // Top right
    glVertex2f(x + size, y - size);  // Bottom right
    glVertex2f(x - size, y - size);  // Bottom left
    
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
        
        // Draw all previously created squares
        for (int i = 0; i < squareCount; i++) {
            drawSquareAt(squareX[i], squareY[i]);
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
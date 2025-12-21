#include <GLFW/glfw3.h>
#include <math.h>
#include "block_output.h"
#include "text_renderer.h"

// FlowNode structure definition (must match main.c)
typedef struct FlowNode {
    double x;
    double y;
    float width;
    float height;
    char value[256];
    int type;  // NodeType enum value
} FlowNode;

void draw_block_output(const struct FlowNode *n) {
    // Output block: Parallelogram slanted right (green color)
    glColor3f(0.5f, 0.9f, 0.5f); // Light green
    
    float slant = n->width * 0.15f; // Slant offset
    
    glBegin(GL_QUADS);
    // Top-left
    glVertex2f(n->x - n->width * 0.5f - slant, n->y + n->height * 0.5f);
    // Top-right (slanted right)
    glVertex2f(n->x + n->width * 0.5f - slant, n->y + n->height * 0.5f);
    // Bottom-right (slanted right)
    glVertex2f(n->x + n->width * 0.5f + slant, n->y - n->height * 0.5f);
    // Bottom-left
    glVertex2f(n->x - n->width * 0.5f + slant, n->y - n->height * 0.5f);
    glEnd();
    
    // Border
    glColor3f(0.2f, 0.2f, 0.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(n->x - n->width * 0.5f - slant, n->y + n->height * 0.5f);
    glVertex2f(n->x + n->width * 0.5f - slant, n->y + n->height * 0.5f);
    glVertex2f(n->x + n->width * 0.5f + slant, n->y - n->height * 0.5f);
    glVertex2f(n->x - n->width * 0.5f + slant, n->y - n->height * 0.5f);
    glEnd();
    
    // Draw connectors
    float r = 0.03f;
    glColor3f(0.1f, 0.1f, 0.1f);
    
    // Input connector (top)
    float cx = (float)n->x;
    float cy = (float)(n->y + n->height * 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Output connector (bottom)
    cy = (float)(n->y - n->height * 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Draw value text (starting a bit left of block center)
    if (n->value[0] != '\0') {
        float fontSize = n->height * 0.3f;
        // Position text starting a bit left of block center
        float textX = n->x - n->width * 0.3f;
        float textY = n->y - fontSize * 0.25f;
        draw_text(textX, textY, n->value, fontSize, 0.0f, 0.0f, 0.0f);
    }
}


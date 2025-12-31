#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include "block_converge.h"
#include "text_renderer.h"

// FlowNode structure definition (must match main.c)
typedef struct FlowNode {
    double x;
    double y;
    float width;
    float height;
    char value[256];
    int type;  // NodeType enum value
    int branchColumn;
    int owningIfBlock;
} FlowNode;

void draw_block_converge(const struct FlowNode *n) {
    // Convergence point: Small gray circle
    float radius = n->width * 0.5f;  // Use width as diameter
    
    // Draw filled circle
    glColor3f(0.6f, 0.6f, 0.6f);  // Gray
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(n->x, n->y);
    for (int i = 0; i <= 32; ++i) {
        float angle = (float)i / 32.0f * 6.2831853f;
        glVertex2f(n->x + cosf(angle) * radius, n->y + sinf(angle) * radius);
    }
    glEnd();
    
    // Border
    glColor3f(0.2f, 0.2f, 0.2f);  // Dark gray border
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 32; ++i) {
        float angle = (float)i / 32.0f * 6.2831853f;
        glVertex2f(n->x + cosf(angle) * radius, n->y + sinf(angle) * radius);
    }
    glEnd();
    
    // Draw connectors
    float r = 0.02f;  // Smaller connector size for smaller node
    glColor3f(0.1f, 0.1f, 0.1f);
    
    // Left input connector (for true branch)
    float cx = (float)(n->x - radius);
    float cy = (float)n->y;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Right input connector (for false branch)
    cx = (float)(n->x + radius);
    cy = (float)n->y;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Bottom output connector
    cx = (float)n->x;
    cy = (float)(n->y - radius);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
}



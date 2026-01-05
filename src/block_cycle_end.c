#include <GLFW/glfw3.h>
#include <math.h>
#include "block_cycle_end.h"
#include "text_renderer.h"

// FlowNode structure definition (must match main.c layout)
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

void draw_block_cycle_end(const struct FlowNode *n) {
    // Cycle end point: circular marker with same color as cycle block
    float radius = n->width * 0.5f;
    
    glColor3f(0.95f, 0.6f, 0.15f);  // Orange fill
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(n->x, n->y);
    for (int i = 0; i <= 32; ++i) {
        float angle = (float)i / 32.0f * 6.2831853f;
        glVertex2f(n->x + cosf(angle) * radius, n->y + sinf(angle) * radius);
    }
    glEnd();
    
    glColor3f(0.55f, 0.3f, 0.05f); // Border
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 32; ++i) {
        float angle = (float)i / 32.0f * 6.2831853f;
        glVertex2f(n->x + cosf(angle) * radius, n->y + sinf(angle) * radius);
    }
    glEnd();
    
    // Connectors (top/bottom) to match converge style
    float r = 0.02f;
    glColor3f(0.1f, 0.1f, 0.1f);
    
    // Top connector
    float cx = (float)n->x;
    float cy = (float)(n->y + radius);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Bottom connector
    cy = (float)(n->y - radius);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
}


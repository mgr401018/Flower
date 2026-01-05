#include <GLFW/glfw3.h>
#include <math.h>
#include <string.h>
#include "block_cycle.h"
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

void draw_block_cycle(const struct FlowNode *n) {
    // Cycle block: hexagon-like shape with orange tone
    glColor3f(0.95f, 0.6f, 0.15f); // Orange
    
    float halfW = n->width * 0.5f;
    float halfH = n->height * 0.5f;
    float inset = n->width * 0.18f;
    
    glBegin(GL_POLYGON);
    glVertex2f(n->x - halfW + inset, n->y + halfH);
    glVertex2f(n->x + halfW - inset, n->y + halfH);
    glVertex2f(n->x + halfW, n->y);
    glVertex2f(n->x + halfW - inset, n->y - halfH);
    glVertex2f(n->x - halfW + inset, n->y - halfH);
    glVertex2f(n->x - halfW, n->y);
    glEnd();
    
    // Border
    glColor3f(0.55f, 0.3f, 0.05f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(n->x - halfW + inset, n->y + halfH);
    glVertex2f(n->x + halfW - inset, n->y + halfH);
    glVertex2f(n->x + halfW, n->y);
    glVertex2f(n->x + halfW - inset, n->y - halfH);
    glVertex2f(n->x - halfW + inset, n->y - halfH);
    glVertex2f(n->x - halfW, n->y);
    glEnd();
    
    // Connectors (top and bottom)
    float r = 0.03f;
    glColor3f(0.1f, 0.1f, 0.1f);
    
    // Top connector
    float cx = (float)n->x;
    float cy = (float)(n->y + halfH);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Bottom connector
    cy = (float)(n->y - halfH);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Draw value text centered
    if (n->value[0] != '\0') {
        float fontSize = n->height * 0.3f;
        float textWidth = get_text_width(n->value, fontSize);
        float textX = n->x - textWidth * 0.5f;
        float textY = n->y;
        draw_text(textX, textY, n->value, fontSize, 0.0f, 0.0f, 0.0f);
    }
}


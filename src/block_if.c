#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include "block_if.h"
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

void draw_block_if(const struct FlowNode *n) {
    // IF block: Diamond shape with light blue/cyan color
    glColor3f(0.5f, 0.8f, 1.0f); // Light blue/cyan
    
    // Calculate diamond vertices (rotated square)
    // Diamond extends width/2 horizontally and height/2 vertically
    float halfW = n->width * 0.5f;
    float halfH = n->height * 0.5f;
    
    // Draw filled diamond
    glBegin(GL_QUADS);
    glVertex2f(n->x, n->y + halfH);           // Top
    glVertex2f(n->x + halfW, n->y);           // Right
    glVertex2f(n->x, n->y - halfH);           // Bottom
    glVertex2f(n->x - halfW, n->y);           // Left
    glEnd();
    
    // Border
    glColor3f(0.1f, 0.3f, 0.5f);  // Dark blue border
    glBegin(GL_LINE_LOOP);
    glVertex2f(n->x, n->y + halfH);           // Top
    glVertex2f(n->x + halfW, n->y);           // Right
    glVertex2f(n->x, n->y - halfH);           // Bottom
    glVertex2f(n->x - halfW, n->y);           // Left
    glEnd();
    
    // Draw connectors
    float r = 0.03f;
    glColor3f(0.1f, 0.1f, 0.1f);
    
    // Input connector (top vertex of diamond)
    float cx = (float)n->x;
    float cy = (float)(n->y + halfH);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Left connector (true branch - left vertex of diamond)
    cx = (float)(n->x - halfW);
    cy = (float)n->y;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Right connector (false branch - right vertex of diamond)
    cx = (float)(n->x + halfW);
    cy = (float)n->y;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; ++i) {
        float a = (float)i / 20.0f * 6.2831853f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
    
    // Draw "True" label on the left side
    float fontSize = n->height * 0.25f;
    float trueLabelX = n->x - halfW - 0.15f;  // To the left of left connector
    float trueLabelY = n->y;
    draw_text(trueLabelX, trueLabelY, "True", fontSize, 0.0f, 0.6f, 0.0f);  // Green text
    
    // Draw "False" label on the right side
    float falseLabelX = n->x + halfW + 0.05f;  // To the right of right connector
    float falseLabelY = n->y;
    draw_text(falseLabelX, falseLabelY, "False", fontSize, 0.8f, 0.0f, 0.0f);  // Red text
    
    // Draw condition text centered in the diamond
    if (n->value[0] != '\0') {
        float condFontSize = n->height * 0.2f;
        // Calculate text width and center it in the block
        float textWidth = get_text_width(n->value, condFontSize);
        float textX = n->x - textWidth * 0.5f;  // Center the text
        float textY = n->y;
        draw_text(textX, textY, n->value, condFontSize, 0.0f, 0.0f, 0.0f);
    }
}



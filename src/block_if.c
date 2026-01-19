#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
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
    // Keep font size fixed (not scaled with block size) - use size for 0.35f block
    float fontSize = 0.35f * 0.25f;  // Fixed size, equivalent to original
    float trueLabelX = n->x - halfW - 0.2f;  // Moved further to the left
    float trueLabelY = n->y + 0.15f;  // Moved up a bit
    draw_text(trueLabelX, trueLabelY, "True", fontSize, 0.0f, 0.6f, 0.0f);  // Green text
    
    // Draw "False" label on the right side
    float falseLabelX = n->x + halfW + 0.02f;  // Used to be 0.15
    float falseLabelY = n->y + 0.15f;  // Moved up a bit
    draw_text(falseLabelX, falseLabelY, "False", fontSize, 0.8f, 0.0f, 0.0f);  // Red text
    
    // Draw condition text - inside diamond if short, scale font size if > 11 characters,
    // move below if font size would reach minimum (50%)
    // Keep base font size fixed (not scaled with block size) - use size for 0.35f block
    if (n->value[0] != '\0') {
        float baseFontSize = 0.35f * 0.2f;  // Fixed base size, equivalent to original
        int valueLen = strlen(n->value);
        
        // Calculate when scale factor would hit 0.5 (minimum)
        // scaleFactor = 1.0f - (extraChars * 0.04f) = 0.5
        // extraChars = 12.5, so when extraChars >= 13 (valueLen >= 24), move below
        int maxCharsForInside = 11 + 12;  // 23 characters max before moving below
        
        float condFontSize = baseFontSize;
        float textY = n->y;  // Default: inside the diamond (centered)
        
        if (valueLen > maxCharsForInside) {
            // Move below the IF block when we would hit minimum scale factor
            textY = n->y - halfH - 0.05f;  // Below the diamond, moved up a bit
            // Use base font size when below
        } else if (valueLen > 11) {
            // Scale down font size for each character beyond 11
            int extraChars = valueLen - 11;
            // Reduce font size by 4% for each character beyond 11
            float scaleFactor = 1.0f - (extraChars * 0.04f);
            if (scaleFactor < 0.5f) scaleFactor = 0.5f;  // Minimum 50% of original size
            condFontSize = baseFontSize * scaleFactor;
        }
        
        float textWidth = get_text_width(n->value, condFontSize);
        float textX = n->x - textWidth * 0.5f;  // Center the text
        draw_text(textX, textY, n->value, condFontSize, 0.0f, 0.0f, 0.0f);
    }
}







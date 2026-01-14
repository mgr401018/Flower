#include <GLFW/glfw3.h>
#include <math.h>
#include <stdbool.h>
#include "flowchart_state.h"
#include "text_renderer.h"
#include "block_process.h"
#include "block_input.h"
#include "block_output.h"
#include "block_assignment.h"
#include "block_declare.h"
#include "block_if.h"
#include "block_converge.h"
#include "block_cycle.h"
#include "block_cycle_end.h"

// Forward declarations for helper functions (defined in main.c)
bool is_cycle_loopback(int connIndex);
int get_if_branch_type(int connIndex);
float get_cycle_loopback_offset(int cycleIndex);
bool cursor_over_button(float buttonX, float buttonY, GLFWwindow* window);

static void draw_rounded_rectangle(float x, float y, float width, float height, float radius, bool filled) {
    float halfW = width * 0.5f;
    float halfH = height * 0.5f;
    int segments = 12; // Number of segments per corner
    
    // Ensure radius doesn't exceed half the width or height
    if (radius > halfW) radius = halfW;
    if (radius > halfH) radius = halfH;
    
    // Corner arc centers (these are the inner corners where the arc starts)
    float cx_tl = x - halfW + radius;  // Top-left
    float cy_tl = y + halfH - radius;
    float cx_tr = x + halfW - radius;  // Top-right
    float cy_tr = y + halfH - radius;
    float cx_br = x + halfW - radius;  // Bottom-right
    float cy_br = y - halfH + radius;
    float cx_bl = x - halfW + radius;  // Bottom-left
    float cy_bl = y - halfH + radius;
    
    if (filled) {
        // Draw filled rounded rectangle using a polygon
        glBegin(GL_POLYGON);
        
        // Top-left corner arc (from left edge to top edge, going counter-clockwise)
        for (int i = 0; i <= segments; ++i) {
            float angle = 3.14159265f - 1.57079633f * (float)i / segments; // π to π/2
            glVertex2f(cx_tl + cosf(angle) * radius, cy_tl + sinf(angle) * radius);
        }
        
        // Top edge
        glVertex2f(x + halfW - radius, y + halfH);
        
        // Top-right corner arc
        for (int i = 1; i <= segments; ++i) {
            float angle = 1.57079633f - 1.57079633f * (float)i / segments; // π/2 to 0
            glVertex2f(cx_tr + cosf(angle) * radius, cy_tr + sinf(angle) * radius);
        }
        
        // Right edge
        glVertex2f(x + halfW, y - halfH + radius);
        
        // Bottom-right corner arc
        for (int i = 1; i <= segments; ++i) {
            float angle = 0.0f - 1.57079633f * (float)i / segments; // 0 to -π/2
            glVertex2f(cx_br + cosf(angle) * radius, cy_br + sinf(angle) * radius);
        }
        
        // Bottom edge
        glVertex2f(x - halfW + radius, y - halfH);
        
        // Bottom-left corner arc
        for (int i = 1; i <= segments; ++i) {
            float angle = 4.71238898f - 1.57079633f * (float)i / segments; // 3π/2 to π
            glVertex2f(cx_bl + cosf(angle) * radius, cy_bl + sinf(angle) * radius);
        }
        
        // Left edge
        glVertex2f(x - halfW, y + halfH - radius);
        
        glEnd();
    } else {
        // Draw rounded rectangle border (clockwise from top-left)
        glBegin(GL_LINE_LOOP);
        
        // Top-left corner arc
        for (int i = 0; i <= segments; ++i) {
            float angle = 3.14159265f - 1.57079633f * (float)i / segments;
            glVertex2f(cx_tl + cosf(angle) * radius, cy_tl + sinf(angle) * radius);
        }
        
        // Top edge
        glVertex2f(x + halfW - radius, y + halfH);
        
        // Top-right corner arc
        for (int i = 1; i <= segments; ++i) {
            float angle = 1.57079633f - 1.57079633f * (float)i / segments;
            glVertex2f(cx_tr + cosf(angle) * radius, cy_tr + sinf(angle) * radius);
        }
        
        // Right edge
        glVertex2f(x + halfW, y - halfH + radius);
        
        // Bottom-right corner arc
        for (int i = 1; i <= segments; ++i) {
            float angle = 0.0f - 1.57079633f * (float)i / segments;
            glVertex2f(cx_br + cosf(angle) * radius, cy_br + sinf(angle) * radius);
        }
        
        // Bottom edge
        glVertex2f(x - halfW + radius, y - halfH);
        
        // Bottom-left corner arc
        for (int i = 1; i <= segments; ++i) {
            float angle = 4.71238898f - 1.57079633f * (float)i / segments;
            glVertex2f(cx_bl + cosf(angle) * radius, cy_bl + sinf(angle) * radius);
        }
        
        // Left edge
        glVertex2f(x - halfW, y + halfH - radius);
        
        glEnd();
    }
}

void drawFlowNode(const FlowNode *n) {
    
    // Route to appropriate block drawing function based on type
    if (n->type == NODE_START) {
        // Start node: green rounded rectangle
        glColor3f(0.3f, 0.9f, 0.3f); // green for start
        float radius = (n->width < n->height ? n->width : n->height) * 0.30f; // 25% of smaller dimension
        draw_rounded_rectangle((float)n->x, (float)n->y, n->width, n->height, radius, true);

    // Border
    glColor3f(0.2f, 0.2f, 0.0f);
    draw_rounded_rectangle((float)n->x, (float)n->y, n->width, n->height, radius, false);

        // Output connector (bottom) only
    float r = 0.03f;
        float cx = (float)n->x;
        float cy = (float)(n->y - n->height * 0.5f);
    glColor3f(0.1f, 0.1f, 0.1f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 20; ++i) {
            float a = (float)i / 20.0f * 6.2831853f;
            glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
        }
        glEnd();
        
        // Always draw "START" label centered in the block
        float fontSize = n->height * 0.3f;
        const char* labelText = "START";
        float textWidth = get_text_width(labelText, fontSize);
        float textX = n->x - textWidth * 0.5f;  // Center the text
        float textY = n->y;
        draw_text(textX, textY, labelText, fontSize, 0.0f, 0.0f, 0.0f);
    } else if (n->type == NODE_END) {
        // End node: red rounded rectangle
        glColor3f(0.9f, 0.3f, 0.3f); // red for end
        float radius = (n->width < n->height ? n->width : n->height) * 0.30f; // 25% of smaller dimension
        draw_rounded_rectangle((float)n->x, (float)n->y, n->width, n->height, radius, true);
        
        // Border
        glColor3f(0.2f, 0.2f, 0.0f);
        draw_rounded_rectangle((float)n->x, (float)n->y, n->width, n->height, radius, false);
        
        // Input connector (top) only
        float r = 0.03f;
        float cx = (float)n->x;
        float cy = (float)(n->y + n->height * 0.5f);
        glColor3f(0.1f, 0.1f, 0.1f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 20; ++i) {
            float a = (float)i / 20.0f * 6.2831853f;
            glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
        }
        glEnd();
        
        // Always draw "END" label centered in the block
        float fontSize = n->height * 0.3f;
        const char* labelText = "END";
        float textWidth = get_text_width(labelText, fontSize);
        float textX = n->x - textWidth * 0.5f;  // Center the text
        float textY = n->y;
        draw_text(textX, textY, labelText, fontSize, 0.0f, 0.0f, 0.0f);
    } else if (n->type == NODE_PROCESS || n->type == NODE_NORMAL) {
        // Process block (NODE_NORMAL maps to PROCESS for backward compatibility)
        draw_block_process(n);
    } else if (n->type == NODE_INPUT) {
        draw_block_input(n);
    } else if (n->type == NODE_OUTPUT) {
        draw_block_output(n);
    } else if (n->type == NODE_ASSIGNMENT) {
        draw_block_assignment(n);
    } else if (n->type == NODE_DECLARE) {
        draw_block_declare(n);
    } else if (n->type == NODE_IF) {
        draw_block_if(n);
    } else if (n->type == NODE_CONVERGE) {
        draw_block_converge(n);
    } else if (n->type == NODE_CYCLE) {
        draw_block_cycle(n);
    } else if (n->type == NODE_CYCLE_END) {
        draw_block_cycle_end(n);
    }
}

static void draw_cycle_loopbacks(void) {
    glLineWidth(2.5f);
    for (int i = 0; i < cycleBlockCount; i++) {
        const CycleBlock *cycle = &cycleBlocks[i];
        if (cycle->cycleNodeIndex < 0 || cycle->cycleNodeIndex >= nodeCount ||
            cycle->cycleEndNodeIndex < 0 || cycle->cycleEndNodeIndex >= nodeCount) {
            continue;
        }
        const FlowNode *loopNode = &nodes[cycle->cycleNodeIndex];
        const FlowNode *endNode = &nodes[cycle->cycleEndNodeIndex];
        
        // Use an orange tone for loops
        glColor3f(0.95f, 0.6f, 0.15f);
        
        float offset = get_cycle_loopback_offset(i);
        
        // Start from the left side center of the cycle block
        float startX = (float)(loopNode->x - loopNode->width * 0.5f);
        float startY = (float)loopNode->y;
        float anchorX = startX - offset;
        
        float targetX, targetY;
        if (cycle->cycleType == CYCLE_DO) {
            // DO: loopback goes downward from cycle block to end block
            targetX = (float)endNode->x;
            targetY = (float)(endNode->y + endNode->height * 0.5f);
        } else {
            // WHILE / FOR: loopback goes upward from cycle block to end block
            targetX = (float)endNode->x;
            targetY = (float)(endNode->y - endNode->height * 0.5f);
        }
        
        glBegin(GL_LINES);
        // Horizontal from cycle block left side center to offset
        glVertex2f(startX, startY);
        glVertex2f(anchorX, startY);
        // Vertical segment
        glVertex2f(anchorX, startY);
        glVertex2f(anchorX, targetY);
        // Back to end block
        glVertex2f(anchorX, targetY);
        glVertex2f(targetX, targetY);
        glEnd();
    }
    glLineWidth(1.0f);
}

void drawFlowchart(GLFWwindow* window) {
    
    // Apply scroll transformation (both horizontal and vertical)
    glPushMatrix();
    // Scale down flowchart to 2/3 size for more screen space
    // Apply scale first, then translate (OpenGL applies in reverse order: translate then scale)
    // Transformation: screen = scale * (world - scrollOffset)
    // When scrollOffset increases, world content moves left (negative direction)
    // Since scrollOffset is in screen coordinates, we need to scale it for world coordinates
    glScalef(FLOWCHART_SCALE, FLOWCHART_SCALE, 1.0f);
    glTranslatef(-(float)scrollOffsetX / FLOWCHART_SCALE, -(float)scrollOffsetY / FLOWCHART_SCALE, 0.0f);
    
    // Set scroll offsets and flowchart scale in text renderer so block labels move with blocks
    // (only while drawing blocks, not menus/buttons)
    // Text renderer applies: screen = FLOWCHART_SCALE * world - scrollOffset
    // So we pass scrollOffset directly (not negated)
    text_renderer_set_scroll_offsets((float)scrollOffsetX, (float)scrollOffsetY);
    text_renderer_set_flowchart_scale(FLOWCHART_SCALE);
    
    // Draw connections as right-angle L-shapes
    glLineWidth(3.0f);
    for (int i = 0; i < connectionCount; ++i) {
        // Skip drawing cycle loopback connections (they're drawn as bracket lines)
        if (is_cycle_loopback(i)) {
            continue;
        }
        
        const FlowNode *from = &nodes[connections[i].fromNode];
        const FlowNode *to   = &nodes[connections[i].toNode];
        
        // Highlight hovered connection
        if (i == hoveredConnection) {
            glColor3f(1.0f, 0.8f, 0.0f);  // Bright orange/yellow glow
        } else {
            glColor3f(0.0f, 0.6f, 0.8f);  // Normal cyan
        }
        
        // Determine if this is an IF branch connection
        int branchType = get_if_branch_type(i);
        
        if (branchType == 0) {
            // True branch (exits left side of IF block)
            float x1 = (float)(from->x - from->width * 0.5f);  // Left side of diamond
            float y1 = (float)from->y;  // Middle of diamond

            int ifBlockIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == connections[i].fromNode) {
                    ifBlockIdx = j;
                    break;
                }
            }
            double branchWidth = (ifBlockIdx >= 0) ? ifBlocks[ifBlockIdx].leftBranchWidth : 1.0;
            
            // Target depends on whether it's a convergence point
            float x2, y2;
            if (to->type == NODE_CONVERGE) {
                // Connect to left side of convergence
                x2 = (float)(to->x - to->width * 0.5f);
                y2 = (float)to->y;
            } else {
                // Connect to top of normal block
                x2 = (float)to->x;
                y2 = (float)(to->y + to->height * 0.5f);
            }
            
            // Route: exit left -> go to branch column center -> down -> arrive at target
            float branchX = (float)(from->x - branchWidth);  // Dynamic branch width
            
            glBegin(GL_LINES);
            // Horizontal segment: from IF left to branch column center
            glVertex2f(x1, y1);
            glVertex2f(branchX, y1);
            // Vertical segment: down to target Y level
            glVertex2f(branchX, y1);
            glVertex2f(branchX, y2);
            // Horizontal segment: from branch column to target
            glVertex2f(branchX, y2);
            glVertex2f(x2, y2);
            glEnd();
            
        } else if (branchType == 1) {
            // False branch (exits right side of IF block)
            float x1 = (float)(from->x + from->width * 0.5f);  // Right side of diamond
            float y1 = (float)from->y;  // Middle of diamond

            int ifBlockIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == connections[i].fromNode) {
                    ifBlockIdx = j;
                    break;
                }
            }
            double branchWidth = (ifBlockIdx >= 0) ? ifBlocks[ifBlockIdx].rightBranchWidth : 1.0;
            
            // Target depends on whether it's a convergence point
            float x2, y2;
            if (to->type == NODE_CONVERGE) {
                // Connect to right side of convergence
                x2 = (float)(to->x + to->width * 0.5f);
                y2 = (float)to->y;
            } else {
                // Connect to top of normal block
                x2 = (float)to->x;
                y2 = (float)(to->y + to->height * 0.5f);
            }
            
            // Route: exit right -> go to branch column center -> down -> arrive at target
            float branchX = (float)(from->x + branchWidth);  // Dynamic branch width
            
            glBegin(GL_LINES);
            // Horizontal segment: from IF right to branch column center
            glVertex2f(x1, y1);
            glVertex2f(branchX, y1);
            // Vertical segment: down to target Y level
            glVertex2f(branchX, y1);
            glVertex2f(branchX, y2);
            // Horizontal segment: from branch column to target
            glVertex2f(branchX, y2);
            glVertex2f(x2, y2);
            glEnd();
            
        } else {
            // Normal connection (not an IF branch)
            // Check if both nodes are in the same branch (non-zero branchColumn and equal)
            bool sameBranch = (from->branchColumn != 0 && from->branchColumn == to->branchColumn);
            
            float x1 = (float)from->x;
            // For cycle end blocks, use radius (width/2) instead of height/2 for connector position
            float y1;
            if (from->type == NODE_CYCLE_END) {
                float fromRadius = from->width * 0.5f;
                y1 = (float)(from->y - fromRadius);
            } else {
                y1 = (float)(from->y - from->height * 0.5f);
            }
            
            float x2 = (float)to->x;
            // For cycle end blocks, use radius (width/2) instead of height/2 for connector position
            float y2;
            if (to->type == NODE_CYCLE_END) {
                float toRadius = to->width * 0.5f;
                y2 = (float)(to->y + toRadius);
            } else {
                y2 = (float)(to->y + to->height * 0.5f);
            }
            
            
            // Special handling for connections TO convergence point from branch blocks
            // This includes nodes in branches (branchColumn != 0) OR nodes that are owned by an IF (nested IF false branches can have branchColumn=0)
            if (to->type == NODE_CONVERGE && (from->branchColumn != 0 || from->owningIfBlock >= 0)) {
                // Source is in a branch, target is convergence
                // Route: from block bottom -> stay in branch column -> horizontal to convergence side
                float branchX = (float)from->x;  // Stay in branch column
                
                // Determine which side of convergence to connect to based on FROM's branchColumn
                // relative to the convergence's branchColumn (for nested IFs)
                float convergeX;
                int connectSide = -1; // 0=left, 1=right
                if (to->branchColumn == 0) {
                    // Convergence is in main branch - use from's branchColumn
                    if (from->branchColumn < 0) {
                        convergeX = (float)(to->x - to->width * 0.5f);
                        connectSide = 0;
                    } else {
                        convergeX = (float)(to->x + to->width * 0.5f);
                        connectSide = 1;
                    }
                } else {
                    // Convergence is in a branch (nested IF)
                    // Connect based on whether from is LEFT or RIGHT of the convergence
                    if (from->branchColumn < to->branchColumn) {
                        // From is MORE negative (further left) - connect to left side
                        convergeX = (float)(to->x - to->width * 0.5f);
                        connectSide = 0;
                    } else {
                        // From is MORE positive (further right) - connect to right side
                        convergeX = (float)(to->x + to->width * 0.5f);
                        connectSide = 1;
                    }
                }
                float convergeY = (float)to->y;
                
                glBegin(GL_LINES);
                // Vertical segment: down in branch column
                glVertex2f(x1, y1);
                glVertex2f(branchX, convergeY);
                // Horizontal segment: to convergence side
                glVertex2f(branchX, convergeY);
                glVertex2f(convergeX, convergeY);
                glEnd();
            } else if (sameBranch) {
                // Both in same branch - draw vertical line
                glBegin(GL_LINES);
                glVertex2f(x1, y1);
                glVertex2f(x2, y2);
                glEnd();
            } else {
                // Different branches or normal routing
                
                // Handle different connection shapes
        if (fabs(x1 - x2) < 0.001f) {
            // Same X: vertical line only
        glBegin(GL_LINES);
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
        glEnd();
        } else if (fabs(y1 - y2) < 0.001f) {
            // Same Y: horizontal line only
            glBegin(GL_LINES);
            glVertex2f(x1, y1);
            glVertex2f(x2, y2);
            glEnd();
        } else {
            // Different X and Y: L-shape (horizontal then vertical)
            float midX = x2;  // Horizontal segment goes to target X
            float midY = y1;  // Vertical segment starts from source Y
            
            glBegin(GL_LINES);
            // Horizontal segment: from source to (target X, source Y)
            glVertex2f(x1, y1);
            glVertex2f(midX, midY);
            // Vertical segment: from (target X, source Y) to target
            glVertex2f(midX, midY);
            glVertex2f(x2, y2);
            glEnd();
                }
            }
        }
    }
    
    // Draw decorative cycle loopback brackets
    draw_cycle_loopbacks();
    
    glLineWidth(1.0f);

    // Draw nodes (block labels will use scroll offsets set above)
    for (int i = 0; i < nodeCount; ++i) {
        drawFlowNode(&nodes[i]);
    }
    
    glPopMatrix();
    
    // Reset scroll offsets to 0 for screen-space elements (menus, buttons)
    text_renderer_set_scroll_offsets(0.0, 0.0);
    text_renderer_set_flowchart_scale(1.0f);  // No scaling for menus/buttons
    
    // Draw popup menu in screen space (not affected by scroll)
    drawPopupMenu(window);
}

void drawPopupMenu(GLFWwindow* window) {
    if (!popupMenu.active) return;
    
    // Menu is stored in screen space (not affected by scroll)
    float menuX = (float)popupMenu.x;
    float menuY = (float)popupMenu.y;
    
    // Calculate font size
    float fontSize = menuItemHeight * 0.45f;  // Slightly smaller for better fit
    float menuItemWidth = menuMinWidth;
    
    // Determine menu item count and items based on menu type
    int currentMenuItemCount = 0;
    if (popupMenu.type == MENU_TYPE_CONNECTION) {
        currentMenuItemCount = connectionMenuItemCount;
    } else if (popupMenu.type == MENU_TYPE_NODE) {
        currentMenuItemCount = nodeMenuItemCount;
    }
    
    float totalMenuHeight = currentMenuItemCount * menuItemHeight + (currentMenuItemCount - 1) * menuItemSpacing;
    
    // Draw menu background (all items)
    glColor3f(0.2f, 0.2f, 0.25f);
    glBegin(GL_QUADS);
    glVertex2f(menuX, menuY);
    glVertex2f(menuX + menuItemWidth, menuY);
    glVertex2f(menuX + menuItemWidth, menuY - totalMenuHeight);
    glVertex2f(menuX, menuY - totalMenuHeight);
    glEnd();
    
    // Draw menu border
    glColor3f(0.8f, 0.8f, 0.8f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(menuX, menuY);
    glVertex2f(menuX + menuItemWidth, menuY);
    glVertex2f(menuX + menuItemWidth, menuY - totalMenuHeight);
    glVertex2f(menuX, menuY - totalMenuHeight);
    glEnd();
    
    // Draw each menu item
    // Use screen space cursor since menu is in screen space
    for (int i = 0; i < currentMenuItemCount; i++) {
        float itemY = menuY - i * (menuItemHeight + menuItemSpacing);
        float itemBottom = itemY - menuItemHeight;
        
        // Check if hovering this item (using screen space cursor)
        bool hovering = cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
                        cursorY <= itemY && cursorY >= itemBottom;
        
        // Draw hover highlight
        if (hovering) {
            glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBegin(GL_QUADS);
            glVertex2f(menuX, itemY);
            glVertex2f(menuX + menuItemWidth, itemY);
            glVertex2f(menuX + menuItemWidth, itemBottom);
            glVertex2f(menuX, itemBottom);
            glEnd();
            glDisable(GL_BLEND);
        }
        
        // Draw item separator (except for last item)
        if (i < currentMenuItemCount - 1) {
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINES);
            glVertex2f(menuX, itemBottom);
            glVertex2f(menuX + menuItemWidth, itemBottom);
            glEnd();
        }
        
        // Get menu text based on menu type
        const char* menuText = NULL;
        if (popupMenu.type == MENU_TYPE_CONNECTION) {
            menuText = connectionMenuItems[i].text;
        } else if (popupMenu.type == MENU_TYPE_NODE) {
            menuText = nodeMenuItems[i].text;
        }
        
        if (menuText) {
            // Left-align text horizontally (with padding from left edge)
            float textX = menuX + menuPadding;
            
            // Center vertically (adjust slightly for baseline)
            float textY = itemY - menuItemHeight * 0.5f - fontSize * 0.15f;
            
            // Draw the text in white
            draw_text(textX, textY, menuText, fontSize, 1.0f, 1.0f, 1.0f);
        }
    }
}


void drawButtons(GLFWwindow* window) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    float aspectRatio = (float)width / (float)height;
    
    // Convert button positions to new coordinate system (X is now -aspectRatio to aspectRatio)
    // buttonX was -0.95 in old system (-1 to 1), now map to same visual position
    float buttonX_scaled = buttonX * aspectRatio;
    
    bool hoveringClose = cursor_over_button(buttonX_scaled, closeButtonY, window);
    bool hoveringSave = cursor_over_button(buttonX_scaled, saveButtonY, window);
    bool hoveringLoad = cursor_over_button(buttonX_scaled, loadButtonY, window);
    bool hoveringExport = cursor_over_button(buttonX_scaled, exportButtonY, window);
    bool hoveringUndo = cursor_over_button(buttonX_scaled, undoButtonY, window);
    bool hoveringRedo = cursor_over_button(buttonX_scaled, redoButtonY, window);
    
    // Draw close button (red) - use same radius for X and Y to make it circular (top)
    glColor3f(0.9f, 0.2f, 0.2f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX_scaled, closeButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   closeButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw close button border
    glColor3f(0.5f, 0.1f, 0.1f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   closeButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw hover labels for close button
    if (hoveringClose) {
        // Draw label background
        float labelX = buttonX_scaled + buttonRadius + 0.05f;
        float labelY = closeButtonY;
        float labelWidth = 0.18f;
        float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "CLOSE" text
        float fontSize = labelHeight * 0.65f;
        float textWidth = get_text_width("CLOSE", fontSize);
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "CLOSE", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    // Draw save button (blue) - use same radius for X and Y to make it circular
    glColor3f(0.2f, 0.4f, 0.9f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX_scaled, saveButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   saveButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw save button border
    glColor3f(0.1f, 0.2f, 0.5f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   saveButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw load button (yellow) - use same radius for X and Y to make it circular
    glColor3f(0.95f, 0.9f, 0.25f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX_scaled, loadButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   loadButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw load button border
    glColor3f(0.5f, 0.5f, 0.1f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   loadButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
        // Draw hover labels
        if (hoveringSave) {
            // Draw label background
            float labelX = buttonX_scaled + buttonRadius + 0.05f;
            float labelY = saveButtonY;
            float labelWidth = 0.18f;
            float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "SAVE" text using real font (centered in the box)
        float fontSize = labelHeight * 0.65f;  // Reduced from 0.8f for better fit
        float textWidth = get_text_width("SAVE", fontSize);
        // Center horizontally:
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        // Center vertically:
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "SAVE", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    if (hoveringLoad) {
        // Draw label background
        float labelX = buttonX_scaled + buttonRadius + 0.05f;
        float labelY = loadButtonY;
        float labelWidth = 0.18f;
        float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "LOAD" text using real font (centered in the box)
        float fontSize = labelHeight * 0.65f;  // Reduced from 0.8f for better fit
        float textWidth = get_text_width("LOAD", fontSize);
        // Center horizontally
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        float textY = labelY - fontSize * 0.25f;
        // Center vertically
        draw_text(textX, textY, "LOAD", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    // Draw hover labels for close button
    if (hoveringClose) {
        // Draw label background
        float labelX = buttonX_scaled + buttonRadius + 0.05f;
        float labelY = closeButtonY;
        float labelWidth = 0.18f;
        float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "CLOSE" text
        float fontSize = labelHeight * 0.65f;
        float textWidth = get_text_width("CLOSE", fontSize);
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "CLOSE", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    // Draw export button (green) - use same radius for X and Y to make it circular
    glColor3f(0.3f, 0.8f, 0.3f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX_scaled, exportButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   exportButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw export button border
    glColor3f(0.15f, 0.5f, 0.15f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   exportButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw hover labels for export button
    if (hoveringExport) {
        // Draw label background
        float labelX = buttonX_scaled + buttonRadius + 0.05f;
        float labelY = exportButtonY;
        float labelWidth = 0.2f;
        float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "EXPORT" text
        float fontSize = labelHeight * 0.65f;
        float textWidth = get_text_width("EXPORT", fontSize);
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "EXPORT", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    // Draw undo button (purple) - use same radius for X and Y to make it circular
    bool canUndo = (undoHistoryIndex > 0);
    glColor3f(canUndo ? 0.7f : 0.4f, 0.3f, canUndo ? 0.8f : 0.5f);  // Purple, dimmed if disabled
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX_scaled, undoButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   undoButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw undo button border
    glColor3f(canUndo ? 0.4f : 0.2f, 0.15f, canUndo ? 0.5f : 0.3f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   undoButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw redo button (orange) - use same radius for X and Y to make it circular
    bool canRedo = (undoHistoryIndex >= 0 && undoHistoryIndex < undoHistoryCount - 1);
    glColor3f(canRedo ? 1.0f : 0.6f, canRedo ? 0.5f : 0.3f, 0.2f);  // Orange, dimmed if disabled
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX_scaled, redoButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   redoButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw redo button border
    glColor3f(canRedo ? 0.6f : 0.4f, canRedo ? 0.3f : 0.2f, 0.1f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX_scaled + cosf(angle) * buttonRadius, 
                   redoButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw hover labels for undo button
    if (hoveringUndo) {
        // Draw label background
        float labelX = buttonX_scaled + buttonRadius + 0.05f;
        float labelY = undoButtonY;
        float labelWidth = 0.18f;
        float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "UNDO" text
        float fontSize = labelHeight * 0.65f;
        float textWidth = get_text_width("UNDO", fontSize);
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "UNDO", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    // Draw hover labels for redo button
    if (hoveringRedo) {
        // Draw label background
        float labelX = buttonX_scaled + buttonRadius + 0.05f;
        float labelY = redoButtonY;
        float labelWidth = 0.18f;
        float labelHeight = 0.06f;
        
        glColor3f(0.1f, 0.1f, 0.15f);
        glBegin(GL_QUADS);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw label border
        glColor3f(0.7f, 0.7f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(labelX, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY + labelHeight * 0.5f);
        glVertex2f(labelX + labelWidth, labelY - labelHeight * 0.5f);
        glVertex2f(labelX, labelY - labelHeight * 0.5f);
        glEnd();
        
        // Draw "REDO" text
        float fontSize = labelHeight * 0.65f;
        float textWidth = get_text_width("REDO", fontSize);
        float textX = labelX + (labelWidth - textWidth) * 0.5f;  // Center the text
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "REDO", fontSize, 1.0f, 1.0f, 1.0f);
    }
}

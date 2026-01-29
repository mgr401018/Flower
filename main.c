#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#define TINYFD_NOLIB
#include "imports/tinyfiledialogs.h"

#include "src/text_renderer.h"
#include "src/block_process.h"
#include "src/block_input.h"
#include "src/block_output.h"
#include "src/block_assignment.h"
#include "src/block_declare.h"
#include "src/block_if.h"
#include "src/block_converge.h"
#include "src/block_cycle.h"
#include "src/block_cycle_end.h"
#include "src/code_exporter.h"
#include "src/flowchart_state.h"
#include "src/file_io.h"
#include "src/drawing.h"
#include "src/actions.h"

// Global variables for cursor position
double cursorX = 0.0;
double cursorY = 0.0;
bool isPanning = false;
double panStartX = 0.0;
double panStartY = 0.0;
double panStartScrollX = 0.0;
double panStartScrollY = 0.0;

// Hovered connection tracking
int hoveredConnection = -1;

// Scroll offset for panning
double scrollOffsetX = 0.0;
double scrollOffsetY = 0.0;

// Grid system
const double GRID_CELL_SIZE = 0.5;

// Flowchart scale factor (2/3 = 0.6667)
const float FLOWCHART_SCALE = 0.6667f;

// Circular button configuration (top-left corner, vertically aligned)
const float buttonRadius = 0.04f;
const float buttonX = -0.95f;  // Fixed X position for both buttons
const float closeButtonY = 0.9f;  // Red close button (top)
const float saveButtonY = 0.8f;   // Blue save button
const float loadButtonY = 0.7f;   // Yellow load button
const float exportButtonY = 0.6f; // Green export button
const float undoButtonY = 0.5f;   // Purple undo button
const float redoButtonY = 0.4f;   // Orange redo button

// Flowchart node and connection data (now in flowchart_state.h)

FlowNode nodes[MAX_NODES];
int nodeCount = 0;

Connection connections[MAX_CONNECTIONS];
int connectionCount = 0;

// IF Block tracking system (now in flowchart_state.h)

IFBlock ifBlocks[MAX_IF_BLOCKS];
int ifBlockCount = 0;

// Cycle tracking system (now in flowchart_state.h)

CycleBlock cycleBlocks[MAX_CYCLE_BLOCKS];
int cycleBlockCount = 0;

// Undo/Redo system (now in flowchart_state.h)

FlowchartState undoHistory[MAX_UNDO_HISTORY];
int undoHistoryCount = 0;
int undoHistoryIndex = -1;  // -1 means no undo available, 0 means first state, etc.

// Forward declarations
void rebuild_variable_table(void);
int get_if_branch_type(int connIndex);
void reposition_convergence_point(int ifBlockIndex, bool shouldPushNodesBelow);
void update_all_branch_positions(void);
bool is_valid_if_converge_connection(int fromNode, int toNode);
void save_state_for_undo(void);
void perform_undo(void);
void perform_redo(void);
int calculate_branch_depth(int ifBlockIndex, int branchType);
bool is_valid_variable_name(const char* name);
Variable* find_variable(const char* name);
bool variable_name_exists(const char* name, int excludeNodeIndex);
VariableType detect_literal_type(const char* value);
void extract_array_accesses(const char* expr, char arrayNames[][MAX_VAR_NAME_LENGTH], 
                                   char indexExprs[][MAX_VALUE_LENGTH], int* accessCount);

// Cycle helper utilities
int find_cycle_block_by_cycle_node(int nodeIndex) {
    for (int i = 0; i < cycleBlockCount; i++) {
        if (cycleBlocks[i].cycleNodeIndex == nodeIndex) {
            return i;
        }
    }
    return -1;
}

int find_cycle_block_by_end_node(int nodeIndex) {
    for (int i = 0; i < cycleBlockCount; i++) {
        if (cycleBlocks[i].cycleEndNodeIndex == nodeIndex) {
            return i;
        }
    }
    return -1;
}

int calculate_cycle_depth(int cycleIndex) {
    int depth = 0;
    int current = cycleIndex;
    while (current >= 0 && current < cycleBlockCount) {
        depth++;
        current = cycleBlocks[current].parentCycleIndex;
    }
    return depth;
}

float get_cycle_loopback_offset(int cycleIndex) {
    if (cycleIndex < 0 || cycleIndex >= cycleBlockCount) return 0.3f;
    if (cycleBlocks[cycleIndex].loopbackOffset > 0.0f) {
        return cycleBlocks[cycleIndex].loopbackOffset;
    }
    
    // Base offset - doesn't expand with block width
    float offset = 0.3f;
    
    // Count direct children (nested loops)
    int childCount = 0;
    for (int i = 0; i < cycleBlockCount; i++) {
        if (cycleBlocks[i].parentCycleIndex == cycleIndex) {
            childCount++;
        }
    }
    
    // Parent loops (with children) move further left to avoid overlap
    // Children loops stay at base offset
    if (childCount > 0) {
        offset += 0.25f * (float)childCount;
    }
    
    return offset;
}

CycleType prompt_cycle_type(void) {
    // Forward declare - implementation in actions.c
    extern int tinyfd_listDialog(const char* aTitle, const char* aMessage, int numOptions, const char* const* options);
    const char* options[] = {"WHILE", "DO", "FOR"};
    int selection = tinyfd_listDialog("Loop Type", "Select loop type:", 3, options);
    if (selection < 0 || selection > 2) {
        return CYCLE_WHILE;
    }
    return (CycleType)selection;
}

// Variable tracking system (types now in flowchart_state.h)
Variable variables[MAX_VARIABLES];
int variableCount = 0;

// Popup menu state (types now in flowchart_state.h)
PopupMenu popupMenu = {false, MENU_TYPE_CONNECTION, 0.0, 0.0, -1, -1};

// Menu item dimensions
// Menu item dimensions - reduced sizes for better fit on larger window
const float menuItemHeight = 0.12f;  // Reduced from 0.15f
const float menuItemSpacing = 0.015f;  // Reduced from 0.02f
const float menuPadding = 0.03f;  // Reduced from 0.04f
// Original window was 800x600 (aspect 1.333), new is 1600x900 (aspect 1.778)
// Scale menu width to maintain original appearance: 1.333/1.778 ≈ 0.75
const float menuMinWidth = 0.6f * (1.333f / 1.778f);  // Scaled to maintain original size (reduced from 0.8f)

// Menu items (types now in flowchart_state.h)
// Connection menu items (for inserting nodes)
MenuItem connectionMenuItems[] = {
    {"Process", NODE_PROCESS},
    {"Input", NODE_INPUT},
    {"Output", NODE_OUTPUT},
    {"Assignment", NODE_ASSIGNMENT},
    {"Declare", NODE_DECLARE},
    {"IF", NODE_IF},
    {"Cycle", NODE_CYCLE}
};
int connectionMenuItemCount = 7;

// Node menu items (for node operations)
NodeMenuItem nodeMenuItems[] = {
    {"Delete", 0},
    {"Value", 1}
};
int nodeMenuItemCount = 2;

// Deletion toggle (enabled by default)
bool deletionEnabled = true;

// Grid helper functions
double grid_to_world_x(int gridX) {
    return gridX * GRID_CELL_SIZE;
}

double grid_to_world_y(int gridY) {
    return gridY * GRID_CELL_SIZE;
}

int world_to_grid_x(double x) {
    return (int)round(x / GRID_CELL_SIZE);
}

int world_to_grid_y(double y) {
    return (int)round(y / GRID_CELL_SIZE);
}

double snap_to_grid_x(double x) {
    return grid_to_world_x(world_to_grid_x(x));
}

double snap_to_grid_y(double y) {
    return grid_to_world_y(world_to_grid_y(y));
}

// Calculate block width based on text content
float calculate_block_width(const char* text, float fontSize, float minWidth) {
    if (!text || text[0] == '\0') {
        return minWidth;
    }
    
    // Don't expand blocks for short text (3 characters or less)
    int textLen = strlen(text);
    if (textLen <= 3) {
        return minWidth;
    }
    
    float textWidth = get_text_width(text, fontSize);
    // Padding should be proportional to font size for better scaling
    // Use 1.5x font size as padding (0.75x on each side)
    float padding = fontSize * 1.5f;
    float requiredWidth = textWidth + padding;
    
    // Round up to nearest grid cell multiple
    int gridCells = (int)ceil(requiredWidth / GRID_CELL_SIZE);
    float gridAlignedWidth = gridCells * GRID_CELL_SIZE;
    
    // Ensure minimum width
    return gridAlignedWidth > minWidth ? gridAlignedWidth : minWidth;
}

// Cursor position callback
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    float aspectRatio = (float)width / (float)height;
    
    // Convert screen coordinates to OpenGL coordinates
    // X maps to [-aspectRatio, aspectRatio] due to projection matrix
    // Y maps to [-1, 1] (inverted)
    // Don't apply scroll offset here - we'll handle it where needed
    cursorX = (xpos / width) * 2.0 * aspectRatio - aspectRatio;
    cursorY = -((ypos / height) * 2.0 - 1.0);
    
    // Handle mouse panning
    if (isPanning) {
        // Calculate mouse movement delta
        double deltaX = cursorX - panStartX;
        double deltaY = cursorY - panStartY;
        
        // Update scroll offsets (inverse movement - dragging right moves content left)
        scrollOffsetX = panStartScrollX - deltaX;
        scrollOffsetY = panStartScrollY - deltaY;
    }
}

// Scroll callback for panning (both horizontal and vertical)
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window;   // Mark as intentionally unused
    scrollOffsetX -= xoffset * 0.1;  // Smooth horizontal scrolling factor
    scrollOffsetY += yoffset * 0.1;  // Smooth vertical scrolling factor
}

// Check if cursor is over a menu item (deprecated - menu width is now dynamic)
// This function is kept for compatibility but may not work correctly
bool cursor_over_menu_item(double menuX, double menuY, int itemIndex) {
    // Use fixed menu width
    float menuItemWidth = menuMinWidth;
    
    float itemY = menuY - itemIndex * (menuItemHeight + menuItemSpacing);
    return cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
           cursorY <= itemY && cursorY >= itemY - menuItemHeight;
}

// Check if cursor is over a circular button (with aspect ratio correction)
bool cursor_over_button(float buttonX, float buttonY, GLFWwindow* window) {
    // With new projection matrix, cursorX and buttonX are already in the same coordinate system
    // No need to multiply by aspectRatio anymore
    float dx = cursorX - buttonX;
    float dy = cursorY - buttonY;
    return sqrt(dx*dx + dy*dy) <= buttonRadius;
}

// Find which node the cursor is over
int hit_node(double x, double y) {
    for (int i = 0; i < nodeCount; ++i) {
        const FlowNode *n = &nodes[i];
        
        // Check if point is within node bounds
        if (x >= n->x - n->width * 0.5f && x <= n->x + n->width * 0.5f &&
            y >= n->y - n->height * 0.5f && y <= n->y + n->height * 0.5f) {
            return i;
        }
    }
    return -1;
}

// Helper function to calculate distance from point to line segment
static float point_to_line_segment_dist(float px, float py, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len2 = dx*dx + dy*dy;
    
    if (len2 < 0.0001f) {
        // Degenerate segment, just return distance to endpoint
        float d1 = sqrt((px - x1)*(px - x1) + (py - y1)*(py - y1));
        float d2 = sqrt((px - x2)*(px - x2) + (py - y2)*(py - y2));
        return fmin(d1, d2);
    }
    
    float t = ((px - x1) * dx + (py - y1) * dy) / len2;
    t = fmax(0.0f, fmin(1.0f, t));
    
    float projX = x1 + t * dx;
    float projY = y1 + t * dy;
    
    return sqrt((px - projX)*(px - projX) + (py - projY)*(py - projY));
}

// Check if a connection is a cycle loopback (should not be clickable)
bool is_cycle_loopback(int connIndex) {
    if (connIndex < 0 || connIndex >= connectionCount) return false;
    int from = connections[connIndex].fromNode;
    int to = connections[connIndex].toNode;
    
    for (int c = 0; c < cycleBlockCount; c++) {
        int cycleNode = cycleBlocks[c].cycleNodeIndex;
        int endNode = cycleBlocks[c].cycleEndNodeIndex;
        
        // For WHILE/FOR: loopback is end -> cycle (backwards)
        // For DO: loopback is cycle -> end (backwards from normal flow)
        // The loopback is always the connection that goes in the "backwards" direction
        if (cycleBlocks[c].cycleType == CYCLE_DO) {
            // DO: loopback is cycle -> end (this is the bracket line)
            if (from == cycleNode && to == endNode) {
                return true;
            }
        } else {
            // WHILE/FOR: loopback is end -> cycle (this is the bracket line)
            if (from == endNode && to == cycleNode) {
                return true;
            }
        }
    }
    return false;
}

// Find which connection (L-shaped path) the cursor is near
int hit_connection(double x, double y, float threshold) {
    for (int i = 0; i < connectionCount; ++i) {
        // Skip cycle loopback connections (they're drawn as bracket lines, not clickable)
        if (is_cycle_loopback(i)) {
            continue;
        }
        
        const FlowNode *from = &nodes[connections[i].fromNode];
        const FlowNode *to   = &nodes[connections[i].toNode];
        
        // Determine if this is an IF branch connection
        int branchType = get_if_branch_type(i);
        
        float dist;
        
        if (branchType == 0) {
            // True branch (3 segments)
            float x1 = (float)(from->x - from->width * 0.5f);
            float y1 = (float)from->y;
            
            float x2, y2;
            if (to->type == NODE_CONVERGE) {
                x2 = (float)(to->x - to->width * 0.5f);
                y2 = (float)to->y;
            } else {
                x2 = (float)to->x;
                y2 = (float)(to->y + to->height * 0.5f);
            }
            
            // Find IF block to get dynamic branch width
            int ifBlockIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == connections[i].fromNode) {
                    ifBlockIdx = j;
                    break;
                }
            }
            double leftWidth = (ifBlockIdx >= 0) ? ifBlocks[ifBlockIdx].leftBranchWidth : 1.0;
            float branchX = (float)(from->x - leftWidth);  // Branch column center using dynamic width
            
            // Check distance to all three segments
            float dist1 = point_to_line_segment_dist((float)x, (float)y, x1, y1, branchX, y1);
            float dist2 = point_to_line_segment_dist((float)x, (float)y, branchX, y1, branchX, y2);
            float dist3 = point_to_line_segment_dist((float)x, (float)y, branchX, y2, x2, y2);
            
            dist = fmin(dist1, fmin(dist2, dist3));
            
        } else if (branchType == 1) {
            // False branch (3 segments)
            float x1 = (float)(from->x + from->width * 0.5f);
            float y1 = (float)from->y;
            
            float x2, y2;
            if (to->type == NODE_CONVERGE) {
                x2 = (float)(to->x + to->width * 0.5f);
                y2 = (float)to->y;
            } else {
                x2 = (float)to->x;
                y2 = (float)(to->y + to->height * 0.5f);
            }
            
            // Find IF block to get dynamic branch width
            int ifBlockIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == connections[i].fromNode) {
                    ifBlockIdx = j;
                    break;
                }
            }
            double rightWidth = (ifBlockIdx >= 0) ? ifBlocks[ifBlockIdx].rightBranchWidth : 1.0;
            float branchX = (float)(from->x + rightWidth);  // Branch column center using dynamic width
            
            // Check distance to all three segments
            float dist1 = point_to_line_segment_dist((float)x, (float)y, x1, y1, branchX, y1);
            float dist2 = point_to_line_segment_dist((float)x, (float)y, branchX, y1, branchX, y2);
            float dist3 = point_to_line_segment_dist((float)x, (float)y, branchX, y2, x2, y2);
            
            dist = fmin(dist1, fmin(dist2, dist3));
            
        } else {
            // Normal connection
            // Check if both nodes are in the same branch
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
                float branchX = (float)from->x;  // Stay in branch column
                
                // Determine which side of convergence to connect to based on FROM's branchColumn
                // relative to the convergence's branchColumn (for nested IFs)
                float convergeX;
                if (to->branchColumn == 0) {
                    // Convergence is in main branch - use from's branchColumn
                    if (from->branchColumn < 0) {
                        convergeX = (float)(to->x - to->width * 0.5f);
                    } else {
                        convergeX = (float)(to->x + to->width * 0.5f);
                    }
                } else {
                    // Convergence is in a branch (nested IF)
                    // Connect based on whether from is LEFT or RIGHT of the convergence
                    if (from->branchColumn < to->branchColumn) {
                        // From is MORE negative (further left) - connect to left side
                        convergeX = (float)(to->x - to->width * 0.5f);
                    } else {
                        // From is MORE positive (further right) - connect to right side
                        convergeX = (float)(to->x + to->width * 0.5f);
                    }
                }
                float convergeY = (float)to->y;
                
                // Check distance to vertical segment (down in branch)
                float distVert = point_to_line_segment_dist((float)x, (float)y, x1, y1, branchX, convergeY);
                // Check distance to horizontal segment (to convergence)
                float distHoriz = point_to_line_segment_dist((float)x, (float)y, branchX, convergeY, convergeX, convergeY);
                
                dist = fmin(distVert, distHoriz);
            } else if (sameBranch) {
                // Both in same branch - vertical line
                dist = point_to_line_segment_dist((float)x, (float)y, x1, y1, x2, y2);
            } else {
                // Different branches or normal routing
        // Handle different connection shapes
        if (fabs(x1 - x2) < 0.001f) {
            // Same X: vertical line only
            dist = point_to_line_segment_dist((float)x, (float)y, x1, y1, x2, y2);
        } else if (fabs(y1 - y2) < 0.001f) {
            // Same Y: horizontal line only
            dist = point_to_line_segment_dist((float)x, (float)y, x1, y1, x2, y2);
        } else {
            // Different X and Y: L-shape (horizontal then vertical)
            float midX = x2;  // Horizontal segment goes to target X
            float midY = y1;  // Vertical segment starts from source Y
            
            // Check distance to horizontal segment
            float distHoriz = point_to_line_segment_dist((float)x, (float)y, x1, y1, midX, midY);
            // Check distance to vertical segment
            float distVert = point_to_line_segment_dist((float)x, (float)y, midX, midY, x2, y2);
            
            // Use minimum distance to either segment
            dist = fmin(distHoriz, distVert);
                }
            }
        }
        
        if (dist < threshold) {
            return i;
        }
    }
    return -1;
}

// Save flowchart as adjacency matrix
// Function save_flowchart moved to file_io.c

void extract_variables_from_expression_simple(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount) {
    *varCount = 0;
    if (!expr || expr[0] == '\0') return;
    
    const char* p = expr;
    while (*p != '\0' && *varCount < MAX_VARIABLES) {
        // Skip whitespace and operators
        while (*p == ' ' || *p == '\t' || *p == '+' || *p == '-' || *p == '*' || 
               *p == '/' || *p == '(' || *p == ')') {
            p++;
        }
        
        if (*p == '\0') break;
        
        // Check if it starts with letter/underscore (potential variable)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            int len = 0;
            char varName[MAX_VAR_NAME_LENGTH];
            
            // Extract identifier
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
                   (*p >= '0' && *p <= '9') || *p == '_') {
                if (len < MAX_VAR_NAME_LENGTH - 1) {
                    varName[len++] = *p++;
                } else {
                    break;
                }
            }
            varName[len] = '\0';
            
            // Check if it's a valid variable name
            if (len > 0 && is_valid_variable_name(varName)) {
                // Check if already in list
                bool found = false;
                for (int i = 0; i < *varCount; i++) {
                    if (strcmp(varNames[i], varName) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    strncpy(varNames[*varCount], varName, MAX_VAR_NAME_LENGTH - 1);
                    varNames[*varCount][MAX_VAR_NAME_LENGTH - 1] = '\0';
                    (*varCount)++;
                }
            }
        } else {
            p++;
        }
    }
}

// Extract variables from expression (handles array access)
void extract_variables_from_expression(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount) {
    *varCount = 0;
    if (!expr || expr[0] == '\0') return;
    
    // First extract array accesses
    char arrayNames[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
    char indexExprs[MAX_VARIABLES][MAX_VALUE_LENGTH];
    int accessCount = 0;
    extract_array_accesses(expr, arrayNames, indexExprs, &accessCount);
    
    // Add array names to variable list
    for (int i = 0; i < accessCount && *varCount < MAX_VARIABLES; i++) {
        // Check if already in list
        bool found = false;
        for (int j = 0; j < *varCount; j++) {
            if (strcmp(varNames[j], arrayNames[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            strncpy(varNames[*varCount], arrayNames[i], MAX_VAR_NAME_LENGTH - 1);
            varNames[*varCount][MAX_VAR_NAME_LENGTH - 1] = '\0';
            (*varCount)++;
        }
        
        // Also extract variables from index expression
        char indexVars[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
        int indexVarCount = 0;
        extract_variables_from_expression_simple(indexExprs[i], indexVars, &indexVarCount);
        for (int j = 0; j < indexVarCount && *varCount < MAX_VARIABLES; j++) {
            bool found = false;
            for (int k = 0; k < *varCount; k++) {
                if (strcmp(varNames[k], indexVars[j]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                strncpy(varNames[*varCount], indexVars[j], MAX_VAR_NAME_LENGTH - 1);
                varNames[*varCount][MAX_VAR_NAME_LENGTH - 1] = '\0';
                (*varCount)++;
            }
        }
    }
    
    // Remove array access patterns from expression and extract remaining variables
    char cleanedExpr[MAX_VALUE_LENGTH];
    strncpy(cleanedExpr, expr, MAX_VALUE_LENGTH - 1);
    cleanedExpr[MAX_VALUE_LENGTH - 1] = '\0';
    
    // Remove array access patterns (e.g., "arr[index]" -> "arr")
    for (int i = 0; i < accessCount; i++) {
        char pattern[MAX_VALUE_LENGTH];
        snprintf(pattern, sizeof(pattern), "%s[%s]", arrayNames[i], indexExprs[i]);
        char* pos = strstr(cleanedExpr, pattern);
        if (pos) {
            // Replace with just array name
            int patternLen = strlen(pattern);
            int nameLen = strlen(arrayNames[i]);
            memmove(pos + nameLen, pos + patternLen, strlen(pos + patternLen) + 1);
        }
    }
    
    // Extract remaining variables
    char remainingVars[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
    int remainingCount = 0;
    extract_variables_from_expression_simple(cleanedExpr, remainingVars, &remainingCount);
    
    // Add remaining variables to list
    for (int i = 0; i < remainingCount && *varCount < MAX_VARIABLES; i++) {
        bool found = false;
        for (int j = 0; j < *varCount; j++) {
            if (strcmp(varNames[j], remainingVars[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            strncpy(varNames[*varCount], remainingVars[i], MAX_VAR_NAME_LENGTH - 1);
            varNames[*varCount][MAX_VAR_NAME_LENGTH - 1] = '\0';
            (*varCount)++;
        }
    }
}

// Parse array access (format: "arr[index]" or "arr[i]")
// Returns true if it's an array access, extracts array name and index expression
bool parse_array_access(const char* expr, char* arrayName, char* indexExpr) {
    if (!expr || expr[0] == '\0') return false;
    
    const char* p = expr;
    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract array name (must be valid identifier)
    int nameLen = 0;
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
               (*p >= '0' && *p <= '9') || *p == '_') {
            if (nameLen < MAX_VAR_NAME_LENGTH - 1) {
                arrayName[nameLen++] = *p++;
            } else {
                return false;
            }
        }
        arrayName[nameLen] = '\0';
        
        // Check for '['
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            p++; // Skip '['
            // Extract index expression until ']'
            int indexLen = 0;
            while (*p != '\0' && *p != ']' && indexLen < MAX_VALUE_LENGTH - 1) {
                indexExpr[indexLen++] = *p++;
            }
            indexExpr[indexLen] = '\0';
            
            if (*p == ']') {
                return true;
            }
        }
    }
    
    return false;
}

// Evaluate index expression to get integer value (for bounds checking)
// Handles: literals, variables, simple arithmetic (i+1, i-1, etc.)
bool evaluate_index_expression(const char* indexExpr, int* result, char* errorMsg) {
    if (!indexExpr || indexExpr[0] == '\0') {
        strcpy(errorMsg, "Index expression is empty");
        return false;
    }
    
    // Try to parse as integer literal first
    char* endptr;
    long val = strtol(indexExpr, &endptr, 10);
    if (*endptr == '\0') {
        // It's a literal integer
        *result = (int)val;
        return true;
    }
    
    // Check if it's a variable name
    if (is_valid_variable_name(indexExpr)) {
        Variable* var = find_variable(indexExpr);
        if (!var) {
            snprintf(errorMsg, MAX_VALUE_LENGTH, "Index variable '%s' is not declared", indexExpr);
            return false;
        }
        if (var->type != VAR_TYPE_INT) {
            snprintf(errorMsg, MAX_VALUE_LENGTH, "Index variable '%s' must be of type int", indexExpr);
            return false;
        }
        // For now, we can't evaluate variable values at compile time
        // So we'll return a placeholder and check bounds at runtime conceptually
        // For validation, we'll just check the variable exists and is int
        *result = 0; // Placeholder - actual bounds check would need runtime evaluation
        return true;
    }
    
    // Try to parse simple arithmetic (i+1, i-1, etc.)
    // Look for pattern: variable +/- number
    const char* p = indexExpr;
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract variable name
    char varName[MAX_VAR_NAME_LENGTH];
    int nameLen = 0;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
           (*p >= '0' && *p <= '9') || *p == '_') {
        if (nameLen < MAX_VAR_NAME_LENGTH - 1) {
            varName[nameLen++] = *p++;
        } else {
            break;
        }
    }
    varName[nameLen] = '\0';
    
    if (nameLen > 0 && is_valid_variable_name(varName)) {
        Variable* var = find_variable(varName);
        if (!var) {
            snprintf(errorMsg, MAX_VALUE_LENGTH, "Index variable '%s' is not declared", varName);
            return false;
        }
        if (var->type != VAR_TYPE_INT) {
            snprintf(errorMsg, MAX_VALUE_LENGTH, "Index variable '%s' must be of type int", varName);
            return false;
        }
        
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        
        // Check for + or -
        if (*p == '+' || *p == '-') {
            char op = *p++;
            while (*p == ' ' || *p == '\t') p++;
            
            // Try to parse number
            long offset = strtol(p, &endptr, 10);
            if (*endptr == '\0' || (*endptr == ' ' && endptr[1] == '\0')) {
                // Valid arithmetic expression
                *result = (op == '+') ? (int)offset : -(int)offset;
                return true;
            }
        } else if (*p == '\0') {
            // Just a variable
            *result = 0; // Placeholder
            return true;
        }
    }
    
    strcpy(errorMsg, "Invalid index expression. Must be integer literal, int variable, or int variable +/- number");
    return false;
}

// Check array bounds for array access
bool check_array_bounds(const char* arrayName, const char* indexExpr, char* errorMsg) {
    Variable* var = find_variable(arrayName);
    if (!var || !var->is_array) {
        snprintf(errorMsg, MAX_VALUE_LENGTH, "Variable '%s' is not an array", arrayName);
        return false;
    }
    
    if (var->array_size <= 0) {
        // Array size not specified, can't check bounds
        return true;
    }
    
    int indexValue;
    if (!evaluate_index_expression(indexExpr, &indexValue, errorMsg)) {
        return false;
    }
    
    // For variable indices, we can't check bounds at compile time
    // But we validate the variable exists and is int type
    if (is_valid_variable_name(indexExpr)) {
        // It's a variable - already validated in evaluate_index_expression
        return true;
    }
    
    // For literal indices, check bounds
    if (indexValue < 0 || indexValue >= var->array_size) {
        snprintf(errorMsg, MAX_VALUE_LENGTH, 
            "Array index %d is out of bounds. Array '%s' has size %d (valid indices: 0-%d)",
            indexValue, arrayName, var->array_size, var->array_size - 1);
        return false;
    }
    
    return true;
}

// Extract all array accesses from an expression (e.g., "arr[i] = arr[i-1]")
void extract_array_accesses(const char* expr, char arrayNames[][MAX_VAR_NAME_LENGTH], 
                                   char indexExprs[][MAX_VALUE_LENGTH], int* accessCount) {
    *accessCount = 0;
    if (!expr || expr[0] == '\0') return;
    
    const char* p = expr;
    while (*p != '\0' && *accessCount < MAX_VARIABLES) {
        // Look for array access pattern: identifier[
        while (*p != '\0' && *p != '[') {
            // Skip quoted strings
            if (*p == '"') {
                p++;
                while (*p != '\0' && *p != '"') {
                    if (*p == '\\' && p[1] != '\0') p++;
                    p++;
                }
                if (*p == '"') p++;
            } else {
                p++;
            }
        }
        
        if (*p == '\0') break;
        
        // Found '[', now backtrack to find array name
        const char* bracketPos = p;
        p--; // Move back before '['
        
        // Skip whitespace
        while (p > expr && (*p == ' ' || *p == '\t')) p--;
        
        // Extract array name backwards
        int nameEnd = p - expr;
        while (p >= expr && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
               (*p >= '0' && *p <= '9') || *p == '_')) {
            p--;
        }
        p++; // Move to start of name
        
        int nameStart = p - expr;
        if (nameStart < nameEnd) {
            // Extract array name
            int nameLen = nameEnd - nameStart + 1;
            if (nameLen < MAX_VAR_NAME_LENGTH) {
                strncpy(arrayNames[*accessCount], expr + nameStart, nameLen);
                arrayNames[*accessCount][nameLen] = '\0';
                
                // Extract index expression
                p = bracketPos + 1; // After '['
                int indexLen = 0;
                while (*p != '\0' && *p != ']' && indexLen < MAX_VALUE_LENGTH - 1) {
                    indexExprs[*accessCount][indexLen++] = *p++;
                }
                indexExprs[*accessCount][indexLen] = '\0';
                
                if (*p == ']') {
                    (*accessCount)++;
                    p++; // Skip ']'
                }
            }
        } else {
            p = bracketPos + 1;
        }
    }
}

// Validate expression - check all variables exist and infer return type
bool validate_expression(const char* expr, VariableType expectedType, VariableType* actualType, char* errorMsg) {
    if (!expr || expr[0] == '\0') {
        strcpy(errorMsg, "Expression cannot be empty");
        return false;
    }
    
    // Check if expression is a quoted string FIRST - if so, skip variable extraction
    bool isQuotedString = (expr[0] == '"' && expr[strlen(expr) - 1] == '"');
    
    if (isQuotedString) {
        // It's a quoted string literal - set type to STRING and skip variable extraction
        *actualType = VAR_TYPE_STRING;
        // Check if actual type matches expected
        if (*actualType != expectedType) {
            strcpy(errorMsg, "Expression type doesn't match variable type");
            return false;
        }
        return true;
    }
    
    // Check if expression is a boolean literal (true/false) - skip variable extraction
    if (strcmp(expr, "true") == 0 || strcmp(expr, "false") == 0) {
        *actualType = VAR_TYPE_BOOL;
        // Check if actual type matches expected
        if (*actualType != expectedType) {
            strcpy(errorMsg, "Expression type doesn't match variable type");
            return false;
        }
        return true;
    }
    
    // Extract all variables from expression
    char varNames[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
    int varCount = 0;
    extract_variables_from_expression(expr, varNames, &varCount);
    
    // Check all variables are declared
    for (int i = 0; i < varCount; i++) {
        Variable* var = find_variable(varNames[i]);
        if (!var) {
            snprintf(errorMsg, MAX_VALUE_LENGTH, "Variable '%s' is not declared", varNames[i]);
            return false;
        }
    }
    
    // Simple type inference: if expression contains only one variable, use its type
    // If it's a literal, detect its type
    // Otherwise, assume it's an arithmetic expression (int/real)
    if (varCount == 0) {
        // No variables - must be a literal
        *actualType = detect_literal_type(expr);
    } else if (varCount == 1) {
        // Single variable - use its type
        Variable* var = find_variable(varNames[0]);
        *actualType = var->type;
    } else {
        // Multiple variables - check they're all same type and numeric
        Variable* firstVar = find_variable(varNames[0]);
        if (!firstVar) {
            strcpy(errorMsg, "Internal error: variable not found");
            return false;
        }
        
        // All variables must be same type
        for (int i = 1; i < varCount; i++) {
            Variable* var = find_variable(varNames[i]);
            if (!var || var->type != firstVar->type) {
                strcpy(errorMsg, "All variables in expression must be the same type");
                return false;
            }
        }
        
        // Must be numeric for arithmetic
        if (firstVar->type != VAR_TYPE_INT && firstVar->type != VAR_TYPE_REAL) {
            strcpy(errorMsg, "Arithmetic operations only work with numeric types (int/real)");
            return false;
        }
        
        *actualType = firstVar->type;
    }
    
    // Check if actual type matches expected
    if (*actualType != expectedType) {
        strcpy(errorMsg, "Expression type doesn't match variable type");
        return false;
    }
    
    return true;
}

// Parse declare block value (format: "int a" or "real arr[]" or "int arr[10]")
bool parse_declare_block(const char* value, char* varName, VariableType* varType, bool* isArray, int* arraySize) {
    if (!value || value[0] == '\0') return false;
    
    // Skip whitespace
    const char* p = value;
    while (*p == ' ' || *p == '\t') p++;
    
    // Determine type
    if (strncmp(p, "int", 3) == 0 && (p[3] == ' ' || p[3] == '\t')) {
        *varType = VAR_TYPE_INT;
        p += 4;
    } else if (strncmp(p, "real", 4) == 0 && (p[4] == ' ' || p[4] == '\t')) {
        *varType = VAR_TYPE_REAL;
        p += 5;
    } else if (strncmp(p, "string", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
        *varType = VAR_TYPE_STRING;
        p += 7;
    } else if (strncmp(p, "bool", 4) == 0 && (p[4] == ' ' || p[4] == '\t')) {
        *varType = VAR_TYPE_BOOL;
        p += 5;
    } else {
        return false;
    }
    
    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract variable name
    int nameLen = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1) {
        varName[nameLen++] = *p++;
    }
    varName[nameLen] = '\0';
    
    if (nameLen == 0) return false;
    
    // Check for array indicator
    *isArray = false;
    *arraySize = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '[') {
        *isArray = true;
        p++; // Skip '['
        // Try to parse array size
        if (*p >= '0' && *p <= '9') {
            *arraySize = atoi(p);
        }
    }
    
    return true;
}

// Parse assignment (format: "a = 5" or "a = b")
bool parse_assignment(const char* value, char* leftVar, char* rightValue, bool* isRightVar, bool* isQuotedString) {
    if (!value || value[0] == '\0') return false;
    
    // Skip ":=" prefix if present
    const char* p = value;
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p == '=') p++;
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract left side - could be variable or array access
    int leftLen = 0;
    bool inArrayIndex = false;
    while (*p != '\0' && *p != '=' && leftLen < MAX_VAR_NAME_LENGTH - 1) {
        if (*p == '[') {
            inArrayIndex = true;
            // Include '[' in leftVar for array access
            leftVar[leftLen++] = *p++;
            // Skip index expression
            while (*p != '\0' && *p != ']' && leftLen < MAX_VAR_NAME_LENGTH - 1) {
                leftVar[leftLen++] = *p++;
            }
            if (*p == ']') {
                leftVar[leftLen++] = *p++;
                inArrayIndex = false;
            }
        } else if (*p == ' ' || *p == '\t') {
            if (!inArrayIndex) break; // Stop at whitespace if not in array index
            leftVar[leftLen++] = *p++;
        } else {
            leftVar[leftLen++] = *p++;
        }
    }
    leftVar[leftLen] = '\0';
    
    if (leftLen == 0) return false;
    
    // Skip to '='
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract right side - track if it's a quoted string
    bool startsWithQuote = (*p == '"');
    int rightLen = 0;
    bool inQuotes = false;
    
    while (*p != '\0' && rightLen < MAX_VALUE_LENGTH - 1) {
        if (*p == '"' && (rightLen == 0 || rightValue[rightLen - 1] != '\\')) {
            inQuotes = !inQuotes;
            p++;
            continue;
        }
        if (!inQuotes && (*p == '\n' || *p == '\r')) break;
        rightValue[rightLen++] = *p++;
    }
    rightValue[rightLen] = '\0';
    
    // If it started with a quote and we're no longer in quotes, it's a complete quoted string
    *isQuotedString = startsWithQuote && !inQuotes;
    
    // Determine if right side is a variable (starts with letter/underscore, not quoted, not boolean literal)
    *isRightVar = false;
    if (rightLen > 0 && !*isQuotedString) {
        // Check if it's a boolean literal first - these are not variables
        if (strcmp(rightValue, "true") == 0 || strcmp(rightValue, "false") == 0) {
            *isRightVar = false; // Boolean literals are not variables
        } else {
            char first = rightValue[0];
            if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_') {
                // Check if it's all alphanumeric/underscore (variable name)
                bool isVar = true;
                for (int i = 0; i < rightLen; i++) {
                    char c = rightValue[i];
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                          (c >= '0' && c <= '9') || c == '_')) {
                        isVar = false;
                        break;
                    }
                }
                *isRightVar = isVar;
            }
        }
    }
    
    return true;
}

// Detect the type of a literal value
VariableType detect_literal_type(const char* value) {
    if (!value || value[0] == '\0') {
        return VAR_TYPE_INT;  // Default
    }
    
    // Check for quoted string
    if (value[0] == '"' && value[strlen(value) - 1] == '"') {
        return VAR_TYPE_STRING;
    }
    
    // Check for boolean
    if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
        return VAR_TYPE_BOOL;
    }
    
    // Check for real number (contains decimal point)
    const char* p = value;
    bool hasDecimal = false;
    while (*p != '\0') {
        if (*p == '.') {
            hasDecimal = true;
            break;
        }
        p++;
    }
    if (hasDecimal) {
        return VAR_TYPE_REAL;
    }
    
    // Default to int
    return VAR_TYPE_INT;
}

// Extract variable placeholders from output format string (pattern: {varName} or {arr[index]})
void extract_output_placeholders(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount) {
    *varCount = 0;
    if (!formatStr || formatStr[0] == '\0') return;
    
    const char* p = formatStr;
    while (*p != '\0' && *varCount < MAX_VARIABLES) {
        // Look for '{'
        while (*p != '\0' && *p != '{') p++;
        if (*p == '\0') break;
        
        p++; // Skip '{'
        
        // Extract variable name until '}' or '['
        int nameLen = 0;
        bool isArrayAccess = false;
        while (*p != '\0' && *p != '}' && *p != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1) {
            char c = *p;
            // Valid variable name characters
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                (c >= '0' && c <= '9') || c == '_') {
                varNames[*varCount][nameLen++] = c;
                p++;
            } else {
                // Invalid character in placeholder, skip this placeholder
                break;
            }
        }
        
        // Check if it's an array access
        if (*p == '[' && nameLen > 0) {
            isArrayAccess = true;
            p++; // Skip '['
            // Skip index expression until ']'
            while (*p != '\0' && *p != ']') p++;
            if (*p == ']') {
                p++; // Skip ']'
            } else {
                // Missing closing bracket, invalid placeholder
                while (*p != '\0' && *p != '{') p++;
                continue;
            }
        }
        
        if (*p == '}' && nameLen > 0) {
            varNames[*varCount][nameLen] = '\0';
            (*varCount)++;
            p++; // Skip '}'
        } else {
            // Invalid placeholder, skip to next '{' or end
            while (*p != '\0' && *p != '{') p++;
        }
    }
}

// Extract variable placeholders from output format string with array access info (pattern: {varName} or {arr[index]})
void extract_output_placeholders_with_arrays(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], 
                                                    char indexExprs[][MAX_VALUE_LENGTH], bool* isArrayAccess, int* varCount) {
    *varCount = 0;
    if (!formatStr || formatStr[0] == '\0') return;
    
    const char* p = formatStr;
    while (*p != '\0' && *varCount < MAX_VARIABLES) {
        // Look for '{'
        while (*p != '\0' && *p != '{') p++;
        if (*p == '\0') break;
        
        p++; // Skip '{'
        
        // Extract variable name until '}' or '['
        int nameLen = 0;
        isArrayAccess[*varCount] = false;
        indexExprs[*varCount][0] = '\0';
        
        while (*p != '\0' && *p != '}' && *p != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1) {
            char c = *p;
            // Valid variable name characters
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                (c >= '0' && c <= '9') || c == '_') {
                varNames[*varCount][nameLen++] = c;
                p++;
            } else {
                // Invalid character in placeholder, skip this placeholder
                break;
            }
        }
        
        // Check if it's an array access
        if (*p == '[' && nameLen > 0) {
            isArrayAccess[*varCount] = true;
            p++; // Skip '['
            
            // Extract index expression until ']'
            int indexLen = 0;
            while (*p != '\0' && *p != ']' && indexLen < MAX_VALUE_LENGTH - 1) {
                indexExprs[*varCount][indexLen++] = *p++;
            }
            indexExprs[*varCount][indexLen] = '\0';
            
            if (*p == ']') {
                p++; // Skip ']'
            } else {
                // Missing closing bracket, invalid placeholder
                while (*p != '\0' && *p != '{') p++;
                continue;
            }
        }
        
        if (*p == '}' && nameLen > 0) {
            varNames[*varCount][nameLen] = '\0';
            (*varCount)++;
            p++; // Skip '}'
        } else {
            // Invalid placeholder, skip to next '{' or end
            while (*p != '\0' && *p != '{') p++;
        }
    }
}

// Parse input block value (format: "varName" or "arrName[index]")
bool parse_input_block(const char* value, char* varName, char* indexExpr, bool* isArray) {
    if (!value || value[0] == '\0') return false;
    
    // Skip whitespace
    const char* p = value;
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract variable name
    int nameLen = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '_') {
            varName[nameLen++] = c;
            p++;
        } else {
            break;
        }
    }
    varName[nameLen] = '\0';
    
    if (nameLen == 0) return false;
    
    // Check for array access
    *isArray = false;
    indexExpr[0] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '[') {
        *isArray = true;
        p++; // Skip '['
        
        // Extract index expression until ']'
        int indexLen = 0;
        while (*p != '\0' && *p != ']' && indexLen < MAX_VALUE_LENGTH - 1) {
            indexExpr[indexLen++] = *p++;
        }
        indexExpr[indexLen] = '\0';
        
        if (*p != ']') {
            // Missing closing bracket
            return false;
        }
    }
    
    return true;
}

// Validate assignment block
bool validate_assignment(const char* value) {
    char leftVar[MAX_VAR_NAME_LENGTH];
    char rightValue[MAX_VALUE_LENGTH];
    bool isRightVar = false;
    bool isQuotedString = false;
    
    if (!parse_assignment(value, leftVar, rightValue, &isRightVar, &isQuotedString)) {
        return false;
    }
    
    // Extract array name from left side (could be "arr" or "arr[index]")
    char leftArrayName[MAX_VAR_NAME_LENGTH];
    char leftIndexExpr[MAX_VALUE_LENGTH];
    bool isLeftArray = parse_array_access(leftVar, leftArrayName, leftIndexExpr);
    
    const char* leftVarName = isLeftArray ? leftArrayName : leftVar;
    
    // Check if left variable exists
    Variable* leftVarInfo = find_variable(leftVarName);
    if (!leftVarInfo) {
        tinyfd_messageBox("Validation Error", "Variable not declared", "ok", "error", 1);
        return false;
    }
    
    // If left side is array access, validate it
    if (isLeftArray) {
        if (!leftVarInfo->is_array) {
            tinyfd_messageBox("Validation Error", "Left side is array access but variable is not an array", "ok", "error", 1);
            return false;
        }
        char errorMsg[MAX_VALUE_LENGTH];
        if (!check_array_bounds(leftArrayName, leftIndexExpr, errorMsg)) {
            tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
            return false;
        }
    } else {
        if (leftVarInfo->is_array) {
            tinyfd_messageBox("Validation Error", "Variable is an array, use array[index] syntax", "ok", "error", 1);
            return false;
        }
    }
    
    // Check right side - could be variable, array access, or literal
    // IMPORTANT: Check isQuotedString FIRST - if it's a quoted string, it's a literal, not a variable
    if (isQuotedString) {
        // Right side is a quoted string literal - check type matches
        if (leftVarInfo->type != VAR_TYPE_STRING) {
            tinyfd_messageBox("Validation Error", "Type mismatch: quoted string can only be assigned to string variables", "ok", "error", 1);
            return false;
        }
        return true; // Quoted string is valid for string variables
    }
    
    char rightArrayName[MAX_VAR_NAME_LENGTH];
    char rightIndexExpr[MAX_VALUE_LENGTH];
    bool isRightArray = parse_array_access(rightValue, rightArrayName, rightIndexExpr);
    
    if (isRightArray) {
        // Right side is array access
        Variable* rightVarInfo = find_variable(rightArrayName);
        if (!rightVarInfo) {
            tinyfd_messageBox("Validation Error", "Source array not declared", "ok", "error", 1);
            return false;
        }
        if (!rightVarInfo->is_array) {
            tinyfd_messageBox("Validation Error", "Right side is array access but variable is not an array", "ok", "error", 1);
            return false;
        }
        if (rightVarInfo->type != leftVarInfo->type) {
            tinyfd_messageBox("Validation Error", "Type mismatch: array types must match", "ok", "error", 1);
            return false;
        }
        // Check bounds for right side array access
        char errorMsg[MAX_VALUE_LENGTH];
        if (!check_array_bounds(rightArrayName, rightIndexExpr, errorMsg)) {
            tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
            return false;
        }
    } else if (isRightVar) {
        // Right side is a simple variable - check it exists and types match
        Variable* rightVarInfo = find_variable(rightValue);
        if (!rightVarInfo) {
            tinyfd_messageBox("Validation Error", "Source variable not declared", "ok", "error", 1);
            return false;
        }
        if (rightVarInfo->type != leftVarInfo->type) {
            tinyfd_messageBox("Validation Error", "Type mismatch: variables must be same type", "ok", "error", 1);
            return false;
        }
        if (rightVarInfo->is_array) {
            tinyfd_messageBox("Validation Error", "Right side variable is an array, use array[index] syntax", "ok", "error", 1);
            return false;
        }
    } else {
        // Right side is a literal - check type matches
        VariableType literalType = detect_literal_type(rightValue);
        if (literalType != leftVarInfo->type) {
            tinyfd_messageBox("Validation Error", "Type mismatch: literal type doesn't match variable type", "ok", "error", 1);
            return false;
        }
    }
    
    return true;
}

// Rebuild variable table from declare blocks
void rebuild_variable_table(void) {
    variableCount = 0;
    
    for (int i = 0; i < nodeCount; i++) {
        if (nodes[i].type == NODE_DECLARE) {
            char varName[MAX_VAR_NAME_LENGTH];
            VariableType varType;
            bool isArray;
            int arraySize;
            
            if (parse_declare_block(nodes[i].value, varName, &varType, &isArray, &arraySize)) {
                if (variableCount < MAX_VARIABLES) {
                    strncpy(variables[variableCount].name, varName, MAX_VAR_NAME_LENGTH - 1);
                    variables[variableCount].name[MAX_VAR_NAME_LENGTH - 1] = '\0';
                    variables[variableCount].type = varType;
                    variables[variableCount].is_array = isArray;
                    variables[variableCount].array_size = arraySize;
                    variableCount++;
                }
            }
        }
    }
}

// tinyfd_listDialog moved to actions.c

// Check if a variable name is valid (starts with letter/underscore, followed by letters/numbers/underscores)
bool is_valid_variable_name(const char* name) {
    if (!name || name[0] == '\0') {
        return false;
    }
    
    // First character must be letter or underscore
    if (!((name[0] >= 'a' && name[0] <= 'z') || 
          (name[0] >= 'A' && name[0] <= 'Z') || 
          name[0] == '_')) {
        return false;
    }
    
    // Remaining characters must be letters, numbers, or underscores
    for (int i = 1; name[i] != '\0'; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') || 
              (name[i] >= 'A' && name[i] <= 'Z') || 
              (name[i] >= '0' && name[i] <= '9') || 
              name[i] == '_')) {
            return false;
        }
    }
    
    return true;
}

// Check if a variable name already exists (excluding a specific node)
bool variable_name_exists(const char* name, int excludeNodeIndex) {
    if (!name || name[0] == '\0') {
        return false;
    }
    
    // Check in variable table
    for (int i = 0; i < variableCount; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return true;
        }
    }
    
    // Check in all declare nodes (except the excluded one)
    for (int i = 0; i < nodeCount; i++) {
        if (i == excludeNodeIndex) continue;
        if (nodes[i].type == NODE_DECLARE) {
            char varName[MAX_VAR_NAME_LENGTH];
            VariableType varType;
            bool isArray;
            int arraySize;
            if (parse_declare_block(nodes[i].value, varName, &varType, &isArray, &arraySize)) {
                if (strcmp(varName, name) == 0) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

// Find variable by name
Variable* find_variable(const char* name) {
    if (!name || name[0] == '\0') {
        return NULL;
    }
    
    // Check in variable table
    for (int i = 0; i < variableCount; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return &variables[i];
        }
    }
    
    return NULL;
}

// Calculate the depth (in grid cells) of a branch, recursively accounting for nested IFs
// branchType: 0 = true/left, 1 = false/right
int calculate_branch_depth(int ifBlockIndex, int branchType) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) return 0;
    
    int *branchNodes = (branchType == 0) ? ifBlocks[ifBlockIndex].trueBranchNodes : ifBlocks[ifBlockIndex].falseBranchNodes;
    int branchCount = (branchType == 0) ? ifBlocks[ifBlockIndex].trueBranchCount : ifBlocks[ifBlockIndex].falseBranchCount;
    
    if (branchCount == 0) return 0;
    
    int maxDepth = 1;  // At least 1 grid cell for the branch itself
    for (int i = 0; i < branchCount; i++) {
        int nodeIdx = branchNodes[i];
        if (nodeIdx < 0 || nodeIdx >= nodeCount) continue;
        
        if (nodes[nodeIdx].type == NODE_IF) {
            // Find the IF block for this node
            int nestedIfIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                    nestedIfIdx = j;
                    break;
                }
            }
            if (nestedIfIdx >= 0) {
                int nestedDepth = calculate_branch_depth(nestedIfIdx, 0);
                int nestedDepth2 = calculate_branch_depth(nestedIfIdx, 1);
                int maxNested = (nestedDepth > nestedDepth2) ? nestedDepth : nestedDepth2;
                if (maxNested + 1 > maxDepth) {
                    maxDepth = maxNested + 1;
                }
            }
        }
    }
    return maxDepth;
}

// Helper function to reposition the convergence point based on branch lengths
// The convergence point should align with the longest branch
void reposition_convergence_point(int ifBlockIndex, bool shouldPushNodesBelow) {
    
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) {
        return;
    }
    
    // Calculate depth of each branch recursively (accounts for nested IFs)
    int trueDepth = calculate_branch_depth(ifBlockIndex, 0);
    int falseDepth = calculate_branch_depth(ifBlockIndex, 1);
    
    // If branches are equal depth and non-empty, and we're not pushing nodes below,
    // don't move convergence (this prevents unnecessary repositioning during load)
    // However, when shouldPushNodesBelow is true (e.g., when adding a node), we should
    // always recalculate based on actual positions to ensure proper expansion
    if (trueDepth == falseDepth && trueDepth > 0 && !shouldPushNodesBelow) {
        return;
    }
    
    // Position convergence based on branch depth
    int convergeIdx = ifBlocks[ifBlockIndex].convergeNodeIndex;
    if (convergeIdx >= 0 && convergeIdx < nodeCount) {
        double oldConvergeY = nodes[convergeIdx].y;
        double newConvergeY;
        
        int ifNodeIdx = ifBlocks[ifBlockIndex].ifNodeIndex;
        if (ifNodeIdx < 0 || ifNodeIdx >= nodeCount) {
            return;
        }
        
        double ifY = nodes[ifNodeIdx].y;
        int maxDepth = (trueDepth > falseDepth) ? trueDepth : falseDepth;
        
        // Calculate convergence based on actual node positions, not just depth count
        // Find the lowest Y position of any node in either branch
        // Note: In this coordinate system, Y decreases downward (more negative = lower on screen)
        double lowestBranchY = ifY;  // Start with IF Y position
        bool foundBranchNode = false;
        int lowestNodeIdx = -1;
        
        // Check true branch nodes
        for (int i = 0; i < ifBlocks[ifBlockIndex].trueBranchCount; i++) {
            int nodeIdx = ifBlocks[ifBlockIndex].trueBranchNodes[i];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                // In this coordinate system, smaller Y means lower on screen
                if (nodes[nodeIdx].y < lowestBranchY) {
                    lowestBranchY = nodes[nodeIdx].y;
                    lowestNodeIdx = nodeIdx;
                    foundBranchNode = true;
                }
                
                // If this node is a nested IF, recursively check all nodes in its branches
                if (nodes[nodeIdx].type == NODE_IF) {
                    // Find the nested IF block index
                    for (int j = 0; j < ifBlockCount; j++) {
                        if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                            // Check all nodes in the nested IF's true branch
                            for (int k = 0; k < ifBlocks[j].trueBranchCount; k++) {
                                int nestedNodeIdx = ifBlocks[j].trueBranchNodes[k];
                                if (nestedNodeIdx >= 0 && nestedNodeIdx < nodeCount) {
                                    if (nodes[nestedNodeIdx].y < lowestBranchY) {
                                        lowestBranchY = nodes[nestedNodeIdx].y;
                                        lowestNodeIdx = nestedNodeIdx;
                                        foundBranchNode = true;
                                    }
                                }
                            }
                            // Check all nodes in the nested IF's false branch
                            for (int k = 0; k < ifBlocks[j].falseBranchCount; k++) {
                                int nestedNodeIdx = ifBlocks[j].falseBranchNodes[k];
                                if (nestedNodeIdx >= 0 && nestedNodeIdx < nodeCount) {
                                    if (nodes[nestedNodeIdx].y < lowestBranchY) {
                                        lowestBranchY = nodes[nestedNodeIdx].y;
                                        lowestNodeIdx = nestedNodeIdx;
                                        foundBranchNode = true;
                                    }
                                }
                            }
                            // Also check the nested IF's convergence point
                            int nestedConvergeIdx = ifBlocks[j].convergeNodeIndex;
                            if (nestedConvergeIdx >= 0 && nestedConvergeIdx < nodeCount) {
                                double nestedConvergeY = nodes[nestedConvergeIdx].y;
                                if (nestedConvergeY < lowestBranchY) {
                                    lowestBranchY = nestedConvergeY;
                                    lowestNodeIdx = nestedConvergeIdx;
                                    foundBranchNode = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
        
        // Check false branch nodes
        for (int i = 0; i < ifBlocks[ifBlockIndex].falseBranchCount; i++) {
            int nodeIdx = ifBlocks[ifBlockIndex].falseBranchNodes[i];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                // In this coordinate system, smaller Y means lower on screen
                if (nodes[nodeIdx].y < lowestBranchY) {
                    lowestBranchY = nodes[nodeIdx].y;
                    lowestNodeIdx = nodeIdx;
                    foundBranchNode = true;
                }
                
                // If this node is a nested IF, recursively check all nodes in its branches
                if (nodes[nodeIdx].type == NODE_IF) {
                    // Find the nested IF block index
                    for (int j = 0; j < ifBlockCount; j++) {
                        if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                            // Check all nodes in the nested IF's true branch
                            for (int k = 0; k < ifBlocks[j].trueBranchCount; k++) {
                                int nestedNodeIdx = ifBlocks[j].trueBranchNodes[k];
                                if (nestedNodeIdx >= 0 && nestedNodeIdx < nodeCount) {
                                    if (nodes[nestedNodeIdx].y < lowestBranchY) {
                                        lowestBranchY = nodes[nestedNodeIdx].y;
                                        lowestNodeIdx = nestedNodeIdx;
                                        foundBranchNode = true;
                                    }
                                }
                            }
                            // Check all nodes in the nested IF's false branch
                            for (int k = 0; k < ifBlocks[j].falseBranchCount; k++) {
                                int nestedNodeIdx = ifBlocks[j].falseBranchNodes[k];
                                if (nestedNodeIdx >= 0 && nestedNodeIdx < nodeCount) {
                                    if (nodes[nestedNodeIdx].y < lowestBranchY) {
                                        lowestBranchY = nodes[nestedNodeIdx].y;
                                        lowestNodeIdx = nestedNodeIdx;
                                        foundBranchNode = true;
                                    }
                                }
                            }
                            // Also check the nested IF's convergence point
                            int nestedConvergeIdx = ifBlocks[j].convergeNodeIndex;
                            if (nestedConvergeIdx >= 0 && nestedConvergeIdx < nodeCount) {
                                double nestedConvergeY = nodes[nestedConvergeIdx].y;
                                if (nestedConvergeY < lowestBranchY) {
                                    lowestBranchY = nestedConvergeY;
                                    lowestNodeIdx = nestedConvergeIdx;
                                    foundBranchNode = true;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
        
        if (foundBranchNode) {
            // Position convergence below the lowest node
            // Account for the node's height: node center Y - (height/2) = node bottom
            // Then add spacing (GRID_CELL_SIZE) below the node bottom
            double nodeHeight = (lowestNodeIdx >= 0 && lowestNodeIdx < nodeCount) ? nodes[lowestNodeIdx].height : 0.22f;
            double nodeBottomY = lowestBranchY - (nodeHeight * 0.5);
            newConvergeY = nodeBottomY - GRID_CELL_SIZE;
        } else if (maxDepth > 0) {
            // Fallback: use depth-based calculation if no nodes found (shouldn't happen)
            newConvergeY = ifY - (maxDepth * GRID_CELL_SIZE) - GRID_CELL_SIZE;
        } else {
            // Both branches are empty
            // Position convergence at same distance as if there was 1 element (2 grid cells below IF)
            newConvergeY = ifY - (2 * GRID_CELL_SIZE);
        }
        
        double deltaY = newConvergeY - oldConvergeY;
        nodes[convergeIdx].y = newConvergeY;
        
        // If convergence moved (delta != 0), move all nodes below it by the same amount
        // Only move nodes in main branch (branchColumn == 0) since those are below convergence
        // - If convergence moves UP (deltaY > 0): ALWAYS pull nodes up (when deleting)
        // - If convergence moves DOWN (deltaY < 0): ONLY push nodes down if shouldPushNodesBelow is true (when adding)
        bool shouldMoveNodes = false;
        if (fabs(deltaY) > 0.001) {  // Small epsilon to avoid floating point errors
            if (deltaY > 0) {
                // Convergence moved UP - always pull nodes up
                shouldMoveNodes = true;
            } else if (shouldPushNodesBelow) {
                // Convergence moved DOWN - only push if flag is set
                shouldMoveNodes = true;
            }
        }
        
        if (shouldMoveNodes) {
            
            // Store original Y positions BEFORE moving any nodes
            // This ensures we compare against the original positions, not positions that may have been
            // updated by previous reposition_convergence_point calls
            double originalNodeYs[MAX_NODES];
            for (int j = 0; j < nodeCount; j++) {
                originalNodeYs[j] = nodes[j].y;
            }
            
            // Track which IF blocks are moved so we can move their branches too
            int movedIfBlocks[MAX_IF_BLOCKS];
            int movedIfBlockCount = 0;
            
            // Determine which branch this IF block is in (for nested IFs)
            int currentIfBranchColumn = 0;
            int parentIfIdx = -1;
            if (ifBlockIndex >= 0 && ifBlockIndex < ifBlockCount) {
                parentIfIdx = ifBlocks[ifBlockIndex].parentIfIndex;
                if (parentIfIdx >= 0 && parentIfIdx < ifBlockCount) {
                    int ifNodeIdx = ifBlocks[ifBlockIndex].ifNodeIndex;
                    if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount) {
                        currentIfBranchColumn = nodes[ifNodeIdx].branchColumn;
                    }
                }
            }
            
            for (int i = 0; i < nodeCount; i++) {
                
                // Move nodes that are:
                // 1. Below the OLD convergence position (always use old position as reference)
                // 2. Either in the main branch (branchColumn == 0) OR in the same parent branch as this IF
                // 3. Not the convergence point itself
                // 4. Not owned by THIS IF block (to avoid moving branch nodes)
                // 5. CRITICAL: If node is owned by a nested IF, only move if it's in the same nested IF context
                // When convergence moves UP or DOWN: move all nodes below old convergence position
                // This ensures nodes maintain their relative position to convergence
                // IMPORTANT: Use originalNodeYs[i] instead of nodes[i].y to compare against oldConvergeY
                // This ensures we check against the original position, not a position that may have been
                // updated by a previous reposition_convergence_point call
                bool isMainBranch = (nodes[i].branchColumn == 0);
                bool isInSameParentBranch = (parentIfIdx >= 0 && nodes[i].owningIfBlock == parentIfIdx && nodes[i].branchColumn == currentIfBranchColumn);
                
                // CRITICAL FIX: Don't move nodes from a different nested IF, even if they have branchColumn=0
                // Check if the node is owned by a different nested IF that's a sibling (has the same parent)
                bool isFromDifferentNestedIF = false;
                if (nodes[i].owningIfBlock >= 0 && nodes[i].owningIfBlock < ifBlockCount) {
                    int nodeOwningIfParent = ifBlocks[nodes[i].owningIfBlock].parentIfIndex;
                    if (parentIfIdx >= 0 && nodeOwningIfParent == parentIfIdx && nodes[i].owningIfBlock != ifBlockIndex) {
                        // Node is owned by a different nested IF that's a sibling (same parent)
                        isFromDifferentNestedIF = true;
                    }
                }
                
                if (i != convergeIdx && originalNodeYs[i] < oldConvergeY && (isMainBranch || isInSameParentBranch) && nodes[i].owningIfBlock != ifBlockIndex && !isFromDifferentNestedIF) {
                    
                    // double oldNodeY = nodes[i].y;  // Unused
                    nodes[i].y = snap_to_grid_y(nodes[i].y + deltaY);
                    
                    // If this is an IF node, track it to move its branches
                    if (nodes[i].type == NODE_IF) {
                        for (int j = 0; j < ifBlockCount; j++) {
                            if (ifBlocks[j].ifNodeIndex == i) {
                                movedIfBlocks[movedIfBlockCount++] = j;
                                break;
                            }
                        }
                    }
                }
            }
            
            // Move all branch nodes of IF blocks that were moved
            // This includes both branch nodes (branchColumn != 0) and main branch nodes (branchColumn == 0)
            // that belong to the IF block and are below the old convergence position
            for (int i = 0; i < movedIfBlockCount; i++) {
                int movedIfBlockIdx = movedIfBlocks[i];
                for (int j = 0; j < nodeCount; j++) {
                    if (nodes[j].owningIfBlock == movedIfBlockIdx) {
                        // Move branch nodes (branchColumn != 0) - these are always moved
                        // Also move main branch nodes (branchColumn == 0) if they're below the old convergence
                        if (nodes[j].branchColumn != 0 || originalNodeYs[j] < oldConvergeY) {
                            // double oldBranchY = nodes[j].y;  // Unused
                            nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
                        }
                    }
                }
            }
            
            // Also move branch nodes that belong to THIS IF block (ifBlockIndex) and are below the old convergence
            // These are branch nodes in the main branch (branchColumn == 0) that should move with the convergence
            for (int j = 0; j < nodeCount; j++) {
                if (j != convergeIdx && nodes[j].owningIfBlock == ifBlockIndex && 
                    nodes[j].branchColumn == 0 && originalNodeYs[j] < oldConvergeY) {
                    // double oldBranchY = nodes[j].y;  // Unused
                    nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
                }
            }
        }
    }
}

// Helper function to determine which IF branch a connection belongs to
// Returns: -1 = not an IF branch, 0 = true branch (left), 1 = false branch (right)
int get_if_branch_type(int connIndex) {
    const FlowNode *from = &nodes[connections[connIndex].fromNode];
    const FlowNode *to = &nodes[connections[connIndex].toNode];
    
    // Check if source is an IF block
    if (from->type != NODE_IF) {
        return -1;
    }
    
    // CRITICAL FIX: For nested IFs, branchColumn can be ambiguous.
    // We need to check which branch array the target node is actually in.
    // Find the IF block that this connection belongs to
    int ifBlockIdx = -1;
    for (int j = 0; j < ifBlockCount; j++) {
        if (ifBlocks[j].ifNodeIndex == connections[connIndex].fromNode) {
            ifBlockIdx = j;
            break;
        }
    }
    
    if (ifBlockIdx >= 0 && to->type != NODE_CONVERGE) {
        // Check which branch array contains the target node
        // This is the source of truth for nested IFs
        bool foundInTrueBranch = false;
        bool foundInFalseBranch = false;
        
        // Check true branch array
        for (int i = 0; i < ifBlocks[ifBlockIdx].trueBranchCount; i++) {
            if (ifBlocks[ifBlockIdx].trueBranchNodes[i] == connections[connIndex].toNode) {
                foundInTrueBranch = true;
                break;
            }
        }
        
        // Check false branch array
        for (int i = 0; i < ifBlocks[ifBlockIdx].falseBranchCount; i++) {
            if (ifBlocks[ifBlockIdx].falseBranchNodes[i] == connections[connIndex].toNode) {
                foundInFalseBranch = true;
                break;
            }
        }
        
        if (foundInTrueBranch) {
            return 0;  // True branch
        } else if (foundInFalseBranch) {
            return 1;  // False branch
        }
    }
    
    // Fallback: Use branchColumn or connection order for convergence points
    if (to->type == NODE_CONVERGE) {
        // Special case: both IF->convergence connections have the same branchColumn
        // Use connection order instead
        int fromNode = connections[connIndex].fromNode;
        int connectionIndex = 0;
        for (int i = 0; i < connectionCount; i++) {
            if (connections[i].fromNode == fromNode) {
                if (i == connIndex) {
                    return connectionIndex;  // 0 = true (first), 1 = false (second)
                }
                connectionIndex++;
            }
        }
    } else if (to->branchColumn < 0) {
        // Target is in a left branch (negative column) = true branch
        return 0;
    } else if (to->branchColumn > 0) {
        // Target is in a right branch (positive column) = false branch
        return 1;
    } else {
        // Target is in main branch (convergence point)
        // Determine which branch based on which connection we're on
        // We need to check the SPATIAL relationship or connection properties
        
        // Strategy: Check if there are existing connections from this IF to branch nodes
        // and determine the branch based on the visual/spatial arrangement
        int fromNode = connections[connIndex].fromNode;
        
        // Count how many connections go to LEFT (true) vs RIGHT (false) branches
        int leftBranchConns = 0;
        int rightBranchConns = 0;
        
        for (int i = 0; i < connectionCount; i++) {
            if (connections[i].fromNode == fromNode && i != connIndex) {
                int targetNode = connections[i].toNode;
                if (targetNode >= 0 && targetNode < nodeCount) {
                    
                    if (nodes[targetNode].branchColumn < 0) {
                        leftBranchConns++;
                    } else if (nodes[targetNode].branchColumn > 0) {
                        rightBranchConns++;
                    }
                }
            }
        }
        
        // If one branch is empty and we're adding to it, determine by the connection's visual position
        // For now, use connection order but prioritize spatial logic
        // Connection order: first connection = true (0), second = false (1)
        int connectionIndex = 0;
        for (int i = 0; i < connectionCount; i++) {
            if (connections[i].fromNode == fromNode) {
                if (i == connIndex) {
                    // If both branches exist or we can't tell, use connection order
                    // But if only one branch has nodes, we should be adding to the EMPTY one
                    if (leftBranchConns > 0 && rightBranchConns == 0) {
                        // Only left branch has nodes, so we're adding to right (false) branch
                        return 1;
                    } else if (rightBranchConns > 0 && leftBranchConns == 0) {
                        // Only right branch has nodes, so we're adding to left (true) branch
                        return 0;
                    }
                    return connectionIndex;
                }
                connectionIndex++;
            }
        }
    }
    
    return -1;
}

// Calculate required width for a branch, accounting for nested IFs
// branchType: 0 = true/left, 1 = false/right
// Validate that a connection from an IF to a CONVERGE is correct
// Returns: true if valid, false if invalid (cross-IF connection)
bool is_valid_if_converge_connection(int fromNode, int toNode) {
    // Check if this is an IF -> CONVERGE connection
    if (nodes[fromNode].type != NODE_IF || nodes[toNode].type != NODE_CONVERGE) {
        return true;  // Not an IF->CONVERGE connection, allow it
    }
    
    // Find which IF block this IF node belongs to
    int ifBlockIdx = -1;
    for (int i = 0; i < ifBlockCount; i++) {
        if (ifBlocks[i].ifNodeIndex == fromNode) {
            ifBlockIdx = i;
            break;
        }
    }
    
    if (ifBlockIdx < 0) {
        return true;  // IF block not found (shouldn't happen), allow for now
    }
    
    // Check if the convergence node is THIS IF's convergence
    if (ifBlocks[ifBlockIdx].convergeNodeIndex == toNode) {
        return true;  // Valid: IF connecting to its own convergence
    }
    
    // Invalid: IF trying to connect to another IF's convergence
    
    return false;
}

double calculate_branch_width(int ifBlockIndex, int branchType) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) {
        return 1.0;  // Default minimum width
    }

    IFBlock *ifBlock = &ifBlocks[ifBlockIndex];
    double maxWidth = 1.0;  // At least one grid unit

    int *branchNodes = (branchType == 0) ? ifBlock->trueBranchNodes : ifBlock->falseBranchNodes;
    int branchCount = (branchType == 0) ? ifBlock->trueBranchCount : ifBlock->falseBranchCount;

    for (int i = 0; i < branchCount; i++) {
        int nodeIdx = branchNodes[i];
        if (nodeIdx < 0 || nodeIdx >= nodeCount) continue;

        if (nodes[nodeIdx].type == NODE_IF) {
            int nestedIfIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                    nestedIfIdx = j;
                    break;
                }
            }

            if (nestedIfIdx >= 0) {
                double nestedLeft = calculate_branch_width(nestedIfIdx, 0);
                double nestedRight = calculate_branch_width(nestedIfIdx, 1);
                
                // CRITICAL FIX: For nested IFs, we need to account for the expansion needed
                // based on which branch of the nested IF contains deeper nesting.
                // If the nested IF is in the true branch (left), we need space for:
                // - The nested IF's true branch width (expands left)
                // - The nested IF's false branch width (expands right)
                // - 1.0 for the IF node itself
                // But we also need to ensure that if the nested IF's inner branch expands
                // toward the main branch, the parent branch expands enough to prevent overlap.
                
                // Calculate the total width needed: nested IF center + both branch widths
                double nestedTotalWidth = nestedLeft + nestedRight + 1.0;
                
                if (nestedTotalWidth > maxWidth) {
                    maxWidth = nestedTotalWidth;
                }
            }
        } else {
            if (nodes[nodeIdx].width > maxWidth) {
                maxWidth = nodes[nodeIdx].width;
            }
        }
    }

    return maxWidth;
}

// Recursively update X positions for all nodes in an IF block's branches
void update_branch_x_positions(int ifBlockIndex) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) return;

    IFBlock *ifBlock = &ifBlocks[ifBlockIndex];
    double ifCenterX = nodes[ifBlock->ifNodeIndex].x;
    
    // Update convergence node to match IF node X position
    if (ifBlock->convergeNodeIndex >= 0 && ifBlock->convergeNodeIndex < nodeCount) {
        nodes[ifBlock->convergeNodeIndex].x = ifCenterX;
    }

    // Update true branch (left) nodes
    double leftBranchX = ifCenterX - ifBlock->leftBranchWidth;
    for (int i = 0; i < ifBlock->trueBranchCount; i++) {
        int nodeIdx = ifBlock->trueBranchNodes[i];
        if (nodeIdx >= 0 && nodeIdx < nodeCount) {
            nodes[nodeIdx].x = snap_to_grid_x(leftBranchX);

            // If this is a nested IF, recursively update its branches
            if (nodes[nodeIdx].type == NODE_IF) {
                for (int j = 0; j < ifBlockCount; j++) {
                    if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                        update_branch_x_positions(j);
                        break;
                    }
                }
            }
        }
    }

    // Update false branch (right) nodes
    double rightBranchX = ifCenterX + ifBlock->rightBranchWidth;
    for (int i = 0; i < ifBlock->falseBranchCount; i++) {
        int nodeIdx = ifBlock->falseBranchNodes[i];
        if (nodeIdx >= 0 && nodeIdx < nodeCount) {
            nodes[nodeIdx].x = snap_to_grid_x(rightBranchX);

            // If this is a nested IF, recursively update its branches
            if (nodes[nodeIdx].type == NODE_IF) {
                for (int j = 0; j < ifBlockCount; j++) {
                    if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                        update_branch_x_positions(j);
                        break;
                    }
                }
            }
        }
    }
}

// Update all IF block branch widths and node positions
void update_all_branch_positions(void) {
    // First pass: calculate widths bottom-up until stable
    bool changed = true;
    int iterations = 0;
    const int maxIterations = 10;

    while (changed && iterations < maxIterations) {
        changed = false;
        iterations++;

        for (int i = 0; i < ifBlockCount; i++) {
            double oldLeft = ifBlocks[i].leftBranchWidth;
            double oldRight = ifBlocks[i].rightBranchWidth;

            ifBlocks[i].leftBranchWidth = calculate_branch_width(i, 0);
            ifBlocks[i].rightBranchWidth = calculate_branch_width(i, 1);

            if (fabs(ifBlocks[i].leftBranchWidth - oldLeft) > 0.001 ||
                fabs(ifBlocks[i].rightBranchWidth - oldRight) > 0.001) {
                changed = true;
            }
        }
    }

    // Second pass: update positions top-down from root IFs
    for (int i = 0; i < ifBlockCount; i++) {
        if (ifBlocks[i].parentIfIndex == -1) {
            update_branch_x_positions(i);
        }
    }
}

// Insert IF block with branches in a connection
// Function insert_if_block_in_connection moved to actions.c

// Save current state to undo history
void save_state_for_undo(void) {
    // If we're not at the end of history, truncate future history
    if (undoHistoryIndex < undoHistoryCount - 1) {
        undoHistoryCount = undoHistoryIndex + 1;
    }
    
    // Shift history if we're at max capacity
    if (undoHistoryCount >= MAX_UNDO_HISTORY) {
        // Shift all entries left by one
        for (int i = 0; i < MAX_UNDO_HISTORY - 1; i++) {
            undoHistory[i] = undoHistory[i + 1];
        }
        undoHistoryCount = MAX_UNDO_HISTORY - 1;
    }
    
    // Save current state
    FlowchartState *state = &undoHistory[undoHistoryCount];
    state->nodeCount = nodeCount;
    state->connectionCount = connectionCount;
    state->ifBlockCount = ifBlockCount;
    state->cycleBlockCount = cycleBlockCount;
    
    // Copy nodes
    for (int i = 0; i < nodeCount; i++) {
        state->nodes[i] = nodes[i];
    }
    
    // Copy connections
    for (int i = 0; i < connectionCount; i++) {
        state->connections[i] = connections[i];
    }
    
    // Copy IF blocks
    for (int i = 0; i < ifBlockCount; i++) {
        state->ifBlocks[i] = ifBlocks[i];
    }
    
    // Copy cycle blocks
    for (int i = 0; i < cycleBlockCount; i++) {
        state->cycleBlocks[i] = cycleBlocks[i];
    }
    
    undoHistoryCount++;
    undoHistoryIndex = undoHistoryCount - 1;
}

// Restore state from undo history
static void restore_state(const FlowchartState *state) {
    nodeCount = state->nodeCount;
    connectionCount = state->connectionCount;
    ifBlockCount = state->ifBlockCount;
    cycleBlockCount = state->cycleBlockCount;
    
    // Restore nodes
    for (int i = 0; i < nodeCount; i++) {
        nodes[i] = state->nodes[i];
    }
    
    // Restore connections
    for (int i = 0; i < connectionCount; i++) {
        connections[i] = state->connections[i];
    }
    
    // Restore IF blocks
    for (int i = 0; i < ifBlockCount; i++) {
        ifBlocks[i] = state->ifBlocks[i];
    }
    
    // Restore cycle blocks
    for (int i = 0; i < cycleBlockCount; i++) {
        cycleBlocks[i] = state->cycleBlocks[i];
    }
    
    // Rebuild variable table after restore
    rebuild_variable_table();
    
    // Recalculate branch positions after restore (critical for correct rendering)
    update_all_branch_positions();
}

// Perform undo
void perform_undo(void) {
    if (undoHistoryIndex <= 0) {
        return;  // No undo available
    }
    
    undoHistoryIndex--;
    restore_state(&undoHistory[undoHistoryIndex]);
}

// Perform redo
void perform_redo(void) {
    if (undoHistoryIndex >= undoHistoryCount - 1) {
        return;  // No redo available
    }
    
    undoHistoryIndex++;
    restore_state(&undoHistory[undoHistoryIndex]);
}

// Keyboard callback
// Function key_callback moved to actions.c

// Function insert_cycle_block_in_connection moved to actions.c

// Initialize with connected START and END nodes
void initialize_flowchart() {
    nodeCount = 0;
    connectionCount = 0;
    ifBlockCount = 0;
    cycleBlockCount = 0;
    variableCount = 0;
    
    // Create START node first to calculate its width (needed for END positioning)
    nodes[nodeCount].x = 0.0;
    nodes[nodeCount].y = 0.0;
    nodes[nodeCount].height = 0.22f;  // Same height as other blocks
    strcpy(nodes[nodeCount].value, "START");
    nodes[nodeCount].type = NODE_START;
    nodes[nodeCount].branchColumn = 0;
    nodes[nodeCount].owningIfBlock = -1;
    // Calculate width based on text content, same as other blocks
    float fontSize = nodes[nodeCount].height * 0.3f;
    nodes[nodeCount].width = calculate_block_width(nodes[nodeCount].value, fontSize, 0.35f);
    int startIndex = nodeCount++;
    float startWidth = nodes[startIndex].width;  // Store START's width
    
    // Create END node positioned at standard connection length below START
    // Standard connection length formula: from->y - from->height * 0.5 - to->height * 0.5 - initialConnectionLength
    const double initialConnectionLength = 0.28;
    const float nodeHeight = 0.22f;  // Same height for both blocks
    double startBottomY = nodes[startIndex].y - nodes[startIndex].height * 0.5;
    double endTopY = startBottomY - initialConnectionLength;
    double endCenterY = endTopY - nodeHeight * 0.5;
    
    nodes[nodeCount].x = 0.0;
    nodes[nodeCount].y = endCenterY;  // Position END at standard connection length
    nodes[nodeCount].height = 0.22f;  // Same height as other blocks
    strcpy(nodes[nodeCount].value, "END");
    nodes[nodeCount].type = NODE_END;
    nodes[nodeCount].branchColumn = 0;
    nodes[nodeCount].owningIfBlock = -1;
    // Calculate width based on text content, same as other blocks
    fontSize = nodes[nodeCount].height * 0.3f;
    nodes[nodeCount].width = calculate_block_width(nodes[nodeCount].value, fontSize, 0.35f);
    int endIndex = nodeCount++;
    float endWidth = nodes[endIndex].width;  // Store END's width
    
    // Update START to use END's width (make START narrower)
    nodes[startIndex].width = endWidth;
    
    // Connect START to END
    connections[connectionCount].fromNode = startIndex;
    connections[connectionCount].toNode = endIndex;
    connectionCount++;
}

int main(void) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }
    
    GLFWwindow* window = glfwCreateWindow(1600, 900, "Flowchart Editor", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    
    if (!init_text_renderer(NULL)) {
        fprintf(stderr, "Warning: Failed to initialize text renderer\n");
    }
    
    // Initialize with connected START and END nodes
    initialize_flowchart();

    // Set background color to white
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    
    while (!glfwWindowShouldClose(window)) {
        process_pending_file_actions();
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Set up viewport and projection matrix to account for aspect ratio
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
        // Update text renderer with current window size
        text_renderer_set_window_size(width, height);
        
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float aspectRatio = (float)width / (float)height;
        // Use orthographic projection that accounts for aspect ratio
        // This prevents horizontal stretching on wider windows
        // Keep Y range as -1 to 1 to maintain proper vertical proportions
        glOrtho(-aspectRatio, aspectRatio, -1.0, 1.0, -1.0, 1.0);
        
        // Update text renderer with current aspect ratio
        text_renderer_set_aspect_ratio(aspectRatio);
        text_renderer_set_y_scale(1.0f);  // No Y scaling needed
        
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        
        // Update hovered connection (use world-space cursor)
        // Transformation: screen = scale * (world - scrollOffset/scale) = scale * world - scrollOffset
        // So: world = (screen + scrollOffset) / scale
        double worldCursorX = (cursorX + scrollOffsetX) / FLOWCHART_SCALE;
        double worldCursorY = (cursorY + scrollOffsetY) / FLOWCHART_SCALE;
        hoveredConnection = hit_connection(worldCursorX, worldCursorY, 0.05f);
        
        drawFlowchart(window);
        
        // Ensure scroll offsets and flowchart scale are reset for buttons (already reset in drawFlowchart, but be safe)
        text_renderer_set_scroll_offsets(0.0, 0.0);
        text_renderer_set_flowchart_scale(1.0f);
        
        // Draw buttons in screen space (not affected by scroll)
        drawButtons(window);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    cleanup_text_renderer();
    glfwTerminate();
    return 0;
}
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

// Forward declaration for local tinyfd_listDialog implementation
static int tinyfd_listDialog(const char* aTitle, const char* aMessage, int numOptions, const char* const* options);

// Global variables for cursor position
double cursorX = 0.0;
double cursorY = 0.0;

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
const float saveButtonY = 0.9f;   // Blue save button
const float loadButtonY = 0.8f;   // Yellow load button
const float closeButtonY = 0.7f;  // Red close button
const float exportButtonY = 0.6f; // Green export button

// Flowchart node and connection data
#define MAX_NODES 100
#define MAX_CONNECTIONS 200
#define MAX_VALUE_LENGTH 256
#define MAX_VARIABLES 200
#define MAX_VAR_NAME_LENGTH 64
#define MAX_IF_BLOCKS 50

typedef enum {
    NODE_NORMAL = 0,    // Deprecated, maps to NODE_PROCESS
    NODE_START  = 1,
    NODE_END    = 2,
    NODE_PROCESS = 3,
    NODE_INPUT = 4,
    NODE_OUTPUT = 5,
    NODE_ASSIGNMENT = 6,
    NODE_DECLARE = 7,
    NODE_IF = 8,
    NODE_CONVERGE = 9,
    NODE_CYCLE = 10,
    NODE_CYCLE_END = 11
} NodeType;

typedef struct FlowNode {
    double x;
    double y;
    float width;
    float height;
    char value[MAX_VALUE_LENGTH];
    NodeType type;
    int branchColumn;         // 0 = main, -2/-4/-6 = left branches, +2/+4/+6 = right
    int owningIfBlock;        // Index of IF block this node belongs to (-1 if main)
} FlowNode;

typedef struct {
    int fromNode;
    int toNode;
} Connection;

FlowNode nodes[MAX_NODES];
int nodeCount = 0;

Connection connections[MAX_CONNECTIONS];
int connectionCount = 0;

// IF Block tracking system
typedef struct {
    int ifNodeIndex;          // Index of the IF block
    int convergeNodeIndex;    // Index of the convergence point
    int parentIfIndex;        // Parent IF block (-1 if none)
    int branchColumn;         // Column offset from parent (-2 or +2)
    int trueBranchNodes[MAX_NODES];   // Nodes in true branch
    int trueBranchCount;
    int falseBranchNodes[MAX_NODES];  // Nodes in false branch
    int falseBranchCount;
    double leftBranchWidth;   // Calculated width of left (true) branch
    double rightBranchWidth;  // Calculated width of right (false) branch
} IFBlock;

IFBlock ifBlocks[MAX_IF_BLOCKS];
int ifBlockCount = 0;

// Cycle tracking system
#define MAX_CYCLE_BLOCKS 50
typedef enum {
    CYCLE_WHILE = 0,
    CYCLE_DO = 1,
    CYCLE_FOR = 2
} CycleType;

typedef struct {
    int cycleNodeIndex;        // Index of the cycle block
    int cycleEndNodeIndex;     // Index of the cycle end point
    int parentCycleIndex;      // Parent cycle (-1 if none)
    CycleType cycleType;       // Loop type
    float loopbackOffset;      // X offset for loopback routing
    char initVar[MAX_VAR_NAME_LENGTH];   // FOR init variable (optional)
    char condition[MAX_VALUE_LENGTH];    // Loop condition
    char increment[MAX_VALUE_LENGTH];    // FOR increment/decrement
} CycleBlock;

CycleBlock cycleBlocks[MAX_CYCLE_BLOCKS];
int cycleBlockCount = 0;

// Forward declarations
void rebuild_variable_table(void);
int get_if_branch_type(int connIndex);
void reposition_convergence_point(int ifBlockIndex, bool shouldPushNodesBelow);
void update_all_branch_positions(void);
bool is_valid_if_converge_connection(int fromNode, int toNode);
void insert_cycle_block_in_connection(int connIndex);

// Cycle helper utilities
static int find_cycle_block_by_cycle_node(int nodeIndex) {
    for (int i = 0; i < cycleBlockCount; i++) {
        if (cycleBlocks[i].cycleNodeIndex == nodeIndex) {
            return i;
        }
    }
    return -1;
}

static int find_cycle_block_by_end_node(int nodeIndex) {
    for (int i = 0; i < cycleBlockCount; i++) {
        if (cycleBlocks[i].cycleEndNodeIndex == nodeIndex) {
            return i;
        }
    }
    return -1;
}

static int calculate_cycle_depth(int cycleIndex) {
    int depth = 0;
    int current = cycleIndex;
    while (current >= 0 && current < cycleBlockCount) {
        depth++;
        current = cycleBlocks[current].parentCycleIndex;
    }
    return depth;
}

static float get_cycle_loopback_offset(int cycleIndex) {
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

static CycleType prompt_cycle_type(void) {
    const char* options[] = {"WHILE", "DO", "FOR"};
    int selection = tinyfd_listDialog("Loop Type", "Select loop type:", 3, options);
    if (selection < 0 || selection > 2) {
        return CYCLE_WHILE;
    }
    return (CycleType)selection;
}

// Variable tracking system
typedef enum {
    VAR_TYPE_INT = 0,
    VAR_TYPE_REAL = 1,
    VAR_TYPE_STRING = 2,
    VAR_TYPE_BOOL = 3
} VariableType;

typedef struct {
    char name[MAX_VAR_NAME_LENGTH];
    VariableType type;
    bool is_array;
    int array_size;  // Size of array (0 if not an array or size not specified)
} Variable;

Variable variables[MAX_VARIABLES];
int variableCount = 0;

// Popup menu types
typedef enum {
    MENU_TYPE_CONNECTION = 0,
    MENU_TYPE_NODE = 1
} MenuType;

// Popup menu state
typedef struct {
    bool active;
    MenuType type;  // Type of menu (connection or node)
    double x;
    double y;
    int connectionIndex;  // which connection was clicked (for connection menu)
    int nodeIndex;  // which node was clicked (for node menu)
} PopupMenu;

PopupMenu popupMenu = {false, MENU_TYPE_CONNECTION, 0.0, 0.0, -1, -1};

// Menu item dimensions
// Menu item dimensions - reduced sizes for better fit on larger window
const float menuItemHeight = 0.12f;  // Reduced from 0.15f
const float menuItemSpacing = 0.015f;  // Reduced from 0.02f
const float menuPadding = 0.03f;  // Reduced from 0.04f
// Original window was 800x600 (aspect 1.333), new is 1600x900 (aspect 1.778)
// Scale menu width to maintain original appearance: 1.333/1.778 ≈ 0.75
const float menuMinWidth = 0.6f * (1.333f / 1.778f);  // Scaled to maintain original size (reduced from 0.8f)

// Menu items
#define MAX_MENU_ITEMS 10
typedef struct {
    const char* text;
    NodeType nodeType;  // Node type to insert when clicked
} MenuItem;

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
typedef struct {
    const char* text;
    int action;  // 0 = Delete, 1 = Value, etc.
} NodeMenuItem;

NodeMenuItem nodeMenuItems[] = {
    {"Delete", 0},
    {"Value", 1}
};
int nodeMenuItemCount = 2;

// Deletion toggle (off by default)
bool deletionEnabled = false;

// Grid helper functions
static double grid_to_world_x(int gridX) {
    return gridX * GRID_CELL_SIZE;
}

static double grid_to_world_y(int gridY) {
    return gridY * GRID_CELL_SIZE;
}

static int world_to_grid_x(double x) {
    return (int)round(x / GRID_CELL_SIZE);
}

static int world_to_grid_y(double y) {
    return (int)round(y / GRID_CELL_SIZE);
}

static double snap_to_grid_x(double x) {
    return grid_to_world_x(world_to_grid_x(x));
}

static double snap_to_grid_y(double y) {
    return grid_to_world_y(world_to_grid_y(y));
}

// Calculate block width based on text content
static float calculate_block_width(const char* text, float fontSize, float minWidth) {
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
}

// Scroll callback for panning (both horizontal and vertical)
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window;   // Mark as intentionally unused
    scrollOffsetX += xoffset * 0.1;  // Smooth horizontal scrolling factor
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
static int hit_node(double x, double y) {
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
static bool is_cycle_loopback(int connIndex) {
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
static int hit_connection(double x, double y, float threshold) {
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
void save_flowchart(const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Failed to open file for writing: %s\n", filename);
        return;
    }
    
    // Write header
    fprintf(file, "# Flowchart adjacency matrix\n");
    fprintf(file, "# Nodes: %d\n", nodeCount);
    fprintf(file, "%d\n", nodeCount);
    
    // Create adjacency matrix
    for (int i = 0; i < nodeCount; i++) {
        for (int j = 0; j < nodeCount; j++) {
            int connected = 0;
            // Check if there's a connection from i to j
            for (int k = 0; k < connectionCount; k++) {
                if (connections[k].fromNode == i && connections[k].toNode == j) {
                    connected = 1;
                    break;
                }
            }
            fprintf(file, "%d", connected);
            if (j < nodeCount - 1) fprintf(file, " ");
        }
        fprintf(file, "\n");
    }
    
    // Write node data
    fprintf(file, "# Node data: x y width height type \"value_string\"\n");
    for (int i = 0; i < nodeCount; i++) {
        // Escape quotes in value string
        char escaped_value[MAX_VALUE_LENGTH * 2]; // Worst case: all chars are quotes
        int j = 0;
        for (int k = 0; nodes[i].value[k] != '\0' && j < (int)(sizeof(escaped_value) - 1); k++) {
            if (nodes[i].value[k] == '"') {
                escaped_value[j++] = '\\';
            }
            escaped_value[j++] = nodes[i].value[k];
        }
        escaped_value[j] = '\0';
        
        fprintf(file, "%.6f %.6f %.6f %.6f %d \"%s\"\n",
                nodes[i].x, nodes[i].y, nodes[i].width, nodes[i].height,
                (int)nodes[i].type, escaped_value);
    }
    
    // Write IF blocks data
    fprintf(file, "# IF Blocks: %d\n", ifBlockCount);
    for (int i = 0; i < ifBlockCount; i++) {
        fprintf(file, "%d %d %d %d %d %d\n",
                ifBlocks[i].ifNodeIndex,
                ifBlocks[i].convergeNodeIndex,
                ifBlocks[i].parentIfIndex,
                ifBlocks[i].branchColumn,
                ifBlocks[i].trueBranchCount,
                ifBlocks[i].falseBranchCount);
        
        // Write true branch nodes (always write a newline, even if empty)
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            fprintf(file, "%d ", ifBlocks[i].trueBranchNodes[j]);
        }
        fprintf(file, "\n");  // Always write newline, even for empty branches
        
        // Write false branch nodes (always write a newline, even if empty)
        for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
            fprintf(file, "%d ", ifBlocks[i].falseBranchNodes[j]);
        }
        fprintf(file, "\n");  // Always write newline, even for empty branches
    }

    // Write Cycle blocks data
    fprintf(file, "# Cycle Blocks: %d\n", cycleBlockCount);
    for (int i = 0; i < cycleBlockCount; i++) {
        fprintf(file, "%d %d %d %d %.3f\n",
                cycleBlocks[i].cycleNodeIndex,
                cycleBlocks[i].cycleEndNodeIndex,
                cycleBlocks[i].parentCycleIndex,
                (int)cycleBlocks[i].cycleType,
                cycleBlocks[i].loopbackOffset);
        fprintf(file, "%s|%s|%s\n",
                cycleBlocks[i].initVar,
                cycleBlocks[i].condition,
                cycleBlocks[i].increment);
    }
    
    fclose(file);
    printf("Flowchart saved to %s\n", filename);
}

// Load flowchart from adjacency matrix
void load_flowchart(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Failed to open file for reading: %s\n", filename);
        return;
    }
    
    // Skip comment lines and read node count
    char line[256];
    int loadedNodeCount = 0;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] != '#') {
            sscanf(line, "%d", &loadedNodeCount);
            break;
        }
    }
    
    if (loadedNodeCount <= 0 || loadedNodeCount > MAX_NODES) {
        fprintf(stderr, "Invalid node count: %d\n", loadedNodeCount);
        fclose(file);
        return;
    }
    
    // Read adjacency matrix
    int adjMatrix[MAX_NODES][MAX_NODES] = {0};
    for (int i = 0; i < loadedNodeCount; i++) {
        for (int j = 0; j < loadedNodeCount; j++) {
            if (fscanf(file, "%d", &adjMatrix[i][j]) != 1) {
                fprintf(stderr, "Error reading adjacency matrix\n");
                fclose(file);
                return;
            }
        }
    }
    
    // Skip comment line
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') break;
    }
    
    // Read node data
    nodeCount = 0;
    for (int i = 0; i < loadedNodeCount; i++) {
        int nodeType;
        double x, y;
        float width, height;
        
        // Read x, y, width, height, type
        if (fscanf(file, "%lf %lf %f %f %d", &x, &y, &width, &height, &nodeType) != 5) {
            fprintf(stderr, "Error reading node data\n");
            fclose(file);
            return;
        }
        
        // Skip whitespace before quoted string
        int c;
        while ((c = fgetc(file)) != EOF && (c == ' ' || c == '\t'));
        
        if (c == '"') {
            // Read quoted string
            int j = 0;
            bool escaped = false;
            while (j < MAX_VALUE_LENGTH - 1) {
                c = fgetc(file);
                if (c == EOF) break;
                
                if (escaped) {
                    if (c == '"') {
                        nodes[i].value[j++] = '"';
                    } else if (c == '\\') {
                        nodes[i].value[j++] = '\\';
                    } else {
                        nodes[i].value[j++] = c;
                    }
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    // End of quoted string
                    break;
                } else {
                    nodes[i].value[j++] = c;
                }
            }
            nodes[i].value[j] = '\0';
            
            // Skip rest of line
            while ((c = fgetc(file)) != EOF && c != '\n');
        } else {
            // Old format or no value - set empty string
            nodes[i].value[0] = '\0';
            // If we read something that wasn't a quote, it might be old format value
            // Try to read it as integer for backward compatibility
            if (c != EOF && c != '\n') {
                ungetc(c, file);
                int oldValue;
                if (fscanf(file, "%d", &oldValue) == 1) {
                    // Old format had integer value, ignore it
                }
            }
        }
        
        // Snap loaded nodes to grid
        nodes[i].x = snap_to_grid_x(x);
        nodes[i].y = snap_to_grid_y(y);
        nodes[i].height = height;
        nodes[i].type = (NodeType)nodeType;
        // Initialize branch tracking (will be updated when IF blocks are loaded)
        nodes[i].branchColumn = 0;
        nodes[i].owningIfBlock = -1;
        
        // Recalculate width for content blocks based on text content
        if (nodes[i].type == NODE_PROCESS || nodes[i].type == NODE_NORMAL ||
            nodes[i].type == NODE_INPUT || nodes[i].type == NODE_OUTPUT ||
            nodes[i].type == NODE_ASSIGNMENT || nodes[i].type == NODE_DECLARE ||
            nodes[i].type == NODE_CYCLE) {
            float fontSize = nodes[i].height * 0.3f;
            nodes[i].width = calculate_block_width(nodes[i].value, fontSize, 0.35f);
        } else {
            // START and END blocks keep their loaded width
            nodes[i].width = width;
        }
        
        nodeCount++;
    }
    
    // Rebuild connections from adjacency matrix
    connectionCount = 0;
    for (int i = 0; i < nodeCount; i++) {
        for (int j = 0; j < nodeCount; j++) {
            if (adjMatrix[i][j] && connectionCount < MAX_CONNECTIONS) {
                connections[connectionCount].fromNode = i;
                connections[connectionCount].toNode = j;
                connectionCount++;
            }
        }
    }
    
    // Try to read IF blocks section (may not exist in older files)
    ifBlockCount = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "# IF Blocks:", 12) == 0) {
            sscanf(line + 12, "%d", &ifBlockCount);
            break;
        }
    }
    
    // Read IF blocks if any exist
    for (int i = 0; i < ifBlockCount && i < MAX_IF_BLOCKS; i++) {
        if (fscanf(file, "%d %d %d %d %d %d",
                   &ifBlocks[i].ifNodeIndex,
                   &ifBlocks[i].convergeNodeIndex,
                   &ifBlocks[i].parentIfIndex,
                   &ifBlocks[i].branchColumn,
                   &ifBlocks[i].trueBranchCount,
                   &ifBlocks[i].falseBranchCount) != 6) {
            fprintf(stderr, "Error reading IF block data\n");
            ifBlockCount = i;
            break;
        }
        
        // Read true branch nodes
        // First, peek at the next line to check if it's "EMPTY"
        long filePos = ftell(file);
        char lineBuffer[256];
        if (fgets(lineBuffer, sizeof(lineBuffer), file) == NULL) {
            fprintf(stderr, "Error reading true branch line\n");
            break;
        }
        
        // Remove trailing newline
        lineBuffer[strcspn(lineBuffer, "\n")] = 0;
        
        // Trim whitespace
        char *trimmed = lineBuffer;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        int len = strlen(trimmed);
        while (len > 0 && (trimmed[len-1] == ' ' || trimmed[len-1] == '\t')) {
            trimmed[--len] = '\0';
        }
        
        // Check if line is "EMPTY" marker
        if (strcmp(trimmed, "EMPTY") == 0) {
            // Empty branch - verify count matches
            if (ifBlocks[i].trueBranchCount != 0) {
                fprintf(stderr, "Warning: True branch marked EMPTY but count is %d, setting to 0\n", ifBlocks[i].trueBranchCount);
                ifBlocks[i].trueBranchCount = 0;
            }
        } else {
            // Not empty - rewind and parse node indices
            fseek(file, filePos, SEEK_SET);
            
            if (ifBlocks[i].trueBranchCount > 0) {
                for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
                    if (fscanf(file, "%d", &ifBlocks[i].trueBranchNodes[j]) != 1) {
                        fprintf(stderr, "Error reading true branch nodes\n");
                        break;
                    }
                }
            }
            // Skip rest of line (spaces and newline)
            if (fgets(lineBuffer, sizeof(lineBuffer), file) == NULL) {
                fprintf(stderr, "Error reading true branch line terminator\n");
                break;
            }
        }
        
        // Read false branch nodes
        // First, peek at the next line to check if it's "EMPTY"
        long filePosFalse = ftell(file);
        if (fgets(lineBuffer, sizeof(lineBuffer), file) == NULL) {
            fprintf(stderr, "Error reading false branch line\n");
            break;
        }
        
        // Remove trailing newline
        lineBuffer[strcspn(lineBuffer, "\n")] = 0;
        
        // Trim whitespace
        char *trimmedFalse = lineBuffer;
        while (*trimmedFalse == ' ' || *trimmedFalse == '\t') trimmedFalse++;
        int lenFalse = strlen(trimmedFalse);
        while (lenFalse > 0 && (trimmedFalse[lenFalse-1] == ' ' || trimmedFalse[lenFalse-1] == '\t')) {
            trimmedFalse[--lenFalse] = '\0';
        }
        
        // Check if line is "EMPTY" marker
        if (strcmp(trimmedFalse, "EMPTY") == 0) {
            // Empty branch - verify count matches
            if (ifBlocks[i].falseBranchCount != 0) {
                fprintf(stderr, "Warning: False branch marked EMPTY but count is %d, setting to 0\n", ifBlocks[i].falseBranchCount);
                ifBlocks[i].falseBranchCount = 0;
            }
        } else {
            // Not empty - rewind and parse node indices
            fseek(file, filePosFalse, SEEK_SET);
            
            if (ifBlocks[i].falseBranchCount > 0) {
                for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
                    if (fscanf(file, "%d", &ifBlocks[i].falseBranchNodes[j]) != 1) {
                        fprintf(stderr, "Error reading false branch nodes\n");
                        break;
                    }
                }
            }
            // Skip rest of line (spaces and newline)
            if (fgets(lineBuffer, sizeof(lineBuffer), file) == NULL) {
                fprintf(stderr, "Error reading false branch line terminator\n");
                break;
            }
        }
        
        // Update IF and CONVERGE nodes' owningIfBlock and branchColumn
        // For nested IFs, they should be owned by their parent
        if (ifBlocks[i].ifNodeIndex >= 0 && ifBlocks[i].ifNodeIndex < nodeCount) {
            if (ifBlocks[i].parentIfIndex >= 0) {
                // Nested IF: owned by parent
                nodes[ifBlocks[i].ifNodeIndex].owningIfBlock = ifBlocks[i].parentIfIndex;
            } else {
                // Top-level IF: not owned by any IF
                nodes[ifBlocks[i].ifNodeIndex].owningIfBlock = -1;
            }
            nodes[ifBlocks[i].ifNodeIndex].branchColumn = ifBlocks[i].branchColumn;
        }
        
        if (ifBlocks[i].convergeNodeIndex >= 0 && ifBlocks[i].convergeNodeIndex < nodeCount) {
            if (ifBlocks[i].parentIfIndex >= 0) {
                // Nested IF convergence: owned by parent
                nodes[ifBlocks[i].convergeNodeIndex].owningIfBlock = ifBlocks[i].parentIfIndex;
            } else {
                // Top-level IF convergence: not owned by any IF
                nodes[ifBlocks[i].convergeNodeIndex].owningIfBlock = -1;
            }
            nodes[ifBlocks[i].convergeNodeIndex].branchColumn = ifBlocks[i].branchColumn;
        }
        
        // Update ownership and branch column for all branch nodes
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            int nodeIdx = ifBlocks[i].trueBranchNodes[j];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                nodes[nodeIdx].owningIfBlock = i;
                // True branch is left: branchColumn = ifBlock's branchColumn - 2
                int oldBranchCol = nodes[nodeIdx].branchColumn;
                nodes[nodeIdx].branchColumn = ifBlocks[i].branchColumn - 2;
            }
        }
        
        for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
            int nodeIdx = ifBlocks[i].falseBranchNodes[j];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                nodes[nodeIdx].owningIfBlock = i;
                // False branch is right: branchColumn calculation matches creation logic
                int falseBranchCol = ifBlocks[i].branchColumn + 2;
                if (falseBranchCol <= 0) {
                    falseBranchCol = abs(ifBlocks[i].branchColumn) + 2;
                }
                int oldBranchCol = nodes[nodeIdx].branchColumn;
                nodes[nodeIdx].branchColumn = falseBranchCol;
            }
        }
    }
    
    // After loading all IF blocks, verify and correct branchColumn for nested IFs
    // Check which branch array of the parent each nested IF is actually in
    for (int i = 0; i < ifBlockCount; i++) {
        if (ifBlocks[i].parentIfIndex >= 0 && ifBlocks[i].parentIfIndex < ifBlockCount) {
            int parentIdx = ifBlocks[i].parentIfIndex;
            int ifNodeIdx = ifBlocks[i].ifNodeIndex;
            
            if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount) {
                // Check if this IF node is in the parent's true branch array
                bool inTrueBranch = false;
                for (int j = 0; j < ifBlocks[parentIdx].trueBranchCount; j++) {
                    if (ifBlocks[parentIdx].trueBranchNodes[j] == ifNodeIdx) {
                        inTrueBranch = true;
                        break;
                    }
                }
                
                // Check if this IF node is in the parent's false branch array
                bool inFalseBranch = false;
                for (int j = 0; j < ifBlocks[parentIdx].falseBranchCount; j++) {
                    if (ifBlocks[parentIdx].falseBranchNodes[j] == ifNodeIdx) {
                        inFalseBranch = true;
                        break;
                    }
                }
                
                // Correct branchColumn based on which branch array it's actually in
                // If the branchColumn is wrong, we also need to swap the true/false branch arrays
                // because they were saved with the wrong branchColumn
                // SPECIAL CASE: If the nested IF's branchColumn sign doesn't match which branch it's in,
                // we need to swap the parent's branch arrays (the file structure is wrong)
                // NOTE: We should NOT swap based on branch counts alone, because a nested IF can legitimately
                // have nodes in one branch and be empty in the other (e.g., false->true with empty false->false)
                bool parentBranchesSwapped = false;
                if (inTrueBranch && !inFalseBranch && ifBlocks[i].branchColumn > 0) {
                    // Nested IF is in true branch array but has positive branchColumn - parent branches are swapped
                    parentBranchesSwapped = true;
                } else if (inFalseBranch && !inTrueBranch && ifBlocks[i].branchColumn < 0) {
                    // Nested IF is in false branch array but has negative branchColumn - parent branches are swapped
                    parentBranchesSwapped = true;
                }
                
                if (parentBranchesSwapped) {
                    // Swap parent's branch arrays
                    int tempCount = ifBlocks[parentIdx].trueBranchCount;
                    ifBlocks[parentIdx].trueBranchCount = ifBlocks[parentIdx].falseBranchCount;
                    ifBlocks[parentIdx].falseBranchCount = tempCount;
                    int tempNodes[MAX_NODES];
                    memcpy(tempNodes, ifBlocks[parentIdx].trueBranchNodes, sizeof(ifBlocks[parentIdx].trueBranchNodes));
                    memcpy(ifBlocks[parentIdx].trueBranchNodes, ifBlocks[parentIdx].falseBranchNodes, sizeof(ifBlocks[parentIdx].falseBranchNodes));
                    memcpy(ifBlocks[parentIdx].falseBranchNodes, tempNodes, sizeof(ifBlocks[parentIdx].falseBranchNodes));
                    // After swapping parent's branches, the nested IF's branchColumn needs to be inverted
                    // but its own branch arrays should NOT be swapped (they're correct)
                    // If it was in false branch (positive branchColumn), it's now in true branch (negative)
                    // If it was in true branch (negative branchColumn), it's now in false branch (positive)
                    if (ifBlocks[i].branchColumn > 0) {
                        // Was in false branch, now in true branch - invert to negative
                        int parentBranchCol = (ifBlocks[parentIdx].ifNodeIndex >= 0 && ifBlocks[parentIdx].ifNodeIndex < nodeCount) 
                                             ? nodes[ifBlocks[parentIdx].ifNodeIndex].branchColumn : 0;
                        int correctBranchCol = parentBranchCol - 2;
                        ifBlocks[i].branchColumn = correctBranchCol;
                        nodes[ifNodeIdx].branchColumn = correctBranchCol;
                        if (ifBlocks[i].convergeNodeIndex >= 0 && ifBlocks[i].convergeNodeIndex < nodeCount) {
                            nodes[ifBlocks[i].convergeNodeIndex].branchColumn = correctBranchCol;
                        }
                        inTrueBranch = true;
                        inFalseBranch = false;
                    } else if (ifBlocks[i].branchColumn < 0) {
                        // Was in true branch, now in false branch - invert to positive
                        int parentBranchCol = (ifBlocks[parentIdx].ifNodeIndex >= 0 && ifBlocks[parentIdx].ifNodeIndex < nodeCount) 
                                             ? nodes[ifBlocks[parentIdx].ifNodeIndex].branchColumn : 0;
                        int falseBranchCol = parentBranchCol + 2;
                        if (falseBranchCol <= 0) {
                            falseBranchCol = abs(parentBranchCol) + 2;
                        }
                        ifBlocks[i].branchColumn = falseBranchCol;
                        nodes[ifNodeIdx].branchColumn = falseBranchCol;
                        if (ifBlocks[i].convergeNodeIndex >= 0 && ifBlocks[i].convergeNodeIndex < nodeCount) {
                            nodes[ifBlocks[i].convergeNodeIndex].branchColumn = falseBranchCol;
                        }
                        inTrueBranch = false;
                        inFalseBranch = true;
                    }
                    // DO NOT swap the nested IF's own branch arrays - they're correct
                    // Skip the rest of the correction logic since we've already fixed it
                    continue;
                }
                
                if (inTrueBranch && !inFalseBranch) {
                    // In true branch - branchColumn should be negative
                    int parentBranchCol = (ifBlocks[parentIdx].ifNodeIndex >= 0 && ifBlocks[parentIdx].ifNodeIndex < nodeCount) 
                                         ? nodes[ifBlocks[parentIdx].ifNodeIndex].branchColumn : 0;
                    int correctBranchCol = parentBranchCol - 2;
                    // Also check if the sign is wrong - if current is positive but should be negative, or vice versa
                    bool needsCorrection = (ifBlocks[i].branchColumn != correctBranchCol) ||
                                          (ifBlocks[i].branchColumn > 0 && correctBranchCol < 0) ||
                                          (ifBlocks[i].branchColumn < 0 && correctBranchCol > 0);
                    if (needsCorrection) {
                        // Swap true and false branch arrays because they were saved with wrong branchColumn
                        int tempCount = ifBlocks[i].trueBranchCount;
                        ifBlocks[i].trueBranchCount = ifBlocks[i].falseBranchCount;
                        ifBlocks[i].falseBranchCount = tempCount;
                        int tempNodes[MAX_NODES];
                        memcpy(tempNodes, ifBlocks[i].trueBranchNodes, sizeof(ifBlocks[i].trueBranchNodes));
                        memcpy(ifBlocks[i].trueBranchNodes, ifBlocks[i].falseBranchNodes, sizeof(ifBlocks[i].falseBranchNodes));
                        memcpy(ifBlocks[i].falseBranchNodes, tempNodes, sizeof(ifBlocks[i].falseBranchNodes));
                        
                        ifBlocks[i].branchColumn = correctBranchCol;
                        nodes[ifNodeIdx].branchColumn = correctBranchCol;
                        if (ifBlocks[i].convergeNodeIndex >= 0 && ifBlocks[i].convergeNodeIndex < nodeCount) {
                            nodes[ifBlocks[i].convergeNodeIndex].branchColumn = correctBranchCol;
                        }
                    }
                } else if (inFalseBranch && !inTrueBranch) {
                    // In false branch - branchColumn should be positive
                    int parentBranchCol = (ifBlocks[parentIdx].ifNodeIndex >= 0 && ifBlocks[parentIdx].ifNodeIndex < nodeCount) 
                                         ? nodes[ifBlocks[parentIdx].ifNodeIndex].branchColumn : 0;
                    int falseBranchCol = parentBranchCol + 2;
                    if (falseBranchCol <= 0) {
                        falseBranchCol = abs(parentBranchCol) + 2;
                    }
                    // Also check if the sign is wrong - if current is negative but should be positive, or vice versa
                    bool needsCorrection = (ifBlocks[i].branchColumn != falseBranchCol) ||
                                          (ifBlocks[i].branchColumn < 0 && falseBranchCol > 0) ||
                                          (ifBlocks[i].branchColumn > 0 && falseBranchCol < 0);
                    if (needsCorrection) {
                        // Swap true and false branch arrays because they were saved with wrong branchColumn
                        int tempCount = ifBlocks[i].trueBranchCount;
                        ifBlocks[i].trueBranchCount = ifBlocks[i].falseBranchCount;
                        ifBlocks[i].falseBranchCount = tempCount;
                        int tempNodes[MAX_NODES];
                        memcpy(tempNodes, ifBlocks[i].trueBranchNodes, sizeof(ifBlocks[i].trueBranchNodes));
                        memcpy(ifBlocks[i].trueBranchNodes, ifBlocks[i].falseBranchNodes, sizeof(ifBlocks[i].falseBranchNodes));
                        memcpy(ifBlocks[i].falseBranchNodes, tempNodes, sizeof(ifBlocks[i].falseBranchNodes));
                        
                        ifBlocks[i].branchColumn = falseBranchCol;
                        nodes[ifNodeIdx].branchColumn = falseBranchCol;
                        if (ifBlocks[i].convergeNodeIndex >= 0 && ifBlocks[i].convergeNodeIndex < nodeCount) {
                            nodes[ifBlocks[i].convergeNodeIndex].branchColumn = falseBranchCol;
                        }
                    }
                }
            }
        }
    }
    
    // After verifying branchColumns, update branch node branchColumns again
    for (int i = 0; i < ifBlockCount; i++) {
        // Update true branch nodes
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            int nodeIdx = ifBlocks[i].trueBranchNodes[j];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                nodes[nodeIdx].branchColumn = ifBlocks[i].branchColumn - 2;
            }
        }
        
        // Update false branch nodes
        for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
            int nodeIdx = ifBlocks[i].falseBranchNodes[j];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                int falseBranchCol = ifBlocks[i].branchColumn + 2;
                if (falseBranchCol <= 0) {
                    falseBranchCol = abs(ifBlocks[i].branchColumn) + 2;
                }
                nodes[nodeIdx].branchColumn = falseBranchCol;
            }
        }
    }
    
    // Load Cycle blocks (if present)
    cycleBlockCount = 0;
    // Seek cycle header if available
    fpos_t cyclePos;
    fgetpos(file, &cyclePos);
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "# Cycle Blocks:", 15) == 0) {
            sscanf(line + 15, "%d", &cycleBlockCount);
            break;
        }
    }
    if (cycleBlockCount < 0) cycleBlockCount = 0;
    if (cycleBlockCount > MAX_CYCLE_BLOCKS) cycleBlockCount = MAX_CYCLE_BLOCKS;
    
    for (int i = 0; i < cycleBlockCount; i++) {
        float offset = 0.0f;
        int cycleTypeInt = 0;
        if (fscanf(file, "%d %d %d %d %f",
                   &cycleBlocks[i].cycleNodeIndex,
                   &cycleBlocks[i].cycleEndNodeIndex,
                   &cycleBlocks[i].parentCycleIndex,
                   &cycleTypeInt,
                   &offset) != 5) {
            cycleBlockCount = i;
            break;
        }
        cycleBlocks[i].cycleType = (CycleType)cycleTypeInt;
        cycleBlocks[i].loopbackOffset = offset;
        
        // Read init|condition|increment line
        if (fscanf(file, "%255[^|]|%255[^|]|%255[^\n]\n",
                   cycleBlocks[i].initVar,
                   cycleBlocks[i].condition,
                   cycleBlocks[i].increment) != 3) {
            cycleBlocks[i].initVar[0] = '\0';
            cycleBlocks[i].condition[0] = '\0';
            cycleBlocks[i].increment[0] = '\0';
        }
    }
    
    // After loading all IF blocks, update branch positions and reposition convergence points
    // This ensures nested IFs are properly positioned
    update_all_branch_positions();
    
    // Reposition all convergence points to ensure they're in the correct positions
    for (int i = 0; i < ifBlockCount; i++) {
        reposition_convergence_point(i, false);  // Don't push nodes below when loading
    }
    
    // Fix nodes below nested IF convergence points
    for (int i = 0; i < ifBlockCount; i++) {
        if (ifBlocks[i].parentIfIndex >= 0 && ifBlocks[i].convergeNodeIndex >= 0 && 
            ifBlocks[i].convergeNodeIndex < nodeCount) {
            double convergeY = nodes[ifBlocks[i].convergeNodeIndex].y;
            int parentIfIdx = ifBlocks[i].parentIfIndex;
            
            // Find all nodes below this convergence point
            for (int j = 0; j < nodeCount; j++) {
                if (nodes[j].y < convergeY && j != ifBlocks[i].convergeNodeIndex) {
                    // Check if this node is NOT part of the nested IF (not in branch arrays)
                    bool isInNestedIfBranch = false;
                    for (int k = 0; k < ifBlocks[i].trueBranchCount; k++) {
                        if (ifBlocks[i].trueBranchNodes[k] == j) {
                            isInNestedIfBranch = true;
                            break;
                        }
                    }
                    if (!isInNestedIfBranch) {
                        for (int k = 0; k < ifBlocks[i].falseBranchCount; k++) {
                            if (ifBlocks[i].falseBranchNodes[k] == j) {
                                isInNestedIfBranch = true;
                                break;
                            }
                        }
                    }
                    
                    // Also check if it's the nested IF node itself
                    if (j == ifBlocks[i].ifNodeIndex) {
                        isInNestedIfBranch = true;
                    }
                    
                    if (!isInNestedIfBranch) {
                        // This node is below the nested IF convergence but not part of it
                        // It should be in the parent's context or main branch
                        int oldOwningIfBlock = nodes[j].owningIfBlock;
                        int oldBranchColumn = nodes[j].branchColumn;
                        
                        // Set to parent's context
                        if (parentIfIdx >= 0 && parentIfIdx < ifBlockCount) {
                            nodes[j].owningIfBlock = parentIfIdx;
                            nodes[j].branchColumn = ifBlocks[parentIfIdx].branchColumn;
                        } else {
                            // Top-level: main branch
                            nodes[j].owningIfBlock = -1;
                            nodes[j].branchColumn = 0;
                        }
                    }
                }
            }
        }
    }
    
    fclose(file);
    printf("Flowchart loaded from %s (%d nodes, %d connections, %d IF blocks)\n", 
           filename, nodeCount, connectionCount, ifBlockCount);
    
    // Rebuild variable table after loading
    rebuild_variable_table();
}
// Delete a node and reconnect adjacent nodes automatically
void delete_node(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= nodeCount) {
        return;
    }
    
    // Special handling for IF and CONVERGE nodes
    if (nodes[nodeIndex].type == NODE_IF || nodes[nodeIndex].type == NODE_CONVERGE) {
        // Find the IF block that this node belongs to
        int ifBlockIndex = -1;
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == nodeIndex || ifBlocks[i].convergeNodeIndex == nodeIndex) {
                ifBlockIndex = i;
                break;
            }
        }
        
        if (ifBlockIndex >= 0) {
            IFBlock *ifBlock = &ifBlocks[ifBlockIndex];
            int ifIdx = ifBlock->ifNodeIndex;
            int convergeIdx = ifBlock->convergeNodeIndex;
            
            // Find the connection coming into the IF block
            int incomingFromNode = -1;
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].toNode == ifIdx) {
                    incomingFromNode = connections[i].fromNode;
                    break;
                }
            }
            
            // Find the connection going out of the convergence point
            int outgoingToNode = -1;
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].fromNode == convergeIdx) {
                    outgoingToNode = connections[i].toNode;
                    break;
                }
            }
            
            // Find all nodes owned by this IF block (branch nodes)
            int branchNodes[MAX_NODES];
            int branchNodeCount = 0;
            for (int i = 0; i < nodeCount; i++) {
                if (nodes[i].owningIfBlock == ifBlockIndex) {
                    branchNodes[branchNodeCount++] = i;
                }
            }
            
            // Remove all connections involving IF, convergence, or branch nodes
            for (int i = connectionCount - 1; i >= 0; i--) {
                bool shouldDelete = false;
                
                // Check if connection involves IF or convergence
                if (connections[i].fromNode == ifIdx || connections[i].toNode == ifIdx ||
                    connections[i].fromNode == convergeIdx || connections[i].toNode == convergeIdx) {
                    shouldDelete = true;
                }
                
                // Check if connection involves any branch node
                for (int j = 0; j < branchNodeCount; j++) {
                    if (connections[i].fromNode == branchNodes[j] || connections[i].toNode == branchNodes[j]) {
                        shouldDelete = true;
                        break;
                    }
                }
                
                if (shouldDelete) {
                    // Remove this connection by shifting others down
                    for (int j = i; j < connectionCount - 1; j++) {
                        connections[j] = connections[j + 1];
                    }
                    connectionCount--;
                }
            }
            
            // Create direct connection from incoming to outgoing
            if (incomingFromNode >= 0 && outgoingToNode >= 0) {
                connections[connectionCount].fromNode = incomingFromNode;
                connections[connectionCount].toNode = outgoingToNode;
                connectionCount++;
            }
            
            // Build list of all nodes to delete: IF, convergence, and all branch nodes
            // Sort from highest to lowest index to avoid shifting issues
            int nodesToDelete[MAX_NODES];
            int deleteCount = 0;
            nodesToDelete[deleteCount++] = ifIdx;
            nodesToDelete[deleteCount++] = convergeIdx;
            for (int i = 0; i < branchNodeCount; i++) {
                nodesToDelete[deleteCount++] = branchNodes[i];
            }
            
            // Sort in descending order (highest index first)
            for (int i = 0; i < deleteCount - 1; i++) {
                for (int j = i + 1; j < deleteCount; j++) {
                    if (nodesToDelete[i] < nodesToDelete[j]) {
                        int temp = nodesToDelete[i];
                        nodesToDelete[i] = nodesToDelete[j];
                        nodesToDelete[j] = temp;
                    }
                }
            }
            
            // Delete the nodes (higher index first)
            for (int i = 0; i < deleteCount; i++) {
                int delIdx = nodesToDelete[i];
                
                // Shift all nodes after this one down
                for (int j = delIdx; j < nodeCount - 1; j++) {
                    nodes[j] = nodes[j + 1];
                }
                nodeCount--;
                
                // Update all connections to account for shifted indices
                for (int j = 0; j < connectionCount; j++) {
                    if (connections[j].fromNode > delIdx) {
                        connections[j].fromNode--;
                    }
                    if (connections[j].toNode > delIdx) {
                        connections[j].toNode--;
                    }
                }
                
                // Update IF blocks to account for shifted indices
                for (int j = 0; j < ifBlockCount; j++) {
                    if (ifBlocks[j].ifNodeIndex > delIdx) {
                        ifBlocks[j].ifNodeIndex--;
                    }
                    if (ifBlocks[j].convergeNodeIndex > delIdx) {
                        ifBlocks[j].convergeNodeIndex--;
                    }
                    
                    // Update branch arrays - shift node indices that are greater than delIdx
                    for (int k = 0; k < ifBlocks[j].trueBranchCount; k++) {
                        if (ifBlocks[j].trueBranchNodes[k] > delIdx) {
                            ifBlocks[j].trueBranchNodes[k]--;
                        }
                    }
                    for (int k = 0; k < ifBlocks[j].falseBranchCount; k++) {
                        if (ifBlocks[j].falseBranchNodes[k] > delIdx) {
                            ifBlocks[j].falseBranchNodes[k]--;
                        }
                    }
                }
                
                // Update owningIfBlock for all remaining nodes
                for (int j = 0; j < nodeCount; j++) {
                    if (nodes[j].owningIfBlock > delIdx) {
                        // Node index shifted, but owningIfBlock references haven't been updated yet
                        // This will be handled after we remove the IF block from tracking
                    }
                }
            }
            
            // Remove the IF block from the tracking array
            for (int i = ifBlockIndex; i < ifBlockCount - 1; i++) {
                ifBlocks[i] = ifBlocks[i + 1];
            }
            ifBlockCount--;
            
            // If this IF had a parent IF, reposition the parent's convergence
            // because the parent's branch depth has changed
            int parentIfIdx = (ifBlockIndex < ifBlockCount && ifBlock->parentIfIndex >= 0) ? 
                             ifBlock->parentIfIndex : -1;
            // Adjust parent index if it's after the deleted IF block
            if (parentIfIdx > ifBlockIndex) {
                parentIfIdx--;
            }
            
            // Clean up branch arrays: remove references to deleted nodes
            // nodesToDelete contains all node indices that were deleted
            for (int i = 0; i < ifBlockCount; i++) {
                // Clean up true branch
                int newTrueCount = 0;
                for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
                    int nodeIdx = ifBlocks[i].trueBranchNodes[j];
                    bool wasDeleted = false;
                    for (int k = 0; k < deleteCount; k++) {
                        if (nodeIdx == nodesToDelete[k]) {
                            wasDeleted = true;
                            break;
                        }
                    }
                    if (!wasDeleted) {
                        // Keep this node (it wasn't deleted)
                        ifBlocks[i].trueBranchNodes[newTrueCount++] = nodeIdx;
                    }
                }
                ifBlocks[i].trueBranchCount = newTrueCount;
                
                // Clean up false branch
                int newFalseCount = 0;
                for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
                    int nodeIdx = ifBlocks[i].falseBranchNodes[j];
                    bool wasDeleted = false;
                    for (int k = 0; k < deleteCount; k++) {
                        if (nodeIdx == nodesToDelete[k]) {
                            wasDeleted = true;
                            break;
                        }
                    }
                    if (!wasDeleted) {
                        // Keep this node (it wasn't deleted)
                        ifBlocks[i].falseBranchNodes[newFalseCount++] = nodeIdx;
                    }
                }
                ifBlocks[i].falseBranchCount = newFalseCount;
            }
            
            // Reposition parent IF's convergence if it exists
            if (parentIfIdx >= 0 && parentIfIdx < ifBlockCount) {
                
                reposition_convergence_point(parentIfIdx, true);
            }
            
            // Update owningIfBlock for all remaining nodes
            // Any nodes that were owned by IF blocks after this one need their index decremented
            for (int i = 0; i < nodeCount; i++) {
                if (nodes[i].owningIfBlock > ifBlockIndex) {
                    nodes[i].owningIfBlock--;
                } else if (nodes[i].owningIfBlock == ifBlockIndex) {
                    // This shouldn't happen - all nodes owned by this IF should have been deleted
                    nodes[i].owningIfBlock = -1;
                }
            }
            
            // Pull up the outgoing node and everything below it to maintain normal connection length
            if (incomingFromNode >= 0 && outgoingToNode >= 0) {
                // Calculate how many nodes were deleted that were above outgoingToNode
                int deletedAboveOutgoing = 0;
                for (int i = 0; i < deleteCount; i++) {
                    if (nodesToDelete[i] < outgoingToNode) {
                        deletedAboveOutgoing++;
                    }
                }
                
                // The new index of the outgoing node after deletions
                int newOutgoingIdx = outgoingToNode - deletedAboveOutgoing;
                
                // Similarly, calculate the new index of the incoming node
                int deletedAboveIncoming = 0;
                for (int i = 0; i < deleteCount; i++) {
                    if (nodesToDelete[i] < incomingFromNode) {
                        deletedAboveIncoming++;
                    }
                }
                int newIncomingIdx = incomingFromNode - deletedAboveIncoming;
                
                if (newIncomingIdx >= 0 && newIncomingIdx < nodeCount && 
                    newOutgoingIdx >= 0 && newOutgoingIdx < nodeCount) {
                    
                    FlowNode *incoming = &nodes[newIncomingIdx];
                    FlowNode *outgoing = &nodes[newOutgoingIdx];
                    
                    // Calculate the desired connection length (normal)
                    const double initialConnectionLength = 0.28;
                    double desiredOutgoingY = incoming->y - incoming->height * 0.5 - outgoing->height * 0.5 - initialConnectionLength;
                    
                    // Calculate how much to move up
                    double deltaY = desiredOutgoingY - outgoing->y;
                    
                    // Only pull up if deltaY is positive (moving up)
                    if (deltaY > 0.001) {
                        
                        // First pass: move main branch nodes and track which IF blocks are moved
                        int movedIfBlocks[MAX_IF_BLOCKS];
                        int movedIfBlockCount = 0;
                        
                        for (int i = 0; i < nodeCount; i++) {
                            if (nodes[i].y <= outgoing->y && nodes[i].branchColumn == 0) {
                                nodes[i].y = snap_to_grid_y(nodes[i].y + deltaY);
                                
                                // If this is an IF node, track its IF block for moving branches
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
                        
                        // Second pass: move all branch nodes of the IF blocks that were moved
                        for (int i = 0; i < movedIfBlockCount; i++) {
                            int ifBlockIdx = movedIfBlocks[i];
                            for (int j = 0; j < nodeCount; j++) {
                                if (nodes[j].owningIfBlock == ifBlockIdx && nodes[j].branchColumn != 0) {
                                    nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
                                }
                            }
                        }
                    }
                }
            }
            
            return;  // Done handling IF/CONVERGE deletion
        }
    }
    
    // Save the IF block ownership and branch before deletion (we'll need these later)
    int deletedNodeOwningIfBlock = nodes[nodeIndex].owningIfBlock;
    int deletedNodeBranchColumn = nodes[nodeIndex].branchColumn;
    
    // Find all connections involving this node
    // Remove the node from IF block branch arrays if it belongs to one
    if (nodes[nodeIndex].owningIfBlock >= 0 && nodes[nodeIndex].owningIfBlock < ifBlockCount) {
        int ifIdx = nodes[nodeIndex].owningIfBlock;
        IFBlock *ifBlock = &ifBlocks[ifIdx];
        
        // Check if it's in the true branch
        for (int i = 0; i < ifBlock->trueBranchCount; i++) {
            if (ifBlock->trueBranchNodes[i] == nodeIndex) {
                // Found it - shift remaining nodes down
                for (int j = i; j < ifBlock->trueBranchCount - 1; j++) {
                    ifBlock->trueBranchNodes[j] = ifBlock->trueBranchNodes[j + 1];
                }
                ifBlock->trueBranchCount--;
                break;
            }
        }
        
        // Check if it's in the false branch
        for (int i = 0; i < ifBlock->falseBranchCount; i++) {
            if (ifBlock->falseBranchNodes[i] == nodeIndex) {
                // Found it - shift remaining nodes down
                for (int j = i; j < ifBlock->falseBranchCount - 1; j++) {
                    ifBlock->falseBranchNodes[j] = ifBlock->falseBranchNodes[j + 1];
                }
                ifBlock->falseBranchCount--;
                break;
            }
        }
    }
    
    int incomingConnections[MAX_CONNECTIONS];
    int outgoingConnections[MAX_CONNECTIONS];
    int incomingCount = 0;
    int outgoingCount = 0;
    
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == nodeIndex) {
            outgoingConnections[outgoingCount++] = i;
        }
        if (connections[i].toNode == nodeIndex) {
            incomingConnections[incomingCount++] = i;
        }
    }
    
    // Track newly created connections for position adjustment
    int newConnections[MAX_CONNECTIONS];
    int newConnectionCount = 0;
    
    // Reconnect: for each incoming connection (A -> deleted) and each outgoing connection (deleted -> B),
    // create a new connection (A -> B) if it doesn't already exist
    // IMPORTANT: Only reconnect if A and B are in compatible branches
    for (int i = 0; i < incomingCount; i++) {
        int incomingConn = incomingConnections[i];
        int fromNode = connections[incomingConn].fromNode;
        
        for (int j = 0; j < outgoingCount; j++) {
            int outgoingConn = outgoingConnections[j];
            int toNode = connections[outgoingConn].toNode;
            
            // Skip if trying to connect to itself
            if (fromNode == toNode) continue;
            
            // CRITICAL: Only reconnect if the nodes are in compatible branches
            // Special cases:
            // 1. IF block (branch 0) can connect to nodes in branches (they're its children)
            // 2. Nodes in branches can connect to convergence (branch 0)
            // 3. Nodes in same branch can connect to each other
            // 4. Nodes in different branches should NEVER connect
            bool compatibleBranches = false;
            
            if (nodes[fromNode].type == NODE_IF || nodes[toNode].type == NODE_CONVERGE) {
                // IF to branch or branch to convergence is always allowed
                compatibleBranches = true;
            } else if (nodes[fromNode].branchColumn == nodes[toNode].branchColumn) {
                // Same branch - allowed
                compatibleBranches = true;
            } else if (nodes[fromNode].branchColumn == 0 || nodes[toNode].branchColumn == 0) {
                // One is in main branch (0), other is in a branch - allowed
                compatibleBranches = true;
            }
            // else: different non-zero branches - NOT allowed
            
            if (!compatibleBranches) {
                continue;
            }
            
            // Check if connection already exists for THIS BRANCH
            // When reconnecting IF->convergence, we need separate connections for true/false branches
            bool connectionExists = false;
            int deletedBranch = nodes[nodeIndex].branchColumn;
            
            for (int k = 0; k < connectionCount; k++) {
                if (connections[k].fromNode == fromNode && connections[k].toNode == toNode) {
                    // Found a matching connection, but is it for the correct branch?
                    if (nodes[fromNode].type == NODE_IF && nodes[toNode].type == NODE_CONVERGE) {
                        // This is an IF->convergence connection
                        // Determine which branch this connection represents
                        int existingBranchType = get_if_branch_type(k);
                        int deletedBranchType = (deletedBranch < 0) ? 0 : 1;  // 0=true (left), 1=false (right)
                        
                        if (existingBranchType == deletedBranchType) {
                            // This connection is for the same branch we're reconnecting
                    connectionExists = true;
                            
                    break;
                        } else {
                            // This connection is for a DIFFERENT branch, keep looking
                            continue;
                        }
                    } else {
                        // Not an IF->convergence connection, regular check applies
                        connectionExists = true;
                        
                        break;
                    }
                }
            }
            
            // Create new connection if it doesn't exist and we have room
            if (!connectionExists && connectionCount < MAX_CONNECTIONS) {
                
                connections[connectionCount].fromNode = fromNode;
                connections[connectionCount].toNode = toNode;
                newConnections[newConnectionCount++] = connectionCount;
                connectionCount++;
            }
        }
    }
    
    // Store original Y positions before any adjustments
    double originalYPositions[MAX_NODES];
    for (int i = 0; i < nodeCount; i++) {
        originalYPositions[i] = nodes[i].y;
    }
    
    // Adjust positions of reconnected nodes to maintain standard connection length
    // Only adjust based on newly created connections
    // Track which nodes need to move and by how much
    double nodePositionDeltas[MAX_NODES] = {0.0};
    bool nodeNeedsMove[MAX_NODES] = {false};
    
    for (int i = 0; i < newConnectionCount; i++) {
        int connIdx = newConnections[i];
        int fromNodeIdx = connections[connIdx].fromNode;
        int toNodeIdx = connections[connIdx].toNode;
        
        if (toNodeIdx != nodeIndex) {
            FlowNode *from = &nodes[fromNodeIdx];
            
            // Calculate the new Y position for the "to" node
            const double initialConnectionLength = 0.28;
            double newY = from->y - from->height * 0.5 - nodes[toNodeIdx].height * 0.5 - initialConnectionLength;
            
            // Calculate how much the node needs to move (negative means move up)
            double deltaY = newY - originalYPositions[toNodeIdx];
            
            // Track the movement (use the maximum delta if node is moved multiple times)
            if (!nodeNeedsMove[toNodeIdx] || fabs(deltaY) > fabs(nodePositionDeltas[toNodeIdx])) {
                nodePositionDeltas[toNodeIdx] = deltaY;
                nodeNeedsMove[toNodeIdx] = true;
            }
        }
    }
    
    // Apply movements: move each node and all nodes below it
    // Process nodes from top to bottom (highest Y first) to avoid double-moving
    // Create sorted list of nodes that need to move
    int nodesToMove[MAX_NODES];
    int nodesToMoveCount = 0;
    for (int i = 0; i < nodeCount; i++) {
        if (nodeNeedsMove[i] && i != nodeIndex) {
            nodesToMove[nodesToMoveCount++] = i;
        }
    }
    
    // Sort by original Y position (highest first, since Y decreases downward)
    for (int i = 0; i < nodesToMoveCount - 1; i++) {
        for (int j = i + 1; j < nodesToMoveCount; j++) {
            if (originalYPositions[nodesToMove[i]] < originalYPositions[nodesToMove[j]]) {
                int temp = nodesToMove[i];
                nodesToMove[i] = nodesToMove[j];
                nodesToMove[j] = temp;
            }
        }
    }
    
    // Track IF blocks that are pulled up so we can reposition their convergence points
    int pulledIfBlocks[MAX_NODES];
    int pulledIfBlockCount = 0;
    
    // Apply movements in order from top to bottom
    for (int i = 0; i < nodesToMoveCount; i++) {
        int nodeIdx = nodesToMove[i];
        double deltaY = nodePositionDeltas[nodeIdx];
        double originalY = originalYPositions[nodeIdx];
        
        // Move this node and snap to grid
        nodes[nodeIdx].y = snap_to_grid_y(originalY + deltaY);
        
        // Check if the node WE JUST MOVED is an IF block
        // If so, also move its branch nodes (works for both main branch and nested IFs)
        if (nodes[nodeIdx].type == NODE_IF) {
            // Find the IF block index for this IF node
            int pulledIfBlockIdx = -1;
            for (int k = 0; k < ifBlockCount; k++) {
                if (ifBlocks[k].ifNodeIndex == nodeIdx) {
                    pulledIfBlockIdx = k;
                    break;
                }
            }
            
            if (pulledIfBlockIdx >= 0) {
                // Track this IF block for convergence repositioning later
                bool alreadyTracked = false;
                for (int k = 0; k < pulledIfBlockCount; k++) {
                    if (pulledIfBlocks[k] == pulledIfBlockIdx) {
                        alreadyTracked = true;
                        break;
                    }
                }
                if (!alreadyTracked && pulledIfBlockCount < MAX_NODES) {
                    pulledIfBlocks[pulledIfBlockCount++] = pulledIfBlockIdx;
                }
                
                // Pull all branch nodes owned by this IF block
                for (int k = 0; k < nodeCount; k++) {
                    if (k != nodeIndex && nodes[k].owningIfBlock == pulledIfBlockIdx) {
                        
                        nodes[k].y = snap_to_grid_y(nodes[k].y + deltaY);
                    }
                }
            }
        }
        
        // Move all nodes below this one (based on original positions) up by the same amount
        // Use original positions to determine what's below, but apply to current positions
        // IMPORTANT: Only pull nodes in the SAME BRANCH, but also track IF blocks that move
        int pulledIfBlocksInDeletion[MAX_IF_BLOCKS];
        int pulledIfBlockCountInDeletion = 0;
        
        for (int j = 0; j < nodeCount; j++) {
            if (j != nodeIdx && j != nodeIndex && originalYPositions[j] < originalY) {
                // Only pull nodes in the same branch as the deleted node
                // Case 1: Both in main branch (0)
                // Case 2: Both in same non-zero branch AND same IF block
                bool shouldPull = false;
                if (deletedNodeBranchColumn == 0 && nodes[j].branchColumn == 0) {
                    // Both in main branch
                    shouldPull = true;
                } else if (deletedNodeBranchColumn != 0 && deletedNodeBranchColumn == nodes[j].branchColumn && deletedNodeOwningIfBlock == nodes[j].owningIfBlock) {
                    // Same non-zero branch AND same IF block ownership
                    shouldPull = true;
                }
                // Don't pull nodes in different branches
                
                if (!shouldPull) {
                    continue;
                }
                
                nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
                
                // If this is an IF node in main branch, track it to pull its branches too
                if (nodes[j].type == NODE_IF && nodes[j].branchColumn == 0) {
                    for (int k = 0; k < ifBlockCount; k++) {
                        if (ifBlocks[k].ifNodeIndex == j) {
                            pulledIfBlocksInDeletion[pulledIfBlockCountInDeletion++] = k;
                            break;
                        }
                    }
                }
            }
        }
        
        // Second pass: pull all branch nodes of IF blocks that were moved
        for (int i = 0; i < pulledIfBlockCountInDeletion; i++) {
            int ifBlockIdx = pulledIfBlocksInDeletion[i];
            for (int j = 0; j < nodeCount; j++) {
                if (nodes[j].owningIfBlock == ifBlockIdx && nodes[j].branchColumn != 0) {
                    nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
                }
            }
        }
    }
    
    // Reposition convergence points for any IF blocks that were pulled
    for (int i = 0; i < pulledIfBlockCount; i++) {
        reposition_convergence_point(pulledIfBlocks[i], false);  // Don't push nodes below when deleting
    }
    
    // Remove all connections involving the deleted node
    // Work backwards to avoid index shifting issues
    for (int i = connectionCount - 1; i >= 0; i--) {
        if (connections[i].fromNode == nodeIndex || connections[i].toNode == nodeIndex) {
            
            // Shift remaining connections down
            for (int j = i; j < connectionCount - 1; j++) {
                connections[j] = connections[j + 1];
            }
            connectionCount--;
        }
    }
    
    // Update connection indices that reference nodes after the deleted one
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode > nodeIndex) {
            connections[i].fromNode--;
        }
        if (connections[i].toNode > nodeIndex) {
            connections[i].toNode--;
        }
    }
    
    // Remove the node and shift remaining nodes
    for (int i = nodeIndex; i < nodeCount - 1; i++) {
        
        nodes[i] = nodes[i + 1];
    }
    nodeCount--;
    
    // Update branch node indices in all IF blocks after deletion
    // Any node index > nodeIndex needs to be decremented
    
    for (int i = 0; i < ifBlockCount; i++) {
        // Update IF and CONVERGE node indices
        if (ifBlocks[i].ifNodeIndex > nodeIndex) {
            ifBlocks[i].ifNodeIndex--;
        }
        if (ifBlocks[i].convergeNodeIndex > nodeIndex) {
            ifBlocks[i].convergeNodeIndex--;
        }
        
        // Update true branch nodes
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            if (ifBlocks[i].trueBranchNodes[j] > nodeIndex) {
                ifBlocks[i].trueBranchNodes[j]--;
            }
        }
        // Update false branch nodes
        for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
            if (ifBlocks[i].falseBranchNodes[j] > nodeIndex) {
                ifBlocks[i].falseBranchNodes[j]--;
            }
        }
    }
    
    // NOW reposition convergence point after the node has been deleted
    // This ensures we count the correct number of remaining nodes in each branch
    // Don't push nodes below when deleting - we're shrinking, not growing
    if (deletedNodeOwningIfBlock >= 0) {
        reposition_convergence_point(deletedNodeOwningIfBlock, false);
    }
    
    // Recalculate all branch widths and positions after deletion
    // This ensures parent IF branches shrink when nested IFs are removed
    update_all_branch_positions();
    
    // Rebuild variable table after deletion
    rebuild_variable_table();
}

static bool parse_declare_block(const char* value, char* varName, VariableType* varType, bool* isArray, int* arraySize);
static bool parse_assignment(const char* value, char* leftVar, char* rightValue, bool* isRightVar, bool* isQuotedString);
static VariableType detect_literal_type(const char* value);
static bool parse_array_access(const char* expr, char* arrayName, char* indexExpr);
static bool evaluate_index_expression(const char* indexExpr, int* result, char* errorMsg);
static bool check_array_bounds(const char* arrayName, const char* indexExpr, char* errorMsg);
static void extract_array_accesses(const char* expr, char arrayNames[][MAX_VAR_NAME_LENGTH], 
                                   char indexExprs[][MAX_VALUE_LENGTH], int* accessCount);
static void extract_variables_from_expression_simple(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount);
static void extract_output_placeholders(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount);
static void extract_output_placeholders_with_arrays(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], 
                                                    char indexExprs[][MAX_VALUE_LENGTH], bool* isArrayAccess, int* varCount);
static bool parse_input_block(const char* value, char* varName, char* indexExpr, bool* isArray);

// Find variable by name
static Variable* find_variable(const char* name) {
    for (int i = 0; i < variableCount; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return &variables[i];
        }
    }
    return NULL;
}

// Validate variable name (must start with letter/underscore, then alphanumeric/underscore)
static bool is_valid_variable_name(const char* name) {
    if (!name || name[0] == '\0') return false;
    
    // Reject reserved keywords: true and false
    if (strcmp(name, "true") == 0 || strcmp(name, "false") == 0) {
        return false;
    }
    
    // First character must be letter or underscore
    char first = name[0];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
        return false;
    }
    
    // Rest must be alphanumeric or underscore
    for (int i = 1; name[i] != '\0'; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    
    return true;
}

// Check if variable name already exists
static bool variable_name_exists(const char* name, int excludeNodeIndex) {
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

// Extract all variable names from an expression (including array names from array accesses)
static void extract_variables_from_expression(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount) {
    *varCount = 0;
    if (!expr || expr[0] == '\0') return;
    
    // First, extract array accesses
    char arrayNames[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
    char indexExprs[MAX_VARIABLES][MAX_VALUE_LENGTH];
    int arrayAccessCount = 0;
    extract_array_accesses(expr, arrayNames, indexExprs, &arrayAccessCount);
    
    // Add array names to variable list
    for (int i = 0; i < arrayAccessCount; i++) {
        bool found = false;
        for (int j = 0; j < *varCount; j++) {
            if (strcmp(varNames[j], arrayNames[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found && *varCount < MAX_VARIABLES) {
            strncpy(varNames[*varCount], arrayNames[i], MAX_VAR_NAME_LENGTH - 1);
            varNames[*varCount][MAX_VAR_NAME_LENGTH - 1] = '\0';
            (*varCount)++;
        }
    }
    
    // Also extract index variables from array index expressions
    for (int i = 0; i < arrayAccessCount; i++) {
        char indexVars[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
        int indexVarCount = 0;
        extract_variables_from_expression_simple(indexExprs[i], indexVars, &indexVarCount);
        
        for (int j = 0; j < indexVarCount; j++) {
            bool found = false;
            for (int k = 0; k < *varCount; k++) {
                if (strcmp(varNames[k], indexVars[j]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found && *varCount < MAX_VARIABLES) {
                strncpy(varNames[*varCount], indexVars[j], MAX_VAR_NAME_LENGTH - 1);
                varNames[*varCount][MAX_VAR_NAME_LENGTH - 1] = '\0';
                (*varCount)++;
            }
        }
    }
    
    // Then extract simple variables (not in array accesses)
    char buffer[MAX_VALUE_LENGTH];
    strncpy(buffer, expr, MAX_VALUE_LENGTH - 1);
    buffer[MAX_VALUE_LENGTH - 1] = '\0';
    
    const char* p = buffer;
    while (*p != '\0' && *varCount < MAX_VARIABLES) {
        // Skip whitespace and operators
        while (*p == ' ' || *p == '\t' || *p == '+' || *p == '-' || *p == '*' || 
               *p == '/' || *p == '(' || *p == ')' || *p == '=') {
            p++;
        }
        
        // Skip array accesses (we already handled them)
        if (*p == '[') {
            // Skip to matching ']'
            p++;
            while (*p != '\0' && *p != ']') {
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
            if (*p == ']') p++;
            continue;
        }
        
        if (*p == '\0') break;
        
        // Check if it starts with letter/underscore (potential variable)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            int len = 0;
            char varName[MAX_VAR_NAME_LENGTH];
            const char* start = p;
            
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
            
            // Check if next char is '[' - if so, it's an array access we already handled
            if (*p == '[') {
                // Skip the array access
                p++;
                while (*p != '\0' && *p != ']') {
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
                if (*p == ']') p++;
                continue;
            }
            
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

// Simple variable extraction (for index expressions)
static void extract_variables_from_expression_simple(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount) {
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

// Parse array access (format: "arr[index]" or "arr[i]")
// Returns true if it's an array access, extracts array name and index expression
static bool parse_array_access(const char* expr, char* arrayName, char* indexExpr) {
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
static bool evaluate_index_expression(const char* indexExpr, int* result, char* errorMsg) {
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
static bool check_array_bounds(const char* arrayName, const char* indexExpr, char* errorMsg) {
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
static void extract_array_accesses(const char* expr, char arrayNames[][MAX_VAR_NAME_LENGTH], 
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
static bool validate_expression(const char* expr, VariableType expectedType, VariableType* actualType, char* errorMsg) {
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
static bool parse_declare_block(const char* value, char* varName, VariableType* varType, bool* isArray, int* arraySize) {
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
static bool parse_assignment(const char* value, char* leftVar, char* rightValue, bool* isRightVar, bool* isQuotedString) {
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

// Determine type of a literal value
static VariableType detect_literal_type(const char* value) {
    if (!value || value[0] == '\0') return VAR_TYPE_INT; // Default
    
    // String literal (quoted)
    if (value[0] == '"' && value[strlen(value) - 1] == '"') {
        return VAR_TYPE_STRING;
    }
    
    // Boolean literal
    if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
        return VAR_TYPE_BOOL;
    }
    
    // Check for real number (contains decimal point)
    bool hasDecimal = false;
    for (int i = 0; value[i] != '\0'; i++) {
        if (value[i] == '.') {
            hasDecimal = true;
            break;
        }
    }
    if (hasDecimal) {
        return VAR_TYPE_REAL;
    }
    
    // Integer (default)
    return VAR_TYPE_INT;
}

// Extract variable placeholders from output format string (pattern: {varName} or {arr[index]})
static void extract_output_placeholders(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount) {
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
static void extract_output_placeholders_with_arrays(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], 
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
static bool parse_input_block(const char* value, char* varName, char* indexExpr, bool* isArray) {
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
static bool validate_assignment(const char* value) {
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

// Dropdown list dialog helper function (returns selected index or -1 on cancel)
// Similar to tinyfiledialogs, uses native OS dialogs
static int tinyfd_listDialog(const char* aTitle, const char* aMessage, int numOptions, const char* const* options) {
    if (numOptions <= 0 || !options) {
        return -1;
    }
    
#ifdef _WIN32
    // Windows: Try PowerShell Out-GridView first, then fallback to VBScript InputBox
    char tempFile[512];
    char psFile[512];
    char cmd[2048];
    FILE* f;
    int selected = -1;
    
    // Get temp directory
    const char* tempDir = getenv("TEMP");
    if (!tempDir) tempDir = getenv("TMP");
    if (!tempDir) tempDir = "C:\\Windows\\Temp";
    
    snprintf(tempFile, sizeof(tempFile), "%s\\tinyfd_list_result.txt", tempDir);
    snprintf(psFile, sizeof(psFile), "%s\\tinyfd_list.ps1", tempDir);
    
    // Try PowerShell Out-GridView first (provides a selection grid)
    FILE* ps = fopen(psFile, "w");
    if (ps) {
        fprintf(ps, "$options = @(");
        for (int i = 0; i < numOptions; i++) {
            if (i > 0) fprintf(ps, ", ");
            fprintf(ps, "'");
            // Escape single quotes in PowerShell
            for (int j = 0; options[i][j] != '\0'; j++) {
                if (options[i][j] == '\'') {
                    fprintf(ps, "''");
                } else {
                    fprintf(ps, "%c", options[i][j]);
                }
            }
            fprintf(ps, "'");
        }
        fprintf(ps, ")\n");
        fprintf(ps, "$selected = $options | Out-GridView -Title \"%s\" -OutputMode Single\n", 
            aTitle ? aTitle : "Select");
        fprintf(ps, "if ($selected) {\n");
        fprintf(ps, "  $index = [array]::IndexOf($options, $selected)\n");
        fprintf(ps, "  [System.IO.File]::WriteAllText(\"%s\", $index.ToString())\n", tempFile);
        fprintf(ps, "}\n");
        fclose(ps);
        
        // Run PowerShell script
        snprintf(cmd, sizeof(cmd), "powershell -ExecutionPolicy Bypass -File \"%s\"", psFile);
        int result = system(cmd);
        
        // Read result if PowerShell succeeded
        if (result == 0) {
            f = fopen(tempFile, "r");
            if (f) {
                char line[32];
                if (fgets(line, sizeof(line), f)) {
                    selected = atoi(line);
                    if (selected < 0 || selected >= numOptions) {
                        selected = -1;
                    }
                }
                fclose(f);
                remove(tempFile);
            }
        }
        remove(psFile);
    }
    
    // Fallback to console input if PowerShell failed
    if (selected == -1) {
        printf("\n%s\n", aTitle ? aTitle : "Select");
        if (aMessage) {
            printf("%s\n", aMessage);
        }
        printf("Options:\n");
        for (int i = 0; i < numOptions; i++) {
            printf("  %d: %s\n", i + 1, options[i]);
        }
        printf("Enter option number (1-%d): ", numOptions);
        fflush(stdout);
        
        char input[32];
        if (fgets(input, sizeof(input), stdin)) {
            int choice = atoi(input) - 1;
            if (choice >= 0 && choice < numOptions) {
                selected = choice;
            }
        }
    }
    
    return selected;
#else
    // Unix/Linux: Use zenity or kdialog
    char cmd[4096];
    FILE* pipe;
    char result[256];
    int selected = -1;
    
    // Try zenity first
    snprintf(cmd, sizeof(cmd), "zenity --list --title=\"%s\" --text=\"%s\" --column=\"Options\"",
        aTitle ? aTitle : "Select", aMessage ? aMessage : "");
    
    for (int i = 0; i < numOptions; i++) {
        // Escape quotes in option text
        char escaped[512];
        int j = 0;
        for (int k = 0; options[i][k] != '\0' && j < sizeof(escaped) - 1; k++) {
            if (options[i][k] == '"' || options[i][k] == '\\') {
                escaped[j++] = '\\';
            }
            escaped[j++] = options[i][k];
        }
        escaped[j] = '\0';
        strncat(cmd, " \"", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, escaped, sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, "\"", sizeof(cmd) - strlen(cmd) - 1);
    }
    
    strncat(cmd, " 2>/dev/null", sizeof(cmd) - strlen(cmd) - 1);
    
    pipe = popen(cmd, "r");
    if (pipe) {
        if (fgets(result, sizeof(result), pipe)) {
            // Remove newline
            result[strcspn(result, "\n")] = '\0';
            // Find which option was selected
            for (int i = 0; i < numOptions; i++) {
                if (strcmp(result, options[i]) == 0) {
                    selected = i;
                    break;
                }
            }
        }
        pclose(pipe);
    } else {
        // Try kdialog as fallback
        snprintf(cmd, sizeof(cmd), "kdialog --title \"%s\" --combobox \"%s\"",
            aTitle ? aTitle : "Select", aMessage ? aMessage : "");
        
        for (int i = 0; i < numOptions; i++) {
            char escaped[512];
            int j = 0;
            for (int k = 0; options[i][k] != '\0' && j < sizeof(escaped) - 1; k++) {
                if (options[i][k] == '"' || options[i][k] == '\\') {
                    escaped[j++] = '\\';
                }
                escaped[j++] = options[i][k];
            }
            escaped[j] = '\0';
            strncat(cmd, " \"", sizeof(cmd) - strlen(cmd) - 1);
            strncat(cmd, escaped, sizeof(cmd) - strlen(cmd) - 1);
            strncat(cmd, "\"", sizeof(cmd) - strlen(cmd) - 1);
        }
        
        strncat(cmd, " 2>/dev/null", sizeof(cmd) - strlen(cmd) - 1);
        
        pipe = popen(cmd, "r");
        if (pipe) {
            if (fgets(result, sizeof(result), pipe)) {
                result[strcspn(result, "\n")] = '\0';
                for (int i = 0; i < numOptions; i++) {
                    if (strcmp(result, options[i]) == 0) {
                        selected = i;
                        break;
                    }
                }
            }
            pclose(pipe);
        }
    }
    
    // Fallback to console input if native dialogs failed
    if (selected == -1) {
        printf("\n%s\n", aTitle ? aTitle : "Select");
        if (aMessage) {
            printf("%s\n", aMessage);
        }
        printf("Options:\n");
        for (int i = 0; i < numOptions; i++) {
            printf("  %d: %s\n", i + 1, options[i]);
        }
        printf("Enter option number (1-%d): ", numOptions);
        fflush(stdout);
        
        char input[32];
        if (fgets(input, sizeof(input), stdin)) {
            int choice = atoi(input) - 1;
            if (choice >= 0 && choice < numOptions) {
                selected = choice;
            }
        }
    }
    
    return selected;
#endif
}

// Edit node value using text input dialog
void edit_node_value(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= nodeCount) {
        return;
    }
    
    FlowNode *node = &nodes[nodeIndex];
    
    if (node->type == NODE_DECLARE) {
        // DECLARE BLOCK: Step 1 - Select type using dropdown
        const char* typeOptions[] = {"int", "real", "string", "bool"};
        int typeChoice = tinyfd_listDialog("Select Variable Type", 
            "Choose the variable type:", 4, typeOptions);
        
        if (typeChoice < 0 || typeChoice >= 4) return; // User cancelled or invalid
        
        VariableType selectedType = (VariableType)typeChoice;
        const char* typeName = typeOptions[selectedType];
        
        // Step 2 - Get variable name
        char currentName[MAX_VAR_NAME_LENGTH] = "";
        int currentArraySize = 0;
        // Try to extract current name if block already has a value
        if (node->value[0] != '\0') {
            char varName[MAX_VAR_NAME_LENGTH];
            VariableType varType;
            bool isArray;
            int arraySize;
            if (parse_declare_block(node->value, varName, &varType, &isArray, &arraySize)) {
                strncpy(currentName, varName, MAX_VAR_NAME_LENGTH - 1);
                currentArraySize = arraySize;
            }
        }
        
        const char* nameResult = tinyfd_inputBox(
            "Variable Name",
            "Enter variable name:",
            currentName
        );
        
        if (!nameResult || nameResult[0] == '\0') return;
        
        // Copy nameResult to local buffer immediately (tinyfd might reuse its buffer)
        char varName[MAX_VAR_NAME_LENGTH];
        strncpy(varName, nameResult, MAX_VAR_NAME_LENGTH - 1);
        varName[MAX_VAR_NAME_LENGTH - 1] = '\0';
        
        // Step 3 - Validate variable name
        if (!is_valid_variable_name(varName)) {
            tinyfd_messageBox("Validation Error", 
                "Invalid variable name. Must start with letter or underscore, followed by letters, numbers, or underscores.",
                "ok", "error", 1);
            return;
        }
        
        // Step 4 - Check for duplicate
        if (variable_name_exists(varName, nodeIndex)) {
            tinyfd_messageBox("Validation Error", 
                "Variable name already exists. Please choose a different name.",
                "ok", "error", 1);
            return;
        }
        
        // Step 5 - Ask for array
        int isArrayChoice = tinyfd_messageBox("Array Variable?", 
            "Is this an array variable?", "yesno", "question", 0);
        bool isArray = (isArrayChoice == 1); // 1 = yes, 0 = no
        
        int arraySize = 0;
        if (isArray) {
            // Step 5a - Get array size
            char sizeStr[32];
            if (currentArraySize > 0) {
                snprintf(sizeStr, sizeof(sizeStr), "%d", currentArraySize);
            } else {
                sizeStr[0] = '\0';
            }
            
            const char* sizeInput = tinyfd_inputBox(
                "Array Size",
                "Enter array size (number of elements):",
                sizeStr
            );
            
            if (!sizeInput || sizeInput[0] == '\0') return;
            
            arraySize = atoi(sizeInput);
            if (arraySize <= 0) {
                tinyfd_messageBox("Validation Error", 
                    "Array size must be a positive integer.",
                    "ok", "error", 1);
                return;
            }
        }
        
        // Step 6 - Build and save value string
        char newValue[MAX_VALUE_LENGTH];
        if (isArray) {
            if (arraySize > 0) {
                snprintf(newValue, sizeof(newValue), "%s %s[%d]", typeName, varName, arraySize);
            } else {
                snprintf(newValue, sizeof(newValue), "%s %s[]", typeName, varName);
            }
        } else {
            snprintf(newValue, sizeof(newValue), "%s %s", typeName, varName);
        }
        
        strncpy(node->value, newValue, MAX_VALUE_LENGTH - 1);
        node->value[MAX_VALUE_LENGTH - 1] = '\0';
        
        // Recalculate width based on text content
        float fontSize = node->height * 0.3f;
        node->width = calculate_block_width(node->value, fontSize, 0.35f);
        
        // Rebuild variable table
        rebuild_variable_table();
        
    } else if (node->type == NODE_ASSIGNMENT) {
        // ASSIGNMENT BLOCK: Step 1 - Select variable
        if (variableCount == 0) {
            tinyfd_messageBox("No Variables", 
                "No variables declared yet. Please declare a variable first.",
                "ok", "warning", 1);
            return;
        }
        
        // Build array of variable option strings for dropdown
        char varOptions[MAX_VARIABLES][MAX_VAR_NAME_LENGTH + 30];
        const char* varOptionPtrs[MAX_VARIABLES];
        for (int i = 0; i < variableCount; i++) {
            const char* typeStr = "";
            switch (variables[i].type) {
                case VAR_TYPE_INT: typeStr = "int"; break;
                case VAR_TYPE_REAL: typeStr = "real"; break;
                case VAR_TYPE_STRING: typeStr = "string"; break;
                case VAR_TYPE_BOOL: typeStr = "bool"; break;
            }
            if (variables[i].is_array) {
                if (variables[i].array_size > 0) {
                    snprintf(varOptions[i], sizeof(varOptions[i]), "%s %s[%d]", 
                        typeStr, variables[i].name, variables[i].array_size);
                } else {
                    snprintf(varOptions[i], sizeof(varOptions[i]), "%s %s[]", 
                        typeStr, variables[i].name);
                }
            } else {
                snprintf(varOptions[i], sizeof(varOptions[i]), "%s %s", 
                    typeStr, variables[i].name);
            }
            varOptionPtrs[i] = varOptions[i];
        }
        
        int varChoice = tinyfd_listDialog("Select Variable", 
            "Choose the variable to assign to:", variableCount, varOptionPtrs);
        
        if (varChoice < 0 || varChoice >= variableCount) {
            return; // User cancelled or invalid
        }
        
        Variable* selectedVar = &variables[varChoice];
        
        // Step 2 - Get index if array
        char indexExpr[MAX_VALUE_LENGTH] = "";
        char leftSide[MAX_VALUE_LENGTH];
        
        if (selectedVar->is_array) {
            // Extract current index if block already has a value
            if (node->value[0] != '\0') {
                char arrayName[MAX_VAR_NAME_LENGTH];
                char currentIndex[MAX_VALUE_LENGTH];
                if (parse_array_access(node->value, arrayName, currentIndex)) {
                    if (strcmp(arrayName, selectedVar->name) == 0) {
                        strncpy(indexExpr, currentIndex, MAX_VALUE_LENGTH - 1);
                    }
                }
            }
            
            const char* indexInput = tinyfd_inputBox(
                "Array Index",
                "Enter index (integer literal or int variable, e.g., 0, i, i+1):",
                indexExpr
            );
            
            if (!indexInput || indexInput[0] == '\0') return;
            
            // Validate index expression
            char errorMsg[MAX_VALUE_LENGTH];
            int dummyIndex;
            if (!evaluate_index_expression(indexInput, &dummyIndex, errorMsg)) {
                tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                return;
            }
            
            // Check array bounds
            if (!check_array_bounds(selectedVar->name, indexInput, errorMsg)) {
                tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                return;
            }
            
            strncpy(indexExpr, indexInput, MAX_VALUE_LENGTH - 1);
            indexExpr[MAX_VALUE_LENGTH - 1] = '\0';
            
            // Build left side: arr[index]
            snprintf(leftSide, sizeof(leftSide), "%s[%s]", selectedVar->name, indexExpr);
        } else {
            // Not an array, just variable name
            strncpy(leftSide, selectedVar->name, MAX_VALUE_LENGTH - 1);
            leftSide[MAX_VALUE_LENGTH - 1] = '\0';
        }
        
        // Step 3 - Get expression
        char currentExpr[MAX_VALUE_LENGTH] = "";
        // Try to extract current expression if block already has a value
        if (node->value[0] != '\0') {
            char leftVar[MAX_VAR_NAME_LENGTH];
            char rightValue[MAX_VALUE_LENGTH];
            bool isRightVar = false;
            bool isQuotedString = false;
            if (parse_assignment(node->value, leftVar, rightValue, &isRightVar, &isQuotedString)) {
                strncpy(currentExpr, rightValue, MAX_VALUE_LENGTH - 1);
            }
        }
        
        const char* exprResult = tinyfd_inputBox(
            "Assignment Expression",
            "Enter expression (e.g., 5, b, a + 1, \"hello\", arr[i]):",
            currentExpr
        );
        
        if (!exprResult || exprResult[0] == '\0') return;
        
        // Step 4 - Validate expression and check array bounds in expression
        VariableType actualType;
        char errorMsg[MAX_VALUE_LENGTH];
        
        // Check for array accesses in the expression
        char exprArrayNames[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
        char exprIndexExprs[MAX_VARIABLES][MAX_VALUE_LENGTH];
        int exprAccessCount = 0;
        extract_array_accesses(exprResult, exprArrayNames, exprIndexExprs, &exprAccessCount);
        
        // Validate each array access in expression
        for (int i = 0; i < exprAccessCount; i++) {
            if (!check_array_bounds(exprArrayNames[i], exprIndexExprs[i], errorMsg)) {
                tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                return;
            }
        }
        
        if (!validate_expression(exprResult, selectedVar->type, &actualType, errorMsg)) {
            tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
            return;
        }
        
        // Step 5 - Save assignment
        char newValue[MAX_VALUE_LENGTH];
        snprintf(newValue, sizeof(newValue), "%s = %s", leftSide, exprResult);
        
        strncpy(node->value, newValue, MAX_VALUE_LENGTH - 1);
        node->value[MAX_VALUE_LENGTH - 1] = '\0';
        
        // Recalculate width based on text content
        float fontSize = node->height * 0.3f;
        node->width = calculate_block_width(node->value, fontSize, 0.35f);
        
    } else if (node->type == NODE_INPUT) {
        // INPUT BLOCK: Step 1 - Check if variables exist
        if (variableCount == 0) {
            tinyfd_messageBox("No Variables", 
                "No variables declared yet. Please declare a variable first.",
                "ok", "warning", 1);
            return;
        }
        
        // Step 2 - Build array of variable option strings for dropdown
        char varOptions[MAX_VARIABLES][MAX_VAR_NAME_LENGTH + 30];
        const char* varOptionPtrs[MAX_VARIABLES];
        for (int i = 0; i < variableCount; i++) {
            const char* typeStr = "";
            switch (variables[i].type) {
                case VAR_TYPE_INT: typeStr = "int"; break;
                case VAR_TYPE_REAL: typeStr = "real"; break;
                case VAR_TYPE_STRING: typeStr = "string"; break;
                case VAR_TYPE_BOOL: typeStr = "bool"; break;
            }
            if (variables[i].is_array) {
                if (variables[i].array_size > 0) {
                    snprintf(varOptions[i], sizeof(varOptions[i]), "%s %s[%d]", 
                        typeStr, variables[i].name, variables[i].array_size);
                } else {
                    snprintf(varOptions[i], sizeof(varOptions[i]), "%s %s[]", 
                        typeStr, variables[i].name);
                }
            } else {
                snprintf(varOptions[i], sizeof(varOptions[i]), "%s %s", 
                    typeStr, variables[i].name);
            }
            varOptionPtrs[i] = varOptions[i];
        }
        
        int varChoice = tinyfd_listDialog("Select Variable", 
            "Choose the variable to read input into:", variableCount, varOptionPtrs);
        
        if (varChoice < 0 || varChoice >= variableCount) {
            return; // User cancelled or invalid
        }
        
        Variable* selectedVar = &variables[varChoice];
        
        // Step 3 - Get index if array
        char indexExpr[MAX_VALUE_LENGTH] = "";
        char newValue[MAX_VALUE_LENGTH];
        
        if (selectedVar->is_array) {
            // Extract current index if block already has a value
            if (node->value[0] != '\0') {
                char varName[MAX_VAR_NAME_LENGTH];
                char currentIndex[MAX_VALUE_LENGTH];
                bool isArray;
                if (parse_input_block(node->value, varName, currentIndex, &isArray)) {
                    if (strcmp(varName, selectedVar->name) == 0 && isArray) {
                        strncpy(indexExpr, currentIndex, MAX_VALUE_LENGTH - 1);
                    }
                }
            }
            
            const char* indexInput = tinyfd_inputBox(
                "Array Index",
                "Enter index (integer literal or int variable, e.g., 0, i, i+1):",
                indexExpr
            );
            
            if (!indexInput || indexInput[0] == '\0') return;
            
            // Validate index expression
            char errorMsg[MAX_VALUE_LENGTH];
            int dummyIndex;
            if (!evaluate_index_expression(indexInput, &dummyIndex, errorMsg)) {
                tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                return;
            }
            
            // Check array bounds
            if (!check_array_bounds(selectedVar->name, indexInput, errorMsg)) {
                tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                return;
            }
            
            strncpy(indexExpr, indexInput, MAX_VALUE_LENGTH - 1);
            indexExpr[MAX_VALUE_LENGTH - 1] = '\0';
            
            // Build value: arrName[index]
            snprintf(newValue, sizeof(newValue), "%s[%s]", selectedVar->name, indexExpr);
        } else {
            // Not an array, just variable name
            strncpy(newValue, selectedVar->name, MAX_VALUE_LENGTH - 1);
            newValue[MAX_VALUE_LENGTH - 1] = '\0';
        }
        
        // Step 4 - Save input block value
        strncpy(node->value, newValue, MAX_VALUE_LENGTH - 1);
        node->value[MAX_VALUE_LENGTH - 1] = '\0';
        
        // Recalculate width based on text content
        float fontSize = node->height * 0.3f;
        node->width = calculate_block_width(node->value, fontSize, 0.35f);
        
    } else if (node->type == NODE_OUTPUT) {
        // OUTPUT BLOCK: Step 1 - Get format string
        char currentFormat[MAX_VALUE_LENGTH] = "";
        // Try to extract current format if block already has a value
        if (node->value[0] != '\0') {
            strncpy(currentFormat, node->value, MAX_VALUE_LENGTH - 1);
            currentFormat[MAX_VALUE_LENGTH - 1] = '\0';
        }
        
        const char* formatResult = tinyfd_inputBox(
            "Output Format String",
            "Enter format string with variable placeholders (e.g., \"Hello {name}, value is {x}\" or \"Array[0] = {arr[i]}\"):",
            currentFormat
        );
        
        if (!formatResult || formatResult[0] == '\0') return;
        
        // Step 2 - Extract and validate placeholders (including array accesses)
        char varNames[MAX_VARIABLES][MAX_VAR_NAME_LENGTH];
        char indexExprs[MAX_VARIABLES][MAX_VALUE_LENGTH];
        bool isArrayAccess[MAX_VARIABLES];
        int varCount = 0;
        extract_output_placeholders_with_arrays(formatResult, varNames, indexExprs, isArrayAccess, &varCount);
        
        // Validate that all referenced variables exist and array accesses are valid
        char errorMsg[MAX_VALUE_LENGTH];
        for (int i = 0; i < varCount; i++) {
            Variable* var = find_variable(varNames[i]);
            if (!var) {
                snprintf(errorMsg, MAX_VALUE_LENGTH, 
                    "Variable '%s' referenced in format string is not declared", varNames[i]);
                tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                return;
            }
            
            // If it's an array access, validate the index
            if (isArrayAccess[i]) {
                if (!var->is_array) {
                    snprintf(errorMsg, MAX_VALUE_LENGTH, 
                        "Variable '%s' is not an array, but array access syntax was used", varNames[i]);
                    tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                    return;
                }
                
                // Validate index expression
                int dummyIndex;
                if (!evaluate_index_expression(indexExprs[i], &dummyIndex, errorMsg)) {
                    tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                    return;
                }
                
                // Check array bounds
                if (!check_array_bounds(varNames[i], indexExprs[i], errorMsg)) {
                    tinyfd_messageBox("Validation Error", errorMsg, "ok", "error", 1);
                    return;
                }
            }
        }
        
        // Step 3 - Save output block value
        strncpy(node->value, formatResult, MAX_VALUE_LENGTH - 1);
        node->value[MAX_VALUE_LENGTH - 1] = '\0';
        
        // Recalculate width based on text content
        float fontSize = node->height * 0.3f;
        node->width = calculate_block_width(node->value, fontSize, 0.35f);
        
    } else if (node->type == NODE_CYCLE) {
        int cycleIdx = find_cycle_block_by_cycle_node(nodeIndex);
        if (cycleIdx < 0 || cycleIdx >= cycleBlockCount) return;
        CycleBlock *cycle = &cycleBlocks[cycleIdx];
        
        // Select type (default WHILE)
        CycleType prevType = cycle->cycleType;
        CycleType chosenType = prompt_cycle_type();
        cycle->cycleType = chosenType;
        
        // Only adjust wiring/positions if switching TO or FROM DO
        // WHILE and FOR have the same connection structure (cycle -> body -> end),
        // so switching between them doesn't require rewiring
        // Only DO is different (end -> body -> cycle), so rewiring is needed when DO is involved
        bool needsRewiring = (prevType == CYCLE_DO || chosenType == CYCLE_DO) && (prevType != chosenType);
        
        if (!needsRewiring) {
            // No rewiring needed - WHILE/FOR to WHILE/FOR, or same type
            // Just update the value/condition without rewiring
            // This preserves the existing connections that work correctly
        } else {
            // Type changed - proceed with rewiring
        
        // Adjust wiring/positions if type changed (handles DO reversal)
        int cycleNodeIndex = cycle->cycleNodeIndex;
        int endNodeIndex = cycle->cycleEndNodeIndex;
        int parentConn = -1, middleConn = -1, nextConn = -1, nextTarget = -1;
        int parentToCycle = -1, parentToEnd = -1;
        
        // CRITICAL: Swap positions FIRST before detecting/rewiring connections
        // This ensures connection detection uses the correct positions
        if (chosenType == CYCLE_DO) {
            // DO: end point should be above cycle block
            if (nodes[endNodeIndex].y < nodes[cycleNodeIndex].y) {
                double tmpY = nodes[cycleNodeIndex].y;
                nodes[cycleNodeIndex].y = nodes[endNodeIndex].y;
                nodes[endNodeIndex].y = tmpY;
            }
        } else {
            // WHILE/FOR: cycle should be above end
            if (nodes[cycleNodeIndex].y < nodes[endNodeIndex].y) {
                double tmpY = nodes[cycleNodeIndex].y;
                nodes[cycleNodeIndex].y = nodes[endNodeIndex].y;
                nodes[endNodeIndex].y = tmpY;
            }
        }
        
        // First, find next target (needed for body node detection)
        // The next target is the node AFTER the loop, which depends on CURRENT loop type (prevType):
        // - For DO: next target is connected FROM cycle node (cycle is at bottom)
        // - For WHILE/FOR: next target is connected FROM end node (end is at bottom)
        // We use prevType because we're looking for the current state's next connection
        for (int i = 0; i < connectionCount; i++) {
            int f = connections[i].fromNode;
            int t = connections[i].toNode;
            // Use prevType to find the current next connection (before rewiring)
            bool isExitConnection = false;
            if (prevType == CYCLE_DO) {
                // DO: exit is from cycle node
                isExitConnection = (f == cycleNodeIndex && t != endNodeIndex);
            } else {
                // WHILE/FOR: exit is from end node
                isExitConnection = (f == endNodeIndex && t != cycleNodeIndex);
            }
            if (isExitConnection) {
                nextConn = i;
                nextTarget = t;
                break; // Just need one next target
            }
        }
        
        // Identify all body nodes (nodes that are part of the loop body)
        // Body nodes are those that are reachable from cycle/end through the loop body
        bool isBodyNode[MAX_NODES] = {false};
        bool changed = true;
        // Start with nodes directly connected to cycle/end (excluding next target)
        for (int i = 0; i < connectionCount; i++) {
            int f = connections[i].fromNode;
            int t = connections[i].toNode;
            // If a node connects from cycle/end (and is not next target), it's a body node
            if ((f == cycleNodeIndex || f == endNodeIndex) && t != cycleNodeIndex && t != endNodeIndex && t != nextTarget) {
                isBodyNode[t] = true;
            }
            // If a node connects to cycle/end (and is not the parent), it might be a body node
            // We'll mark it as body node if it's also reachable from cycle/end
            if ((t == cycleNodeIndex || t == endNodeIndex) && f != cycleNodeIndex && f != endNodeIndex) {
                // Check if 'f' is reachable from cycle/end (making it a body node)
                for (int j = 0; j < connectionCount; j++) {
                    if ((connections[j].fromNode == cycleNodeIndex || connections[j].fromNode == endNodeIndex) && 
                        connections[j].toNode == f) {
                        isBodyNode[f] = true;
                        break;
                    }
                }
            }
        }
        // Propagate: any node connected to a body node is also a body node
        while (changed) {
            changed = false;
            for (int i = 0; i < connectionCount; i++) {
                int f = connections[i].fromNode;
                int t = connections[i].toNode;
                // Skip connections involving cycle/end/nextTarget
                if (f == cycleNodeIndex || f == endNodeIndex || f == nextTarget ||
                    t == cycleNodeIndex || t == endNodeIndex || t == nextTarget) {
                    continue;
                }
                if (isBodyNode[f] && !isBodyNode[t]) {
                    isBodyNode[t] = true;
                    changed = true;
                }
                if (isBodyNode[t] && !isBodyNode[f]) {
                    isBodyNode[f] = true;
                    changed = true;
                }
            }
        }
        
        // FIX: Adjust first body block position to compensate for connector position difference
        // Cycle block bottom connector: y - height/2 = y - 0.13
        // End block bottom connector: y - width/2 = y - 0.06
        // Difference: 0.07 units
        // When switching FROM WHILE to DO: connection FROM cycle (bottom at -0.13) becomes FROM end (bottom at -0.06)
        //   -> first body block needs to move DOWN by 0.07 to compensate
        // When switching FROM DO to WHILE: connection FROM end (bottom at -0.06) becomes FROM cycle (bottom at -0.13)
        //   -> first body block needs to move UP by 0.07 to compensate
        const double CONNECTOR_POSITION_DIFF = 0.07; // 0.13 - 0.06 = 0.07
        int firstBodyNode = -1;
        for (int i = 0; i < connectionCount; i++) {
            int f = connections[i].fromNode;
            int t = connections[i].toNode;
            // Find the first body entry connection (the one that goes into the first body block)
            // Use prevType to find the connection in the current state
            if (isBodyNode[t] && ((prevType == CYCLE_DO && f == endNodeIndex) || (prevType != CYCLE_DO && f == cycleNodeIndex))) {
                firstBodyNode = t;
                break;
            }
        }
        if (firstBodyNode >= 0) {
            double adjustment = 0.0;
            if (prevType == CYCLE_DO && chosenType != CYCLE_DO) {
                // DO -> WHILE/FOR: end bottom connector (-0.06) to cycle bottom connector (-0.13)
                // Cycle connector is 0.07 units lower, so move first body block UP
                adjustment = -CONNECTOR_POSITION_DIFF;
            } else if (prevType != CYCLE_DO && chosenType == CYCLE_DO) {
                // WHILE/FOR -> DO: cycle bottom connector (-0.13) to end bottom connector (-0.06)
                // End connector is 0.07 units higher, so move first body block DOWN
                adjustment = CONNECTOR_POSITION_DIFF;
            }
            if (fabs(adjustment) > 0.001) {
                nodes[firstBodyNode].y += adjustment;
                // DO NOT snap to grid - preserve the micro-adjustment to compensate for connector position difference
                // nodes[firstBodyNode].y = snap_to_grid_y(nodes[firstBodyNode].y);  // This was eliminating the adjustment!
            }
        }
        
        for (int i = 0; i < connectionCount; i++) {
            int f = connections[i].fromNode;
            int t = connections[i].toNode;
            // Find parent connections - exclude body nodes
            if (t == cycleNodeIndex && f != cycleNodeIndex && f != endNodeIndex && !isBodyNode[f]) {
                parentToCycle = i;
            }
            if (t == endNodeIndex && f != cycleNodeIndex && f != endNodeIndex && !isBodyNode[f]) {
                parentToEnd = i;
            }
            if ((f == cycleNodeIndex && t == endNodeIndex) || (f == endNodeIndex && t == cycleNodeIndex)) {
                middleConn = i;
            }
            // NOTE: nextConn is already found above using prevType to determine correct exit point
            // Don't overwrite it here - this would incorrectly use the last matching connection
        }
        // Prefer parent to cycle (for WHILE/FOR), but use parent to end if that's all we have
        // However, we must ensure the selected parent is NOT a body node
        if (parentToCycle >= 0 && !isBodyNode[connections[parentToCycle].fromNode]) {
            parentConn = parentToCycle;
        } else if (parentToEnd >= 0 && !isBodyNode[connections[parentToEnd].fromNode]) {
            parentConn = parentToEnd;
        } else {
            // Fallback: use whichever exists (shouldn't happen if body detection works)
            parentConn = (parentToCycle >= 0) ? parentToCycle : parentToEnd;
        }
        int parentNode = (parentConn >= 0) ? connections[parentConn].fromNode : -1;
        
        // Rewire body connections to keep loop body intact
        int bodyRewireCount = 0;
        for (int i = 0; i < connectionCount; i++) {
            if (i == parentConn || i == middleConn || i == nextConn) {
                continue;
            }
            int f = connections[i].fromNode;
            int t = connections[i].toNode;
            
            if (chosenType == CYCLE_DO) {
                // DO: end -> [body] -> cycle
                // Rewire FROM cycle to FROM end (body entry)
                if (f == cycleNodeIndex && t != endNodeIndex && t != nextTarget) {
                    connections[i].fromNode = endNodeIndex;
                    bodyRewireCount++;
                }
                // Rewire TO end to TO cycle (body exit)
                if (t == endNodeIndex && f != cycleNodeIndex && f != parentNode && f != endNodeIndex) {
                    connections[i].toNode = cycleNodeIndex;
                    bodyRewireCount++;
                }
            } else {
                // WHILE/FOR: cycle -> [body] -> end
                // When switching FROM DO to WHILE, connections are in DO state:
                // - Body entry: FROM end (needs to become FROM cycle)
                // - Body exit: TO cycle (needs to become TO end)
                // - Body connections FROM cycle should stay FROM cycle (already correct)
                // - Body connections TO end should stay TO end (already correct)
                
                // Rewire FROM end to FROM cycle (body entry)
                // This handles: end→body connections that need to become cycle→body
                if (f == endNodeIndex && t != cycleNodeIndex && t != nextTarget) {
                    connections[i].fromNode = cycleNodeIndex;
                    bodyRewireCount++;
                }
                // Rewire TO cycle to TO end (body exit)
                // This handles: body→cycle connections that need to become body→end
                if (t == cycleNodeIndex && f != endNodeIndex && f != parentNode && f != cycleNodeIndex) {
                    connections[i].toNode = endNodeIndex;
                    bodyRewireCount++;
                }
            }
        }
        
        if (chosenType == CYCLE_DO) {
            // Positions already swapped above, now rewire connections
            // Rewire: parent -> end, end -> cycle, cycle -> next
            if (parentConn >= 0) {
                connections[parentConn].toNode = endNodeIndex;
            }
            // Ensure middle connection exists for DO (cycle -> end for loopback)
            if (middleConn < 0 && connectionCount < MAX_CONNECTIONS) {
                middleConn = connectionCount++;
                connections[middleConn].fromNode = cycleNodeIndex;
                connections[middleConn].toNode = endNodeIndex;
            } else if (middleConn >= 0) {
                connections[middleConn].fromNode = cycleNodeIndex;
                connections[middleConn].toNode = endNodeIndex;
            }
            if (nextConn >= 0 && nextTarget >= 0) {
                connections[nextConn].fromNode = cycleNodeIndex;
                connections[nextConn].toNode = nextTarget;
            }
            
            // CRITICAL FIX: Ensure body entry connection exists (end -> first body node)
            // After rewiring, we need to make sure there's a connection FROM end to the first body node
            // This is the connection where blocks are placed
            bool hasBodyEntryConnection = false;
            int bodyEntryConn = -1;
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].fromNode == endNodeIndex && 
                    connections[i].toNode != cycleNodeIndex && 
                    connections[i].toNode != nextTarget &&
                    isBodyNode[connections[i].toNode]) {
                    hasBodyEntryConnection = true;
                    bodyEntryConn = i;
                    break;
                }
            }
            // If no body entry connection exists, create one to the first body node
            // Also check if firstBodyNode was found - if not, find it from current connections
            if (firstBodyNode < 0) {
                // Find first body node from current connections (after rewiring)
                for (int i = 0; i < connectionCount; i++) {
                    if (connections[i].fromNode == endNodeIndex && 
                        connections[i].toNode != cycleNodeIndex && 
                        connections[i].toNode != nextTarget &&
                        isBodyNode[connections[i].toNode]) {
                        firstBodyNode = connections[i].toNode;
                        break;
                    }
                }
            }
            if (!hasBodyEntryConnection && firstBodyNode >= 0 && connectionCount < MAX_CONNECTIONS) {
                connections[connectionCount].fromNode = endNodeIndex;
                connections[connectionCount].toNode = firstBodyNode;
                connectionCount++;
            }
            // SPECIAL CASE: Empty loop (no body nodes)
            // For empty DO loops, we need a connection FROM end where blocks can be placed
            // This connection goes to the cycle node as a placeholder
            // When blocks are added, they'll be inserted in this connection
            if (!hasBodyEntryConnection && firstBodyNode < 0 && connectionCount < MAX_CONNECTIONS) {
                // Check if there's already a connection end -> cycle (shouldn't exist, but check anyway)
                bool hasEndToCycle = false;
                for (int i = 0; i < connectionCount; i++) {
                    if (connections[i].fromNode == endNodeIndex && connections[i].toNode == cycleNodeIndex) {
                        hasEndToCycle = true;
                        break;
                    }
                }
                // Create end -> cycle connection as body entry point for empty loops
                // Note: This is different from the loopback (cycle -> end)
                if (!hasEndToCycle) {
                    connections[connectionCount].fromNode = endNodeIndex;
                    connections[connectionCount].toNode = cycleNodeIndex;
                    connectionCount++;
                }
            }
        } else {
            // Positions already swapped above, now rewire connections
            // Rewire: parent -> cycle, cycle -> end, end -> next
            if (parentConn >= 0) {
                connections[parentConn].toNode = cycleNodeIndex;
            }
            // CRITICAL FIX: Also rewire parentToEnd if it exists and is different from parentConn
            // This handles the case when switching from DO to WHILE/FOR where an old connection
            // was going to the END node but should now go to the CYCLE node
            // BUT: Only rewire if the source node is NOT a body node (to avoid rewiring body exit connections)
            if (parentToEnd >= 0 && parentToEnd != parentConn) {
                int fromNode = connections[parentToEnd].fromNode;
                bool isFromBodyNode = isBodyNode[fromNode];
                if (!isFromBodyNode) {
                    connections[parentToEnd].toNode = cycleNodeIndex;
                }
            }
            // Ensure middle connection exists for WHILE/FOR (end -> cycle for loopback)
            // CRITICAL FIX: For WHILE/FOR, loopback must be end->cycle (not cycle->end) so is_cycle_loopback() recognizes it
            if (middleConn < 0 && connectionCount < MAX_CONNECTIONS) {
                middleConn = connectionCount++;
                connections[middleConn].fromNode = endNodeIndex;  // FIXED: was cycleNodeIndex
                connections[middleConn].toNode = cycleNodeIndex;  // FIXED: was endNodeIndex
            } else if (middleConn >= 0) {
                connections[middleConn].fromNode = endNodeIndex;  // FIXED: was cycleNodeIndex
                connections[middleConn].toNode = cycleNodeIndex;  // FIXED: was endNodeIndex
            }
            if (nextConn >= 0 && nextTarget >= 0) {
                connections[nextConn].fromNode = endNodeIndex;
                connections[nextConn].toNode = nextTarget;
            }
        }
        } // End of else block for type change
        
        // Update value/condition for all types (whether rewired or not)
        if (chosenType == CYCLE_FOR) {
            // FOR: init, condition, increment
            const char* initDefault = cycle->initVar[0] ? cycle->initVar : "i = 0";
            const char* initResult = tinyfd_inputBox(
                "For Init",
                "Initialize loop variable (e.g., int i = 0, i = 0, or just 'i' to auto-initialize):",
                initDefault
            );
            if (!initResult || initResult[0] == '\0') return;
            
            // Copy immediately to avoid buffer reuse
            char initVarCopy[MAX_VALUE_LENGTH];
            strncpy(initVarCopy, initResult, sizeof(initVarCopy) - 1);
            initVarCopy[sizeof(initVarCopy) - 1] = '\0';
            
            // Auto-initialization: if input is just a variable name (no '=' and no type keywords), add "int " prefix and " = 0" suffix
            const char* typeKeywords[] = {"int", "float", "double", "char", "bool", "long", "short", "unsigned"};
            bool hasTypeKeyword = false;
            bool hasEquals = (strchr(initVarCopy, '=') != NULL);
            
            // Check if starts with a type keyword
            char trimmed[MAX_VALUE_LENGTH];
            const char* p = initVarCopy;
            while (*p == ' ' || *p == '\t') p++;
            int trimmedLen = 0;
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '=' && trimmedLen < MAX_VALUE_LENGTH - 1) {
                trimmed[trimmedLen++] = *p++;
            }
            trimmed[trimmedLen] = '\0';
            
            for (int i = 0; i < 8; i++) {
                if (strncmp(trimmed, typeKeywords[i], strlen(typeKeywords[i])) == 0 && 
                    (trimmed[strlen(typeKeywords[i])] == '\0' || trimmed[strlen(typeKeywords[i])] == ' ' || trimmed[strlen(typeKeywords[i])] == '\t')) {
                    hasTypeKeyword = true;
                    break;
                }
            }
            
            // If no '=' and no type keyword, it's just a variable name - auto-initialize
            if (!hasEquals && !hasTypeKeyword && trimmedLen > 0 && is_valid_variable_name(trimmed)) {
                snprintf(initVarCopy, sizeof(initVarCopy), "int %s = 0", trimmed);
            }
            
            const char* condDefault = cycle->condition[0] ? cycle->condition : "i < 10";
            const char* condResult = tinyfd_inputBox(
                "For Condition",
                "Enter loop condition (e.g., i < 10):",
                condDefault
            );
            if (!condResult || condResult[0] == '\0') return;
            
            // Copy immediately to avoid buffer reuse
            char condCopy[MAX_VALUE_LENGTH];
            strncpy(condCopy, condResult, sizeof(condCopy) - 1);
            condCopy[sizeof(condCopy) - 1] = '\0';
            
            const char* incrDefault = cycle->increment[0] ? cycle->increment : "i++";
            const char* incrResult = tinyfd_inputBox(
                "For Increment",
                "Enter increment/decrement (e.g., i++ or i += 1):",
                incrDefault
            );
            if (!incrResult || incrResult[0] == '\0') return;
            
            // Copy immediately to avoid buffer reuse
            char incrCopy[MAX_VALUE_LENGTH];
            strncpy(incrCopy, incrResult, sizeof(incrCopy) - 1);
            incrCopy[sizeof(incrCopy) - 1] = '\0';
            
            // Persist using copies
            strncpy(cycle->initVar, initVarCopy, sizeof(cycle->initVar) - 1);
            cycle->initVar[sizeof(cycle->initVar) - 1] = '\0';
            
            strncpy(cycle->condition, condCopy, sizeof(cycle->condition) - 1);
            cycle->condition[sizeof(cycle->condition) - 1] = '\0';
            
            strncpy(cycle->increment, incrCopy, sizeof(cycle->increment) - 1);
            cycle->increment[sizeof(cycle->increment) - 1] = '\0';
            
            // Extract variable name from init expression (improved parsing)
            // Handles: "int i = 0", "i = 0", "int i", etc.
            char varName[MAX_VAR_NAME_LENGTH];
            varName[0] = '\0';
            p = cycle->initVar;
            
            // Skip whitespace
            while (*p == ' ' || *p == '\t') p++;
            
            // Skip type keyword if present
            for (int i = 0; i < 8; i++) {
                int typeLen = strlen(typeKeywords[i]);
                if (strncmp(p, typeKeywords[i], typeLen) == 0 && 
                    (p[typeLen] == ' ' || p[typeLen] == '\t' || p[typeLen] == '\0')) {
                    p += typeLen;
                    while (*p == ' ' || *p == '\t') p++;
                    break;
                }
            }
            
            // Extract variable name (identifier)
            int pos = 0;
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
                // Valid start character for identifier
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || 
                       (*p >= '0' && *p <= '9') || *p == '_') {
                    if (pos < MAX_VAR_NAME_LENGTH - 1) {
                        varName[pos++] = *p;
                    }
                    p++;
                }
            }
            varName[pos] = '\0';
            
            // Add to variable table if valid and doesn't exist
            if (pos > 0 && is_valid_variable_name(varName) && !variable_name_exists(varName, -1) && variableCount < MAX_VARIABLES) {
                Variable *v = &variables[variableCount++];
                strncpy(v->name, varName, MAX_VAR_NAME_LENGTH - 1);
                v->name[MAX_VAR_NAME_LENGTH - 1] = '\0';
                v->type = VAR_TYPE_INT;
                v->is_array = false;
                v->array_size = 0;
            }
            
            snprintf(node->value, MAX_VALUE_LENGTH, "FOR|%s|%s|%s", cycle->initVar, cycle->condition, cycle->increment);
        } else {
            const char* condPrompt = (chosenType == CYCLE_DO) ? "Enter post-condition (evaluated after body):" : "Enter condition (evaluated before body):";
            const char* condResult = tinyfd_inputBox(
                "Loop Condition",
                condPrompt,
                cycle->condition[0] ? cycle->condition : "true"
            );
            if (!condResult || condResult[0] == '\0') return;
            
            strncpy(cycle->condition, condResult, sizeof(cycle->condition) - 1);
            cycle->condition[sizeof(cycle->condition) - 1] = '\0';
            
            snprintf(node->value, MAX_VALUE_LENGTH, "%s|%s",
                     (chosenType == CYCLE_DO) ? "DO" : "WHILE",
                     cycle->condition);
        }
        
        // Slight offset if inside an IF to avoid overlap
        if (node->owningIfBlock >= 0 && cycle->loopbackOffset < 0.45f) {
            cycle->loopbackOffset += 0.15f;
        }
        
        // Adjust width to fit label
        float fontSize = node->height * 0.3f;
        node->width = calculate_block_width(node->value, fontSize, 0.35f);
        
    } else {
        // Other block types - use simple input dialog
        const char* result = tinyfd_inputBox(
            "Edit Block Value",
            "Enter the value for this block:",
            node->value
        );
        
        if (result != NULL) {
            strncpy(node->value, result, MAX_VALUE_LENGTH - 1);
            node->value[MAX_VALUE_LENGTH - 1] = '\0';
            
            // Recalculate width based on text content for content blocks
            if (node->type == NODE_PROCESS || node->type == NODE_NORMAL) {
                float fontSize = node->height * 0.3f;
                node->width = calculate_block_width(node->value, fontSize, 0.35f);
            }
        }
    }
}

void insert_node_in_connection(int connIndex, NodeType nodeType) {
    if (nodeCount >= MAX_NODES || connectionCount >= MAX_CONNECTIONS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
    // Validate that this isn't a cross-IF connection
    if (!is_valid_if_converge_connection(oldConn.fromNode, oldConn.toNode)) {
        return;  // Reject this operation
    }
    
    // Store the original Y position of the "to" node before we modify anything
    double originalToY = to->y;
    
    // Calculate grid positions
    int fromGridY = world_to_grid_y(from->y);
    
    // Determine the branch column and target X for the new node
    int targetBranchColumn = from->branchColumn;
    double targetX = from->x;
    int newNodeOwningIfBlock = from->owningIfBlock;  // Default: inherit from parent
    int branchType = -1;
    bool insertingAboveNestedIF = false;
    
    // Check if we're inserting ABOVE a nested IF (not INTO it)
    // This can happen in multiple scenarios:
    // 1. 'to' is the nested IF node itself and is below 'from'
    // 2. 'to' is owned by an IF that is itself below 'from'
    // 3. 'to' is a convergence point of an IF that is below 'from'
    // 4. 'to' is any node that is part of an IF structure that is below 'from'
    if (to->type == NODE_IF && to->y < from->y) {
        // Case 1: Inserting directly above a nested IF node
        insertingAboveNestedIF = true;
        // Keep targetBranchColumn and targetX as from's values
        // newNodeOwningIfBlock stays as from->owningIfBlock
    } else if (to->type == NODE_CONVERGE) {
        // Case 3: 'to' is a convergence point - check if its IF is below 'from'
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].convergeNodeIndex == oldConn.toNode) {
                int ifNodeIdx = ifBlocks[i].ifNodeIndex;
                if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount && nodes[ifNodeIdx].y < from->y) {
                    insertingAboveNestedIF = true;
                    break;
                }
            }
        }
    } else if (to->owningIfBlock >= 0 && to->owningIfBlock < ifBlockCount) {
        int toOwningIfIdx = to->owningIfBlock;
        int toOwningIfNodeIdx = ifBlocks[toOwningIfIdx].ifNodeIndex;
        
        // Case 2: If the IF that owns 'to' is itself below 'from', we're inserting above that IF
        if (toOwningIfNodeIdx >= 0 && toOwningIfNodeIdx < nodeCount && 
            nodes[toOwningIfNodeIdx].y < from->y) {
            insertingAboveNestedIF = true;
            // Keep targetBranchColumn and targetX as from's values
            // newNodeOwningIfBlock stays as from->owningIfBlock
        }
    }
    
    // Case 4: Check if 'to' is below 'from' and there's any IF structure between them
    // This handles cases where we're inserting above a nested IF from any distance
    if (!insertingAboveNestedIF && to->y < from->y) {
        // Check all IF blocks to see if any are between 'from' and 'to'
        for (int i = 0; i < ifBlockCount; i++) {
            int ifNodeIdx = ifBlocks[i].ifNodeIndex;
            if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount) {
                // If the IF node is below 'from' and at or above 'to', we're inserting above it
                if (nodes[ifNodeIdx].y < from->y && nodes[ifNodeIdx].y >= to->y) {
                    // Check if this IF is in the same branch context as 'from'
                    // (i.e., they share the same parent IF or both are in main branch)
                    bool sameContext = false;
                    if (from->owningIfBlock < 0 && nodes[ifNodeIdx].owningIfBlock < 0) {
                        // Both in main branch
                        sameContext = true;
                    } else if (from->owningIfBlock >= 0 && nodes[ifNodeIdx].owningIfBlock >= 0) {
                        // Check if they're in the same parent IF
                        int fromParent = (from->owningIfBlock < ifBlockCount) ? 
                                       ifBlocks[from->owningIfBlock].parentIfIndex : -1;
                        int ifParent = (nodes[ifNodeIdx].owningIfBlock < ifBlockCount) ? 
                                      ifBlocks[nodes[ifNodeIdx].owningIfBlock].parentIfIndex : -1;
                        if (fromParent == ifParent) {
                            sameContext = true;
                        }
                    }
                    
                    if (sameContext) {
                        insertingAboveNestedIF = true;
                        break;
                    }
                }
            }
        }
    }
    
    // Special case: if we're inserting INTO a connection from an IF node (even if the clicked
    // connection source is not the IF), check if TO is a convergence of an IF we should own
    if (!insertingAboveNestedIF && to->type == NODE_CONVERGE) {
        // Find which IF this convergence belongs to
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].convergeNodeIndex == oldConn.toNode) {
                // Found the IF - check if FROM is this IF node
                if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                    // We're inserting into a connection directly from this IF
                    newNodeOwningIfBlock = i;
                    branchType = get_if_branch_type(connIndex);
                    
                    int ifBlockIdx = i;
                    double leftWidth = ifBlocks[ifBlockIdx].leftBranchWidth;
                    double rightWidth = ifBlocks[ifBlockIdx].rightBranchWidth;
                    
                    if (branchType == 0) {
                        targetBranchColumn = from->branchColumn - 2;
                        targetX = from->x - leftWidth;
                    } else if (branchType == 1) {
                        // False branch should always have positive branchColumn (or at least != 0)
                        // If from is at -2, false branch would be 0, which conflicts with main branch
                        // So we need to use absolute values to ensure false branch is always distinguishable
                        int falseBranchColumn = from->branchColumn + 2;
                        if (falseBranchColumn <= 0) {
                            // Convert to positive to ensure it's recognizable as a right/false branch
                            falseBranchColumn = abs(from->branchColumn) + 2;
                        }
                        targetBranchColumn = falseBranchColumn;
                        targetX = from->x + rightWidth;
                    }
                    break;
                }
            }
        }
    }
    
    if (!insertingAboveNestedIF && from->type == NODE_IF && branchType < 0) {
        branchType = get_if_branch_type(connIndex);

        int ifBlockIdx = -1;
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                ifBlockIdx = i;
                break;
            }
        }

        double leftWidth = (ifBlockIdx >= 0) ? ifBlocks[ifBlockIdx].leftBranchWidth : 1.0;
        double rightWidth = (ifBlockIdx >= 0) ? ifBlocks[ifBlockIdx].rightBranchWidth : 1.0;

        if (branchType == 0) {
            targetBranchColumn = from->branchColumn - 2;
            targetX = from->x - leftWidth;
        } else if (branchType == 1) {
            // False branch should always have positive branchColumn (or at least != 0)
            int falseBranchColumn = from->branchColumn + 2;
            if (falseBranchColumn <= 0) {
                // Convert to positive to ensure it's recognizable as a right/false branch
                falseBranchColumn = abs(from->branchColumn) + 2;
            }
            targetBranchColumn = falseBranchColumn;
            targetX = from->x + rightWidth;
        }

        if (ifBlockIdx >= 0) {
            newNodeOwningIfBlock = ifBlockIdx;
        }
    }
    
    // Create new node positioned one grid cell below the "from" node
    int newGridY = fromGridY - 1;
    FlowNode *newNode = &nodes[nodeCount];
    newNode->x = snap_to_grid_x(targetX);  // Position in correct branch column
    newNode->y = snap_to_grid_y(grid_to_world_y(newGridY));  // One grid cell below
    newNode->height = 0.22f;
    newNode->value[0] = '\0';  // Initialize value as empty string
    newNode->type = nodeType;
    newNode->branchColumn = targetBranchColumn;  // Set correct branch column
    newNode->owningIfBlock = newNodeOwningIfBlock;  // Set IF block ownership
    
    // Calculate initial width (will be recalculated when value is set)
    float fontSize = newNode->height * 0.3f;
    newNode->width = calculate_block_width(newNode->value, fontSize, 0.35f);
    
    int newNodeIndex = nodeCount;
    nodeCount++;
    
    
    // Determine which IF block to reposition later (after push-down)
    int relevantIfBlock = -1;
    bool nodeAddedToBranch = false;  // Track if we added a node to a branch array
    if (from->type == NODE_IF) {
        // Inserting directly from IF block - find this IF block's index
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                relevantIfBlock = i;
                
                // Add the new node to the appropriate branch array
                int resolvedBranchType = (branchType >= 0) ? branchType : get_if_branch_type(connIndex);
                if (resolvedBranchType == 0) {
                    // True branch (left)
                    if (ifBlocks[i].trueBranchCount < MAX_NODES) {
                        ifBlocks[i].trueBranchNodes[ifBlocks[i].trueBranchCount] = newNodeIndex;
                        ifBlocks[i].trueBranchCount++;
                        nodeAddedToBranch = true;
                        // Update node's owningIfBlock to match the branch it was added to
                        nodes[newNodeIndex].owningIfBlock = i;
                        newNodeOwningIfBlock = i;
                    }
                } else if (resolvedBranchType == 1) {
                    // False branch (right)
                    if (ifBlocks[i].falseBranchCount < MAX_NODES) {
                        ifBlocks[i].falseBranchNodes[ifBlocks[i].falseBranchCount] = newNodeIndex;
                        ifBlocks[i].falseBranchCount++;
                        nodeAddedToBranch = true;
                        // Update node's owningIfBlock to match the branch it was added to
                        nodes[newNodeIndex].owningIfBlock = i;
                        newNodeOwningIfBlock = i;
                    }
                }
                break;
            }
        }
    } else if (from->owningIfBlock >= 0) {
        // Inserting from a node that's already in a branch
        relevantIfBlock = from->owningIfBlock;
        
        // Add the new node to the same branch as the parent
        if (relevantIfBlock < ifBlockCount) {
            // Determine which branch based on the from node's branchColumn
            // (searching branch arrays won't work for convergence nodes)
            bool addToTrueBranch = (from->branchColumn < 0);
            
            // HYPOTHESIS H3: If from is an IF node, we should use get_if_branch_type instead
            int actualBranchType = -1;
            if (from->type == NODE_IF) {
                actualBranchType = get_if_branch_type(connIndex);
                
                // Override with actual branch type from connection
                if (actualBranchType >= 0) {
                    addToTrueBranch = (actualBranchType == 0);
                }
            } else if (from->branchColumn == 0 && from->owningIfBlock >= 0) {
                // CRITICAL FIX: When branchColumn is 0 but node is in a nested IF branch,
                // we need to check which branch array it's actually in
                // This happens when a nested IF's true branch has branchColumn = 0
                bool foundInTrueBranch = false;
                bool foundInFalseBranch = false;
                
                // Check true branch array
                for (int i = 0; i < ifBlocks[relevantIfBlock].trueBranchCount; i++) {
                    if (ifBlocks[relevantIfBlock].trueBranchNodes[i] == oldConn.fromNode) {
                        foundInTrueBranch = true;
                        break;
                    }
                }
                
                // Check false branch array
                for (int i = 0; i < ifBlocks[relevantIfBlock].falseBranchCount; i++) {
                    if (ifBlocks[relevantIfBlock].falseBranchNodes[i] == oldConn.fromNode) {
                        foundInFalseBranch = true;
                        break;
                    }
                }
                
                if (foundInTrueBranch) {
                    addToTrueBranch = true;
                } else if (foundInFalseBranch) {
                    addToTrueBranch = false;
                }
                // If neither found, keep the branchColumn-based decision (shouldn't happen for regular nodes)
            }
            
            if (addToTrueBranch) {
                // Add to true branch
                if (ifBlocks[relevantIfBlock].trueBranchCount < MAX_NODES) {
                    ifBlocks[relevantIfBlock].trueBranchNodes[ifBlocks[relevantIfBlock].trueBranchCount] = newNodeIndex;
                    ifBlocks[relevantIfBlock].trueBranchCount++;
                    nodeAddedToBranch = true;
                    // Update node's owningIfBlock to match the branch it was added to
                    nodes[newNodeIndex].owningIfBlock = relevantIfBlock;
                    newNodeOwningIfBlock = relevantIfBlock;
                }
            } else {
                // Add to false branch
                if (ifBlocks[relevantIfBlock].falseBranchCount < MAX_NODES) {
                    ifBlocks[relevantIfBlock].falseBranchNodes[ifBlocks[relevantIfBlock].falseBranchCount] = newNodeIndex;
                    ifBlocks[relevantIfBlock].falseBranchCount++;
                    nodeAddedToBranch = true;
                    // Update node's owningIfBlock to match the branch it was added to
                    nodes[newNodeIndex].owningIfBlock = relevantIfBlock;
                    newNodeOwningIfBlock = relevantIfBlock;
                }
            }
        }
    }
    
    // Push the "to" node and all nodes below it further down by one grid cell
    // IMPORTANT: When pushing an IF block, we need to push its branches and reposition convergence
    double gridSpacing = GRID_CELL_SIZE;
    int pushedIfBlocks[MAX_NODES];  // Track IF blocks that were pushed
    int pushedIfBlockCount = 0;
    
    // When inserting above a nested IF, identify which nested IF we're inserting above
    // Also handle the case where we're inserting above a regular IF (not nested)
    int targetNestedIfBlock = -1;  // The nested IF we're inserting above (if any)
    int targetNestedIfConvergeIdx = -1;  // Its convergence point
    int targetRegularIfBlock = -1;  // The regular IF we're inserting above (if any, not nested)
    int targetRegularIfConvergeIdx = -1;  // Its convergence point
    
    if (insertingAboveNestedIF) {
        if (to->type == NODE_IF) {
            // Case 1: 'to' is the nested IF node itself
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == oldConn.toNode) {
                    targetNestedIfBlock = j;
                    targetNestedIfConvergeIdx = ifBlocks[j].convergeNodeIndex;
                    break;
                }
            }
        } else if (to->owningIfBlock >= 0 && to->owningIfBlock < ifBlockCount) {
            // Case 2: 'to' is owned by a nested IF
            targetNestedIfBlock = to->owningIfBlock;
            if (targetNestedIfBlock >= 0 && targetNestedIfBlock < ifBlockCount) {
                targetNestedIfConvergeIdx = ifBlocks[targetNestedIfBlock].convergeNodeIndex;
            }
        }
    } else if (to->type == NODE_IF && to->y < from->y) {
        // Inserting above a regular IF (not nested) - detect it
        for (int j = 0; j < ifBlockCount; j++) {
            if (ifBlocks[j].ifNodeIndex == oldConn.toNode) {
                // Check if it's actually a regular IF (not nested)
                if (ifBlocks[j].parentIfIndex < 0) {
                    targetRegularIfBlock = j;
                    targetRegularIfConvergeIdx = ifBlocks[j].convergeNodeIndex;
                    break;
                }
            }
        }
    } else if (to->type == NODE_CONVERGE && to->y < from->y) {
        // 'to' is a convergence point - check if it's a regular IF
        for (int j = 0; j < ifBlockCount; j++) {
            if (ifBlocks[j].convergeNodeIndex == oldConn.toNode) {
                int ifNodeIdx = ifBlocks[j].ifNodeIndex;
                if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount && nodes[ifNodeIdx].y < from->y) {
                    // Check if it's a regular IF (not nested)
                    if (ifBlocks[j].parentIfIndex < 0) {
                        targetRegularIfBlock = j;
                        targetRegularIfConvergeIdx = oldConn.toNode;
                        break;
                    }
                }
            }
        }
    } else if (to->y < from->y) {
        // 'to' is a regular block - check if there's a regular IF between 'from' and 'to'
        // OR if there's a regular IF below 'from' that we're inserting above
        for (int j = 0; j < ifBlockCount; j++) {
            int ifNodeIdx = ifBlocks[j].ifNodeIndex;
            if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount) {
                // If the IF node is below 'from', we might be inserting above it
                // Check if it's a regular IF (not nested) and in the same context as 'from'
                if (nodes[ifNodeIdx].y < from->y && ifBlocks[j].parentIfIndex < 0) {
                    bool sameContext = false;
                    if (from->owningIfBlock < 0 && nodes[ifNodeIdx].owningIfBlock < 0) {
                        // Both in main branch
                        sameContext = true;
                    } else if (from->owningIfBlock >= 0 && nodes[ifNodeIdx].owningIfBlock >= 0) {
                        // Check if they're in the same parent IF
                        int fromParent = (from->owningIfBlock < ifBlockCount) ? 
                                       ifBlocks[from->owningIfBlock].parentIfIndex : -1;
                        int ifParent = (nodes[ifNodeIdx].owningIfBlock < ifBlockCount) ? 
                                      ifBlocks[nodes[ifNodeIdx].owningIfBlock].parentIfIndex : -1;
                        if (fromParent == ifParent) {
                            sameContext = true;
                        }
                    }
                    
                    if (sameContext) {
                        // Check if 'to' is above or at the IF, or if the IF's convergence is below 'from'
                        int convergeIdx = ifBlocks[j].convergeNodeIndex;
                        bool isAboveIF = (to->y >= nodes[ifNodeIdx].y);
                        bool isAboveConverge = (convergeIdx >= 0 && convergeIdx < nodeCount && 
                                                to->y >= nodes[convergeIdx].y);
                        
                        // If we're inserting above the IF or its convergence, we're inserting above it
                        if (isAboveIF || isAboveConverge) {
                            targetRegularIfBlock = j;
                            targetRegularIfConvergeIdx = convergeIdx;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // Store original convergence Y positions for all IF blocks that will be pushed
    // (we need this before the loop starts, since convergence points may be pushed during the loop)
    double originalConvergeYs[MAX_NODES]; // Indexed by ifBlockIdx
    for (int i = 0; i < MAX_NODES; i++) {
        originalConvergeYs[i] = -999999.0; // Invalid marker
    }
    
    for (int i = 0; i < nodeCount; ++i) {
        if (nodes[i].y <= originalToY && i != newNodeIndex) {
            // Determine if this node should be pushed
            bool shouldPush = false;
            
            // SPECIAL CASE: When inserting above a regular IF (not nested), push that IF and ALL its contents
            if (targetRegularIfBlock >= 0) {
                // Check if this node is part of the regular IF we're inserting above
                bool isPartOfTargetRegularIF = false;
                
                // Check if it's the IF node itself
                if (nodes[i].type == NODE_IF) {
                    for (int j = 0; j < ifBlockCount; j++) {
                        if (ifBlocks[j].ifNodeIndex == i && j == targetRegularIfBlock) {
                            isPartOfTargetRegularIF = true;
                            break;
                        }
                    }
                }
                
                // Check if it's owned by the target regular IF (including nested IFs within it)
                if (!isPartOfTargetRegularIF && nodes[i].owningIfBlock >= 0) {
                    // Check if it's directly owned by the target regular IF
                    if (nodes[i].owningIfBlock == targetRegularIfBlock) {
                        isPartOfTargetRegularIF = true;
                    } else {
                        // Check if it's owned by a nested IF within the target regular IF (recursive check)
                        int currentOwningIf = nodes[i].owningIfBlock;
                        while (currentOwningIf >= 0 && currentOwningIf < ifBlockCount) {
                            int parentIdx = ifBlocks[currentOwningIf].parentIfIndex;
                            if (parentIdx == targetRegularIfBlock) {
                                isPartOfTargetRegularIF = true;
                                break;
                            }
                            currentOwningIf = parentIdx;
                        }
                    }
                }
                
                // Check if it's the convergence point of the target regular IF
                if (i == targetRegularIfConvergeIdx) {
                    isPartOfTargetRegularIF = true;
                }
                
                // Check if it's below the convergence point (nodes that should move with the regular IF)
                if (targetRegularIfConvergeIdx >= 0 && targetRegularIfConvergeIdx < nodeCount) {
                    if (nodes[i].y < nodes[targetRegularIfConvergeIdx].y) {
                        // Node is below convergence - check if it should move with it
                        // Only move if it's in the main branch
                        bool isMainBranch = (nodes[i].branchColumn == 0);
                        if (isMainBranch) {
                            isPartOfTargetRegularIF = true;
                        }
                    }
                }
                
                if (isPartOfTargetRegularIF) {
                    shouldPush = true;
                }
            }
            
            // SPECIAL CASE: When inserting above a nested IF, push that nested IF and ALL its contents
            if (insertingAboveNestedIF && targetNestedIfBlock >= 0) {
                // Check if this node is part of the nested IF we're inserting above
                bool isPartOfTargetNestedIF = false;
                
                // Check if it's the nested IF node itself
                if (nodes[i].type == NODE_IF) {
                    for (int j = 0; j < ifBlockCount; j++) {
                        if (ifBlocks[j].ifNodeIndex == i && j == targetNestedIfBlock) {
                            isPartOfTargetNestedIF = true;
                            break;
                        }
                    }
                }
                
                // Check if it's owned by the target nested IF (including nested IFs within it)
                if (!isPartOfTargetNestedIF && nodes[i].owningIfBlock >= 0) {
                    // Check if it's directly owned by the target nested IF
                    if (nodes[i].owningIfBlock == targetNestedIfBlock) {
                        isPartOfTargetNestedIF = true;
                    } else {
                        // Check if it's owned by a nested IF within the target nested IF (recursive check)
                        int currentOwningIf = nodes[i].owningIfBlock;
                        while (currentOwningIf >= 0 && currentOwningIf < ifBlockCount) {
                            int parentIdx = ifBlocks[currentOwningIf].parentIfIndex;
                            if (parentIdx == targetNestedIfBlock) {
                                isPartOfTargetNestedIF = true;
                                break;
                            }
                            currentOwningIf = parentIdx;
                        }
                    }
                }
                
                // Check if it's the convergence point of the target nested IF
                if (i == targetNestedIfConvergeIdx) {
                    isPartOfTargetNestedIF = true;
                }
                
                // Check if it's below the convergence point (nodes that should move with the nested IF)
                if (targetNestedIfConvergeIdx >= 0 && targetNestedIfConvergeIdx < nodeCount) {
                    if (nodes[i].y < nodes[targetNestedIfConvergeIdx].y) {
                        // Node is below convergence - check if it should move with it
                        // Only move if it's in the main branch or in the same parent branch context
                        bool isMainBranch = (nodes[i].branchColumn == 0);
                        int targetNestedIfParent = (targetNestedIfBlock >= 0 && targetNestedIfBlock < ifBlockCount) ? 
                                                   ifBlocks[targetNestedIfBlock].parentIfIndex : -1;
                        bool isInSameParentBranch = (targetNestedIfParent >= 0 && 
                                                     nodes[i].owningIfBlock == targetNestedIfParent);
                        
                        // Don't move nodes from a different nested IF that's a sibling
                        bool isFromDifferentNestedIF = false;
                        if (nodes[i].owningIfBlock >= 0 && nodes[i].owningIfBlock < ifBlockCount) {
                            int nodeOwningIfParent = ifBlocks[nodes[i].owningIfBlock].parentIfIndex;
                            if (targetNestedIfParent >= 0 && nodeOwningIfParent == targetNestedIfParent && 
                                nodes[i].owningIfBlock != targetNestedIfBlock) {
                                isFromDifferentNestedIF = true;
                            }
                        }
                        
                        if ((isMainBranch || isInSameParentBranch) && !isFromDifferentNestedIF) {
                            isPartOfTargetNestedIF = true;
                        }
                    }
                }
                
                if (isPartOfTargetNestedIF) {
                    shouldPush = true;
                }
            }
            
            // Standard push logic (for non-inserting-above-nested-IF cases)
            if (!shouldPush) {
                // SPECIAL CASE: When inserting from an IF node to a node below it (right after the IF),
                // we need to push the "to" node and all nodes below it
                if (from->type == NODE_IF && i == oldConn.toNode) {
                    // This is the "to" node - always push it when inserting right after an IF
                    shouldPush = true;
                } else if (from->type == NODE_IF && nodes[i].y <= originalToY) {
                    // This is a node at or below the "to" node - when inserting directly after an IF,
                    // we need to push all nodes at or below the "to" node that are in the same context
                    bool shouldPushThisNode = false;
                    
                    // Special case: If this is a nested IF (nodeType == NODE_IF) in the same IF block context,
                    // push it even if it's in a different branch, because nested IFs should move with their parent
                    if (nodes[i].type == NODE_IF && nodes[i].owningIfBlock == to->owningIfBlock) {
                        shouldPushThisNode = true;
                    } else if (nodes[i].branchColumn == 0) {
                        // Main branch nodes should be pushed when inserting after an IF
                        // Check if both are in main branch context (both owningIfBlock == -1)
                        // OR if they're in the same IF block context
                        if (nodes[i].owningIfBlock == to->owningIfBlock) {
                            shouldPushThisNode = true;
                        } else if (nodes[i].owningIfBlock == -1 && to->owningIfBlock >= 0) {
                            // Node is in main branch, "to" is in an IF block
                            // Check if "to" is a convergence point - if so, push main branch nodes below it
                            if (to->type == NODE_CONVERGE) {
                                shouldPushThisNode = true;
                            }
                        }
                    } else if (nodes[i].branchColumn == to->branchColumn && nodes[i].owningIfBlock == to->owningIfBlock) {
                        // Same branch column and same IF block - push it
                        shouldPushThisNode = true;
                    } else if (to->type == NODE_CONVERGE && nodes[i].owningIfBlock == to->owningIfBlock) {
                        // If "to" is a convergence point, push nodes that are below it
                        // and in the same IF block context (regardless of branch column)
                        shouldPushThisNode = true;
                    }
                    
                    if (shouldPushThisNode) {
                        shouldPush = true;
                    }
                } else {
                    // Case 1: Both in main branch (0) AND same IF block ownership (or both -1)
                    // This prevents pushing nodes from nested IFs when adding to parent IF branches
                    // IMPORTANT: Use the actual node's owningIfBlock (which may have been updated when added to branch array)
                    // instead of just newNodeOwningIfBlock variable, to handle cases where the node was added to a branch
                    int actualNewNodeOwningIfBlock = nodes[newNodeIndex].owningIfBlock;
                    
                    if (targetBranchColumn == 0 && nodes[i].branchColumn == 0) {
                        // Only push if they're in the same IF block (or both not in any IF block)
                        if (actualNewNodeOwningIfBlock == nodes[i].owningIfBlock) {
                            shouldPush = true;
                        }
                    } else if (targetBranchColumn != 0 && targetBranchColumn == nodes[i].branchColumn && actualNewNodeOwningIfBlock == nodes[i].owningIfBlock) {
                        // Case 2: Same non-zero branch AND same IF block ownership
                        // This ensures we only push nodes in the SAME branch of the SAME IF block
                        shouldPush = true;
                    }
                }
            }
            
            // If this node should be pushed AND it's an IF block, track it for branch node pushing
            if (shouldPush && nodes[i].type == NODE_IF) {
                // Find the IF block index
                int pushedIfBlockIdx = -1;
                for (int j = 0; j < ifBlockCount; j++) {
                    if (ifBlocks[j].ifNodeIndex == i) {
                        pushedIfBlockIdx = j;
                        break;
                    }
                }
                
                // Track this IF block for branch pushing and convergence repositioning later
                if (pushedIfBlockIdx >= 0 && pushedIfBlockCount < MAX_NODES) {
                    pushedIfBlocks[pushedIfBlockCount++] = pushedIfBlockIdx;
                    
                    // Store original convergence Y position BEFORE any pushes
                    int convergeIdx = ifBlocks[pushedIfBlockIdx].convergeNodeIndex;
                    if (convergeIdx >= 0 && convergeIdx < nodeCount) {
                        originalConvergeYs[pushedIfBlockIdx] = nodes[convergeIdx].y;
                    }
                }
            }
            
            // When inserting above a regular IF, also track it and store its convergence Y
            if (targetRegularIfBlock >= 0 && targetRegularIfBlock < ifBlockCount) {
                bool alreadyTracked = false;
                for (int k = 0; k < pushedIfBlockCount; k++) {
                    if (pushedIfBlocks[k] == targetRegularIfBlock) {
                        alreadyTracked = true;
                        break;
                    }
                }
                if (!alreadyTracked && pushedIfBlockCount < MAX_NODES) {
                    pushedIfBlocks[pushedIfBlockCount++] = targetRegularIfBlock;
                    int convergeIdx = ifBlocks[targetRegularIfBlock].convergeNodeIndex;
                    if (convergeIdx >= 0 && convergeIdx < nodeCount) {
                        originalConvergeYs[targetRegularIfBlock] = nodes[convergeIdx].y;
                    }
                }
            }
            
            // When inserting above a nested IF, also track nested IFs within it for pushing
            if (shouldPush && insertingAboveNestedIF && targetNestedIfBlock >= 0) {
                // Check if this node is a nested IF within the target nested IF
                if (nodes[i].type == NODE_IF && nodes[i].owningIfBlock == targetNestedIfBlock) {
                    // Find the nested IF block index
                    int nestedIfBlockIdx = -1;
                    for (int j = 0; j < ifBlockCount; j++) {
                        if (ifBlocks[j].ifNodeIndex == i) {
                            nestedIfBlockIdx = j;
                            break;
                        }
                    }
                    
                    // Track this nested IF for branch pushing
                    if (nestedIfBlockIdx >= 0) {
                        // Check if already tracked
                        bool alreadyTracked = false;
                        for (int k = 0; k < pushedIfBlockCount; k++) {
                            if (pushedIfBlocks[k] == nestedIfBlockIdx) {
                                alreadyTracked = true;
                                break;
                            }
                        }
                        if (!alreadyTracked && pushedIfBlockCount < MAX_NODES) {
                            pushedIfBlocks[pushedIfBlockCount++] = nestedIfBlockIdx;
                        }
                    }
                }
            }
            
            // SPECIAL CASE: If a nested IF is being pushed, also push all nodes that belong to it
            // (branch nodes, convergence point, and nodes below convergence)
            // This applies when:
            // 1. Inserting from an IF node (from->type == NODE_IF)
            // 2. OR when inserting above a nested IF (insertingAboveNestedIF is true)
            // 3. OR when inserting above a regular IF (targetRegularIfBlock >= 0)
            // BUT: When inserting above a regular IF, only push nodes that belong to that specific IF,
            // not nodes that belong to other IFs (like chained IFs)
            if (!shouldPush && (from->type == NODE_IF || insertingAboveNestedIF || targetRegularIfBlock >= 0)) {
                // Check if this node belongs to any IF block that's being pushed
                // When inserting above a nested IF, also check the target nested IF and its nested IFs
                for (int pushedIdx = 0; pushedIdx < pushedIfBlockCount; pushedIdx++) {
                    int ifBlockIdx = pushedIfBlocks[pushedIdx];
                    if (ifBlockIdx >= 0 && ifBlockIdx < ifBlockCount) {
                        // When inserting above a regular IF, only push nodes that belong to that specific IF
                        // Skip nodes that belong to other IFs (chained IFs)
                        if (targetRegularIfBlock >= 0 && !insertingAboveNestedIF && from->type != NODE_IF) {
                            if (ifBlockIdx != targetRegularIfBlock) {
                                // This IF block is not the target regular IF - skip it
                                continue;
                            }
                        }
                        // CRITICAL FIX: Only push branch nodes of nested IFs that are in the same branch
                        // of the parent IF as where we're adding the node
                        // This prevents pushing branch nodes of nested IFs in different parent branches
                        bool shouldPushThisNestedIF = true;
                        if (targetBranchColumn != 0 && ifBlocks[ifBlockIdx].parentIfIndex >= 0) {
                            // This nested IF has a parent - check if it's in the same branch as targetBranchColumn
                            int nestedIfParentIdx = ifBlocks[ifBlockIdx].parentIfIndex;
                            int nestedIfNodeIdx = ifBlocks[ifBlockIdx].ifNodeIndex;
                            if (nestedIfNodeIdx >= 0 && nestedIfNodeIdx < nodeCount) {
                                int nestedIfBranchColumn = nodes[nestedIfNodeIdx].branchColumn;
                                // Only push if the nested IF is in the same branch as where we're adding
                                if (nestedIfBranchColumn != targetBranchColumn) {
                                    shouldPushThisNestedIF = false;
                                }
                            }
                        }
                        if (!shouldPushThisNestedIF) {
                            continue;
                        }
                        
                        // Get original convergence Y (stored before any pushes)
                        int convergeIdx = ifBlocks[ifBlockIdx].convergeNodeIndex;
                        double originalConvergeY = originalConvergeYs[ifBlockIdx];
                        if (originalConvergeY < -999998.0) {
                            // Fallback: use current position if not stored
                            originalConvergeY = (convergeIdx >= 0 && convergeIdx < nodeCount) ? nodes[convergeIdx].y : 0.0;
                        }
                        
                        // Check if this node is in the true branch
                        for (int j = 0; j < ifBlocks[ifBlockIdx].trueBranchCount; j++) {
                            if (ifBlocks[ifBlockIdx].trueBranchNodes[j] == i) {
                                shouldPush = true;
                                break;
                            }
                        }
                        if (shouldPush) break;
                        
                        // Check if this node is in the false branch
                        for (int j = 0; j < ifBlocks[ifBlockIdx].falseBranchCount; j++) {
                            if (ifBlocks[ifBlockIdx].falseBranchNodes[j] == i) {
                                shouldPush = true;
                                break;
                            }
                        }
                        if (shouldPush) break;
                        
                        // Check if this node is the convergence point
                        if (convergeIdx == i) {
                            shouldPush = true;
                            break;
                        }
                        
                        // Check if this node is below the convergence point and in the same context
                        // Use originalConvergeY to compare, since convergence might have been pushed already
                        if (convergeIdx >= 0 && convergeIdx < nodeCount && nodes[i].y < originalConvergeY) {
                            // Node is below convergence - check if it should move with it
                            bool isMainBranch = (nodes[i].branchColumn == 0);
                            int ifParentIdx = ifBlocks[ifBlockIdx].parentIfIndex;
                            bool isInSameParentBranch = (ifParentIdx >= 0 && nodes[i].owningIfBlock == ifParentIdx);
                            
                            // When inserting above a regular IF, don't push nodes that belong to a different IF
                            // (like chained IFs)
                            bool belongsToDifferentIF = false;
                            if (targetRegularIfBlock >= 0 && !insertingAboveNestedIF && from->type != NODE_IF) {
                                if (nodes[i].owningIfBlock >= 0 && nodes[i].owningIfBlock < ifBlockCount) {
                                    if (nodes[i].owningIfBlock != targetRegularIfBlock) {
                                        belongsToDifferentIF = true;
                                    }
                                }
                                // Also check if node is in a branch of a different IF
                                if (nodes[i].branchColumn != 0 && nodes[i].owningIfBlock >= 0 && 
                                    nodes[i].owningIfBlock < ifBlockCount && 
                                    nodes[i].owningIfBlock != targetRegularIfBlock) {
                                    belongsToDifferentIF = true;
                                }
                            }
                            
                            // Don't move nodes from a different nested IF that's a sibling
                            bool isFromDifferentNestedIF = false;
                            if (nodes[i].owningIfBlock >= 0 && nodes[i].owningIfBlock < ifBlockCount) {
                                int nodeOwningIfParent = ifBlocks[nodes[i].owningIfBlock].parentIfIndex;
                                if (ifParentIdx >= 0 && nodeOwningIfParent == ifParentIdx && 
                                    nodes[i].owningIfBlock != ifBlockIdx) {
                                    isFromDifferentNestedIF = true;
                                }
                            }
                            
                            // Also check if the node is in the same branch as the nested IF
                            // (nodes in the nested IF's branch should move with it)
                            int nestedIfNodeIdx = ifBlocks[ifBlockIdx].ifNodeIndex;
                            bool isInNestedIfBranch = false;
                            if (nestedIfNodeIdx >= 0 && nestedIfNodeIdx < nodeCount) {
                                isInNestedIfBranch = (nodes[i].branchColumn == nodes[nestedIfNodeIdx].branchColumn &&
                                                       nodes[i].owningIfBlock == nodes[nestedIfNodeIdx].owningIfBlock);
                            }
                            
                            if ((isMainBranch || isInSameParentBranch || isInNestedIfBranch) && !isFromDifferentNestedIF && !belongsToDifferentIF) {
                                shouldPush = true;
                                break;
                            }
                        }
                    }
                }
            }
            
            // SPECIAL CASE: When inserting above a regular IF, also push nodes below its convergence point
            if (!shouldPush && targetRegularIfBlock >= 0 && targetRegularIfConvergeIdx >= 0 && 
                targetRegularIfConvergeIdx < nodeCount) {
                // Check if this node is below the convergence point
                if (nodes[i].y < nodes[targetRegularIfConvergeIdx].y) {
                    // Only push nodes in the main branch (not owned by any IF block)
                    // BUT: Don't push nodes that belong to a different IF's branches
                    bool isMainBranch = (nodes[i].branchColumn == 0 && nodes[i].owningIfBlock < 0);
                    
                    // Check if this node belongs to a different IF's branches
                    bool belongsToDifferentIF = false;
                    if (nodes[i].owningIfBlock >= 0 && nodes[i].owningIfBlock < ifBlockCount) {
                        // This node belongs to an IF block - check if it's different from targetRegularIfBlock
                        if (nodes[i].owningIfBlock != targetRegularIfBlock) {
                            belongsToDifferentIF = true;
                        }
                    }
                    
                    // Also check if this node is in a branch (branchColumn != 0) of any IF
                    bool isInBranch = (nodes[i].branchColumn != 0);
                    
                    if (isMainBranch && !belongsToDifferentIF && !isInBranch) {
                        shouldPush = true;
                    }
                }
            }
            
            // Don't push nodes in different branches
            
            if (!shouldPush) {
                continue;
            }
            
            nodes[i].y -= gridSpacing;
            nodes[i].y = snap_to_grid_y(nodes[i].y);
        }
    }
    
    // Second pass: Push all branch nodes of IF blocks that were pushed
    // BUT: Only push branch nodes if the IF block is in the same branch we're adding to
    // This prevents pushing nested IF branch nodes when adding to parent IF branches
    
    for (int i = 0; i < pushedIfBlockCount; i++) {
        int ifBlockIdx = pushedIfBlocks[i];
        
        // CRITICAL FIX: When inserting above a nested IF, only process the target nested IF
        // Skip all other IF blocks to prevent pushing branch nodes of other nested IFs
        if (insertingAboveNestedIF && ifBlockIdx != targetNestedIfBlock) {
            continue;
        }
        
        // Check if this IF block is in the same branch we're adding to
        // If we're adding to a specific branch (targetBranchColumn != 0), only push branch nodes
        // if the IF block itself is in that same branch
        // EXCEPTION: When inserting above a nested IF, always push all branch nodes of that nested IF
        bool shouldPushBranchNodes = true;
        bool isTargetNestedIF = (insertingAboveNestedIF && ifBlockIdx == targetNestedIfBlock);
        
        if (!isTargetNestedIF && targetBranchColumn != 0 && ifBlockIdx >= 0 && ifBlockIdx < ifBlockCount) {
            int ifNodeIdx = ifBlocks[ifBlockIdx].ifNodeIndex;
            if (ifNodeIdx >= 0 && ifNodeIdx < nodeCount) {
                // Use the actual node's owningIfBlock (which may have been updated when added to branch array)
                // instead of just newNodeOwningIfBlock variable
                int actualNewNodeOwningIfBlock = nodes[newNodeIndex].owningIfBlock;
                
                // Check if the IF block node is in the same branch as targetBranchColumn
                // AND has the same owningIfBlock as the new node
                // CRITICAL: Also check that the IF block itself matches - if we're adding to Nested IF 2,
                // we should NOT push branch nodes from Nested IF 1, even if they have the same parent
                bool branchMatches = (nodes[ifNodeIdx].branchColumn == targetBranchColumn);
                bool owningMatches = (nodes[ifNodeIdx].owningIfBlock == actualNewNodeOwningIfBlock);
                bool ifBlockMatches = (ifBlockIdx == actualNewNodeOwningIfBlock);
                
                // Use parent chain to check if they're in the same context
                // If the nested IF has a different parent than the new node's IF, they're in different contexts
                int ifParentIdx = ifBlocks[ifBlockIdx].parentIfIndex;
                int newParentIdx = (actualNewNodeOwningIfBlock >= 0 && actualNewNodeOwningIfBlock < ifBlockCount) ? ifBlocks[actualNewNodeOwningIfBlock].parentIfIndex : -1;
                bool parentContextMatches = true;
                if (ifParentIdx >= 0 && newParentIdx >= 0) {
                    // Both have parents - they must be the same parent
                    parentContextMatches = (ifParentIdx == newParentIdx);
                } else if (ifParentIdx >= 0 || newParentIdx >= 0) {
                    // One has a parent, the other doesn't - different contexts
                    parentContextMatches = false;
                }
                // If neither has a parent, they're both at root level, so context matches
                
                // CRITICAL FIX: The IF block must match the new node's owning IF block
                // This prevents pushing branch nodes from one nested IF when adding to another nested IF
                if (!branchMatches || !owningMatches || !parentContextMatches || !ifBlockMatches) {
                    shouldPushBranchNodes = false;
                } else {
                }
            }
        }
        
        if (shouldPushBranchNodes) {
            // When inserting above a nested IF, branch nodes are already pushed in the first pass
            // So we should NOT push them again in the second pass to avoid double movement
            // Only push branch nodes in the second pass if we're NOT inserting above a nested IF
            // OR if this is a different IF block (not the target nested IF)
            // Also skip when inserting above a regular IF - branch nodes are already pushed in first pass
            // CRITICAL FIX: When inserting in the main branch (targetBranchColumn=0), branch nodes of nested IFs
            // are already pushed in the first pass (if they're below the "to" node), so skip pushing them again
            bool skipBranchPush = (insertingAboveNestedIF && ifBlockIdx == targetNestedIfBlock) ||
                                  (targetRegularIfBlock >= 0 && ifBlockIdx == targetRegularIfBlock) ||
                                  (targetBranchColumn == 0 && ifBlocks[ifBlockIdx].parentIfIndex >= 0);
            
            if (!skipBranchPush) {
                // When inserting above a nested IF, push ALL branch nodes (both true and false branches)
                // Otherwise, only push branch nodes matching targetBranchColumn
                bool pushAllBranches = (insertingAboveNestedIF && ifBlockIdx == targetNestedIfBlock);
                
                for (int j = 0; j < nodeCount; j++) {
                    if (j != newNodeIndex && nodes[j].owningIfBlock == ifBlockIdx && nodes[j].branchColumn != 0) {
                        
                        // When inserting above a nested IF, push all branch nodes
                        // Otherwise, only push branch nodes that match the targetBranchColumn
                        if (!pushAllBranches && targetBranchColumn != 0 && nodes[j].branchColumn != targetBranchColumn) {
                            continue;  // Skip this branch node - it's in a different branch
                        }
                        
                        nodes[j].y -= gridSpacing;
                        nodes[j].y = snap_to_grid_y(nodes[j].y);
                    }
                }
            }
        }
    }
    
    // Reposition convergence points for any IF blocks that were pushed
    for (int i = 0; i < pushedIfBlockCount; i++) {
        reposition_convergence_point(pushedIfBlocks[i], true);  // Push nodes below when inserting
    }
    
    // When inserting above a nested IF, also reposition its convergence point
    if (insertingAboveNestedIF && targetNestedIfBlock >= 0) {
        reposition_convergence_point(targetNestedIfBlock, true);
        
        // Also reposition parent IF if the nested IF's depth changed
        if (targetNestedIfBlock < ifBlockCount) {
            int parentIfIdx = ifBlocks[targetNestedIfBlock].parentIfIndex;
            if (parentIfIdx >= 0 && parentIfIdx < ifBlockCount) {
                reposition_convergence_point(parentIfIdx, true);
            }
        }
    }
    
    // NOW reposition convergence point after push-down has completed
    // This ensures convergence aligns with final node positions
    // Reposition if we added a node to a branch array, regardless of targetBranchColumn
    // (targetBranchColumn can be 0 for nested IF branches, but we still need to reposition)
    // IMPORTANT: Only reposition the IF block whose branch we actually added to (relevantIfBlock)
    // Do NOT reposition nested IFs when adding to parent IF branches
    
    if (relevantIfBlock >= 0 && nodeAddedToBranch) {
        
        reposition_convergence_point(relevantIfBlock, true);
        
        // After repositioning the nested IF, we need to reposition its parent IF too,
        // because the nested IF's depth may have changed
        if (relevantIfBlock >= 0 && relevantIfBlock < ifBlockCount) {
            int parentIfIdx = ifBlocks[relevantIfBlock].parentIfIndex;
            if (parentIfIdx >= 0 && parentIfIdx < ifBlockCount) {
                
                reposition_convergence_point(parentIfIdx, true);
                
                // CRITICAL FIX: After repositioning parent IF, find all sibling nested IFs
                // and ensure end node is positioned based on the LOWEST convergence point
                // among all sibling nested IFs (IFs with the same parent) AND the parent IF's convergence
                double lowestSiblingConvergeY = 999999.0;
                bool foundSiblingConverge = false;
                
                // Check the parent IF's convergence point
                int parentConvergeIdx = ifBlocks[parentIfIdx].convergeNodeIndex;
                if (parentConvergeIdx >= 0 && parentConvergeIdx < nodeCount) {
                    if (nodes[parentConvergeIdx].y < lowestSiblingConvergeY) {
                        lowestSiblingConvergeY = nodes[parentConvergeIdx].y;
                        foundSiblingConverge = true;
                    }
                }
                
                // Check all sibling nested IFs (including the current one)
                for (int i = 0; i < ifBlockCount; i++) {
                    if (ifBlocks[i].parentIfIndex == parentIfIdx) {
                        int siblingConvergeIdx = ifBlocks[i].convergeNodeIndex;
                        if (siblingConvergeIdx >= 0 && siblingConvergeIdx < nodeCount) {
                            if (nodes[siblingConvergeIdx].y < lowestSiblingConvergeY) {
                                lowestSiblingConvergeY = nodes[siblingConvergeIdx].y;
                                foundSiblingConverge = true;
                            }
                        }
                    }
                }
                
                // If we found sibling convergences, ensure end node is below the lowest one
                if (foundSiblingConverge) {
                    // Find end node (should be in main branch, not owned by any IF)
                    for (int i = 0; i < nodeCount; i++) {
                        if (nodes[i].type == NODE_END && nodes[i].branchColumn == 0 && nodes[i].owningIfBlock == -1) {
                            double endNodeY = nodes[i].y;
                            double requiredEndY = lowestSiblingConvergeY - GRID_CELL_SIZE;
                            
                            if (endNodeY > requiredEndY) {  // endNodeY is less negative, so it's above
                                nodes[i].y = snap_to_grid_y(requiredEndY);
                            }
                            break;
                        }
                    }
                }
            }
        }
        
        // If the new node's owningIfBlock is different from relevantIfBlock,
        // it means we're inserting into a nested IF's branch, so reposition that nested IF too
        // BUT: Only do this if we actually added a node to a branch (nodeAddedToBranch is true)
        // This prevents repositioning nested IF when adding to parent IF branches
        if (newNodeOwningIfBlock >= 0 && newNodeOwningIfBlock != relevantIfBlock && newNodeOwningIfBlock < ifBlockCount && nodeAddedToBranch) {
            // Verify that newNodeOwningIfBlock is actually a nested IF of relevantIfBlock
            // (i.e., newNodeOwningIfBlock's parent is relevantIfBlock)
            bool isNestedIf = false;
            if (newNodeOwningIfBlock < ifBlockCount) {
                int nestedParentIdx = ifBlocks[newNodeOwningIfBlock].parentIfIndex;
                if (nestedParentIdx == relevantIfBlock) {
                    isNestedIf = true;
                }
            }
            
            // Only reposition if it's actually a nested IF within the relevantIfBlock
            if (isNestedIf) {
                
                reposition_convergence_point(newNodeOwningIfBlock, true);
            } else {
            }
        }
    }
    
    // Replace old connection with two new ones
    connections[connIndex].fromNode = oldConn.fromNode;
    connections[connIndex].toNode = newNodeIndex;
    
    connections[connectionCount].fromNode = newNodeIndex;
    connections[connectionCount].toNode = oldConn.toNode;
    
    connectionCount++;
    

    // Recalculate branch widths and positions after insertion
    update_all_branch_positions();
}

// Calculate the depth (in grid cells) of a branch, recursively accounting for nested IFs
// branchType: 0 = true/left, 1 = false/right
// Returns: depth in grid cells from IF node to end of branch
int calculate_branch_depth(int ifBlockIndex, int branchType) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) {
        return 0;
    }
    
    IFBlock *ifBlock = &ifBlocks[ifBlockIndex];
    int *branchNodes = (branchType == 0) ? ifBlock->trueBranchNodes : ifBlock->falseBranchNodes;
    int branchCount = (branchType == 0) ? ifBlock->trueBranchCount : ifBlock->falseBranchCount;
    
    if (branchCount == 0) {
        return 0;  // Empty branch contributes no depth
    }
    
    int totalDepth = 0;  // Sum of all node depths in this branch
    
    for (int i = 0; i < branchCount; i++) {
        int nodeIdx = branchNodes[i];
        if (nodeIdx < 0 || nodeIdx >= nodeCount) continue;
        
        int nodeDepth = 1;  // Regular node = 1 grid cell
        
        if (nodes[nodeIdx].type == NODE_IF) {
            // Nested IF: recursively calculate its branch depths
            int nestedIfIdx = -1;
            for (int j = 0; j < ifBlockCount; j++) {
                if (ifBlocks[j].ifNodeIndex == nodeIdx) {
                    nestedIfIdx = j;
                    break;
                }
            }
            
            if (nestedIfIdx >= 0) {
                int nestedTrueDepth = calculate_branch_depth(nestedIfIdx, 0);
                int nestedFalseDepth = calculate_branch_depth(nestedIfIdx, 1);
                int nestedMaxDepth = (nestedTrueDepth > nestedFalseDepth) ? nestedTrueDepth : nestedFalseDepth;
                
                // Nested IF contributes: 1 (IF node) + max branch depth (or 1 if empty) + 1 (convergence)
                nodeDepth = 1 + (nestedMaxDepth > 0 ? nestedMaxDepth : 1) + 1;
            }
        }
        
        totalDepth += nodeDepth;  // Sum all nodes (they stack vertically)
    }
    
    return totalDepth;
}

// Helper function to find the last (bottommost) node in a branch
// Returns: Node index of the last node in the branch, or -1 if branch is empty
int find_last_node_in_branch(int ifBlockIndex, int branchColumn) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) {
        return -1;
    }
    
    int lastNodeIndex = -1;
    double lowestY = 999999.0;  // Start with a very high Y value (top of screen)
    int candidateCount = 0;
    
    // Find the node with the lowest Y value (bottommost) in the specified branch
    for (int i = 0; i < nodeCount; i++) {
        if (nodes[i].owningIfBlock == ifBlockIndex) {
            // Check if this node is in the target branch
            bool isInTargetBranch = false;
            if (branchColumn < 0 && nodes[i].branchColumn < 0) {
                // True branch (left) - any negative column
                isInTargetBranch = true;
            } else if (branchColumn > 0 && nodes[i].branchColumn > 0) {
                // False branch (right) - any positive column
                isInTargetBranch = true;
            }
            
            if (isInTargetBranch) {
                candidateCount++;
                if (nodes[i].y < lowestY) {
                    lowestY = nodes[i].y;
                    lastNodeIndex = i;
                }
            }
        }
    }
    
    return lastNodeIndex;
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
    
    // If branches are equal depth and non-empty, don't move convergence
    if (trueDepth == falseDepth && trueDepth > 0) {
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
                    
                    double oldNodeY = nodes[i].y;
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
                            double oldBranchY = nodes[j].y;
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
                    double oldBranchY = nodes[j].y;
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
    
    // Determine branch type based on target node's branchColumn
    // This is more reliable than counting connections
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
                // A nested IF needs space for its widest branch (left or right)
                // Plus 1.0 for the IF node itself as the center
                double nestedWidthNeeded = (nestedLeft > nestedRight ? nestedLeft : nestedRight) + 1.0;
                if (nestedWidthNeeded > maxWidth) {
                    maxWidth = nestedWidthNeeded;
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
    static int pos_log_count = 0;
    
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
    static int width_log_count = 0;

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
void insert_if_block_in_connection(int connIndex) {
    if (nodeCount + 2 >= MAX_NODES || connectionCount + 6 >= MAX_CONNECTIONS || ifBlockCount >= MAX_IF_BLOCKS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
    // Validate that this isn't a cross-IF connection
    if (!is_valid_if_converge_connection(oldConn.fromNode, oldConn.toNode)) {
        return;  // Reject this operation
    }
    
    // Store the original Y position of the "to" node before we modify anything
    double originalToY = to->y;
    
    // Calculate grid positions
    int fromGridY = world_to_grid_y(from->y);
    int fromGridX = world_to_grid_x(from->x);
    
    // Create IF block positioned one grid cell below the "from" node
    int ifGridY = fromGridY - 1;
    FlowNode *ifNode = &nodes[nodeCount];
    ifNode->x = snap_to_grid_x(from->x);  // Keep same X grid position
    ifNode->y = snap_to_grid_y(grid_to_world_y(ifGridY));
    ifNode->height = 0.35f;  // Slightly taller for diamond shape
    ifNode->width = 0.35f;
    ifNode->value[0] = '\0';  // Initialize value as empty string
    ifNode->type = NODE_IF;
    ifNode->branchColumn = from->branchColumn;  // Inherit branch column
    ifNode->owningIfBlock = from->owningIfBlock;  // Inherit IF block ownership
    
    int ifNodeIndex = nodeCount;
    nodeCount++;
    
    // Create convergence point positioned 2 grid cells below IF block
    // This gives empty IFs the same height as IFs with 1 element in their branches
    int convergeGridY = ifGridY - 2;
    FlowNode *convergeNode = &nodes[nodeCount];
    convergeNode->x = ifNode->x;  // Same X as IF block
    convergeNode->y = snap_to_grid_y(grid_to_world_y(convergeGridY));
    convergeNode->height = 0.15f;  // Small circle
    convergeNode->width = 0.15f;
    convergeNode->value[0] = '\0';
    convergeNode->type = NODE_CONVERGE;
    convergeNode->branchColumn = from->branchColumn;  // Same as IF block
    convergeNode->owningIfBlock = from->owningIfBlock;
    
    int convergeNodeIndex = nodeCount;
    nodeCount++;
    
    // Push the "to" node and all nodes below the convergence point further down
    // Need to make room for: IF block (1) + branch space (2) = 3 grid cells total
    double gridSpacing = GRID_CELL_SIZE * 3;  // 3 grid cells (IF + 2 for branch space)
    for (int i = 0; i < nodeCount; ++i) {
        if (nodes[i].y <= originalToY && i != ifNodeIndex && i != convergeNodeIndex) {
            nodes[i].y -= gridSpacing;
            // Snap to grid after moving
            nodes[i].y = snap_to_grid_y(nodes[i].y);
        }
    }
    
    // Create IF block tracking structure
    IFBlock *ifBlock = &ifBlocks[ifBlockCount];
    ifBlock->ifNodeIndex = ifNodeIndex;
    ifBlock->convergeNodeIndex = convergeNodeIndex;
    ifBlock->parentIfIndex = from->owningIfBlock;  // Parent IF (or -1 if none)
    ifBlock->branchColumn = from->branchColumn;
    ifBlock->trueBranchCount = 0;
    ifBlock->falseBranchCount = 0;
    ifBlock->leftBranchWidth = 1.0;
    ifBlock->rightBranchWidth = 1.0;
    
    int currentIfIndex = ifBlockCount;
    ifBlockCount++;
    
    // If this IF is nested inside another IF's branch, add it to the parent's branch array
    int parentIfIdx = -1;
    int branchType = -1;
    
    if (from->type == NODE_IF) {
        // Creating IF directly from another IF's branch connection
        // Find which IF block the from node represents
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                parentIfIdx = i;
                branchType = get_if_branch_type(connIndex);
                break;
            }
        }
    } else if (from->owningIfBlock >= 0) {
        // Creating IF from a regular node that's in a branch
        parentIfIdx = from->owningIfBlock;
        
        // Determine which branch based on from node's branchColumn
        if (from->branchColumn < 0) {
            branchType = 0;  // True branch (left/negative)
        } else if (from->branchColumn > 0) {
            branchType = 1;  // False branch (right/positive)
        }
    }
    
    if (parentIfIdx >= 0 && branchType >= 0) {
        // Update the IF block's parent reference
        ifBlock->parentIfIndex = parentIfIdx;
        
        // Update branch column based on which branch it's in
        if (branchType == 0) {
            // True branch (left) - use negative offset
            ifBlock->branchColumn = from->branchColumn - 2;
            ifNode->branchColumn = ifBlock->branchColumn;
            convergeNode->branchColumn = ifBlock->branchColumn;
        } else if (branchType == 1) {
            // False branch (right) - use positive offset
            int falseBranchCol = from->branchColumn + 2;
            if (falseBranchCol <= 0) {
                falseBranchCol = abs(from->branchColumn) + 2;
            }
            ifBlock->branchColumn = falseBranchCol;
            ifNode->branchColumn = ifBlock->branchColumn;
            convergeNode->branchColumn = ifBlock->branchColumn;
        }
        
        // Update owningIfBlock for both IF and convergence nodes
        ifNode->owningIfBlock = parentIfIdx;
        convergeNode->owningIfBlock = parentIfIdx;
        
        if (branchType == 0) {
            // Add to parent's true branch
            if (ifBlocks[parentIfIdx].trueBranchCount < MAX_NODES) {
                ifBlocks[parentIfIdx].trueBranchNodes[ifBlocks[parentIfIdx].trueBranchCount] = ifNodeIndex;
                ifBlocks[parentIfIdx].trueBranchCount++;
            }
        } else if (branchType == 1) {
            // Add to parent's false branch
            if (ifBlocks[parentIfIdx].falseBranchCount < MAX_NODES) {
                ifBlocks[parentIfIdx].falseBranchNodes[ifBlocks[parentIfIdx].falseBranchCount] = ifNodeIndex;
                ifBlocks[parentIfIdx].falseBranchCount++;
            }
        }
    }
    
    // Replace old connection and create new connections:
    // from -> IF
    connections[connIndex].fromNode = oldConn.fromNode;
    connections[connIndex].toNode = ifNodeIndex;
    
    // IF -> converge (true branch - initially empty, exits left)
    connections[connectionCount].fromNode = ifNodeIndex;
    connections[connectionCount].toNode = convergeNodeIndex;
    connectionCount++;
    
    // IF -> converge (false branch - initially empty, exits right)
    connections[connectionCount].fromNode = ifNodeIndex;
    connections[connectionCount].toNode = convergeNodeIndex;
    connectionCount++;
    
    // converge -> to
    connections[connectionCount].fromNode = convergeNodeIndex;
    connections[connectionCount].toNode = oldConn.toNode;
    connectionCount++;

    // Recalculate branch widths and positions after creating IF block
    update_all_branch_positions();
}

// Get all parent IF blocks from a given IF block to the root
// Returns the number of parent IF blocks found (up to maxParents)
// The parents array is filled from immediate parent to root (reverse order)
int get_parent_if_chain(int ifBlockIndex, int* parents, int maxParents) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount || maxParents <= 0) {
        return 0;
    }
    
    int count = 0;
    int current = ifBlockIndex;
    
    // Follow the parent chain until we reach the root (-1) or maxParents
    while (count < maxParents && current >= 0 && current < ifBlockCount) {
        int parent = ifBlocks[current].parentIfIndex;
        if (parent < 0 || parent >= ifBlockCount) {
            break;  // Reached root or invalid parent
        }
        parents[count++] = parent;
        current = parent;
    }
    
    return count;
}

// Get all parent IF blocks and their branch columns from a given IF block to the root
// Returns the number of parent IF blocks found (up to maxParents)
// The parents array contains IF block indices
// The branchColumns array contains the branch column of each parent (where the child is located)
int get_parent_if_chain_with_branches(int ifBlockIndex, int* parents, int* branchColumns, int maxParents) {
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount || maxParents <= 0) {
        return 0;
    }
    
    int count = 0;
    int current = ifBlockIndex;
    
    // Follow the parent chain until we reach the root (-1) or maxParents
    while (count < maxParents && current >= 0 && current < ifBlockCount) {
        int parent = ifBlocks[current].parentIfIndex;
        if (parent < 0 || parent >= ifBlockCount) {
            break;  // Reached root or invalid parent
        }
        parents[count] = parent;
        branchColumns[count] = ifBlocks[current].branchColumn;  // The branch column of the current IF in its parent
        count++;
        current = parent;
    }
    
    return count;
}

// Keyboard callback
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    
    // Toggle deletion with 'D' key
    if (key == GLFW_KEY_D && action == GLFW_PRESS) {
        deletionEnabled = !deletionEnabled;
        const char* status = deletionEnabled ? "ENABLED" : "DISABLED";
        char message[100];
        snprintf(message, sizeof(message), "Deletion is now %s", status);
        tinyfd_messageBox("Deletion Toggle", message, "ok", "info", 1);
    }
    
    // Arrow key scrolling (works on both press and repeat)
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        const double scrollSpeed = 0.1;  // Same speed as mouse scroll
        switch (key) {
            case GLFW_KEY_UP:
                scrollOffsetY -= scrollSpeed;
                break;
            case GLFW_KEY_DOWN:
                scrollOffsetY += scrollSpeed;
                break;
            case GLFW_KEY_LEFT:
                scrollOffsetX -= scrollSpeed;
                break;
            case GLFW_KEY_RIGHT:
                scrollOffsetX += scrollSpeed;
                break;
        }
    }
}

// Insert CYCLE block with paired end point in a connection
void insert_cycle_block_in_connection(int connIndex) {
    if (nodeCount + 2 >= MAX_NODES || connectionCount + 2 >= MAX_CONNECTIONS || cycleBlockCount >= MAX_CYCLE_BLOCKS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
    double originalToY = to->y;
    int fromGridY = world_to_grid_y(from->y);
    
    // Default placement (WHILE/FOR): cycle block then end point one grid below
    int cycleGridY = fromGridY - 1;
    int endGridY = cycleGridY - 1;
    
    // Create cycle block
    int cycleNodeIndex = nodeCount;
    FlowNode *cycleNode = &nodes[nodeCount++];
    cycleNode->x = snap_to_grid_x(from->x);
    cycleNode->y = snap_to_grid_y(grid_to_world_y(cycleGridY));
    cycleNode->height = 0.26f;
    cycleNode->width = 0.34f;
    cycleNode->value[0] = '\0';
    cycleNode->type = NODE_CYCLE;
    cycleNode->branchColumn = from->branchColumn;
    cycleNode->owningIfBlock = from->owningIfBlock;
    
    // Create cycle end point
    int endNodeIndex = nodeCount;
    FlowNode *endNode = &nodes[nodeCount++];
    endNode->x = cycleNode->x;
    endNode->y = snap_to_grid_y(grid_to_world_y(endGridY));
    endNode->height = 0.12f;
    endNode->width = 0.12f;
    endNode->value[0] = '\0';
    endNode->type = NODE_CYCLE_END;
    endNode->branchColumn = from->branchColumn;
    endNode->owningIfBlock = from->owningIfBlock;
    
    // Push nodes below to make room (2 grid cells)
    double gridSpacing = GRID_CELL_SIZE * 2;
    for (int i = 0; i < nodeCount; ++i) {
        if (nodes[i].y <= originalToY && i != cycleNodeIndex && i != endNodeIndex) {
            nodes[i].y -= gridSpacing;
            nodes[i].y = snap_to_grid_y(nodes[i].y);
        }
    }
    
    // Wire connections to keep the branch intact (default WHILE/FOR order)
    connections[connIndex].fromNode = oldConn.fromNode;
    connections[connIndex].toNode = cycleNodeIndex;
    
    connections[connectionCount].fromNode = cycleNodeIndex;
    connections[connectionCount].toNode = endNodeIndex;
    connectionCount++;
    
    connections[connectionCount].fromNode = endNodeIndex;
    connections[connectionCount].toNode = oldConn.toNode;
    connectionCount++;
    
    // Register cycle metadata
    int parentCycle = -1;
    if (from->type == NODE_CYCLE) {
        parentCycle = find_cycle_block_by_cycle_node(oldConn.fromNode);
    } else {
        parentCycle = find_cycle_block_by_end_node(oldConn.fromNode);
    }
    
    CycleBlock *cycle = &cycleBlocks[cycleBlockCount];
    cycle->cycleNodeIndex = cycleNodeIndex;
    cycle->cycleEndNodeIndex = endNodeIndex;
    cycle->parentCycleIndex = parentCycle;
    cycle->cycleType = CYCLE_WHILE;
    cycle->loopbackOffset = 0.3f * (float)((parentCycle >= 0 ? calculate_cycle_depth(parentCycle) + 1 : 1));
    cycle->initVar[0] = '\0';
    cycle->condition[0] = '\0';
    cycle->increment[0] = '\0';
    
    cycleBlockCount++;
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;    // Mark as intentionally unused
    
    // Calculate world-space cursor position (accounting for scroll and flowchart scale)
    // Transformation: screen = scale * (world - scrollOffset/scale) = scale * world - scrollOffset
    // So: world = (screen + scrollOffset) / scale
    double worldCursorX = (cursorX + scrollOffsetX) / FLOWCHART_SCALE;
    double worldCursorY = (cursorY + scrollOffsetY) / FLOWCHART_SCALE;
    
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Check if clicking on buttons (buttons are in screen space, not world space)
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        float aspectRatio = (float)width / (float)height;
        float buttonX_scaled = buttonX * aspectRatio;
        if (cursor_over_button(buttonX_scaled, saveButtonY, window)) {
            // Blue save button clicked - open save dialog
            const char* filters[] = {"*.txt", "*.flow"};
            const char* filename = tinyfd_saveFileDialog(
                "Save Flowchart",
                "flowchart.txt",
                2, filters,
                "Text Files (*.txt);;Flowchart Files (*.flow)"
            );
            if (filename != NULL && strlen(filename) > 0) {
                save_flowchart(filename);
            }
            return;
        }
        if (cursor_over_button(buttonX_scaled, loadButtonY, window)) {
            // Yellow load button clicked - open load dialog
            const char* filters[] = {"*.txt", "*.flow"};
            const char* filename = tinyfd_openFileDialog(
                "Load Flowchart",
                "",
                2, filters,
                "Text Files (*.txt);;Flowchart Files (*.flow)",
                0
            );
            if (filename != NULL && strlen(filename) > 0) {
                load_flowchart(filename);
            }
            return;
        }
        if (cursor_over_button(buttonX_scaled, closeButtonY, window)) {
            // Red close button clicked - close program
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        }
        if (cursor_over_button(buttonX_scaled, exportButtonY, window)) {
            // Export button clicked - show language selection and export
            const char* langOptions[] = {"C"};
            int langChoice = tinyfd_listDialog("Select Programming Language", 
                "Choose the programming language:", 1, langOptions);
            
            if (langChoice < 0 || langChoice >= 1) {
                return; // User cancelled or invalid
            }
            
            const char* langName = "C";
            
            // Open file save dialog
            const char* filters[] = {"*.c"};
            const char* filename = tinyfd_saveFileDialog(
                "Export Flowchart to Code",
                "output.c",
                1, filters,
                "C Source Files (*.c)"
            );
            
            if (filename != NULL && strlen(filename) > 0) {
                if (export_to_code(filename, langName, nodes, nodeCount, (struct Connection*)connections, connectionCount)) {
                    tinyfd_messageBox("Export Success", "Flowchart exported successfully!", "ok", "info", 1);
                } else {
                    tinyfd_messageBox("Export Error", "Failed to export flowchart. Check console for details.", "ok", "error", 1);
                }
            }
            return;
        }
        
        // Check popup menu interaction
        if (popupMenu.active) {
            // Calculate menu dimensions (same as in drawPopupMenu)
            float menuItemWidth = menuMinWidth;
            
            // Determine menu item count based on menu type
            int currentMenuItemCount = 0;
            if (popupMenu.type == MENU_TYPE_CONNECTION) {
                currentMenuItemCount = connectionMenuItemCount;
            } else if (popupMenu.type == MENU_TYPE_NODE) {
                currentMenuItemCount = nodeMenuItemCount;
            }
            
            float totalMenuHeight = currentMenuItemCount * menuItemHeight + (currentMenuItemCount - 1) * menuItemSpacing;
            
            // Check which menu item was clicked (menu is in screen space)
            float menuX = (float)popupMenu.x;
            float menuY = (float)popupMenu.y;
            
            // Check if click is within menu bounds (using screen-space cursor)
            if (cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
                cursorY <= menuY && cursorY >= menuY - totalMenuHeight) {
                
                // Determine which menu item was clicked
                int clickedItem = -1;
                for (int i = 0; i < currentMenuItemCount; i++) {
                    float itemY = menuY - i * (menuItemHeight + menuItemSpacing);
                    float itemBottom = itemY - menuItemHeight;
                    if (cursorY <= itemY && cursorY >= itemBottom) {
                        clickedItem = i;
                        break;
                    }
                }
                
                if (clickedItem >= 0) {
                    if (popupMenu.type == MENU_TYPE_CONNECTION) {
                        // Insert node of the selected type
                        NodeType selectedType = connectionMenuItems[clickedItem].nodeType;
                        if (selectedType == NODE_IF) {
                            insert_if_block_in_connection(popupMenu.connectionIndex);
                        } else if (selectedType == NODE_CYCLE) {
                            insert_cycle_block_in_connection(popupMenu.connectionIndex);
                        } else {
                            insert_node_in_connection(popupMenu.connectionIndex, selectedType);
                        }
                    } else if (popupMenu.type == MENU_TYPE_NODE) {
                        // Handle node menu actions
                        if (nodeMenuItems[clickedItem].action == 0) {
                            // Delete action - only if deletion is enabled
                            if (deletionEnabled) {
                                delete_node(popupMenu.nodeIndex);
                            } else {
                                tinyfd_messageBox("Deletion Disabled", 
                                    "Deletion is currently disabled. Press 'D' to enable it.", 
                                    "ok", "warning", 1);
                            }
                        } else if (nodeMenuItems[clickedItem].action == 1) {
                            // Value action - edit node value
                            edit_node_value(popupMenu.nodeIndex);
                        }
                    }
                    popupMenu.active = false;
                } else {
                    // Clicked in menu but not on an item
                    popupMenu.active = false;
                }
            } else {
                // Clicked outside menu, close it
                popupMenu.active = false;
            }
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        // Check if we're clicking on a node first (use world-space coordinates)
        int nodeIndex = hit_node(worldCursorX, worldCursorY);
        
        if (nodeIndex >= 0) {
            // Open node popup menu at cursor position (store screen-space coordinates so it doesn't scroll)
            popupMenu.active = true;
            popupMenu.type = MENU_TYPE_NODE;
            popupMenu.x = cursorX;
            popupMenu.y = cursorY;  // Use screen space, not world space
            popupMenu.nodeIndex = nodeIndex;
            popupMenu.connectionIndex = -1;
        } else {
            // Check if we're clicking on a connection (use world-space coordinates)
            int connIndex = hit_connection(worldCursorX, worldCursorY, 0.05f);
            
            if (connIndex >= 0) {
                // Open connection popup menu at cursor position (store screen-space coordinates so it doesn't scroll)
                popupMenu.active = true;
                popupMenu.type = MENU_TYPE_CONNECTION;
                popupMenu.x = cursorX;
                popupMenu.y = cursorY;  // Use screen space, not world space
                popupMenu.connectionIndex = connIndex;
                popupMenu.nodeIndex = -1;
            } else {
                // Close menu if clicking elsewhere
                popupMenu.active = false;
            }
        }
    }
}



void drawPopupMenu(GLFWwindow* window);  // Forward declaration

void drawFlowNode(const FlowNode *n) {
    
    // Route to appropriate block drawing function based on type
    if (n->type == NODE_START) {
        // Start node: green rounded rectangle
        glColor3f(0.3f, 0.9f, 0.3f); // green for start
    
    glBegin(GL_QUADS);
    glVertex2f(n->x - n->width * 0.5f, n->y + n->height * 0.5f);
    glVertex2f(n->x + n->width * 0.5f, n->y + n->height * 0.5f);
    glVertex2f(n->x + n->width * 0.5f, n->y - n->height * 0.5f);
    glVertex2f(n->x - n->width * 0.5f, n->y - n->height * 0.5f);
    glEnd();

    // Border
    glColor3f(0.2f, 0.2f, 0.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(n->x - n->width * 0.5f, n->y + n->height * 0.5f);
    glVertex2f(n->x + n->width * 0.5f, n->y + n->height * 0.5f);
    glVertex2f(n->x + n->width * 0.5f, n->y - n->height * 0.5f);
    glVertex2f(n->x - n->width * 0.5f, n->y - n->height * 0.5f);
    glEnd();

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
        
        glBegin(GL_QUADS);
        glVertex2f(n->x - n->width * 0.5f, n->y + n->height * 0.5f);
        glVertex2f(n->x + n->width * 0.5f, n->y + n->height * 0.5f);
        glVertex2f(n->x + n->width * 0.5f, n->y - n->height * 0.5f);
        glVertex2f(n->x - n->width * 0.5f, n->y - n->height * 0.5f);
        glEnd();
        
        // Border
        glColor3f(0.2f, 0.2f, 0.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(n->x - n->width * 0.5f, n->y + n->height * 0.5f);
        glVertex2f(n->x + n->width * 0.5f, n->y + n->height * 0.5f);
        glVertex2f(n->x + n->width * 0.5f, n->y - n->height * 0.5f);
        glVertex2f(n->x - n->width * 0.5f, n->y - n->height * 0.5f);
        glEnd();
        
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
    
    bool hoveringSave = cursor_over_button(buttonX_scaled, saveButtonY, window);
    bool hoveringLoad = cursor_over_button(buttonX_scaled, loadButtonY, window);
    bool hoveringClose = cursor_over_button(buttonX_scaled, closeButtonY, window);
    bool hoveringExport = cursor_over_button(buttonX_scaled, exportButtonY, window);
    
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
        float textX = labelX + (labelWidth - textWidth) * 0.25f;
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
        float textX = labelX + (labelWidth - textWidth) *0.25f;
        float textY = labelY - fontSize * 0.25f;
        // Center vertically
        draw_text(textX, textY, "LOAD", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    // Draw close button (red) - use same radius for X and Y to make it circular
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
        float textX = labelX + (labelWidth - textWidth) * 0.25f;
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "CLOSE", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
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
        float textX = labelX + (labelWidth - textWidth) * 0.25f;
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "EXPORT", fontSize, 1.0f, 1.0f, 1.0f);
    }
}

void initialize_flowchart() {
    // Create START node at grid position (0, 0)
    nodes[0].x = grid_to_world_x(0);
    nodes[0].y = grid_to_world_y(0);
    nodes[0].width = 0.35f;
    nodes[0].height = 0.22f;
    nodes[0].value[0] = '\0';  // Initialize value as empty string
    nodes[0].type = NODE_START;
    nodes[0].branchColumn = 0;
    nodes[0].owningIfBlock = -1;
    
    // Create END node at grid position (0, -1)
    nodes[1].x = grid_to_world_x(0);
    nodes[1].y = grid_to_world_y(-1);
    nodes[1].width = 0.35f;
    nodes[1].height = 0.22f;
    nodes[1].value[0] = '\0';  // Initialize value as empty string
    nodes[1].type = NODE_END;
    nodes[1].branchColumn = 0;
    nodes[1].owningIfBlock = -1;
    
    nodeCount = 2;
    
    // Connect START to END
    connections[0].fromNode = 0;
    connections[0].toNode = 1;
    connectionCount = 1;
    
    // Initialize IF blocks
    ifBlockCount = 0;
}

int main(void) {
    GLFWwindow* window;

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    window = glfwCreateWindow(1600, 900, "Flowchart Editor", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    
    // Initialize text renderer with embedded DejaVu Sans Mono font
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    text_renderer_set_window_size(width, height);
    float initialAspectRatio = (float)width / (float)height;
    text_renderer_set_aspect_ratio(initialAspectRatio);
    text_renderer_set_y_scale(1.0f);  // No Y scaling needed
    if (!init_text_renderer(NULL)) {  // NULL = use embedded font
        fprintf(stderr, "Warning: Failed to initialize text renderer\n");
    }
    
    // Initialize with connected START and END nodes
    initialize_flowchart();

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Set up viewport and projection matrix to account for aspect ratio
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);
        
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
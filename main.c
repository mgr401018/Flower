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
#include "src/code_exporter.h"

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
    NODE_CONVERGE = 9
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
} IFBlock;

IFBlock ifBlocks[MAX_IF_BLOCKS];
int ifBlockCount = 0;

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
    {"IF", NODE_IF}
};
int connectionMenuItemCount = 6;

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

// Find which connection (L-shaped path) the cursor is near
static int hit_connection(double x, double y, float threshold) {
    for (int i = 0; i < connectionCount; ++i) {
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
            
            float branchX = (float)from->x - 1.0f;  // Branch column center (IF center - 1.0)
            
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
            
            float branchX = (float)from->x + 1.0f;  // Branch column center (IF center + 1.0)
            
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
            float y1 = (float)(from->y - from->height * 0.5f);
            float x2 = (float)to->x;
            float y2 = (float)(to->y + to->height * 0.5f);
            
            // Special handling for connections TO convergence point from branch blocks
            if (to->type == NODE_CONVERGE && from->branchColumn != 0) {
                // Source is in a branch, target is convergence
                float branchX = (float)from->x;  // Stay in branch column
                
                // Determine which side of convergence to connect to
                float convergeX;
                if (from->branchColumn < 0) {
                    convergeX = (float)(to->x - to->width * 0.5f);
                } else {
                    convergeX = (float)(to->x + to->width * 0.5f);
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
        
        // Write true branch nodes
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            fprintf(file, "%d ", ifBlocks[i].trueBranchNodes[j]);
        }
        if (ifBlocks[i].trueBranchCount > 0) fprintf(file, "\n");
        
        // Write false branch nodes
        for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
            fprintf(file, "%d ", ifBlocks[i].falseBranchNodes[j]);
        }
        if (ifBlocks[i].falseBranchCount > 0) fprintf(file, "\n");
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
            nodes[i].type == NODE_ASSIGNMENT || nodes[i].type == NODE_DECLARE) {
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
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            if (fscanf(file, "%d", &ifBlocks[i].trueBranchNodes[j]) != 1) {
                fprintf(stderr, "Error reading true branch nodes\n");
                break;
            }
        }
        
        // Read false branch nodes
        for (int j = 0; j < ifBlocks[i].falseBranchCount; j++) {
            if (fscanf(file, "%d", &ifBlocks[i].falseBranchNodes[j]) != 1) {
                fprintf(stderr, "Error reading false branch nodes\n");
                break;
            }
        }
        
        // Update node ownership based on IF blocks
        if (ifBlocks[i].ifNodeIndex < nodeCount) {
            nodes[ifBlocks[i].ifNodeIndex].owningIfBlock = i;
        }
        if (ifBlocks[i].convergeNodeIndex < nodeCount) {
            nodes[ifBlocks[i].convergeNodeIndex].owningIfBlock = i;
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
            
            // Delete all nodes between IF and convergence (in branches)
            // For now, we assume branches are empty - just delete the IF and convergence
            
            // Remove all connections involving IF or convergence
            for (int i = connectionCount - 1; i >= 0; i--) {
                if (connections[i].fromNode == ifIdx || connections[i].toNode == ifIdx ||
                    connections[i].fromNode == convergeIdx || connections[i].toNode == convergeIdx) {
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
            
            // Mark nodes for deletion (higher index first to avoid shifting issues)
            int nodesToDelete[2];
            int deleteCount = 0;
            if (ifIdx > convergeIdx) {
                nodesToDelete[0] = ifIdx;
                nodesToDelete[1] = convergeIdx;
                deleteCount = 2;
            } else {
                nodesToDelete[0] = convergeIdx;
                nodesToDelete[1] = ifIdx;
                deleteCount = 2;
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
                }
            }
            
            // Remove the IF block from the tracking array
            for (int i = ifBlockIndex; i < ifBlockCount - 1; i++) {
                ifBlocks[i] = ifBlocks[i + 1];
            }
            ifBlockCount--;
            
            return;  // Done handling IF/CONVERGE deletion
        }
    }
    
    // Save the IF block ownership and branch before deletion (we'll need these later)
    int deletedNodeOwningIfBlock = nodes[nodeIndex].owningIfBlock;
    int deletedNodeBranchColumn = nodes[nodeIndex].branchColumn;
    
    // Find all connections involving this node
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
            
            // #region agent log
            FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:reconnect\",\"message\":\"Reconnecting after delete\",\"data\":{\"deletedNode\":%d,\"fromNode\":%d,\"toNode\":%d,\"fromBranch\":%d,\"toBranch\":%d,\"deletedBranch\":%d,\"fromType\":%d,\"toType\":%d},\"timestamp\":%ld}\n", nodeIndex, fromNode, toNode, nodes[fromNode].branchColumn, nodes[toNode].branchColumn, nodes[nodeIndex].branchColumn, nodes[fromNode].type, nodes[toNode].type, (long)time(NULL));
                fclose(debug_log);
            }
            // #endregion
            
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
                // #region agent log
                debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                if (debug_log) {
                    fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:skip_incompatible\",\"message\":\"Skipping incompatible branch connection\",\"data\":{\"fromNode\":%d,\"toNode\":%d,\"fromBranch\":%d,\"toBranch\":%d},\"timestamp\":%ld}\n", fromNode, toNode, nodes[fromNode].branchColumn, nodes[toNode].branchColumn, (long)time(NULL));
                    fclose(debug_log);
                }
                // #endregion
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
                            
                            // #region agent log
                            debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                            if (debug_log) {
                                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:conn_exists_same_branch\",\"message\":\"Connection already exists for this branch\",\"data\":{\"fromNode\":%d,\"toNode\":%d,\"existingConnIndex\":%d,\"existingBranch\":%d,\"deletedBranch\":%d},\"timestamp\":%ld}\n", fromNode, toNode, k, existingBranchType, deletedBranchType, (long)time(NULL));
                                fclose(debug_log);
                            }
                            // #endregion
                            
                            break;
                        } else {
                            // This connection is for a DIFFERENT branch, keep looking
                            // #region agent log
                            debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                            if (debug_log) {
                                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:conn_exists_diff_branch\",\"message\":\"Connection exists but for different branch\",\"data\":{\"fromNode\":%d,\"toNode\":%d,\"existingConnIndex\":%d,\"existingBranch\":%d,\"deletedBranch\":%d},\"timestamp\":%ld}\n", fromNode, toNode, k, existingBranchType, deletedBranchType, (long)time(NULL));
                                fclose(debug_log);
                            }
                            // #endregion
                            continue;
                        }
                    } else {
                        // Not an IF->convergence connection, regular check applies
                        connectionExists = true;
                        
                        // #region agent log
                        debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                        if (debug_log) {
                            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:conn_exists\",\"message\":\"Connection already exists (non-branch)\",\"data\":{\"fromNode\":%d,\"toNode\":%d,\"existingConnIndex\":%d},\"timestamp\":%ld}\n", fromNode, toNode, k, (long)time(NULL));
                            fclose(debug_log);
                        }
                        // #endregion
                        
                        break;
                    }
                }
            }
            
            // Create new connection if it doesn't exist and we have room
            if (!connectionExists && connectionCount < MAX_CONNECTIONS) {
                // #region agent log
                debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                if (debug_log) {
                    fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:create_conn\",\"message\":\"Creating new connection\",\"data\":{\"fromNode\":%d,\"toNode\":%d,\"newConnIndex\":%d,\"deletedBranch\":%d},\"timestamp\":%ld}\n", fromNode, toNode, connectionCount, deletedBranch, (long)time(NULL));
                    fclose(debug_log);
                }
                // #endregion
                
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
    
    // Apply movements in order from top to bottom
    for (int i = 0; i < nodesToMoveCount; i++) {
        int nodeIdx = nodesToMove[i];
        double deltaY = nodePositionDeltas[nodeIdx];
        double originalY = originalYPositions[nodeIdx];
        
        // Move this node and snap to grid
        nodes[nodeIdx].y = snap_to_grid_y(originalY + deltaY);
        
        // Move all nodes below this one (based on original positions) up by the same amount
        // Use original positions to determine what's below, but apply to current positions
        // Skip convergence points - they are managed by reposition_convergence_point()
        // IMPORTANT: Only pull nodes in the SAME BRANCH
        for (int j = 0; j < nodeCount; j++) {
            if (j != nodeIdx && j != nodeIndex && originalYPositions[j] < originalY) {
                // Skip convergence points - they are positioned explicitly by reposition_convergence_point()
                if (nodes[j].type == NODE_CONVERGE) {
                    continue;
                }
                
                // Only pull nodes in the same branch as the deleted node
                // Case 1: Both in main branch (0)
                // Case 2: Both in same non-zero branch
                bool shouldPull = false;
                if (deletedNodeBranchColumn == 0 && nodes[j].branchColumn == 0) {
                    // Both in main branch
                    shouldPull = true;
                } else if (deletedNodeBranchColumn == nodes[j].branchColumn) {
                    // Same branch (including same non-zero branch)
                    shouldPull = true;
                }
                // Don't pull nodes in different branches
                
                if (!shouldPull) {
                    continue;
                }
                
                nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
            }
        }
    }
    
    // #region agent log
    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J,M\",\"location\":\"main.c:delete_node:before_delete\",\"message\":\"About to delete node\",\"data\":{\"nodeIndex\":%d,\"nodeType\":%d,\"nodeBranch\":%d,\"nodeX\":%.3f,\"totalNodes\":%d,\"totalConns\":%d},\"timestamp\":%ld}\n", nodeIndex, nodes[nodeIndex].type, nodes[nodeIndex].branchColumn, nodes[nodeIndex].x, nodeCount, connectionCount, (long)time(NULL));
        
        // Log all nodes before deletion
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"M\",\"location\":\"main.c:delete_node:all_nodes_before\",\"message\":\"All nodes before deletion\",\"data\":{\"nodes\":[");
        for (int dbg_i = 0; dbg_i < nodeCount; dbg_i++) {
            fprintf(debug_log, "{\"idx\":%d,\"type\":%d,\"branch\":%d,\"x\":%.3f}", dbg_i, nodes[dbg_i].type, nodes[dbg_i].branchColumn, nodes[dbg_i].x);
            if (dbg_i < nodeCount - 1) fprintf(debug_log, ",");
        }
        fprintf(debug_log, "]},\"timestamp\":%ld}\n", (long)time(NULL));
        
        // Log all connections before deletion
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"M\",\"location\":\"main.c:delete_node:all_conns_before\",\"message\":\"All connections before deletion\",\"data\":{\"connections\":[");
        for (int dbg_i = 0; dbg_i < connectionCount; dbg_i++) {
            fprintf(debug_log, "{\"idx\":%d,\"from\":%d,\"to\":%d}", dbg_i, connections[dbg_i].fromNode, connections[dbg_i].toNode);
            if (dbg_i < connectionCount - 1) fprintf(debug_log, ",");
        }
        fprintf(debug_log, "]},\"timestamp\":%ld}\n", (long)time(NULL));
        
        fclose(debug_log);
    }
    // #endregion
    
    // Remove all connections involving the deleted node
    // Work backwards to avoid index shifting issues
    for (int i = connectionCount - 1; i >= 0; i--) {
        if (connections[i].fromNode == nodeIndex || connections[i].toNode == nodeIndex) {
            // #region agent log
            debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:remove_conn\",\"message\":\"Removing connection\",\"data\":{\"connIndex\":%d,\"fromNode\":%d,\"toNode\":%d},\"timestamp\":%ld}\n", i, connections[i].fromNode, connections[i].toNode, (long)time(NULL));
                fclose(debug_log);
            }
            // #endregion
            
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
        // #region agent log
        debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
        if (debug_log) {
            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"J\",\"location\":\"main.c:delete_node:shift\",\"message\":\"Shifting node\",\"data\":{\"oldIndex\":%d,\"newIndex\":%d,\"nodeType\":%d,\"nodeBranch\":%d,\"nodeX\":%.3f},\"timestamp\":%ld}\n", i+1, i, nodes[i+1].type, nodes[i+1].branchColumn, nodes[i+1].x, (long)time(NULL));
            fclose(debug_log);
        }
        // #endregion
        
        nodes[i] = nodes[i + 1];
    }
    nodeCount--;
    
    // #region agent log
    debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        // Log all nodes after deletion
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"M\",\"location\":\"main.c:delete_node:all_nodes_after\",\"message\":\"All nodes after deletion\",\"data\":{\"nodes\":[");
        for (int dbg_i = 0; dbg_i < nodeCount; dbg_i++) {
            fprintf(debug_log, "{\"idx\":%d,\"type\":%d,\"branch\":%d,\"x\":%.3f}", dbg_i, nodes[dbg_i].type, nodes[dbg_i].branchColumn, nodes[dbg_i].x);
            if (dbg_i < nodeCount - 1) fprintf(debug_log, ",");
        }
        fprintf(debug_log, "]},\"timestamp\":%ld}\n", (long)time(NULL));
        
        // Log all connections after deletion
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"M\",\"location\":\"main.c:delete_node:all_conns_after\",\"message\":\"All connections after deletion\",\"data\":{\"connections\":[");
        for (int dbg_i = 0; dbg_i < connectionCount; dbg_i++) {
            fprintf(debug_log, "{\"idx\":%d,\"from\":%d,\"to\":%d}", dbg_i, connections[dbg_i].fromNode, connections[dbg_i].toNode);
            if (dbg_i < connectionCount - 1) fprintf(debug_log, ",");
        }
        fprintf(debug_log, "]},\"timestamp\":%ld}\n", (long)time(NULL));
        
        fclose(debug_log);
    }
    // #endregion
    
    // NOW reposition convergence point after the node has been deleted
    // This ensures we count the correct number of remaining nodes in each branch
    if (deletedNodeOwningIfBlock >= 0) {
        reposition_convergence_point(deletedNodeOwningIfBlock);
    }
    
    // Rebuild variable table after deletion
    rebuild_variable_table();
}

// Forward declarations
void rebuild_variable_table(void);
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
    
    // #region agent log
    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"A,B\",\"location\":\"main.c:insert_node_in_connection:entry\",\"message\":\"Inserting node in connection\",\"data\":{\"fromNode\":%d,\"toNode\":%d,\"fromX\":%.3f,\"fromBranchColumn\":%d,\"toBranchColumn\":%d,\"fromType\":%d},\"timestamp\":%ld}\n", oldConn.fromNode, oldConn.toNode, from->x, from->branchColumn, to->branchColumn, from->type, (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    // Store the original Y position of the "to" node before we modify anything
    double originalToY = to->y;
    
    // Calculate grid positions
    int fromGridY = world_to_grid_y(from->y);
    
    // Determine the branch column for the new node
    int targetBranchColumn = from->branchColumn;
    double targetX = from->x;
    
    // If inserting into a connection FROM an IF block, determine which branch
    if (from->type == NODE_IF) {
        int branchType = get_if_branch_type(connIndex);
        if (branchType == 0) {
            // True branch (left)
            targetBranchColumn = from->branchColumn - 2;
            targetX = from->x - 1.0;  // Position in left branch column
        } else if (branchType == 1) {
            // False branch (right)
            targetBranchColumn = from->branchColumn + 2;
            targetX = from->x + 1.0;  // Position in right branch column
        }
    }
    
    // Determine the owning IF block for the new node
    int newNodeOwningIfBlock = from->owningIfBlock;  // Default: inherit from parent
    
    // If inserting directly from an IF block, the new node belongs to THAT IF block
    if (from->type == NODE_IF) {
        // Find the IF block index for this IF node
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                newNodeOwningIfBlock = i;
                break;
            }
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
    
    // #region agent log
    debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"A,C\",\"location\":\"main.c:insert_node_in_connection:after_create\",\"message\":\"Node created\",\"data\":{\"newNodeIndex\":%d,\"newX\":%.3f,\"newBranchColumn\":%d,\"targetBranchColumn\":%d,\"branchType\":%d},\"timestamp\":%ld}\n", newNodeIndex, newNode->x, newNode->branchColumn, targetBranchColumn, (from->type == NODE_IF ? get_if_branch_type(connIndex) : -1), (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    // Determine which IF block to reposition later (after push-down)
    int relevantIfBlock = -1;
    if (from->type == NODE_IF) {
        // Inserting directly from IF block - find this IF block's index
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                relevantIfBlock = i;
                break;
            }
        }
    } else if (from->owningIfBlock >= 0) {
        // Inserting from a node that's already in a branch
        relevantIfBlock = from->owningIfBlock;
    }
    
    // Push the "to" node and all nodes below it further down by one grid cell
    // Skip convergence points - they are positioned explicitly by reposition_convergence_point()
    // IMPORTANT: Only push nodes in the SAME BRANCH or main branch (0)
    double gridSpacing = GRID_CELL_SIZE;
    for (int i = 0; i < nodeCount; ++i) {
        if (nodes[i].y <= originalToY && i != newNodeIndex) {
            // Skip convergence points - they are managed by reposition_convergence_point()
            if (nodes[i].type == NODE_CONVERGE) {
                continue;
            }
            
            // Only push nodes in the same branch
            // Case 1: Both in main branch (0)
            // Case 2: Both in same non-zero branch
            // Case 3: Node is in main branch and new node is in a branch (don't push main branch nodes when inserting in branch)
            bool shouldPush = false;
            if (targetBranchColumn == 0 && nodes[i].branchColumn == 0) {
                // Both in main branch
                shouldPush = true;
            } else if (targetBranchColumn == nodes[i].branchColumn) {
                // Same branch (including same non-zero branch)
                shouldPush = true;
            }
            // Don't push nodes in different branches
            
            if (!shouldPush) {
                continue;
            }
            
            // #region agent log
            double oldY = nodes[i].y;
            int oldBranch = nodes[i].branchColumn;
            // #endregion
            
            nodes[i].y -= gridSpacing;
            // Snap to grid after moving
            nodes[i].y = snap_to_grid_y(nodes[i].y);
            
            // #region agent log
            FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"branch-push\",\"hypothesisId\":\"G,H,I\",\"location\":\"main.c:insert_node_in_connection:push_down\",\"message\":\"Pushing node down\",\"data\":{\"nodeIndex\":%d,\"oldY\":%.3f,\"newY\":%.3f,\"nodeBranch\":%d,\"targetBranch\":%d},\"timestamp\":%ld}\n", i, oldY, nodes[i].y, oldBranch, targetBranchColumn, (long)time(NULL));
                fclose(debug_log);
            }
            // #endregion
        }
    }
    
    // NOW reposition convergence point after push-down has completed
    // This ensures convergence aligns with final node positions
    if (relevantIfBlock >= 0 && targetBranchColumn != 0) {
        reposition_convergence_point(relevantIfBlock);
    }
    
    // Replace old connection with two new ones
    connections[connIndex].fromNode = oldConn.fromNode;
    connections[connIndex].toNode = newNodeIndex;
    
    connections[connectionCount].fromNode = newNodeIndex;
    connections[connectionCount].toNode = oldConn.toNode;
    connectionCount++;
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
    
    // #region agent log
    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"B\",\"location\":\"main.c:find_last_node_in_branch:result\",\"message\":\"Finding last node\",\"data\":{\"ifBlockIndex\":%d,\"branchColumn\":%d,\"candidateCount\":%d,\"lastNodeIndex\":%d,\"lowestY\":%.3f},\"timestamp\":%ld}\n", ifBlockIndex, branchColumn, candidateCount, lastNodeIndex, (lastNodeIndex >= 0 ? lowestY : -999.0), (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    return lastNodeIndex;
}

// Helper function to reposition the convergence point based on branch lengths
// The convergence point should align with the longest branch
void reposition_convergence_point(int ifBlockIndex) {
    // #region agent log
    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"E,F\",\"location\":\"main.c:reposition_convergence_point:entry\",\"message\":\"Function called\",\"data\":{\"ifBlockIndex\":%d,\"ifBlockCount\":%d},\"timestamp\":%ld}\n", ifBlockIndex, ifBlockCount, (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    if (ifBlockIndex < 0 || ifBlockIndex >= ifBlockCount) {
        // #region agent log
        debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
        if (debug_log) {
            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"F\",\"location\":\"main.c:reposition_convergence_point:invalid_index\",\"message\":\"Invalid IF block index\",\"data\":{\"ifBlockIndex\":%d,\"ifBlockCount\":%d},\"timestamp\":%ld}\n", ifBlockIndex, ifBlockCount, (long)time(NULL));
            fclose(debug_log);
        }
        // #endregion
        return;
    }
    
    // Count nodes in each branch
    int trueCount = 0;
    int falseCount = 0;
    
    for (int i = 0; i < nodeCount; i++) {
        if (nodes[i].owningIfBlock == ifBlockIndex) {
            if (nodes[i].branchColumn < 0) {
                trueCount++;
            } else if (nodes[i].branchColumn > 0) {
                falseCount++;
            }
        }
    }
    
    // #region agent log
    debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"A\",\"location\":\"main.c:reposition_convergence_point:counts\",\"message\":\"Branch counts\",\"data\":{\"ifBlockIndex\":%d,\"trueCount\":%d,\"falseCount\":%d},\"timestamp\":%ld}\n", ifBlockIndex, trueCount, falseCount, (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    // If branches are equal length, don't move convergence
    if (trueCount == falseCount) {
        // #region agent log
        debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
        if (debug_log) {
            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"A\",\"location\":\"main.c:reposition_convergence_point:equal_exit\",\"message\":\"Branches equal, not moving\",\"data\":{\"trueCount\":%d,\"falseCount\":%d},\"timestamp\":%ld}\n", trueCount, falseCount, (long)time(NULL));
            fclose(debug_log);
        }
        // #endregion
        return;
    }
    
    // Find which branch is longest and get its last node
    int lastNodeIndex = -1;
    if (trueCount > falseCount) {
        // True branch (left) is longest
        lastNodeIndex = find_last_node_in_branch(ifBlockIndex, -1);
    } else {
        // False branch (right) is longest
        lastNodeIndex = find_last_node_in_branch(ifBlockIndex, 1);
    }
    
    // #region agent log
    debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"B\",\"location\":\"main.c:reposition_convergence_point:last_node\",\"message\":\"Found last node\",\"data\":{\"lastNodeIndex\":%d,\"longestBranch\":\"%s\"},\"timestamp\":%ld}\n", lastNodeIndex, (trueCount > falseCount ? "true" : "false"), (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    // Position convergence one grid cell below the last node of longest branch
    if (lastNodeIndex >= 0) {
        int convergeIdx = ifBlocks[ifBlockIndex].convergeNodeIndex;
        
        // #region agent log
        debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
        if (debug_log) {
            double oldY = (convergeIdx >= 0 && convergeIdx < nodeCount) ? nodes[convergeIdx].y : -999.0;
            double lastNodeY = nodes[lastNodeIndex].y;
            double newY = lastNodeY - GRID_CELL_SIZE;
            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"C,D\",\"location\":\"main.c:reposition_convergence_point:before_move\",\"message\":\"About to move convergence\",\"data\":{\"convergeIdx\":%d,\"oldY\":%.3f,\"lastNodeY\":%.3f,\"newY\":%.3f,\"valid\":%d},\"timestamp\":%ld}\n", convergeIdx, oldY, lastNodeY, newY, (convergeIdx >= 0 && convergeIdx < nodeCount), (long)time(NULL));
            fclose(debug_log);
        }
        // #endregion
        
        if (convergeIdx >= 0 && convergeIdx < nodeCount) {
            double oldConvergeY = nodes[convergeIdx].y;
            double newConvergeY = nodes[lastNodeIndex].y - GRID_CELL_SIZE;
            double deltaY = newConvergeY - oldConvergeY;
            
            nodes[convergeIdx].y = newConvergeY;
            
            // #region agent log
            debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"D,J\",\"location\":\"main.c:reposition_convergence_point:after_move\",\"message\":\"Convergence moved\",\"data\":{\"convergeIdx\":%d,\"oldY\":%.3f,\"newY\":%.3f,\"deltaY\":%.3f},\"timestamp\":%ld}\n", convergeIdx, oldConvergeY, nodes[convergeIdx].y, deltaY, (long)time(NULL));
                fclose(debug_log);
            }
            // #endregion
            
            // If convergence moved (delta != 0), move all nodes below it by the same amount
            // Only move nodes in main branch (branchColumn == 0) since those are below convergence
            if (fabs(deltaY) > 0.001) {  // Small epsilon to avoid floating point errors
                // #region agent log
                debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                if (debug_log) {
                    fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"J,K,L\",\"location\":\"main.c:reposition_convergence_point:move_below\",\"message\":\"Moving nodes below convergence\",\"data\":{\"convergeIdx\":%d,\"deltaY\":%.3f,\"oldConvergeY\":%.3f,\"direction\":\"%s\"},\"timestamp\":%ld}\n", convergeIdx, deltaY, oldConvergeY, (deltaY < 0 ? "down" : "up"), (long)time(NULL));
                    fclose(debug_log);
                }
                // #endregion
                
                for (int i = 0; i < nodeCount; i++) {
                    // Move nodes that are:
                    // 1. Below the old convergence position
                    // 2. In the main branch (branchColumn == 0)
                    // 3. Not the convergence point itself
                    if (i != convergeIdx && nodes[i].y < oldConvergeY && nodes[i].branchColumn == 0) {
                        double oldNodeY = nodes[i].y;
                        nodes[i].y = snap_to_grid_y(nodes[i].y + deltaY);
                        
                        // #region agent log
                        debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                        if (debug_log) {
                            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"converge-debug\",\"hypothesisId\":\"L\",\"location\":\"main.c:reposition_convergence_point:moved_node\",\"message\":\"Moved node below convergence\",\"data\":{\"nodeIdx\":%d,\"nodeType\":%d,\"oldY\":%.3f,\"newY\":%.3f,\"deltaY\":%.3f},\"timestamp\":%ld}\n", i, nodes[i].type, oldNodeY, nodes[i].y, deltaY, (long)time(NULL));
                            fclose(debug_log);
                        }
                        // #endregion
                    }
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
    if (to->branchColumn < 0) {
        // Target is in a left branch (negative column) = true branch
        return 0;
    } else if (to->branchColumn > 0) {
        // Target is in a right branch (positive column) = false branch
        return 1;
    } else {
        // Target is in main branch (convergence point)
        // Need to determine which side based on connection order
        int fromNode = connections[connIndex].fromNode;
        int connectionIndex = 0;
        for (int i = 0; i < connectionCount; i++) {
            if (connections[i].fromNode == fromNode) {
                if (i == connIndex) {
                    return connectionIndex;
                }
                connectionIndex++;
            }
        }
    }
    
    return -1;
}

// Insert IF block with branches in a connection
void insert_if_block_in_connection(int connIndex) {
    if (nodeCount + 2 >= MAX_NODES || connectionCount + 6 >= MAX_CONNECTIONS || ifBlockCount >= MAX_IF_BLOCKS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
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
    
    // #region agent log
    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"initial\",\"hypothesisId\":\"C,E\",\"location\":\"main.c:insert_if_block:if_created\",\"message\":\"IF block created\",\"data\":{\"ifNodeIndex\":%d,\"ifX\":%.3f,\"ifBranchColumn\":%d,\"fromBranchColumn\":%d},\"timestamp\":%ld}\n", ifNodeIndex, ifNode->x, ifNode->branchColumn, from->branchColumn, (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    // Create convergence point positioned 3 grid cells below IF block
    int convergeGridY = ifGridY - 3;
    FlowNode *convergeNode = &nodes[nodeCount];
    convergeNode->x = ifNode->x;  // Same X as IF block
    convergeNode->y = snap_to_grid_y(grid_to_world_y(convergeGridY));
    convergeNode->height = 0.15f;  // Small circle
    convergeNode->width = 0.15f;
    convergeNode->value[0] = '\0';
    convergeNode->type = NODE_CONVERGE;
    convergeNode->branchColumn = from->branchColumn;  // Same as IF block
    convergeNode->owningIfBlock = from->owningIfBlock;
    
    // #region agent log
    debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"initial\",\"hypothesisId\":\"E\",\"location\":\"main.c:insert_if_block:converge_created\",\"message\":\"Convergence created\",\"data\":{\"convergeNodeIndex\":%d,\"convergeX\":%.3f,\"convergeBranchColumn\":%d},\"timestamp\":%ld}\n", nodeCount, convergeNode->x, convergeNode->branchColumn, (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
    int convergeNodeIndex = nodeCount;
    nodeCount++;
    
    // Push the "to" node and all nodes below the convergence point further down
    // Need to make room for IF block + convergence point (4 grid cells total)
    double gridSpacing = GRID_CELL_SIZE * 4;  // 4 grid cells
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
    
    int currentIfIndex = ifBlockCount;
    ifBlockCount++;
    
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
                if (export_to_code(filename, langName, nodes, nodeCount, connections, connectionCount)) {
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
                        } else {
                            insert_node_in_connection(popupMenu.connectionIndex, selectedType);
                        }
                    } else if (popupMenu.type == MENU_TYPE_NODE) {
                        // Handle node menu actions
                        if (nodeMenuItems[clickedItem].action == 0) {
                            // Delete action
                            delete_node(popupMenu.nodeIndex);
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
    // #region agent log
    static int log_count = 0;
    if (log_count < 5) {
        FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
        if (debug_log) {
            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"initial\",\"hypothesisId\":\"C\",\"location\":\"main.c:drawFlowNode\",\"message\":\"Drawing node\",\"data\":{\"nodeIndex\":%d,\"type\":%d,\"x\":%.3f,\"y\":%.3f,\"branchColumn\":%d},\"timestamp\":%ld}\n", log_count, n->type, n->x, n->y, n->branchColumn, (long)time(NULL));
            fclose(debug_log);
            log_count++;
        }
    }
    // #endregion
    
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
        
        // Draw value text if present centered in the block
        if (n->value[0] != '\0') {
            float fontSize = n->height * 0.3f;
            // Calculate text width and center it in the block
            float textWidth = get_text_width(n->value, fontSize);
            float textX = n->x - textWidth * 0.5f;  // Center the text
            float textY = n->y;
            draw_text(textX, textY, n->value, fontSize, 0.0f, 0.0f, 0.0f);
        }
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
        
        // Draw value text if present centered in the block
        if (n->value[0] != '\0') {
            float fontSize = n->height * 0.3f;
            // Calculate text width and center it in the block
            float textWidth = get_text_width(n->value, fontSize);
            float textX = n->x - textWidth * 0.5f;  // Center the text
            float textY = n->y;
            draw_text(textX, textY, n->value, fontSize, 0.0f, 0.0f, 0.0f);
        }
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
    }
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
        const FlowNode *from = &nodes[connections[i].fromNode];
        const FlowNode *to   = &nodes[connections[i].toNode];
        
        // #region agent log
        static int conn_log_count = 0;
        if (conn_log_count < 15) {
            FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"F,G,H\",\"location\":\"main.c:drawFlowchart:draw_connection\",\"message\":\"Drawing connection\",\"data\":{\"connIndex\":%d,\"fromNode\":%d,\"toNode\":%d,\"fromType\":%d,\"toType\":%d,\"fromX\":%.3f,\"toX\":%.3f,\"fromBranchCol\":%d,\"toBranchCol\":%d},\"timestamp\":%ld}\n", i, connections[i].fromNode, connections[i].toNode, from->type, to->type, from->x, to->x, from->branchColumn, to->branchColumn, (long)time(NULL));
                fclose(debug_log);
                conn_log_count++;
            }
        }
        // #endregion
        
        // Highlight hovered connection
        if (i == hoveredConnection) {
            glColor3f(1.0f, 0.8f, 0.0f);  // Bright orange/yellow glow
        } else {
            glColor3f(0.0f, 0.6f, 0.8f);  // Normal cyan
        }
        
        // Determine if this is an IF branch connection
        int branchType = get_if_branch_type(i);
        
        // #region agent log
        if (conn_log_count < 15) {
            FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
            if (debug_log) {
                fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"I\",\"location\":\"main.c:drawFlowchart:branch_type\",\"message\":\"Branch type determined\",\"data\":{\"connIndex\":%d,\"branchType\":%d},\"timestamp\":%ld}\n", i, branchType, (long)time(NULL));
                fclose(debug_log);
            }
        }
        // #endregion
        
        if (branchType == 0) {
            // True branch (exits left side of IF block)
            float x1 = (float)(from->x - from->width * 0.5f);  // Left side of diamond
            float y1 = (float)from->y;  // Middle of diamond
            
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
            float branchX = (float)from->x - 1.0f;  // Branch column center (IF center - 1.0)
            
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
            float branchX = (float)from->x + 1.0f;  // Branch column center (IF center + 1.0)
            
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
            float y1 = (float)(from->y - from->height * 0.5f);
            float x2 = (float)to->x;
            float y2 = (float)(to->y + to->height * 0.5f);
            
            // Special handling for connections TO convergence point from branch blocks
            if (to->type == NODE_CONVERGE && from->branchColumn != 0) {
                // Source is in a branch, target is convergence
                // Route: from block bottom -> stay in branch column -> horizontal to convergence side
                float branchX = (float)from->x;  // Stay in branch column
                
                // Determine which side of convergence to connect to
                float convergeX;
                if (from->branchColumn < 0) {
                    // True branch - connect to left side of convergence
                    convergeX = (float)(to->x - to->width * 0.5f);
                } else {
                    // False branch - connect to right side of convergence
                    convergeX = (float)(to->x + to->width * 0.5f);
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
                // #region agent log
                if (conn_log_count < 15) {
                    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
                    if (debug_log) {
                        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"K,L\",\"location\":\"main.c:drawFlowchart:normal_connection\",\"message\":\"Normal connection routing\",\"data\":{\"connIndex\":%d,\"x1\":%.3f,\"y1\":%.3f,\"x2\":%.3f,\"y2\":%.3f,\"sameX\":%d,\"sameBranch\":%d},\"timestamp\":%ld}\n", i, x1, y1, x2, y2, (fabs(x1 - x2) < 0.001f ? 1 : 0), sameBranch, (long)time(NULL));
                        fclose(debug_log);
                    }
                }
                // #endregion
                
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
    
    // #region agent log
    int debug_width, debug_height;
    glfwGetWindowSize(window, &debug_width, &debug_height);
    float debug_aspect = (float)debug_width / (float)debug_height;
    FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
    if (debug_log) {
        fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"initial\",\"hypothesisId\":\"B\",\"location\":\"main.c:2131\",\"message\":\"Menu dimensions before scaling\",\"data\":{\"menuMinWidth\":%.3f,\"menuItemHeight\":%.3f,\"aspectRatio\":%.3f},\"timestamp\":%ld}\n", menuMinWidth, menuItemHeight, debug_aspect, (long)time(NULL));
        fclose(debug_log);
    }
    // #endregion
    
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
        
        // #region agent log
        int debug_width, debug_height;
        glfwGetWindowSize(window, &debug_width, &debug_height);
        float debug_aspect = (float)debug_width / (float)debug_height;
        FILE* debug_log = fopen("/home/mm1yscttck/Desktop/glfw_test/.cursor/debug.log", "a");
        if (debug_log) {
            fprintf(debug_log, "{\"sessionId\":\"debug-session\",\"runId\":\"post-fix\",\"hypothesisId\":\"A\",\"location\":\"main.c:2415\",\"message\":\"Window size and aspect ratio after fix\",\"data\":{\"width\":%d,\"height\":%d,\"aspectRatio\":%.3f},\"timestamp\":%ld}\n", debug_width, debug_height, debug_aspect, (long)time(NULL));
            fclose(debug_log);
        }
        // #endregion
        
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
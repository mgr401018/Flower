#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#define TINYFD_NOLIB
#include "imports/tinyfiledialogs.h"
#include "src/text_renderer.h"

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

// Circular button configuration (top-left corner, vertically aligned)
const float buttonRadius = 0.04f;
const float buttonX = -0.95f;  // Fixed X position for both buttons
const float saveButtonY = 0.9f;   // Blue save button
const float loadButtonY = 0.8f;   // Yellow load button

// Flowchart node and connection data
#define MAX_NODES 100
#define MAX_CONNECTIONS 200

typedef enum {
    NODE_NORMAL = 0,
    NODE_START  = 1,
    NODE_END    = 2
} NodeType;

typedef struct {
    double x;
    double y;
    float width;
    float height;
    int value;
    NodeType type;
} FlowNode;

typedef struct {
    int fromNode;
    int toNode;
} Connection;

FlowNode nodes[MAX_NODES];
int nodeCount = 0;

Connection connections[MAX_CONNECTIONS];
int connectionCount = 0;

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
const float menuItemHeight = 0.15f;
const float menuItemSpacing = 0.02f;
const float menuPadding = 0.04f;  // Padding on left and right of text (in normalized coordinates)
const float menuMinWidth = 0.8f;  // Minimum menu width in normalized coordinates (~4 cm on typical screen)

// Menu items
#define MAX_MENU_ITEMS 10
typedef struct {
    const char* text;
    NodeType nodeType;  // Node type to insert when clicked
} MenuItem;

// Connection menu items (for inserting nodes)
MenuItem connectionMenuItems[] = {
    {"Process Node", NODE_NORMAL},
    {"Placeholder", NODE_NORMAL}
};
int connectionMenuItemCount = 2;

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

// Cursor position callback
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    // Convert screen coordinates to OpenGL coordinates (-1 to 1)
    // Don't apply scroll offset here - we'll handle it where needed
    cursorX = (xpos / width) * 2.0 - 1.0;
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
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    float aspectRatio = (float)width / (float)height;
    
    float dx = (cursorX - buttonX) * aspectRatio;
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
        
        float x1 = (float)from->x;
        float y1 = (float)(from->y - from->height * 0.5f);
        float x2 = (float)to->x;
        float y2 = (float)(to->y + to->height * 0.5f);
        
        float dist;
        
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
    fprintf(file, "# Node data: x y width height value type\n");
    for (int i = 0; i < nodeCount; i++) {
        fprintf(file, "%.6f %.6f %.6f %.6f %d %d\n",
                nodes[i].x, nodes[i].y, nodes[i].width, nodes[i].height,
                nodes[i].value, (int)nodes[i].type);
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
        if (fscanf(file, "%lf %lf %f %f %d %d",
                   &nodes[i].x, &nodes[i].y, &nodes[i].width, &nodes[i].height,
                   &nodes[i].value, &nodeType) != 6) {
            fprintf(stderr, "Error reading node data\n");
            fclose(file);
            return;
        }
        // Snap loaded nodes to grid
        nodes[i].x = snap_to_grid_x(nodes[i].x);
        nodes[i].y = snap_to_grid_y(nodes[i].y);
        nodes[i].type = (NodeType)nodeType;
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
    
    fclose(file);
    printf("Flowchart loaded from %s (%d nodes, %d connections)\n", 
           filename, nodeCount, connectionCount);
}
// Delete a node and reconnect adjacent nodes automatically
void delete_node(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= nodeCount) {
        return;
    }
    
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
    for (int i = 0; i < incomingCount; i++) {
        int incomingConn = incomingConnections[i];
        int fromNode = connections[incomingConn].fromNode;
        
        for (int j = 0; j < outgoingCount; j++) {
            int outgoingConn = outgoingConnections[j];
            int toNode = connections[outgoingConn].toNode;
            
            // Skip if trying to connect to itself
            if (fromNode == toNode) continue;
            
            // Check if connection already exists
            bool connectionExists = false;
            for (int k = 0; k < connectionCount; k++) {
                if (connections[k].fromNode == fromNode && connections[k].toNode == toNode) {
                    connectionExists = true;
                    break;
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
    
    // Apply movements in order from top to bottom
    for (int i = 0; i < nodesToMoveCount; i++) {
        int nodeIdx = nodesToMove[i];
        double deltaY = nodePositionDeltas[nodeIdx];
        double originalY = originalYPositions[nodeIdx];
        
        // Move this node and snap to grid
        nodes[nodeIdx].y = snap_to_grid_y(originalY + deltaY);
        
        // Move all nodes below this one (based on original positions) up by the same amount
        // Use original positions to determine what's below, but apply to current positions
        for (int j = 0; j < nodeCount; j++) {
            if (j != nodeIdx && j != nodeIndex && originalYPositions[j] < originalY) {
                nodes[j].y = snap_to_grid_y(nodes[j].y + deltaY);
            }
        }
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
}

void insert_node_in_connection(int connIndex, NodeType nodeType) {
    if (nodeCount >= MAX_NODES || connectionCount >= MAX_CONNECTIONS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
    // Store the original Y position of the "to" node before we modify anything
    double originalToY = to->y;
    
    // Calculate grid positions
    int fromGridX = world_to_grid_x(from->x);
    int fromGridY = world_to_grid_y(from->y);
    int toGridY = world_to_grid_y(to->y);
    
    // Create new node positioned one grid cell below the "from" node
    int newGridY = fromGridY - 1;
    FlowNode *newNode = &nodes[nodeCount];
    newNode->x = snap_to_grid_x(from->x);  // Keep same X grid position
    newNode->y = snap_to_grid_y(grid_to_world_y(newGridY));  // One grid cell below
    newNode->width = 0.35f;
    newNode->height = 0.22f;
    newNode->value = nodeCount;
    newNode->type = nodeType;
    int newNodeIndex = nodeCount;
    nodeCount++;
    
    // Push the "to" node and all nodes below it further down by one grid cell
    double gridSpacing = GRID_CELL_SIZE;
    for (int i = 0; i < nodeCount; ++i) {
        if (nodes[i].y <= originalToY && i != newNodeIndex) {
            nodes[i].y -= gridSpacing;
            // Snap to grid after moving
            nodes[i].y = snap_to_grid_y(nodes[i].y);
        }
    }
    
    // Replace old connection with two new ones
    connections[connIndex].fromNode = oldConn.fromNode;
    connections[connIndex].toNode = newNodeIndex;
    
    connections[connectionCount].fromNode = newNodeIndex;
    connections[connectionCount].toNode = oldConn.toNode;
    connectionCount++;
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;    // Mark as intentionally unused
    
    // Calculate world-space cursor position (accounting for scroll)
    double worldCursorX = cursorX - scrollOffsetX;
    double worldCursorY = cursorY - scrollOffsetY;
    
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Check if clicking on buttons (buttons are in screen space, not world space)
        if (cursor_over_button(buttonX, saveButtonY, window)) {
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
        if (cursor_over_button(buttonX, loadButtonY, window)) {
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
                        insert_node_in_connection(popupMenu.connectionIndex, connectionMenuItems[clickedItem].nodeType);
                    } else if (popupMenu.type == MENU_TYPE_NODE) {
                        // Handle node menu actions
                        if (nodeMenuItems[clickedItem].action == 0) {
                            // Delete action
                            delete_node(popupMenu.nodeIndex);
                        } else if (nodeMenuItems[clickedItem].action == 1) {
                            // Value action - do nothing for now
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



void drawPopupMenu();  // Forward declaration

void drawFlowNode(const FlowNode *n) {
    // Node body color based on type
    if (n->type == NODE_START) {
        glColor3f(0.3f, 0.9f, 0.3f); // green for start
    } else if (n->type == NODE_END) {
        glColor3f(0.9f, 0.3f, 0.3f); // red for end
    } else {
        glColor3f(0.95f, 0.9f, 0.25f); // yellow for normal
    }
    
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

    float r = 0.03f;
    float cx;
    float cy;

    glColor3f(0.1f, 0.1f, 0.1f);

    // Input connector (top) — not drawn for start nodes
    if (n->type != NODE_START) {
        cx = (float)n->x;
        cy = (float)(n->y + n->height * 0.5f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 20; ++i) {
            float a = (float)i / 20.0f * 6.2831853f;
            glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
        }
        glEnd();
    }

    // Output connector (bottom) — not drawn for end nodes
    if (n->type != NODE_END) {
        cx = (float)n->x;
        cy = (float)(n->y - n->height * 0.5f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 20; ++i) {
            float a = (float)i / 20.0f * 6.2831853f;
            glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
        }
        glEnd();
    }
}

void drawFlowchart(void) {
    // Apply scroll transformation (both horizontal and vertical)
    glPushMatrix();
    glTranslatef((float)scrollOffsetX, (float)scrollOffsetY, 0.0f);
    
    // Draw connections as right-angle L-shapes
    glLineWidth(3.0f);
    for (int i = 0; i < connectionCount; ++i) {
        const FlowNode *from = &nodes[connections[i].fromNode];
        const FlowNode *to   = &nodes[connections[i].toNode];
        float x1 = (float)from->x;
        float y1 = (float)(from->y - from->height * 0.5f);
        float x2 = (float)to->x;
        float y2 = (float)(to->y + to->height * 0.5f);
        
        // Highlight hovered connection
        if (i == hoveredConnection) {
            glColor3f(1.0f, 0.8f, 0.0f);  // Bright orange/yellow glow
        } else {
            glColor3f(0.0f, 0.6f, 0.8f);  // Normal cyan
        }
        
        // Draw right-angle connection: horizontal then vertical
        // Handle cases: same X (vertical only), same Y (horizontal only), different (L-shape)
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
    glLineWidth(1.0f);

    // Draw nodes
    for (int i = 0; i < nodeCount; ++i) {
        drawFlowNode(&nodes[i]);
    }
    
    glPopMatrix();
    
    // Draw popup menu in screen space (not affected by scroll)
    drawPopupMenu();
}

void drawPopupMenu() {
    if (!popupMenu.active) return;
    
    // Menu is stored in screen space (not affected by scroll)
    float menuX = (float)popupMenu.x;
    float menuY = (float)popupMenu.y;
    
    // Calculate font size
    float fontSize = menuItemHeight * 0.5f;
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
    
    bool hoveringSave = cursor_over_button(buttonX, saveButtonY, window);
    bool hoveringLoad = cursor_over_button(buttonX, loadButtonY, window);
    
    // Draw save button (blue)
    glColor3f(0.2f, 0.4f, 0.9f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX, saveButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX + cosf(angle) * buttonRadius / aspectRatio, 
                   saveButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw save button border
    glColor3f(0.1f, 0.2f, 0.5f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX + cosf(angle) * buttonRadius / aspectRatio, 
                   saveButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw load button (yellow)
    glColor3f(0.95f, 0.9f, 0.25f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(buttonX, loadButtonY);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX + cosf(angle) * buttonRadius / aspectRatio, 
                   loadButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw load button border
    glColor3f(0.5f, 0.5f, 0.1f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 20; ++i) {
        float angle = (float)i / 20.0f * 6.2831853f;
        glVertex2f(buttonX + cosf(angle) * buttonRadius / aspectRatio, 
                   loadButtonY + sinf(angle) * buttonRadius);
    }
    glEnd();
    
    // Draw hover labels
    if (hoveringSave) {
        // Draw label background
        float labelX = buttonX + buttonRadius / aspectRatio + 0.05f;
        float labelY = saveButtonY;
        float labelWidth = 0.15f;
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
        float fontSize = labelHeight * 0.8f;  // Use 80% of label height for better visibility
        float textWidth = get_text_width("SAVE", fontSize);
        // Center horizontally:
        float textX = labelX + (labelWidth - textWidth) * 0.25f;
        // Center vertically:
        float textY = labelY - fontSize * 0.25f;
        draw_text(textX, textY, "SAVE", fontSize, 1.0f, 1.0f, 1.0f);
    }
    
    if (hoveringLoad) {
        // Draw label background
        float labelX = buttonX + buttonRadius / aspectRatio + 0.05f;
        float labelY = loadButtonY;
        float labelWidth = 0.15f;
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
        float fontSize = labelHeight * 0.8f;  // Use 80% of label height for better visibility
        float textWidth = get_text_width("LOAD", fontSize);
        // Center horizontally
        float textX = labelX + (labelWidth - textWidth) *0.25f;
        float textY = labelY - fontSize * 0.25f;
        // Center vertically
        draw_text(textX, textY, "LOAD", fontSize, 1.0f, 1.0f, 1.0f);
    }
}

void initialize_flowchart() {
    // Create START node at grid position (0, 0)
    nodes[0].x = grid_to_world_x(0);
    nodes[0].y = grid_to_world_y(0);
    nodes[0].width = 0.35f;
    nodes[0].height = 0.22f;
    nodes[0].value = 0;
    nodes[0].type = NODE_START;
    
    // Create END node at grid position (0, -1)
    nodes[1].x = grid_to_world_x(0);
    nodes[1].y = grid_to_world_y(-1);
    nodes[1].width = 0.35f;
    nodes[1].height = 0.22f;
    nodes[1].value = 1;
    nodes[1].type = NODE_END;
    
    nodeCount = 2;
    
    // Connect START to END
    connections[0].fromNode = 0;
    connections[0].toNode = 1;
    connectionCount = 1;
}

int main(void) {
    GLFWwindow* window;

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    window = glfwCreateWindow(800, 600, "Flowchart Editor", NULL, NULL);
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
    if (!init_text_renderer(NULL)) {  // NULL = use embedded font
        fprintf(stderr, "Warning: Failed to initialize text renderer\n");
    }
    
    // Initialize with connected START and END nodes
    initialize_flowchart();

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Update hovered connection (use world-space cursor)
        double worldCursorX = cursorX - scrollOffsetX;
        double worldCursorY = cursorY - scrollOffsetY;
        hoveredConnection = hit_connection(worldCursorX, worldCursorY, 0.05f);
        
        drawFlowchart();
        
        // Draw buttons in screen space (not affected by scroll)
        drawButtons(window);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    cleanup_text_renderer();
    glfwTerminate();
    return 0;
}
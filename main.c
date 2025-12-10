// #include <GLFW/glfw3.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <stdbool.h>
// #include <math.h>

// // Global variables for cursor position
// double cursorX = 0.0;
// double cursorY = 0.0;

// // Hovered connection tracking
// int hoveredConnection = -1;

// // Scroll offset for vertical panning
// double scrollOffsetY = 0.0;

// // Flowchart node and connection data
// #define MAX_NODES 100
// #define MAX_CONNECTIONS 200

// typedef enum {
//     NODE_NORMAL = 0,
//     NODE_START  = 1,
//     NODE_END    = 2
// } NodeType;

// typedef struct {
//     double x;
//     double y;
//     float width;
//     float height;
//     int value;
//     NodeType type;
// } FlowNode;

// typedef struct {
//     int fromNode;
//     int toNode;
// } Connection;

// FlowNode nodes[MAX_NODES];
// int nodeCount = 0;

// Connection connections[MAX_CONNECTIONS];
// int connectionCount = 0;

// // Popup menu state
// typedef struct {
//     bool active;
//     double x;
//     double y;
//     int connectionIndex;  // which connection was clicked
// } PopupMenu;

// PopupMenu popupMenu = {false, 0.0, 0.0, -1};

// // Menu item dimensions
// const float menuItemWidth = 0.35f;
// const float menuItemHeight = 0.15f;
// const float menuItemSpacing = 0.02f;

// // Cursor position callback
// void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
//     int width, height;
//     glfwGetWindowSize(window, &width, &height);
    
//     // Convert screen coordinates to OpenGL coordinates (-1 to 1)
//     // Don't apply scroll offset here - we'll handle it where needed
//     cursorX = (xpos / width) * 2.0 - 1.0;
//     cursorY = -((ypos / height) * 2.0 - 1.0);
// }

// // Scroll callback for vertical panning
// void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
//     (void)window;   // Mark as intentionally unused
//     (void)xoffset;  // Mark as intentionally unused
//     scrollOffsetY += yoffset * 0.1;  // Smooth scrolling factor
// }

// // Check if cursor is over a menu item
// bool cursor_over_menu_item(double menuX, double menuY, int itemIndex) {
//     float itemY = menuY - itemIndex * (menuItemHeight + menuItemSpacing);
//     return cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
//            cursorY <= itemY && cursorY >= itemY - menuItemHeight;
// }

// // Find which connection (line) the cursor is near
// static int hit_connection(double x, double y, float threshold) {
//     for (int i = 0; i < connectionCount; ++i) {
//         const FlowNode *from = &nodes[connections[i].fromNode];
//         const FlowNode *to   = &nodes[connections[i].toNode];
        
//         float x1 = (float)from->x;
//         float y1 = (float)(from->y - from->height * 0.5f);
//         float x2 = (float)to->x;
//         float y2 = (float)(to->y + to->height * 0.5f);
        
//         // Calculate distance from point to line segment
//         float dx = x2 - x1;
//         float dy = y2 - y1;
//         float len2 = dx*dx + dy*dy;
        
//         if (len2 < 0.0001f) continue;
        
//         float t = ((x - x1) * dx + (y - y1) * dy) / len2;
//         t = fmax(0.0f, fmin(1.0f, t));
        
//         float projX = x1 + t * dx;
//         float projY = y1 + t * dy;
        
//         float dist = sqrt((x - projX)*(x - projX) + (y - projY)*(y - projY));
        
//         if (dist < threshold) {
//             return i;
//         }
//     }
//     return -1;
// }

// // Insert a node into an existing connection
// void insert_node_in_connection(int connIndex, NodeType nodeType) {
//     if (nodeCount >= MAX_NODES || connectionCount >= MAX_CONNECTIONS) {
//         return;
//     }
    
//     Connection oldConn = connections[connIndex];
//     FlowNode *from = &nodes[oldConn.fromNode];
//     FlowNode *to = &nodes[oldConn.toNode];
    
//     // Calculate vertical spacing - place new node between from and to
//     double spacing = 0.4;  // Distance between nodes
    
//     // Store the original Y position of the "to" node before we modify anything
//     double originalToY = to->y;
    
//     // Create new node positioned vertically between from and to
//     FlowNode *newNode = &nodes[nodeCount];
//     newNode->x = from->x;  // Keep same X position for vertical flow
//     newNode->y = (from->y + to->y) / 2.0;  // Midpoint vertically
//     newNode->width = 0.35f;
//     newNode->height = 0.22f;
//     newNode->value = nodeCount;
//     newNode->type = nodeType;
//     int newNodeIndex = nodeCount;
//     nodeCount++;
    
//     // Push the "to" node and all nodes below it further down
//     // Use the ORIGINAL position for comparison
//     double pushAmount = spacing;
//     for (int i = 0; i < nodeCount; ++i) {
//         if (nodes[i].y <= originalToY && i != newNodeIndex) {
//             nodes[i].y -= pushAmount;
//         }
//     }
    
//     // Replace old connection with two new ones
//     connections[connIndex].fromNode = oldConn.fromNode;
//     connections[connIndex].toNode = newNodeIndex;
    
//     connections[connectionCount].fromNode = newNodeIndex;
//     connections[connectionCount].toNode = oldConn.toNode;
//     connectionCount++;
// }

// // Mouse button callback
// void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
//     (void)window;  // Mark as intentionally unused
//     (void)mods;    // Mark as intentionally unused
    
//     // Calculate world-space cursor position (accounting for scroll)
//     double worldCursorY = cursorY - scrollOffsetY;
    
//     if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
//         // Check if we're clicking on a connection (use world-space coordinates)
//         int connIndex = hit_connection(cursorX, worldCursorY, 0.05f);
        
//         if (connIndex >= 0) {
//             // Open popup menu at cursor position (store world-space coordinates)
//             popupMenu.active = true;
//             popupMenu.x = cursorX;
//             popupMenu.y = worldCursorY;
//             popupMenu.connectionIndex = connIndex;
//         } else {
//             // Close menu if clicking elsewhere
//             popupMenu.active = false;
//         }
//     }
    
//     if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
//         if (popupMenu.active) {
//             // Check which menu item was clicked (menu is in world space)
//             float menuX = (float)popupMenu.x;
//             float menuY = (float)popupMenu.y;
            
//             // But cursor checking needs to compare with world-space cursor
//             if (cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
//                 worldCursorY <= menuY && worldCursorY >= menuY - menuItemHeight) {
//                 // Insert normal node
//                 insert_node_in_connection(popupMenu.connectionIndex, NODE_NORMAL);
//                 popupMenu.active = false;
//             }
//             // Add more menu items here as needed
//             else {
//                 // Clicked outside menu, close it
//                 popupMenu.active = false;
//             }
//         }
//     }
// }



// void drawFlowNode(const FlowNode *n) {
//     // Node body color based on type
//     if (n->type == NODE_START) {
//         glColor3f(0.3f, 0.9f, 0.3f); // green for start
//     } else if (n->type == NODE_END) {
//         glColor3f(0.9f, 0.3f, 0.3f); // red for end
//     } else {
//         glColor3f(0.95f, 0.9f, 0.25f); // yellow for normal
//     }
    
//     glBegin(GL_QUADS);
//     glVertex2f(n->x - n->width * 0.5f, n->y + n->height * 0.5f);
//     glVertex2f(n->x + n->width * 0.5f, n->y + n->height * 0.5f);
//     glVertex2f(n->x + n->width * 0.5f, n->y - n->height * 0.5f);
//     glVertex2f(n->x - n->width * 0.5f, n->y - n->height * 0.5f);
//     glEnd();

//     // Border
//     glColor3f(0.2f, 0.2f, 0.0f);
//     glBegin(GL_LINE_LOOP);
//     glVertex2f(n->x - n->width * 0.5f, n->y + n->height * 0.5f);
//     glVertex2f(n->x + n->width * 0.5f, n->y + n->height * 0.5f);
//     glVertex2f(n->x + n->width * 0.5f, n->y - n->height * 0.5f);
//     glVertex2f(n->x - n->width * 0.5f, n->y - n->height * 0.5f);
//     glEnd();

//     float r = 0.03f;
//     float cx;
//     float cy;

//     glColor3f(0.1f, 0.1f, 0.1f);

//     // Input connector (top) — not drawn for start nodes
//     if (n->type != NODE_START) {
//         cx = (float)n->x;
//         cy = (float)(n->y + n->height * 0.5f);
//         glBegin(GL_TRIANGLE_FAN);
//         glVertex2f(cx, cy);
//         for (int i = 0; i <= 20; ++i) {
//             float a = (float)i / 20.0f * 6.2831853f;
//             glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
//         }
//         glEnd();
//     }

//     // Output connector (bottom) — not drawn for end nodes
//     if (n->type != NODE_END) {
//         cx = (float)n->x;
//         cy = (float)(n->y - n->height * 0.5f);
//         glBegin(GL_TRIANGLE_FAN);
//         glVertex2f(cx, cy);
//         for (int i = 0; i <= 20; ++i) {
//             float a = (float)i / 20.0f * 6.2831853f;
//             glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
//         }
//         glEnd();
//     }
// }

// void drawFlowchart(void) {
//     // Apply scroll transformation
//     glPushMatrix();
//     glTranslatef(0.0f, (float)scrollOffsetY, 0.0f);
    
//     // Draw connections
//     glLineWidth(3.0f);
//     for (int i = 0; i < connectionCount; ++i) {
//         const FlowNode *from = &nodes[connections[i].fromNode];
//         const FlowNode *to   = &nodes[connections[i].toNode];
//         float x1 = (float)from->x;
//         float y1 = (float)(from->y - from->height * 0.5f);
//         float x2 = (float)to->x;
//         float y2 = (float)(to->y + to->height * 0.5f);
        
//         // Highlight hovered connection
//         if (i == hoveredConnection) {
//             glColor3f(1.0f, 0.8f, 0.0f);  // Bright orange/yellow glow
//         } else {
//             glColor3f(0.0f, 0.6f, 0.8f);  // Normal cyan
//         }
        
//         glBegin(GL_LINES);
//         glVertex2f(x1, y1);
//         glVertex2f(x2, y2);
//         glEnd();
//     }
//     glLineWidth(1.0f);

//     // Draw nodes
//     for (int i = 0; i < nodeCount; ++i) {
//         drawFlowNode(&nodes[i]);
//     }
    
//     // Draw popup menu within the same transform (so it scrolls with content)
//     drawPopupMenu();
    
//     glPopMatrix();
// }

// void drawPopupMenu() {
//     if (!popupMenu.active) return;
    
//     // Menu is stored in world space, so it moves with the scroll
//     float menuX = (float)popupMenu.x;
//     float menuY = (float)popupMenu.y;
    
//     // Draw menu item background
//     glColor3f(0.2f, 0.2f, 0.25f);
//     glBegin(GL_QUADS);
//     glVertex2f(menuX, menuY);
//     glVertex2f(menuX + menuItemWidth, menuY);
//     glVertex2f(menuX + menuItemWidth, menuY - menuItemHeight);
//     glVertex2f(menuX, menuY - menuItemHeight);
//     glEnd();
    
//     // Draw menu item border
//     glColor3f(0.8f, 0.8f, 0.8f);
//     glBegin(GL_LINE_LOOP);
//     glVertex2f(menuX, menuY);
//     glVertex2f(menuX + menuItemWidth, menuY);
//     glVertex2f(menuX + menuItemWidth, menuY - menuItemHeight);
//     glVertex2f(menuX, menuY - menuItemHeight);
//     glEnd();
    
//     // Highlight if hovering (compare with world-space cursor)
//     double worldCursorY = cursorY - scrollOffsetY;
//     bool hovering = cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
//                     worldCursorY <= menuY && worldCursorY >= menuY - menuItemHeight;
    
//     if (hovering) {
//         glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
//         glEnable(GL_BLEND);
//         glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//         glBegin(GL_QUADS);
//         glVertex2f(menuX, menuY);
//         glVertex2f(menuX + menuItemWidth, menuY);
//         glVertex2f(menuX + menuItemWidth, menuY - menuItemHeight);
//         glVertex2f(menuX, menuY - menuItemHeight);
//         glEnd();
//         glDisable(GL_BLEND);
//     }
    
//     // Draw text "Process Node"
//     glColor3f(1.0f, 1.0f, 1.0f);
//     float textY = menuY - menuItemHeight * 0.5f;
//     float x = menuX + 0.02f;
//     float h = menuItemHeight * 0.3f;
//     float w = h * 0.5f;
    
//     glBegin(GL_LINES);
//     // "P"
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x + w, textY);
//     glVertex2f(x + w, textY);
//     glVertex2f(x, textY);
//     x += w * 1.3f;
    
//     // "R"
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x + w, textY);
//     glVertex2f(x + w, textY);
//     glVertex2f(x, textY);
//     glVertex2f(x, textY);
//     glVertex2f(x + w, textY - h * 0.5f);
//     x += w * 1.3f;
    
//     // "O"
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     x += w * 1.3f;
    
//     // "C"
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
//     x += w * 1.3f;
    
//     // "E"
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x, textY);
//     glVertex2f(x + w * 0.8f, textY);
//     glVertex2f(x, textY - h * 0.5f);
//     glVertex2f(x + w, textY - h * 0.5f);
//     x += w * 1.3f;
    
//     // "S"
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY);
//     glVertex2f(x, textY);
//     glVertex2f(x + w, textY);
//     glVertex2f(x + w, textY);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
//     x += w * 1.3f;
    
//     // "S" (again)
//     glVertex2f(x + w, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY + h * 0.5f);
//     glVertex2f(x, textY);
//     glVertex2f(x, textY);
//     glVertex2f(x + w, textY);
//     glVertex2f(x + w, textY);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x + w, textY - h * 0.5f);
//     glVertex2f(x, textY - h * 0.5f);
    
//     glEnd();
// }

// void initialize_flowchart() {
//     // Create START node
//     nodes[0].x = 0.0;
//     nodes[0].y = 0.6;
//     nodes[0].width = 0.35f;
//     nodes[0].height = 0.22f;
//     nodes[0].value = 0;
//     nodes[0].type = NODE_START;
    
//     // Create END node with more spacing
//     nodes[1].x = 0.0;
//     nodes[1].y = -0.2;
//     nodes[1].width = 0.35f;
//     nodes[1].height = 0.22f;
//     nodes[1].value = 1;
//     nodes[1].type = NODE_END;
    
//     nodeCount = 2;
    
//     // Connect START to END
//     connections[0].fromNode = 0;
//     connections[0].toNode = 1;
//     connectionCount = 1;
// }

// int main(void) {
//     GLFWwindow* window;

//     if (!glfwInit()) {
//         fprintf(stderr, "Failed to initialize GLFW\n");
//         return -1;
//     }

//     window = glfwCreateWindow(800, 600, "Flowchart Editor", NULL, NULL);
//     if (!window) {
//         fprintf(stderr, "Failed to create GLFW window\n");
//         glfwTerminate();
//         return -1;
//     }

//     glfwMakeContextCurrent(window);
//     glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

//     glfwSetCursorPosCallback(window, cursor_position_callback);
//     glfwSetMouseButtonCallback(window, mouse_button_callback);
//     glfwSetScrollCallback(window, scroll_callback);
    
//     // Initialize with connected START and END nodes
//     initialize_flowchart();

//     while (!glfwWindowShouldClose(window)) {
//         glClear(GL_COLOR_BUFFER_BIT);
        
//         // Update hovered connection (use world-space cursor)
//         double worldCursorY = cursorY - scrollOffsetY;
//         hoveredConnection = hit_connection(cursorX, worldCursorY, 0.05f);
        
//         drawFlowchart();

//         glfwSwapBuffers(window);
//         glfwPollEvents();
//     }

//     glfwTerminate();
//     return 0;
// }

#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

// Global variables for cursor position
double cursorX = 0.0;
double cursorY = 0.0;

// Hovered connection tracking
int hoveredConnection = -1;

// Scroll offset for vertical panning
double scrollOffsetY = 0.0;

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

// Popup menu state
typedef struct {
    bool active;
    double x;
    double y;
    int connectionIndex;  // which connection was clicked
} PopupMenu;

PopupMenu popupMenu = {false, 0.0, 0.0, -1};

// Menu item dimensions
const float menuItemWidth = 0.35f;
const float menuItemHeight = 0.15f;
const float menuItemSpacing = 0.02f;

// Cursor position callback
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    // Convert screen coordinates to OpenGL coordinates (-1 to 1)
    // Don't apply scroll offset here - we'll handle it where needed
    cursorX = (xpos / width) * 2.0 - 1.0;
    cursorY = -((ypos / height) * 2.0 - 1.0);
}

// Scroll callback for vertical panning
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window;   // Mark as intentionally unused
    (void)xoffset;  // Mark as intentionally unused
    scrollOffsetY += yoffset * 0.1;  // Smooth scrolling factor
}

// Check if cursor is over a menu item
bool cursor_over_menu_item(double menuX, double menuY, int itemIndex) {
    float itemY = menuY - itemIndex * (menuItemHeight + menuItemSpacing);
    return cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
           cursorY <= itemY && cursorY >= itemY - menuItemHeight;
}

// Find which connection (line) the cursor is near
static int hit_connection(double x, double y, float threshold) {
    for (int i = 0; i < connectionCount; ++i) {
        const FlowNode *from = &nodes[connections[i].fromNode];
        const FlowNode *to   = &nodes[connections[i].toNode];
        
        float x1 = (float)from->x;
        float y1 = (float)(from->y - from->height * 0.5f);
        float x2 = (float)to->x;
        float y2 = (float)(to->y + to->height * 0.5f);
        
        // Calculate distance from point to line segment
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len2 = dx*dx + dy*dy;
        
        if (len2 < 0.0001f) continue;
        
        float t = ((x - x1) * dx + (y - y1) * dy) / len2;
        t = fmax(0.0f, fmin(1.0f, t));
        
        float projX = x1 + t * dx;
        float projY = y1 + t * dy;
        
        float dist = sqrt((x - projX)*(x - projX) + (y - projY)*(y - projY));
        
        if (dist < threshold) {
            return i;
        }
    }
    return -1;
}

// Insert a node into an existing connection
void insert_node_in_connection(int connIndex, NodeType nodeType) {
    if (nodeCount >= MAX_NODES || connectionCount >= MAX_CONNECTIONS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
    // Fixed vertical spacing for consistent connection lengths
    double fixedSpacing = 0.5;  // Fixed distance between nodes
    
    // Store the original Y position of the "to" node before we modify anything
    double originalToY = to->y;
    
    // Create new node positioned at fixed spacing below the "from" node
    FlowNode *newNode = &nodes[nodeCount];
    newNode->x = from->x;  // Keep same X position for vertical flow
    newNode->y = from->y - fixedSpacing;  // Fixed spacing from parent
    newNode->width = 0.35f;
    newNode->height = 0.22f;
    newNode->value = nodeCount;
    newNode->type = nodeType;
    int newNodeIndex = nodeCount;
    nodeCount++;
    
    // Push the "to" node and all nodes below it further down by the same fixed spacing
    for (int i = 0; i < nodeCount; ++i) {
        if (nodes[i].y <= originalToY && i != newNodeIndex) {
            nodes[i].y -= fixedSpacing;
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
    (void)window;  // Mark as intentionally unused
    (void)mods;    // Mark as intentionally unused
    
    // Calculate world-space cursor position (accounting for scroll)
    double worldCursorY = cursorY - scrollOffsetY;
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        // Check if we're clicking on a connection (use world-space coordinates)
        int connIndex = hit_connection(cursorX, worldCursorY, 0.05f);
        
        if (connIndex >= 0) {
            // Open popup menu at cursor position (store world-space coordinates)
            popupMenu.active = true;
            popupMenu.x = cursorX;
            popupMenu.y = worldCursorY;
            popupMenu.connectionIndex = connIndex;
        } else {
            // Close menu if clicking elsewhere
            popupMenu.active = false;
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (popupMenu.active) {
            // Check which menu item was clicked (menu is in world space)
            float menuX = (float)popupMenu.x;
            float menuY = (float)popupMenu.y;
            
            // But cursor checking needs to compare with world-space cursor
            if (cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
                worldCursorY <= menuY && worldCursorY >= menuY - menuItemHeight) {
                // Insert normal node
                insert_node_in_connection(popupMenu.connectionIndex, NODE_NORMAL);
                popupMenu.active = false;
            }
            // Add more menu items here as needed
            else {
                // Clicked outside menu, close it
                popupMenu.active = false;
            }
        }
    }
}



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
    // Apply scroll transformation
    glPushMatrix();
    glTranslatef(0.0f, (float)scrollOffsetY, 0.0f);
    
    // Draw connections
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
        
        glBegin(GL_LINES);
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
        glEnd();
    }
    glLineWidth(1.0f);

    // Draw nodes
    for (int i = 0; i < nodeCount; ++i) {
        drawFlowNode(&nodes[i]);
    }
    
    // Draw popup menu within the same transform (so it scrolls with content)
    drawPopupMenu();
    
    glPopMatrix();
}

void drawPopupMenu() {
    if (!popupMenu.active) return;
    
    // Menu is stored in world space, so it moves with the scroll
    float menuX = (float)popupMenu.x;
    float menuY = (float)popupMenu.y;
    
    // Draw menu item background
    glColor3f(0.2f, 0.2f, 0.25f);
    glBegin(GL_QUADS);
    glVertex2f(menuX, menuY);
    glVertex2f(menuX + menuItemWidth, menuY);
    glVertex2f(menuX + menuItemWidth, menuY - menuItemHeight);
    glVertex2f(menuX, menuY - menuItemHeight);
    glEnd();
    
    // Draw menu item border
    glColor3f(0.8f, 0.8f, 0.8f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(menuX, menuY);
    glVertex2f(menuX + menuItemWidth, menuY);
    glVertex2f(menuX + menuItemWidth, menuY - menuItemHeight);
    glVertex2f(menuX, menuY - menuItemHeight);
    glEnd();
    
    // Highlight if hovering (compare with world-space cursor)
    double worldCursorY = cursorY - scrollOffsetY;
    bool hovering = cursorX >= menuX && cursorX <= menuX + menuItemWidth &&
                    worldCursorY <= menuY && worldCursorY >= menuY - menuItemHeight;
    
    if (hovering) {
        glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        glVertex2f(menuX, menuY);
        glVertex2f(menuX + menuItemWidth, menuY);
        glVertex2f(menuX + menuItemWidth, menuY - menuItemHeight);
        glVertex2f(menuX, menuY - menuItemHeight);
        glEnd();
        glDisable(GL_BLEND);
    }
    
    // Draw text "Process Node"
    glColor3f(1.0f, 1.0f, 1.0f);
    float textY = menuY - menuItemHeight * 0.5f;
    float x = menuX + 0.02f;
    float h = menuItemHeight * 0.3f;
    float w = h * 0.5f;
    
    glBegin(GL_LINES);
    // "P"
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x + w, textY);
    glVertex2f(x + w, textY);
    glVertex2f(x, textY);
    x += w * 1.3f;
    
    // "R"
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x + w, textY);
    glVertex2f(x + w, textY);
    glVertex2f(x, textY);
    glVertex2f(x, textY);
    glVertex2f(x + w, textY - h * 0.5f);
    x += w * 1.3f;
    
    // "O"
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    x += w * 1.3f;
    
    // "C"
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    x += w * 1.3f;
    
    // "E"
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x, textY);
    glVertex2f(x + w * 0.8f, textY);
    glVertex2f(x, textY - h * 0.5f);
    glVertex2f(x + w, textY - h * 0.5f);
    x += w * 1.3f;
    
    // "S"
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY);
    glVertex2f(x, textY);
    glVertex2f(x + w, textY);
    glVertex2f(x + w, textY);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    x += w * 1.3f;
    
    // "S" (again)
    glVertex2f(x + w, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY + h * 0.5f);
    glVertex2f(x, textY);
    glVertex2f(x, textY);
    glVertex2f(x + w, textY);
    glVertex2f(x + w, textY);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x + w, textY - h * 0.5f);
    glVertex2f(x, textY - h * 0.5f);
    
    glEnd();
}

void initialize_flowchart() {
    // Fixed spacing between all nodes
    double fixedSpacing = 0.5;
    
    // Create START node
    nodes[0].x = 0.0;
    nodes[0].y = 0.6;
    nodes[0].width = 0.35f;
    nodes[0].height = 0.22f;
    nodes[0].value = 0;
    nodes[0].type = NODE_START;
    
    // Create END node with fixed spacing from START
    nodes[1].x = 0.0;
    nodes[1].y = 0.6 - fixedSpacing;  // Fixed spacing from START
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
    
    // Initialize with connected START and END nodes
    initialize_flowchart();

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Update hovered connection (use world-space cursor)
        double worldCursorY = cursorY - scrollOffsetY;
        hoveredConnection = hit_connection(cursorX, worldCursorY, 0.05f);
        
        drawFlowchart();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
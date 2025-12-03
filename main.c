#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

// Global variables for cursor position
double cursorX = 0.0;
double cursorY = 0.0;

// Global variable for right mouse button state
int rightMousePressed = 0;

// Arrays to store normal colored squares
#define MAX_SQUARES 100
double squareX[MAX_SQUARES];
double squareY[MAX_SQUARES];
int squareIsRed[MAX_SQUARES];
int squareCount = 0;

// Flowchart node and connection data
#define MAX_NODES 100
#define MAX_CONNECTIONS 200

typedef enum {
    NODE_NORMAL = 0,
    NODE_START  = 1,  // only bottom output connector
    NODE_END    = 2   // only top input connector
} NodeType;

typedef struct {
    double x;
    double y;
    float width;
    float height;
    int value;  // placeholder for a value carried by this node
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

// Flowchart interaction state
int flowchartModeOn = 0;
int pendingConnectionFrom = -1;  // index of node whose output is selected, or -1
NodeType currentPlacementType = NODE_NORMAL; // which kind of node right-click places in Flow mode
int logConnectionsToTerminal = 0;           // toggle for printing node info on new connections

// Simple menu bar configuration (normalized device coordinates)
// Full-width bar at the top, with five small menu items: "Color", "Flow", "Start", "End", "Exit"
const float menuTop = 1.0f;
const float menuBottom = 0.90f;
const float menuLeft = -1.0f;
const float menuRight = 1.0f;

// Left to right: Color | Flow | Start | End | Log | Exit
const float colorItemLeft  = -1.0f;
const float colorItemRight = -0.666f;

const float flowItemLeft   = -0.666f;
const float flowItemRight  = -0.333f;

const float startItemLeft  = -0.333f;
const float startItemRight = 0.0f;

const float endItemLeft    = 0.0f;
const float endItemRight   = 0.333f;

const float logItemLeft    = 0.333f;
const float logItemRight   = 0.666f;

const float exitItemLeft   = 0.666f;
const float exitItemRight  = 1.0f;

bool menuRedSquaresOn = false;

// Cursor position callback
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    
    // Convert screen coordinates to OpenGL coordinates (-1 to 1)
    cursorX = (xpos / width) * 2.0 - 1.0;
    cursorY = -((ypos / height) * 2.0 - 1.0);
}

// Utility to test if cursor is over the "Color" menu item
bool cursor_over_color_item(void) {
    return cursorX >= colorItemLeft && cursorX <= colorItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is over the "Flow" menu item
bool cursor_over_flow_item(void) {
    return cursorX >= flowItemLeft && cursorX <= flowItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is over the "Start" menu item
bool cursor_over_start_item(void) {
    return cursorX >= startItemLeft && cursorX <= startItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is over the "End" menu item
bool cursor_over_end_item(void) {
    return cursorX >= endItemLeft && cursorX <= endItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is over the "Log" menu item
bool cursor_over_log_item(void) {
    return cursorX >= logItemLeft && cursorX <= logItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is over the "Exit" menu item
bool cursor_over_exit_item(void) {
    return cursorX >= exitItemLeft && cursorX <= exitItemRight &&
           cursorY <= menuTop && cursorY >= menuBottom;
}

// Utility to test if cursor is anywhere in the menu bar area
bool cursor_in_menu_area(void) {
    return cursorY <= menuTop && cursorY >= menuBottom;
}

// Helpers for flowchart hit testing
static int hit_node_output(double x, double y) {
    // Make clicking the bottom connector easier: treat a small strip under
    // the node as the clickable output area.
    const float verticalPadding = 0.06f;
    for (int i = 0; i < nodeCount; ++i) {
        // END nodes have no output
        if (nodes[i].type == NODE_END) {
            continue;
        }
        double cx = nodes[i].x;
        double cy = nodes[i].y - nodes[i].height * 0.5;
        double dx = x - cx;
        double dy = y - cy;
        if (fabs(dx) <= nodes[i].width * 0.5 && fabs(dy) <= verticalPadding) {
            return i;
        }
    }
    return -1;
}

static int hit_node_input(double x, double y) {
    // Same idea for the input: a small strip above the node counts as the input.
    const float verticalPadding = 0.06f;
    for (int i = 0; i < nodeCount; ++i) {
        // START nodes have no input
        if (nodes[i].type == NODE_START) {
            continue;
        }
        double cx = nodes[i].x;
        double cy = nodes[i].y + nodes[i].height * 0.5;
        double dx = x - cx;
        double dy = y - cy;
        if (fabs(dx) <= nodes[i].width * 0.5 && fabs(dy) <= verticalPadding) {
            return i;
        }
    }
    return -1;
}

static const char* node_type_name(NodeType t) {
    switch (t) {
        case NODE_START:  return "START";
        case NODE_END:    return "END";
        case NODE_NORMAL: return "NORMAL";
        default:          return "UNKNOWN";
    }
}

// Mouse button callback
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Menu interactions always available
        if (cursor_over_color_item()) {
            menuRedSquaresOn = !menuRedSquaresOn;
        } else if (cursor_over_flow_item()) {
            flowchartModeOn = !flowchartModeOn;
            pendingConnectionFrom = -1;
        } else if (cursor_over_start_item() && flowchartModeOn) {
            // Toggle START as the type to place with right-click
            if (currentPlacementType == NODE_START) {
                currentPlacementType = NODE_NORMAL;
            } else {
                currentPlacementType = NODE_START;
            }
        } else if (cursor_over_end_item() && flowchartModeOn) {
            // Toggle END as the type to place with right-click
            if (currentPlacementType == NODE_END) {
                currentPlacementType = NODE_NORMAL;
            } else {
                currentPlacementType = NODE_END;
            }
        } else if (cursor_over_log_item()) {
            // Toggle connection logging
            logConnectionsToTerminal = !logConnectionsToTerminal;
        } else if (cursor_over_exit_item()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        } else if (flowchartModeOn) {
            // Flowchart connection logic: click output then input
            double x = cursorX;
            double y = cursorY;
            if (pendingConnectionFrom < 0) {
                int outNode = hit_node_output(x, y);
                if (outNode >= 0) {
                    pendingConnectionFrom = outNode;
                }
            } else {
                int inNode = hit_node_input(x, y);
                if (inNode >= 0 && inNode != pendingConnectionFrom &&
                    connectionCount < MAX_CONNECTIONS) {
                    connections[connectionCount].fromNode = pendingConnectionFrom;
                    connections[connectionCount].toNode = inNode;
                    int idx = connectionCount;
                    connectionCount++;

                    if (logConnectionsToTerminal) {
                        FlowNode *from = &nodes[connections[idx].fromNode];
                        FlowNode *to   = &nodes[connections[idx].toNode];
                        printf("New connection %d: from node %d (%s, x=%.2f, y=%.2f, value=%d) "
                               "to node %d (%s, x=%.2f, y=%.2f, value=%d)\n",
                               idx,
                               connections[idx].fromNode, node_type_name(from->type),
                               (float)from->x, (float)from->y, from->value,
                               connections[idx].toNode,   node_type_name(to->type),
                               (float)to->x,   (float)to->y,   to->value);
                        fflush(stdout);
                    }
                }
                pendingConnectionFrom = -1;
            }
        }
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            rightMousePressed = 1;

            // Don't place items if cursor is in the menu area
            if (cursor_in_menu_area()) {
                return;
            }

            if (flowchartModeOn) {
                // Create a new flowchart node at cursor of the currently selected type
                if (nodeCount < MAX_NODES) {
                    FlowNode *n = &nodes[nodeCount];
                    n->x = cursorX;
                    n->y = cursorY;
                    n->width = 0.35f;
                    n->height = 0.22f;
                    n->value = nodeCount;  // simple placeholder value
                    n->type = currentPlacementType;
                    nodeCount++;
                }
            } else {
                // Normal colored square creation
                if (squareCount < MAX_SQUARES) {
                    squareX[squareCount] = cursorX;
                    squareY[squareCount] = cursorY;
                    squareIsRed[squareCount] = menuRedSquaresOn ? 1 : 0;
                    squareCount++;
                }
            }
        } else if (action == GLFW_RELEASE) {
            rightMousePressed = 0;
        }
    }
}

void drawTriangle() {
    float size = 0.2f;
    
    glBegin(GL_TRIANGLES);
    
    // Red vertex (top)
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex2f(cursorX, cursorY + size);
    
    // Green vertex (bottom left)
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex2f(cursorX - size, cursorY - size);
    
    // Blue vertex (bottom right)
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex2f(cursorX + size, cursorY - size);
    
    glEnd();
}

void drawSquare() {
    float size = 0.15f;
    
    glBegin(GL_QUADS);
    
    // Blue square
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex2f(cursorX - size, cursorY + size);  // Top left
    glVertex2f(cursorX + size, cursorY + size);  // Top right
    glVertex2f(cursorX + size, cursorY - size);  // Bottom right
    glVertex2f(cursorX - size, cursorY - size);  // Bottom left
    
    glEnd();
}

void drawSquareAt(double x, double y, int isRed) {
    float size = 0.15f;
    
    glBegin(GL_QUADS);
    
    if (isRed) {
        glColor3f(1.0f, 0.0f, 0.0f);
    } else {
        glColor3f(0.0f, 0.0f, 1.0f);
    }
    glVertex2f(x - size, y + size);  // Top left
    glVertex2f(x + size, y + size);  // Top right
    glVertex2f(x + size, y - size);  // Bottom right
    glVertex2f(x - size, y - size);  // Bottom left
    
    glEnd();
}

// Draw a single yellow flowchart node with one input and one output connector
void drawFlowNode(const FlowNode *n) {
    // Node body
    glColor3f(0.95f, 0.9f, 0.25f); // yellow-ish
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

// Draw all flowchart nodes and their connections
void drawFlowchart(void) {
    // First draw connections so nodes appear on top
    glLineWidth(3.0f);
    glColor3f(0.0f, 1.0f, 0.8f); // bright cyan connections
    glBegin(GL_LINES);
    for (int i = 0; i < connectionCount; ++i) {
        const FlowNode *from = &nodes[connections[i].fromNode];
        const FlowNode *to   = &nodes[connections[i].toNode];
        // From bottom center of source node to top center of target node
        float x1 = (float)from->x;
        float y1 = (float)(from->y - from->height * 0.5f);
        float x2 = (float)to->x;
        float y2 = (float)(to->y + to->height * 0.5f);
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
    }
    glEnd();

    // Highlight a pending connection from the selected output, if any
    if (pendingConnectionFrom >= 0 && pendingConnectionFrom < nodeCount) {
        const FlowNode *from = &nodes[pendingConnectionFrom];
        float x1 = (float)from->x;
        float y1 = (float)(from->y - from->height * 0.5f);
        glColor3f(1.0f, 0.4f, 0.0f); // bright orange preview
        glBegin(GL_LINES);
        glVertex2f(x1, y1);
        glVertex2f((float)cursorX, (float)cursorY);
        glEnd();
    }

    // Reset line width for the rest of the scene
    glLineWidth(1.0f);

    // Draw nodes
    for (int i = 0; i < nodeCount; ++i) {
        drawFlowNode(&nodes[i]);
    }
}
void drawMenu() {
    // Draw full-width menu bar background
    glBegin(GL_QUADS);
    glColor3f(0.15f, 0.15f, 0.15f);
    glVertex2f(menuLeft, menuTop);
    glVertex2f(menuRight, menuTop);
    glVertex2f(menuRight, menuBottom);
    glVertex2f(menuLeft, menuBottom);
    glEnd();

    // Draw small outlines for the "Color", "Flow", "Start", "End", "Log" and "Exit" buttons (no big filled rectangles)
    glColor3f(0.7f, 0.7f, 0.7f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(colorItemLeft, menuTop);
    glVertex2f(colorItemRight, menuTop);
    glVertex2f(colorItemRight, menuBottom);
    glVertex2f(colorItemLeft, menuBottom);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex2f(flowItemLeft, menuTop);
    glVertex2f(flowItemRight, menuTop);
    glVertex2f(flowItemRight, menuBottom);
    glVertex2f(flowItemLeft, menuBottom);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex2f(startItemLeft, menuTop);
    glVertex2f(startItemRight, menuTop);
    glVertex2f(startItemRight, menuBottom);
    glVertex2f(startItemLeft, menuBottom);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex2f(endItemLeft, menuTop);
    glVertex2f(endItemRight, menuTop);
    glVertex2f(endItemRight, menuBottom);
    glVertex2f(endItemLeft, menuBottom);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex2f(logItemLeft, menuTop);
    glVertex2f(logItemRight, menuTop);
    glVertex2f(logItemRight, menuBottom);
    glVertex2f(logItemLeft, menuBottom);
    glEnd();
    glBegin(GL_LINE_LOOP);
    glVertex2f(exitItemLeft, menuTop);
    glVertex2f(exitItemRight, menuTop);
    glVertex2f(exitItemRight, menuBottom);
    glVertex2f(exitItemLeft, menuBottom);
    glEnd();

    // Draw "Color" label — only the font color changes with the toggle
    if (menuRedSquaresOn) {
        glColor3f(1.0f, 0.2f, 0.2f);  // red when ON
    } else {
        glColor3f(0.2f, 0.4f, 1.0f);  // blue when OFF
    }
    float textY = (menuTop + menuBottom) * 0.5f;
    float x = colorItemLeft + 0.02f;
    float h = (menuTop - menuBottom) * 0.5f;
    float w = h * 0.6f;

    glBegin(GL_LINES);
    // "C"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    x += w * 1.3f;

    // "O"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    x += w * 1.3f;

    // "L"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.1f;

    // "O"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    x += w * 1.3f;

    // "R"
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w,       textY);
    glVertex2f(x,           textY);
    glVertex2f(x,           textY);
    glVertex2f(x + w,       textY - h * 0.5f);
    glEnd();

    // Draw "Flow" label
    if (flowchartModeOn) {
        glColor3f(0.2f, 1.0f, 0.2f);  // green when flowchart mode ON
    } else {
        glColor3f(0.6f, 0.6f, 0.6f);  // gray when OFF
    }
    textY = (menuTop + menuBottom) * 0.5f;
    x = flowItemLeft + 0.05f;
    h = (menuTop - menuBottom) * 0.5f;
    w = h * 0.6f;

    glBegin(GL_LINES);
    // "F"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY);
    glVertex2f(x + w * 0.8f,textY);
    x += w * 1.3f;

    // "L"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.1f;

    // "O"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    x += w * 1.3f;

    // "W"
    float x0 = x;
    float x1 = x0 + w * 0.33f;
    float x2 = x0 + w * 0.66f;
    float x3 = x0 + w;
    float yTop = textY + h * 0.5f;
    float yBot = textY - h * 0.5f;

    glVertex2f(x0, yTop);
    glVertex2f(x0 + w * 0.16f, yBot);
    glVertex2f(x0 + w * 0.16f, yBot);
    glVertex2f(x1, yTop - h * 0.25f);
    glVertex2f(x1, yTop - h * 0.25f);
    glVertex2f(x2, yBot);
    glVertex2f(x2, yBot);
    glVertex2f(x3, yTop);
    x = x3 + w * 0.2f;
    glEnd();

    // Draw "Start" label (highlighted when START placement is active)
    if (currentPlacementType == NODE_START) {
        glColor3f(1.0f, 1.0f, 0.2f);  // bright yellow when active
    } else {
        glColor3f(0.6f, 0.6f, 0.4f);  // dimmer when inactive
    }
    textY = (menuTop + menuBottom) * 0.5f;
    x = startItemLeft + 0.03f;
    h = (menuTop - menuBottom) * 0.5f;
    w = h * 0.5f;

    glBegin(GL_LINES);
    // "S"
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY);
    glVertex2f(x,           textY);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    x += w * 1.3f;

    // "T"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY - h * 0.5f);
    x += w * 1.2f;

    // "A"
    float ax0 = x;
    float ax1 = x + w * 0.5f;
    float ax2 = x + w;
    float ayTop = textY + h * 0.5f;
    float ayBot = textY - h * 0.5f;
    float ayMid = textY;
    glVertex2f(ax0, ayBot);
    glVertex2f(ax1, ayTop);
    glVertex2f(ax1, ayTop);
    glVertex2f(ax2, ayBot);
    glVertex2f(ax0 + w * 0.2f, ayMid);
    glVertex2f(ax2 - w * 0.2f, ayMid);
    x += w * 1.5f;

    // "R"
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w,       textY);
    glVertex2f(x,           textY);
    glVertex2f(x,           textY);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.3f;

    // "T"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY - h * 0.5f);
    glEnd();

    // Draw "End" label (highlighted when END placement is active)
    if (currentPlacementType == NODE_END) {
        glColor3f(1.0f, 0.4f, 0.4f);  // brighter when active
    } else {
        glColor3f(0.7f, 0.5f, 0.5f);  // dimmer when inactive
    }
    textY = (menuTop + menuBottom) * 0.5f;
    x = endItemLeft + 0.06f;
    h = (menuTop - menuBottom) * 0.5f;
    w = h * 0.5f;

    glBegin(GL_LINES);
    // "E"
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY);
    glVertex2f(x + w * 0.8f,textY);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.3f;

    // "N"
    float nx0 = x;
    float nx1 = x + w;
    glVertex2f(nx0,         textY - h * 0.5f);
    glVertex2f(nx0,         textY + h * 0.5f);
    glVertex2f(nx0,         textY + h * 0.5f);
    glVertex2f(nx1,         textY - h * 0.5f);
    glVertex2f(nx1,         textY - h * 0.5f);
    glVertex2f(nx1,         textY + h * 0.5f);
    x += w * 1.3f;

    // "D"
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w * 0.7f,textY + h * 0.25f);
    glVertex2f(x + w * 0.7f,textY + h * 0.25f);
    glVertex2f(x + w * 0.7f,textY - h * 0.25f);
    glVertex2f(x + w * 0.7f,textY - h * 0.25f);
    glVertex2f(x,           textY - h * 0.5f);
    glEnd();

    // Draw "Log" label
    if (logConnectionsToTerminal) {
        glColor3f(0.2f, 1.0f, 0.8f);  // highlighted when ON
    } else {
        glColor3f(0.7f, 0.7f, 0.7f);  // dim when OFF
    }
    textY = (menuTop + menuBottom) * 0.5f;
    x = logItemLeft + 0.06f;
    h = (menuTop - menuBottom) * 0.5f;
    w = h * 0.5f;

    glBegin(GL_LINES);
    // "L"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.2f;

    // "O"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    x += w * 1.3f;

    // "G"
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w,       textY);
    glVertex2f(x + w * 0.6f,textY);
    glEnd();

    // Draw "Exit" label in the right button
    glColor3f(1.0f, 1.0f, 1.0f);
    textY = (menuTop + menuBottom) * 0.5f;
    x = exitItemLeft + 0.05f;
    h = (menuTop - menuBottom) * 0.5f;
    w = h * 0.6f;

    glBegin(GL_LINES);
    // "E"
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x,           textY);
    glVertex2f(x + w * 0.8f,textY);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    x += w * 1.3f;

    // "X"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY - h * 0.5f);
    glVertex2f(x,           textY - h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    x += w * 1.3f;

    // "I"
    glVertex2f(x + w * 0.5f,textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY - h * 0.5f);
    x += w * 0.9f;

    // "T"
    glVertex2f(x,           textY + h * 0.5f);
    glVertex2f(x + w,       textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY + h * 0.5f);
    glVertex2f(x + w * 0.5f,textY - h * 0.5f);
    glEnd();
}

int main(void) {
    GLFWwindow* window;

    // Initialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    // Create a windowed mode window and its OpenGL context
    window = glfwCreateWindow(800, 600, "RGB Triangle Cursor Tracker", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(window);
    
    // White background
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    // Set the cursor position callback
    glfwSetCursorPosCallback(window, cursor_position_callback);
    
    // Set the mouse button callback
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Clear the screen
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Draw all previously created normal squares
        for (int i = 0; i < squareCount; i++) {
            drawSquareAt(squareX[i], squareY[i], squareIsRed[i]);
        }

        // Draw flowchart nodes and connections (if any)
        drawFlowchart();
        
        // Draw the triangle (always follows cursor)
        drawTriangle();

        // Draw menu bar last so it stays on top visually and prevents accidental placement
        drawMenu();

        // Swap front and back buffers
        glfwSwapBuffers(window);

        // Poll for and process events
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
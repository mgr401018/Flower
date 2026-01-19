#ifndef FLOWCHART_STATE_H
#define FLOWCHART_STATE_H

#include <stdbool.h>

// Flowchart node and connection data
#define MAX_NODES 100
#define MAX_CONNECTIONS 200
#define MAX_VALUE_LENGTH 256
#define MAX_VARIABLES 200
#define MAX_VAR_NAME_LENGTH 64
#define MAX_IF_BLOCKS 50
#define MAX_CYCLE_BLOCKS 50
#define MAX_UNDO_HISTORY 10

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

// Cycle tracking system
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

// Undo/Redo system
typedef struct {
    FlowNode nodes[MAX_NODES];
    int nodeCount;
    Connection connections[MAX_CONNECTIONS];
    int connectionCount;
    IFBlock ifBlocks[MAX_IF_BLOCKS];
    int ifBlockCount;
    CycleBlock cycleBlocks[MAX_CYCLE_BLOCKS];
    int cycleBlockCount;
} FlowchartState;

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

// Menu items
#define MAX_MENU_ITEMS 10
typedef struct {
    const char* text;
    NodeType nodeType;  // Node type to insert when clicked
} MenuItem;

typedef struct {
    const char* text;
    int action;  // 0 = Delete, 1 = Value, etc.
} NodeMenuItem;

extern MenuItem connectionMenuItems[];
extern int connectionMenuItemCount;
extern NodeMenuItem nodeMenuItems[];
extern int nodeMenuItemCount;

// Global variables (extern declarations)
extern FlowNode nodes[];
extern int nodeCount;
extern Connection connections[];
extern int connectionCount;
extern IFBlock ifBlocks[];
extern int ifBlockCount;
extern CycleBlock cycleBlocks[];
extern int cycleBlockCount;
extern FlowchartState undoHistory[];
extern int undoHistoryCount;
extern int undoHistoryIndex;
extern Variable variables[];
extern int variableCount;
extern PopupMenu popupMenu;
extern double cursorX;
extern double cursorY;
extern int hoveredConnection;
extern double scrollOffsetX;
extern double scrollOffsetY;
extern bool deletionEnabled;
extern bool isPanning;
extern double panStartX;
extern double panStartY;
extern double panStartScrollX;
extern double panStartScrollY;

// Constants
extern const double GRID_CELL_SIZE;
extern const float FLOWCHART_SCALE;
extern const float buttonRadius;
extern const float buttonX;
extern const float closeButtonY;
extern const float saveButtonY;
extern const float loadButtonY;
extern const float exportButtonY;
extern const float undoButtonY;
extern const float redoButtonY;
extern const float menuItemHeight;
extern const float menuItemSpacing;
extern const float menuPadding;
extern const float menuMinWidth;

#endif // FLOWCHART_STATE_H

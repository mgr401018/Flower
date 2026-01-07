#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "code_exporter.h"

// Define FlowNode and Connection structures (must match main.c)
#define MAX_VALUE_LENGTH 256

typedef struct FlowNode {
    double x;
    double y;
    float width;
    float height;
    char value[MAX_VALUE_LENGTH];
    int type;  // NodeType enum value
    int branchColumn;         // 0 = main, -2/-4/-6 = left branches, +2/+4/+6 = right
    int owningIfBlock;        // Index of IF block this node belongs to (-1 if main)
} FlowNode;

typedef struct Connection {
    int fromNode;
    int toNode;
} Connection;

// Constants (must match main.c)
#define MAX_VALUE_LENGTH 256
#define MAX_VAR_NAME_LENGTH 64
#define MAX_NODES 100

// Node types (must match main.c)
typedef enum {
    NODE_NORMAL = 0,
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

// Variable types (must match main.c)
typedef enum {
    VAR_TYPE_INT = 0,
    VAR_TYPE_REAL = 1,
    VAR_TYPE_STRING = 2,
    VAR_TYPE_BOOL = 3
} VariableType;

// Variable structure for tracking variable types
typedef struct {
    char name[MAX_VAR_NAME_LENGTH];
    VariableType type;
    bool is_array;
} VarInfo;

#define MAX_VARS 200
static VarInfo varTable[MAX_VARS];
static int varTableCount = 0;

// Helper function to parse declare block
static bool parse_declare_block(const char* value, char* varName, VariableType* varType, bool* isArray, int* arraySize) {
    if (!value || value[0] == '\0') return false;
    
    const char* p = value;
    while (*p == ' ' || *p == '\t') p++;
    
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
    
    while (*p == ' ' || *p == '\t') p++;
    
    int nameLen = 0;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1) {
        varName[nameLen++] = *p++;
    }
    varName[nameLen] = '\0';
    
    if (nameLen == 0) return false;
    
    *isArray = false;
    *arraySize = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '[') {
        *isArray = true;
        p++;
        if (*p >= '0' && *p <= '9') {
            *arraySize = atoi(p);
        }
    }
    
    return true;
}

// Find variable in table
static VarInfo* find_var(const char* name) {
    for (int i = 0; i < varTableCount; i++) {
        if (strcmp(varTable[i].name, name) == 0) {
            return &varTable[i];
        }
    }
    return NULL;
}

// Build variable table from declare blocks
static void build_var_table(struct FlowNode* nodes, int nodeCount) {
    varTableCount = 0;
    for (int i = 0; i < nodeCount && varTableCount < MAX_VARS; i++) {
        if (nodes[i].type == NODE_DECLARE) {
            char varName[MAX_VAR_NAME_LENGTH];
            VariableType varType;
            bool isArray;
            int arraySize;
            
            if (parse_declare_block(nodes[i].value, varName, &varType, &isArray, &arraySize)) {
                strncpy(varTable[varTableCount].name, varName, MAX_VAR_NAME_LENGTH - 1);
                varTable[varTableCount].name[MAX_VAR_NAME_LENGTH - 1] = '\0';
                varTable[varTableCount].type = varType;
                varTable[varTableCount].is_array = isArray;
                varTableCount++;
            }
        }
    }
}

// Check if any string variables exist
static bool has_string_variables(void) {
    for (int i = 0; i < varTableCount; i++) {
        if (varTable[i].type == VAR_TYPE_STRING) {
            return true;
        }
    }
    return false;
}

// Parse serialized cycle value string: TYPE|cond or FOR|init|cond|inc
static void parse_cycle_value(const char* value, char* typeBuf, char* condBuf, char* initBuf, char* incrBuf) {
    typeBuf[0] = condBuf[0] = initBuf[0] = incrBuf[0] = '\0';
    if (!value) return;
    
    char temp[MAX_VALUE_LENGTH];
    strncpy(temp, value, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char* token = strtok(temp, "|");
    if (token) {
        strncpy(typeBuf, token, MAX_VAR_NAME_LENGTH - 1);
        typeBuf[MAX_VAR_NAME_LENGTH - 1] = '\0';
    }
    
    // Check if this is a FOR loop
    bool isFor = (strncmp(typeBuf, "FOR", 3) == 0);
    
    token = strtok(NULL, "|");
    if (token) {
        if (isFor) {
            // FOR: token 2 is init, token 3 is cond, token 4 is incr
            strncpy(initBuf, token, MAX_VALUE_LENGTH - 1);
            initBuf[MAX_VALUE_LENGTH - 1] = '\0';
        } else {
            // WHILE/DO: token 2 is cond
            strncpy(condBuf, token, MAX_VALUE_LENGTH - 1);
            condBuf[MAX_VALUE_LENGTH - 1] = '\0';
        }
    }
    token = strtok(NULL, "|");
    if (token) {
        if (isFor) {
            // FOR: token 3 is cond
            strncpy(condBuf, token, MAX_VALUE_LENGTH - 1);
            condBuf[MAX_VALUE_LENGTH - 1] = '\0';
        }
        // For WHILE/DO, token 3 doesn't exist
    }
    token = strtok(NULL, "|");
    if (token) {
        if (isFor) {
            // FOR: token 4 is incr
            strncpy(incrBuf, token, MAX_VALUE_LENGTH - 1);
            incrBuf[MAX_VALUE_LENGTH - 1] = '\0';
        }
        // For WHILE/DO, token 4 doesn't exist
    }
}

// Helper function to parse assignment
static bool parse_assignment(const char* value, char* leftVar, char* rightValue) {
    if (!value || value[0] == '\0') return false;
    
    const char* p = value;
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p == '=') p++;
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract left side (could be variable or array access)
    int leftLen = 0;
    while (*p != '\0' && *p != '=' && leftLen < MAX_VAR_NAME_LENGTH - 1) {
        if (*p == '[') {
            leftVar[leftLen++] = *p++;
            continue;
        }
        if (*p == ']') {
            leftVar[leftLen++] = *p++;
            continue;
        }
        leftVar[leftLen++] = *p++;
    }
    leftVar[leftLen] = '\0';
    
    if (leftLen == 0) return false;
    
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    
    // Extract right side
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
    
    return true;
}

// Helper function to parse input block
static bool parse_input_block(const char* value, char* varName, char* indexExpr, bool* isArray) {
    if (!value || value[0] == '\0') return false;
    
    const char* p = value;
    while (*p == ' ' || *p == '\t') p++;
    
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
    
    *isArray = false;
    indexExpr[0] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '[') {
        *isArray = true;
        p++;
        
        int indexLen = 0;
        while (*p != '\0' && *p != ']' && indexLen < MAX_VALUE_LENGTH - 1) {
            indexExpr[indexLen++] = *p++;
        }
        indexExpr[indexLen] = '\0';
        
        if (*p != ']') {
            return false;
        }
    }
    
    return true;
}

// Extract variable placeholders from output format string
static void extract_output_placeholders(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], 
                                        char indexExprs[][MAX_VALUE_LENGTH], bool* isArrayAccess, int* varCount) {
    *varCount = 0;
    if (!formatStr || formatStr[0] == '\0') return;
    
    const char* p = formatStr;
    while (*p != '\0' && *varCount < 100) {
        while (*p != '\0' && *p != '{') p++;
        if (*p == '\0') break;
        
        p++;
        
        int nameLen = 0;
        isArrayAccess[*varCount] = false;
        indexExprs[*varCount][0] = '\0';
        
        while (*p != '\0' && *p != '}' && *p != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1) {
            char c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                (c >= '0' && c <= '9') || c == '_') {
                varNames[*varCount][nameLen++] = c;
                p++;
            } else {
                break;
            }
        }
        
        if (*p == '[' && nameLen > 0) {
            isArrayAccess[*varCount] = true;
            p++;
            
            int indexLen = 0;
            while (*p != '\0' && *p != ']' && indexLen < MAX_VALUE_LENGTH - 1) {
                indexExprs[*varCount][indexLen++] = *p++;
            }
            indexExprs[*varCount][indexLen] = '\0';
            
            if (*p == ']') {
                p++;
            } else {
                while (*p != '\0' && *p != '{') p++;
                continue;
            }
        }
        
        if (*p == '}' && nameLen > 0) {
            varNames[*varCount][nameLen] = '\0';
            (*varCount)++;
            p++;
        } else {
            while (*p != '\0' && *p != '{') p++;
        }
    }
}

// Get C type name from VariableType
static const char* get_c_type_name(VariableType type) {
    switch (type) {
        case VAR_TYPE_INT: return "int";
        case VAR_TYPE_REAL: return "double";
        case VAR_TYPE_STRING: return "char";
        case VAR_TYPE_BOOL: return "bool";
        default: return "int";
    }
}

// Get scanf format specifier
static const char* get_scanf_format(VariableType type) {
    switch (type) {
        case VAR_TYPE_INT: return "%d";
        case VAR_TYPE_REAL: return "%lf";
        case VAR_TYPE_STRING: return "%s";
        case VAR_TYPE_BOOL: return "%d";  // bool as int
        default: return "%d";
    }
}

// Get printf format specifier
static const char* get_printf_format(VariableType type) {
    switch (type) {
        case VAR_TYPE_INT: return "%d";
        case VAR_TYPE_REAL: return "%lf";
        case VAR_TYPE_STRING: return "%s";
        case VAR_TYPE_BOOL: return "%d";  // bool as int
        default: return "%d";
    }
}

// Find next node in flowchart (follow connections)
static int find_next_node(int currentNode, Connection* connections, int connectionCount) {
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == currentNode) {
            return connections[i].toNode;
        }
    }
    return -1;
}

// Find all connections from a node
static void find_connections_from(int fromNode, Connection* connections, int connectionCount, 
                                  int* outNodes, int* outCount, int maxOut) {
    *outCount = 0;
    for (int i = 0; i < connectionCount && *outCount < maxOut; i++) {
        if (connections[i].fromNode == fromNode) {
            outNodes[(*outCount)++] = connections[i].toNode;
        }
    }
}

// Find connection from a node
static int find_connection_from(int fromNode, Connection* connections, int connectionCount) {
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == fromNode) {
            return i;
        }
    }
    return -1;
}

// Find convergence node for an IF node
// The convergence is the node that both branches eventually reach
static int find_convergence_for_if(int ifNode, Connection* connections, int connectionCount) {
    // Find all nodes reachable from the IF (branch nodes)
    int branchNodes[MAX_NODES];
    int branchCount = 0;
    bool visited[MAX_NODES] = {false};
    
    // Start from all direct connections from IF
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == ifNode) {
            int startNode = connections[i].toNode;
            if (startNode >= 0 && !visited[startNode]) {
                // BFS to find all nodes in this branch
                int queue[MAX_NODES];
                int queueFront = 0, queueBack = 0;
                queue[queueBack++] = startNode;
                visited[startNode] = true;
                
                while (queueFront < queueBack) {
                    int current = queue[queueFront++];
                    branchNodes[branchCount++] = current;
                    
                    // Follow connections from this node
                    for (int j = 0; j < connectionCount; j++) {
                        if (connections[j].fromNode == current && !visited[connections[j].toNode]) {
                            visited[connections[j].toNode] = true;
                            queue[queueBack++] = connections[j].toNode;
                        }
                    }
                }
            }
        }
    }
    
    // Find a node that has incoming connections from multiple branch nodes
    // This is the convergence point
    for (int i = 0; i < connectionCount; i++) {
        int toNode = connections[i].toNode;
        if (toNode == ifNode) continue; // Skip connections back to IF
        
        // Count how many branch nodes connect to this node
        int incomingFromBranches = 0;
        for (int j = 0; j < connectionCount; j++) {
            if (connections[j].toNode == toNode) {
                // Check if the source is a branch node
                for (int k = 0; k < branchCount; k++) {
                    if (connections[j].fromNode == branchNodes[k]) {
                        incomingFromBranches++;
                        break;
                    }
                }
            }
        }
        
        // If multiple branch nodes connect to this node, it's the convergence
        if (incomingFromBranches >= 2) {
            return toNode;
        }
    }
    
    return -1;
}

// Check if a node is in a loop body (between cycle and cycle_end)
static bool is_in_loop_body(int nodeIdx, int cycleNode, int cycleEndNode, 
                            Connection* connections, int connectionCount, bool* visited) {
    if (nodeIdx == cycleNode || nodeIdx == cycleEndNode) return false;
    if (visited[nodeIdx]) return false;
    
    // Check if we can reach this node from cycle without going through cycle_end
    // This is a simplified check - in practice, we'd need proper graph traversal
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == cycleNode && connections[i].toNode == nodeIdx) {
            return true;
        }
        if (connections[i].fromNode == cycleEndNode && connections[i].toNode == nodeIdx) {
            return true;
        }
    }
    
    return false;
}

// Find cycle-end pair for a cycle node
static int find_cycle_end(int cycleNode, FlowNode* nodes, int nodeCount, 
                          Connection* connections, int connectionCount) {
    // First, try direct connection (for DO loops, cycle_end might connect directly to cycle)
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == cycleNode) {
            int target = connections[i].toNode;
            if (target >= 0 && target < nodeCount && nodes[target].type == NODE_CYCLE_END) {
                return target;
            }
        }
        if (connections[i].toNode == cycleNode) {
            int source = connections[i].fromNode;
            if (source >= 0 && source < nodeCount && nodes[source].type == NODE_CYCLE_END) {
                return source;
            }
        }
    }
    
    // If no direct connection, find cycle_end by traversing from cycle node
    // Look for a NODE_CYCLE_END that is reachable from the cycle node
    // and has a connection back to the cycle (loopback) or to a node outside the loop
    bool visited[MAX_NODES] = {false};
    int queue[MAX_NODES];
    int queueFront = 0, queueBack = 0;
    
    // Start BFS from cycle node's outgoing connections (body start)
    for (int i = 0; i < connectionCount; i++) {
        if (connections[i].fromNode == cycleNode) {
            int bodyStart = connections[i].toNode;
            if (bodyStart >= 0 && bodyStart < nodeCount && !visited[bodyStart]) {
                queue[queueBack++] = bodyStart;
                visited[bodyStart] = true;
            }
        }
    }
    
    // BFS to find cycle_end
    while (queueFront < queueBack) {
        int current = queue[queueFront++];
        
        // Check if this is a cycle_end
        if (nodes[current].type == NODE_CYCLE_END) {
            // Verify it's the right one by checking if it has a loopback to cycle
            // or connects to a node that's not directly connected from cycle
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].fromNode == current && connections[i].toNode == cycleNode) {
                    // This is a loopback - found the cycle_end
                    return current;
                }
            }
            // Also check if it connects to a node that's not in the immediate body
            // (for FOR/WHILE, cycle_end connects to exit, not back to cycle)
            bool connectsToExit = false;
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].fromNode == current) {
                    int target = connections[i].toNode;
                    // Check if target is not directly connected from cycle (exit node)
                    bool isDirectFromCycle = false;
                    for (int j = 0; j < connectionCount; j++) {
                        if (connections[j].fromNode == cycleNode && connections[j].toNode == target) {
                            isDirectFromCycle = true;
                            break;
                        }
                    }
                    if (!isDirectFromCycle && target != cycleNode) {
                        connectsToExit = true;
                        break;
                    }
                }
            }
            if (connectsToExit) {
                return current;
            }
        }
        
        // Continue BFS
        for (int i = 0; i < connectionCount; i++) {
            if (connections[i].fromNode == current) {
                int next = connections[i].toNode;
                if (next >= 0 && next < nodeCount && !visited[next] && next != cycleNode) {
                    queue[queueBack++] = next;
                    visited[next] = true;
                }
            }
        }
    }
    
    return -1;
}

// Find START node
static int find_start_node(FlowNode* nodes, int nodeCount) {
    for (int i = 0; i < nodeCount; i++) {
        if (nodes[i].type == NODE_START) {
            return i;
        }
    }
    return -1;
}

// Recursive function to export a node and its successors
// Returns the next node to continue from (after handling branches/loops)
static int export_node_recursive(FILE* file, int nodeIdx, FlowNode* nodes, int nodeCount,
                                 Connection* connections, int connectionCount,
                                 bool* visited, int* indentLevel, int* cycleTop,
                                 char* cycleTypeStack, char (*cycleCondStack)[MAX_VALUE_LENGTH],
                                 char (*cycleInitStack)[MAX_VALUE_LENGTH], char (*cycleIncrStack)[MAX_VALUE_LENGTH]) {
    if (nodeIdx < 0 || nodeIdx >= nodeCount || visited[nodeIdx]) {
        return -1;
    }
    
    visited[nodeIdx] = true;
    FlowNode* node = &nodes[nodeIdx];
    
    // Handle different node types
    switch (node->type) {
        case NODE_START:
            // Already handled by main() declaration
            break;
            
        case NODE_DECLARE: {
            char varName[MAX_VAR_NAME_LENGTH];
            VariableType varType;
            bool isArray;
            int arraySize;
            
            if (parse_declare_block(node->value, varName, &varType, &isArray, &arraySize)) {
                for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
                
                const char* cType = get_c_type_name(varType);
                if (isArray) {
                    if (varType == VAR_TYPE_STRING) {
                        if (arraySize > 0) {
                            fprintf(file, "%s %s[%d][256];\n", cType, varName, arraySize);
                        } else {
                            fprintf(file, "%s %s[256];\n", cType, varName);
                        }
                    } else {
                        if (arraySize > 0) {
                            fprintf(file, "%s %s[%d];\n", cType, varName, arraySize);
                        } else {
                            fprintf(file, "%s %s[];\n", cType, varName);
                        }
                    }
                } else {
                    if (varType == VAR_TYPE_STRING) {
                        fprintf(file, "%s %s[256];\n", cType, varName);
                    } else {
                        fprintf(file, "%s %s;\n", cType, varName);
                    }
                }
            }
            break;
        }
        
        case NODE_ASSIGNMENT: {
            char leftVar[MAX_VALUE_LENGTH];
            char rightValue[MAX_VALUE_LENGTH];
            
            if (parse_assignment(node->value, leftVar, rightValue)) {
                for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
                
                char leftVarName[MAX_VAR_NAME_LENGTH];
                int nameLen = 0;
                for (int i = 0; leftVar[i] != '\0' && leftVar[i] != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1; i++) {
                    leftVarName[nameLen++] = leftVar[i];
                }
                leftVarName[nameLen] = '\0';
                
                VarInfo* leftVarInfo = find_var(leftVarName);
                bool isStringAssignment = (leftVarInfo && leftVarInfo->type == VAR_TYPE_STRING);
                
                const char* origValue = node->value;
                const char* eqPos = strchr(origValue, '=');
                bool isQuotedString = false;
                if (eqPos) {
                    const char* afterEq = eqPos + 1;
                    while (*afterEq == ' ' || *afterEq == '\t') afterEq++;
                    if (*afterEq == '"') {
                        const char* endQuote = strchr(afterEq + 1, '"');
                        if (endQuote) {
                            const char* afterEnd = endQuote + 1;
                            while (*afterEnd == ' ' || *afterEnd == '\t') afterEnd++;
                            if (*afterEnd == '\0' || *afterEnd == '\n' || *afterEnd == '\r') {
                                isQuotedString = true;
                            }
                        }
                    }
                }
                
                if (isQuotedString) {
                    fprintf(file, "strcpy(%s, \"%s\");\n", leftVar, rightValue);
                } else if (isStringAssignment) {
                    fprintf(file, "%s = %s;\n", leftVar, rightValue);
                } else {
                    fprintf(file, "%s = %s;\n", leftVar, rightValue);
                }
            }
            break;
        }
        
        case NODE_INPUT: {
            char varName[MAX_VAR_NAME_LENGTH];
            char indexExpr[MAX_VALUE_LENGTH];
            bool isArray;
            
            if (parse_input_block(node->value, varName, indexExpr, &isArray)) {
                for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
                
                VarInfo* varInfo = find_var(varName);
                VariableType varType = varInfo ? varInfo->type : VAR_TYPE_INT;
                const char* format = get_scanf_format(varType);
                
                if (isArray) {
                    fprintf(file, "scanf(\"%s\", &%s[%s]);\n", format, varName, indexExpr);
                } else {
                    fprintf(file, "scanf(\"%s\", &%s);\n", format, varName);
                }
            }
            break;
        }
        
        case NODE_OUTPUT: {
            if (node->value[0] != '\0') {
                char varNames[100][MAX_VAR_NAME_LENGTH];
                char indexExprs[100][MAX_VALUE_LENGTH];
                bool isArrayAccess[100];
                int varCount = 0;
                
                extract_output_placeholders(node->value, varNames, indexExprs, isArrayAccess, &varCount);
                
                char formatStr[MAX_VALUE_LENGTH * 2];
                char argsStr[MAX_VALUE_LENGTH * 2] = "";
                int formatPos = 0;
                int argsPos = 0;
                
                const char* p = node->value;
                int placeholderIdx = 0;
                
                while (*p != '\0' && formatPos < (int)(sizeof(formatStr) - 1)) {
                    if (*p == '{') {
                        if (placeholderIdx < varCount) {
                            VarInfo* varInfo = find_var(varNames[placeholderIdx]);
                            VariableType varType = varInfo ? varInfo->type : VAR_TYPE_INT;
                            const char* format = get_printf_format(varType);
                            
                            int formatLen = strlen(format);
                            for (int i = 0; i < formatLen && formatPos < (int)(sizeof(formatStr) - 1); i++) {
                                formatStr[formatPos++] = format[i];
                            }
                            
                            if (argsPos > 0) {
                                argsStr[argsPos++] = ',';
                                argsStr[argsPos++] = ' ';
                            }
                            if (isArrayAccess[placeholderIdx]) {
                                int len = snprintf(argsStr + argsPos, sizeof(argsStr) - argsPos, 
                                                   "%s[%s]", varNames[placeholderIdx], indexExprs[placeholderIdx]);
                                argsPos += len;
                            } else {
                                int len = snprintf(argsStr + argsPos, sizeof(argsStr) - argsPos, 
                                                   "%s", varNames[placeholderIdx]);
                                argsPos += len;
                            }
                            placeholderIdx++;
                        }
                        while (*p != '\0' && *p != '}') p++;
                        if (*p == '}') p++;
                    } else {
                        formatStr[formatPos++] = *p++;
                    }
                }
                formatStr[formatPos] = '\0';
                argsStr[argsPos] = '\0';
                
                for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
                
                if (varCount > 0) {
                    fprintf(file, "printf(\"%s\", %s);\n", formatStr, argsStr);
                } else {
                    fprintf(file, "printf(\"%s\");\n", formatStr);
                }
            }
            break;
        }
        
        case NODE_PROCESS: {
            for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
            if (node->value[0] != '\0') {
                fprintf(file, "// Process: %s\n", node->value);
            } else {
                fprintf(file, "// Process\n");
            }
            break;
        }
        
        case NODE_END:
            return -1; // Stop traversal
            
        case NODE_IF: {
            // Find convergence node
            int convergeNode = find_convergence_for_if(nodeIdx, connections, connectionCount);
            
            // Find all connections from IF node
            int outNodes[10];
            int outCount = 0;
            find_connections_from(nodeIdx, connections, connectionCount, outNodes, &outCount, 10);
            
            
            // Generate if statement
            for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
            const char* condition = node->value[0] != '\0' ? node->value : "/* condition */";
            fprintf(file, "if (%s) {\n", condition);
            (*indentLevel)++;
            
            // Find true and false branches
            // In flowcharts, left branch (negative X) is typically true, right branch (positive X) is false
            int trueBranchStart = -1;
            int falseBranchStart = -1;
            double ifNodeX = nodes[nodeIdx].x;
            
            for (int i = 0; i < outCount; i++) {
                int targetNode = outNodes[i];
                if (targetNode != convergeNode && targetNode >= 0 && targetNode < nodeCount) {
                    double targetX = nodes[targetNode].x;
                    if (targetX < ifNodeX - 0.01) {
                        // Left branch (negative X relative to IF) - true branch
                        if (trueBranchStart < 0) {
                            trueBranchStart = targetNode;
                        }
                    } else if (targetX > ifNodeX + 0.01) {
                        // Right branch (positive X relative to IF) - false branch
                        if (falseBranchStart < 0) {
                            falseBranchStart = targetNode;
                        }
                    } else {
                        // Same X position - use order as fallback (first = true, second = false)
                        if (trueBranchStart < 0) {
                            trueBranchStart = targetNode;
                        } else if (falseBranchStart < 0) {
                            falseBranchStart = targetNode;
                        }
                    }
                }
            }
            
            
            // Export true branch
            if (trueBranchStart >= 0) {
                int nextNode = export_node_recursive(file, trueBranchStart, nodes, nodeCount,
                                                    connections, connectionCount, visited, indentLevel,
                                                    cycleTop, cycleTypeStack, cycleCondStack,
                                                    cycleInitStack, cycleIncrStack);
                // Continue until we reach convergence
                while (nextNode >= 0 && nextNode != convergeNode && nextNode < nodeCount) {
                    if (visited[nextNode]) break;
                    nextNode = export_node_recursive(file, nextNode, nodes, nodeCount,
                                                    connections, connectionCount, visited, indentLevel,
                                                    cycleTop, cycleTypeStack, cycleCondStack,
                                                    cycleInitStack, cycleIncrStack);
                }
            }
            
            (*indentLevel)--;
            
            // Only add else if there's a false branch
            if (falseBranchStart >= 0) {
                for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
                fprintf(file, "} else {\n");
                (*indentLevel)++;
                
                // Export false branch
                int nextNode = export_node_recursive(file, falseBranchStart, nodes, nodeCount,
                                                    connections, connectionCount, visited, indentLevel,
                                                    cycleTop, cycleTypeStack, cycleCondStack,
                                                    cycleInitStack, cycleIncrStack);
                while (nextNode >= 0 && nextNode != convergeNode && nextNode < nodeCount) {
                    if (visited[nextNode]) break;
                    nextNode = export_node_recursive(file, nextNode, nodes, nodeCount,
                                                    connections, connectionCount, visited, indentLevel,
                                                    cycleTop, cycleTypeStack, cycleCondStack,
                                                    cycleInitStack, cycleIncrStack);
                }
                
                (*indentLevel)--;
            }
            
            for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
            fprintf(file, "}\n");
            
            // Continue from convergence node
            if (convergeNode >= 0 && convergeNode < nodeCount) {
                return convergeNode;
            }
            break;
        }
        
        case NODE_CONVERGE:
            // Convergence point - just continue to next node
            break;
        
        case NODE_CYCLE: {
            char typeBuf[MAX_VAR_NAME_LENGTH];
            char condBuf[MAX_VALUE_LENGTH];
            char initBuf[MAX_VALUE_LENGTH];
            char incrBuf[MAX_VALUE_LENGTH];
            parse_cycle_value(node->value, typeBuf, condBuf, initBuf, incrBuf);
            
            char loopType = 'W';
            if (strncmp(typeBuf, "DO", 2) == 0) loopType = 'D';
            else if (strncmp(typeBuf, "FOR", 3) == 0) loopType = 'F';
            
            // Find cycle_end
            int cycleEndNode = find_cycle_end(nodeIdx, nodes, nodeCount, connections, connectionCount);
            
            // Push to stack
            if (*cycleTop < 32) {
                cycleTypeStack[*cycleTop] = loopType;
                strncpy(cycleCondStack[*cycleTop], condBuf, MAX_VALUE_LENGTH - 1);
                cycleCondStack[*cycleTop][MAX_VALUE_LENGTH - 1] = '\0';
                strncpy(cycleInitStack[*cycleTop], initBuf, MAX_VALUE_LENGTH - 1);
                cycleInitStack[*cycleTop][MAX_VALUE_LENGTH - 1] = '\0';
                strncpy(cycleIncrStack[*cycleTop], incrBuf, MAX_VALUE_LENGTH - 1);
                cycleIncrStack[*cycleTop][MAX_VALUE_LENGTH - 1] = '\0';
                (*cycleTop)++;
            }
            
            // Generate loop header
            for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
            if (loopType == 'F') {
                fprintf(file, "for (%s; %s; %s) {\n",
                        initBuf[0] ? initBuf : "/* init */",
                        condBuf[0] ? condBuf : "/* condition */",
                        incrBuf[0] ? incrBuf : "/* step */");
            } else if (loopType == 'D') {
                fprintf(file, "do {\n");
            } else {
                fprintf(file, "while (%s) {\n", condBuf[0] ? condBuf : "/* condition */");
            }
            (*indentLevel)++;
            
            // Find loop body start (first node connected from cycle that's not cycle_end)
            int bodyStart = -1;
            int outNodes[10];
            int outCount = 0;
            find_connections_from(nodeIdx, connections, connectionCount, outNodes, &outCount, 10);
            
            for (int i = 0; i < outCount; i++) {
                if (outNodes[i] != cycleEndNode && outNodes[i] >= 0 && outNodes[i] < nodeCount) {
                    bodyStart = outNodes[i];
                    break;
                }
            }
            
            // Export loop body
            if (bodyStart >= 0) {
                // Mark cycle_end as visited to prevent it from being processed during body traversal
                bool cycleEndWasVisited = (cycleEndNode >= 0 && cycleEndNode < nodeCount) ? visited[cycleEndNode] : false;
                if (cycleEndNode >= 0 && cycleEndNode < nodeCount) {
                    visited[cycleEndNode] = true;
                }
                
                int nextNode = bodyStart;
                int loopIterations = 0;
                const int MAX_LOOP_ITERATIONS = 100; // Prevent infinite loops
                while (nextNode >= 0 && nextNode != cycleEndNode && nextNode < nodeCount && loopIterations < MAX_LOOP_ITERATIONS) {
                    loopIterations++;
                    
                    // Stop if we're about to process the cycle_end node
                    if (nextNode == cycleEndNode) {
                        break;
                    }
                    
                    // Check if we've already visited this node (except for cycle node which we allow revisiting)
                    if (visited[nextNode] && nextNode != nodeIdx && nextNode != cycleEndNode) {
                        // Check if this node connects back to cycle (loopback)
                        bool connectsToCycle = false;
                        for (int i = 0; i < connectionCount; i++) {
                            if (connections[i].fromNode == nextNode && connections[i].toNode == nodeIdx) {
                                connectsToCycle = true;
                                break;
                            }
                            if (connections[i].fromNode == nextNode && connections[i].toNode == cycleEndNode) {
                                connectsToCycle = true;
                                break;
                            }
                        }
                        if (!connectsToCycle) break;
                    }
                    
                    int prevNext = nextNode;
                    nextNode = export_node_recursive(file, nextNode, nodes, nodeCount,
                                                    connections, connectionCount, visited, indentLevel,
                                                    cycleTop, cycleTypeStack, cycleCondStack,
                                                    cycleInitStack, cycleIncrStack);
                    
                    if (nextNode == prevNext || nextNode == -1) break;
                    // If we loop back to cycle, we're done with the body
                    if (nextNode == nodeIdx) break;
                    // If we reached cycle_end, stop
                    if (nextNode == cycleEndNode) {
                        break;
                    }
                }
                
                // Restore cycle_end visited state (it will be handled after the loop closes)
                if (cycleEndNode >= 0 && cycleEndNode < nodeCount) {
                    visited[cycleEndNode] = cycleEndWasVisited;
                }
            }
            
            // Pop from stack and close loop
            if (*cycleTop > 0) {
                (*indentLevel)--;
                char loopType = cycleTypeStack[*cycleTop - 1];
                const char* cond = cycleCondStack[*cycleTop - 1];
                (*cycleTop)--;
                
                for (int i = 0; i < *indentLevel; i++) fprintf(file, "    ");
                if (loopType == 'D') {
                    fprintf(file, "} while (%s);\n", cond[0] ? cond : "/* condition */");
                } else {
                    fprintf(file, "}\n");
                }
            }
            
            // Continue from after cycle_end
            if (cycleEndNode >= 0 && cycleEndNode < nodeCount) {
                int exitNode = find_next_node(cycleEndNode, connections, connectionCount);
                if (exitNode == nodeIdx) {
                    // This is the loopback, find the actual exit
                    for (int i = 0; i < connectionCount; i++) {
                        if (connections[i].fromNode == cycleEndNode && connections[i].toNode != nodeIdx) {
                            exitNode = connections[i].toNode;
                            break;
                        }
                    }
                }
                return exitNode;
            }
            break;
        }
        
        case NODE_CYCLE_END:
            // Should be handled within CYCLE case
            // If we reach here, it means we're outside a loop context - just continue to next node
            // Don't process it, just skip to the node after it
            return find_next_node(nodeIdx, connections, connectionCount);
            
        default:
            break;
    }
    
    // Default: continue to next node
    return find_next_node(nodeIdx, connections, connectionCount);
}

// Export to C code
static bool export_to_c(const char* filename, struct FlowNode* nodes, int nodeCount, 
                        struct Connection* connections, int connectionCount) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Failed to open file for writing: %s\n", filename);
        return false;
    }
    
    
    // Build variable table
    build_var_table(nodes, nodeCount);
    
    // Write includes
    fprintf(file, "#include <stdio.h>\n");
    fprintf(file, "#include <stdbool.h>\n");
    if (has_string_variables()) {
        fprintf(file, "#include <string.h>\n");
    }
    fprintf(file, "\n");
    
    // Find START node
    int startNode = find_start_node(nodes, nodeCount);
    if (startNode < 0) {
        fprintf(stderr, "No START node found in flowchart\n");
        fclose(file);
        return false;
    }
    
    
    // Write main function
    fprintf(file, "int main(void) {\n");
    
    // Initialize traversal state
    bool visited[MAX_NODES] = {false};
    int indentLevel = 1;
    
    // Cycle stack
    char cycleTypeStack[32];
    char cycleCondStack[32][MAX_VALUE_LENGTH];
    char cycleInitStack[32][MAX_VALUE_LENGTH];
    char cycleIncrStack[32][MAX_VALUE_LENGTH];
    int cycleTop = 0;
    
    // Recursively export from START
    int currentNode = startNode;
    while (currentNode >= 0 && currentNode < nodeCount) {
        currentNode = export_node_recursive(file, currentNode, nodes, nodeCount,
                                            connections, connectionCount, visited, &indentLevel,
                                            &cycleTop, cycleTypeStack, cycleCondStack,
                                            cycleInitStack, cycleIncrStack);
        if (currentNode < 0) break;
    }
    
    // Write return statement
    for (int i = 0; i < indentLevel; i++) fprintf(file, "    ");
    fprintf(file, "return 0;\n");
    fprintf(file, "}\n");
    
    fclose(file);
    return true;
}

// Main export function
bool export_to_code(const char* filename, const char* language, 
                    struct FlowNode* nodes, int nodeCount,
                    struct Connection* connections, int connectionCount) {
    if (strcmp(language, "C") == 0) {
        return export_to_c(filename, nodes, nodeCount, connections, connectionCount);
    } else {
        fprintf(stderr, "Unsupported language: %s\n", language);
        return false;
    }
}


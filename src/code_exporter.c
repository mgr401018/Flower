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
    NODE_DECLARE = 7
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

// Find START node
static int find_start_node(FlowNode* nodes, int nodeCount) {
    for (int i = 0; i < nodeCount; i++) {
        if (nodes[i].type == NODE_START) {
            return i;
        }
    }
    return -1;
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
    
    // Traverse flowchart from START to END
    bool visited[MAX_NODES] = {false};
    int currentNode = startNode;
    int indentLevel = 1;
    
    while (currentNode >= 0 && currentNode < nodeCount && !visited[currentNode]) {
        visited[currentNode] = true;
        FlowNode* node = &nodes[currentNode];
        
        // Generate code based on node type
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
                    // Indent
                    for (int i = 0; i < indentLevel; i++) fprintf(file, "    ");
                    
                    const char* cType = get_c_type_name(varType);
                    if (isArray) {
                        if (varType == VAR_TYPE_STRING) {
                            // String arrays need special handling
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
                    // Indent
                    for (int i = 0; i < indentLevel; i++) fprintf(file, "    ");
                    
                    // Extract variable name from left side (could be array access)
                    char leftVarName[MAX_VAR_NAME_LENGTH];
                    int nameLen = 0;
                    for (int i = 0; leftVar[i] != '\0' && leftVar[i] != '[' && nameLen < MAX_VAR_NAME_LENGTH - 1; i++) {
                        leftVarName[nameLen++] = leftVar[i];
                    }
                    leftVarName[nameLen] = '\0';
                    
                    // Check if left variable is a string
                    VarInfo* leftVarInfo = find_var(leftVarName);
                    bool isStringAssignment = (leftVarInfo && leftVarInfo->type == VAR_TYPE_STRING);
                    
                    // Check if right value is a quoted string by looking at original string
                    // Find the '=' sign and check if what follows starts with a quote
                    const char* origValue = node->value;
                    const char* eqPos = strchr(origValue, '=');
                    bool isQuotedString = false;
                    if (eqPos) {
                        const char* afterEq = eqPos + 1;
                        // Skip whitespace
                        while (*afterEq == ' ' || *afterEq == '\t') afterEq++;
                        // Check if it starts with a quote
                        if (*afterEq == '"') {
                            // Find the matching closing quote
                            const char* endQuote = strchr(afterEq + 1, '"');
                            if (endQuote) {
                                // Check if there's nothing after the closing quote (or just whitespace)
                                const char* afterEnd = endQuote + 1;
                                while (*afterEnd == ' ' || *afterEnd == '\t') afterEnd++;
                                if (*afterEnd == '\0' || *afterEnd == '\n' || *afterEnd == '\r') {
                                    isQuotedString = true;
                                }
                            }
                        }
                    }
                    
                    // If it's a quoted string, it must be a string assignment - use strcpy
                    // Even if we can't find the variable in the table, quoted strings can only be assigned to strings
                    if (isQuotedString) {
                        // Use strcpy for string assignments - need to reconstruct quoted string
                        fprintf(file, "strcpy(%s, \"%s\");\n", leftVar, rightValue);
                    } else if (isStringAssignment) {
                        // String variable but not a quoted string (could be another string variable)
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
                    // Indent
                    for (int i = 0; i < indentLevel; i++) fprintf(file, "    ");
                    
                    // Look up variable type
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
                    
                    // Build printf format string and arguments
                    char formatStr[MAX_VALUE_LENGTH * 2];
                    char argsStr[MAX_VALUE_LENGTH * 2] = "";
                    int formatPos = 0;
                    int argsPos = 0;
                    
                    const char* p = node->value;
                    int placeholderIdx = 0;
                    
                    while (*p != '\0' && formatPos < (int)(sizeof(formatStr) - 1)) {
                        if (*p == '{') {
                            // Replace placeholder
                            if (placeholderIdx < varCount) {
                                // Look up variable type and get format specifier
                                VarInfo* varInfo = find_var(varNames[placeholderIdx]);
                                VariableType varType = varInfo ? varInfo->type : VAR_TYPE_INT;
                                const char* format = get_printf_format(varType);
                                
                                // Add format specifier
                                int formatLen = strlen(format);
                                for (int i = 0; i < formatLen && formatPos < (int)(sizeof(formatStr) - 1); i++) {
                                    formatStr[formatPos++] = format[i];
                                }
                                
                                // Add argument
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
                            // Skip to closing brace
                            while (*p != '\0' && *p != '}') p++;
                            if (*p == '}') p++;
                        } else {
                            formatStr[formatPos++] = *p++;
                        }
                    }
                    formatStr[formatPos] = '\0';
                    argsStr[argsPos] = '\0';
                    
                    // Indent
                    for (int i = 0; i < indentLevel; i++) fprintf(file, "    ");
                    
                    if (varCount > 0) {
                        fprintf(file, "printf(\"%s\", %s);\n", formatStr, argsStr);
                    } else {
                        fprintf(file, "printf(\"%s\");\n", formatStr);
                    }
                }
                break;
            }
            
            case NODE_PROCESS: {
                // Indent
                for (int i = 0; i < indentLevel; i++) fprintf(file, "    ");
                if (node->value[0] != '\0') {
                    fprintf(file, "// Process: %s\n", node->value);
                } else {
                    fprintf(file, "// Process\n");
                }
                break;
            }
            
            case NODE_END:
                // Will be handled after loop
                break;
                
            default:
                break;
        }
        
        // Move to next node
        if (node->type == NODE_END) {
            break;
        }
        currentNode = find_next_node(currentNode, connections, connectionCount);
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


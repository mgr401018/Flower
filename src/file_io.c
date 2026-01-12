#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "flowchart_state.h"
#include "text_renderer.h"

// Forward declarations for helper functions (defined in main.c)
double snap_to_grid_x(double x);
double snap_to_grid_y(double y);
float calculate_block_width(const char* text, float fontSize, float minWidth);
void update_all_branch_positions(void);
void reposition_convergence_point(int ifBlockIndex, bool shouldPushNodesBelow);
void rebuild_variable_table(void);
void save_state_for_undo(void);

// Grid helper functions (needed by load_flowchart)
static double grid_to_world_x(int gridX) {
    extern const double GRID_CELL_SIZE;
    return gridX * GRID_CELL_SIZE;
}

static double grid_to_world_y(int gridY) {
    extern const double GRID_CELL_SIZE;
    return gridY * GRID_CELL_SIZE;
}

static int world_to_grid_x(double x) {
    extern const double GRID_CELL_SIZE;
    return (int)round(x / GRID_CELL_SIZE);
}

static int world_to_grid_y(double y) {
    extern const double GRID_CELL_SIZE;
    return (int)round(y / GRID_CELL_SIZE);
}

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
    
    // Final pass: ensure all branchColumn values are correct after all updates
    // This is critical for connection shapes to render correctly
    for (int i = 0; i < ifBlockCount; i++) {
        // Update true branch nodes
        for (int j = 0; j < ifBlocks[i].trueBranchCount; j++) {
            int nodeIdx = ifBlocks[i].trueBranchNodes[j];
            if (nodeIdx >= 0 && nodeIdx < nodeCount) {
                nodes[nodeIdx].branchColumn = ifBlocks[i].branchColumn - 2;
                nodes[nodeIdx].owningIfBlock = i;
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
                nodes[nodeIdx].owningIfBlock = i;
            }
        }
        
        // Ensure IF and convergence nodes have correct branchColumn
        if (ifBlocks[i].ifNodeIndex >= 0 && ifBlocks[i].ifNodeIndex < nodeCount) {
            nodes[ifBlocks[i].ifNodeIndex].branchColumn = ifBlocks[i].branchColumn;
        }
        if (ifBlocks[i].convergeNodeIndex >= 0 && ifBlocks[i].convergeNodeIndex < nodeCount) {
            nodes[ifBlocks[i].convergeNodeIndex].branchColumn = ifBlocks[i].branchColumn;
        }
    }
    
    // One final update to ensure all positions are correct
    update_all_branch_positions();
    
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
    
    // Reset undo history after loading
    undoHistoryCount = 0;
    undoHistoryIndex = -1;
    save_state_for_undo();  // Save initial loaded state
}

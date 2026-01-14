#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "flowchart_state.h"
#include "text_renderer.h"
#include "file_io.h"
#include "code_exporter.h"
#define TINYFD_NOLIB
#include "../imports/tinyfiledialogs.h"

// Forward declarations for helper functions (defined in main.c)
void rebuild_variable_table(void);
int get_if_branch_type(int connIndex);
void reposition_convergence_point(int ifBlockIndex, bool shouldPushNodesBelow);
void update_all_branch_positions(void);
bool is_valid_if_converge_connection(int fromNode, int toNode);
void save_state_for_undo(void);
void perform_undo(void);
void perform_redo(void);
int hit_node(double x, double y);
int hit_connection(double x, double y, float threshold);
bool cursor_over_button(float buttonX, float buttonY, GLFWwindow* window);
double snap_to_grid_x(double x);
double snap_to_grid_y(double y);
float calculate_block_width(const char* text, float fontSize, float minWidth);
double grid_to_world_x(int gridX);
double grid_to_world_y(int gridY);
int world_to_grid_x(double x);
int world_to_grid_y(double y);
bool parse_declare_block(const char* value, char* varName, VariableType* varType, bool* isArray, int* arraySize);
bool parse_assignment(const char* value, char* leftVar, char* rightValue, bool* isRightVar, bool* isQuotedString);
VariableType detect_literal_type(const char* value);
bool parse_array_access(const char* expr, char* arrayName, char* indexExpr);
bool evaluate_index_expression(const char* indexExpr, int* result, char* errorMsg);
bool check_array_bounds(const char* arrayName, const char* indexExpr, char* errorMsg);
void extract_array_accesses(const char* expr, char arrayNames[][MAX_VAR_NAME_LENGTH], char indexExprs[][MAX_VALUE_LENGTH], int* accessCount);
void extract_variables_from_expression_simple(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount);
void extract_output_placeholders(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount);
void extract_output_placeholders_with_arrays(const char* formatStr, char varNames[][MAX_VAR_NAME_LENGTH], char indexExprs[][MAX_VALUE_LENGTH], bool* isArrayAccess, int* varCount);
bool parse_input_block(const char* value, char* varName, char* indexExpr, bool* isArray);
Variable* find_variable(const char* name);
_Bool is_valid_variable_name(const char* name);
bool variable_name_exists(const char* name, int excludeNodeIndex);
void extract_variables_from_expression(const char* expr, char varNames[][MAX_VAR_NAME_LENGTH], int* varCount);
bool validate_expression(const char* expr, VariableType expectedType, VariableType* actualType, char* errorMsg);
bool validate_assignment(const char* value);
CycleType prompt_cycle_type(void);
// tinyfd_listDialog implementation
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
            
            // Rebuild variable table after deletion
            rebuild_variable_table();
            
            // Save state AFTER operation completes (for redo to work correctly)
            save_state_for_undo();
            
            return;  // Done handling IF/CONVERGE deletion
        }
    }
    
    // Special handling for CYCLE and CYCLE_END nodes
    if (nodes[nodeIndex].type == NODE_CYCLE || nodes[nodeIndex].type == NODE_CYCLE_END) {
        // Find the cycle block that this node belongs to
        int cycleBlockIndex = -1;
        if (nodes[nodeIndex].type == NODE_CYCLE) {
            cycleBlockIndex = find_cycle_block_by_cycle_node(nodeIndex);
        } else {
            cycleBlockIndex = find_cycle_block_by_end_node(nodeIndex);
        }
        
        if (cycleBlockIndex >= 0) {
            CycleBlock *cycle = &cycleBlocks[cycleBlockIndex];
            int cycleIdx = cycle->cycleNodeIndex;
            int endIdx = cycle->cycleEndNodeIndex;
            
            // Find the connection coming into the cycle block
            int incomingFromNode = -1;
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].toNode == cycleIdx && !is_cycle_loopback(i)) {
                    incomingFromNode = connections[i].fromNode;
                    break;
                }
            }
            
            // Find the connection going out of the cycle end
            int outgoingToNode = -1;
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].fromNode == endIdx && !is_cycle_loopback(i)) {
                    outgoingToNode = connections[i].toNode;
                    break;
                }
            }
            
            // Find all nodes inside the cycle using BFS
            // Start from cycle node's outgoing connections (body start), exclude loopback
            int nodesInside[MAX_NODES];
            int nodesInsideCount = 0;
            bool visited[MAX_NODES] = {false};
            
            // Mark cycle and end nodes as visited (we don't want to include them in body)
            visited[cycleIdx] = true;
            visited[endIdx] = true;
            
            // BFS queue
            int queue[MAX_NODES];
            int queueFront = 0, queueBack = 0;
            
            // Start BFS from cycle node's outgoing connections (body start)
            for (int i = 0; i < connectionCount; i++) {
                if (connections[i].fromNode == cycleIdx && !is_cycle_loopback(i)) {
                    int bodyStart = connections[i].toNode;
                    if (bodyStart >= 0 && bodyStart < nodeCount && !visited[bodyStart]) {
                        queue[queueBack++] = bodyStart;
                        visited[bodyStart] = true;
                    }
                }
            }
            
            // BFS to find all nodes in the loop body
            while (queueFront < queueBack) {
                int current = queue[queueFront++];
                
                // Don't include cycle end node
                if (current == endIdx) {
                    continue;
                }
                
                // Add to body nodes
                nodesInside[nodesInsideCount++] = current;
                
                // Continue BFS from this node
                for (int i = 0; i < connectionCount; i++) {
                    if (connections[i].fromNode == current) {
                        int next = connections[i].toNode;
                        // Don't follow loopback connections or connections to cycle/end nodes
                        if (next >= 0 && next < nodeCount && !visited[next] && 
                            next != cycleIdx && next != endIdx &&
                            !is_cycle_loopback(i)) {
                            queue[queueBack++] = next;
                            visited[next] = true;
                        }
                    }
                }
            }
            
            // Remove all connections involving cycle, end, or body nodes
            for (int i = connectionCount - 1; i >= 0; i--) {
                bool shouldDelete = false;
                
                // Check if connection involves cycle or end node
                if (connections[i].fromNode == cycleIdx || connections[i].toNode == cycleIdx ||
                    connections[i].fromNode == endIdx || connections[i].toNode == endIdx) {
                    shouldDelete = true;
                }
                
                // Check if connection involves any body node
                for (int j = 0; j < nodesInsideCount; j++) {
                    if (connections[i].fromNode == nodesInside[j] || connections[i].toNode == nodesInside[j]) {
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
            
            // Build list of all nodes to delete: cycle, end, and all body nodes
            // Sort from highest to lowest index to avoid shifting issues
            int nodesToDelete[MAX_NODES];
            int deleteCount = 0;
            nodesToDelete[deleteCount++] = cycleIdx;
            nodesToDelete[deleteCount++] = endIdx;
            for (int i = 0; i < nodesInsideCount; i++) {
                nodesToDelete[deleteCount++] = nodesInside[i];
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
                
                // Update cycle blocks to account for shifted indices
                for (int j = 0; j < cycleBlockCount; j++) {
                    if (cycleBlocks[j].cycleNodeIndex > delIdx) {
                        cycleBlocks[j].cycleNodeIndex--;
                    }
                    if (cycleBlocks[j].cycleEndNodeIndex > delIdx) {
                        cycleBlocks[j].cycleEndNodeIndex--;
                    }
                }
            }
            
            // Remove the cycle block
            for (int i = cycleBlockIndex; i < cycleBlockCount - 1; i++) {
                cycleBlocks[i] = cycleBlocks[i + 1];
            }
            cycleBlockCount--;
            
            // Update parent cycle indices
            for (int i = 0; i < cycleBlockCount; i++) {
                if (cycleBlocks[i].parentCycleIndex > cycleBlockIndex) {
                    cycleBlocks[i].parentCycleIndex--;
                }
            }
            
            // Update owningIfBlock for all remaining nodes
            // Any nodes that were owned by IF blocks need their index checked
            for (int i = 0; i < nodeCount; i++) {
                // Check if this node's owningIfBlock references a deleted node
                // (This shouldn't happen for cycle nodes, but we check for safety)
                if (nodes[i].owningIfBlock >= 0) {
                    // The owningIfBlock index should still be valid after deletions
                    // since we only deleted cycle-related nodes, not IF blocks
                }
            }
            
            // Rebuild variable table after deletion
            rebuild_variable_table();
            
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
                        // Move all nodes below the outgoing node up
                        for (int i = 0; i < nodeCount; i++) {
                            if (nodes[i].y <= outgoing->y) {
                                nodes[i].y = snap_to_grid_y(nodes[i].y + deltaY);
                            }
                        }
                    }
                }
            }
            
            // Rebuild variable table after deletion
            rebuild_variable_table();
            
            // Save state AFTER operation completes (for redo to work correctly)
            save_state_for_undo();
            
            return;  // Done handling CYCLE/CYCLE_END deletion
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
    
    // Save state AFTER operation completes (for redo to work correctly)
    save_state_for_undo();
}

void edit_node_value(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= nodeCount) {
        return;
    }
    
    save_state_for_undo();
    
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
    
    // Save state AFTER operation completes (for redo to work correctly)
    save_state_for_undo();
}

void insert_node_in_connection(int connIndex, NodeType nodeType) {
    if (nodeCount >= MAX_NODES || connectionCount >= MAX_CONNECTIONS) {
        return;
    }
    
    // Save state BEFORE the operation (for undo)
    save_state_for_undo();
    
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
            // CRITICAL FIX: Always check which branch array contains the from node
            // BranchColumn can be ambiguous for deeply nested IFs (e.g., false->false->true
            // can have positive branchColumn values). The branch arrays are the source of truth.
            bool addToTrueBranch = false;
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
            } else {
                // Fallback: If from node is not in branch arrays (shouldn't happen for regular nodes),
                // use branchColumn as fallback, or if from is an IF node, use get_if_branch_type
                if (from->type == NODE_IF) {
                    int actualBranchType = get_if_branch_type(connIndex);
                    if (actualBranchType >= 0) {
                        addToTrueBranch = (actualBranchType == 0);
                    } else {
                        // Ultimate fallback: use branchColumn
                        addToTrueBranch = (from->branchColumn < 0);
                    }
                } else {
                    // Fallback: use branchColumn
                    addToTrueBranch = (from->branchColumn < 0);
                }
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
    
    // Save state AFTER operation completes (for redo to work correctly)
    save_state_for_undo();
}

// Calculate the depth (in grid cells) of a branch, recursively accounting for nested IFs
// branchType: 0 = true/left, 1 = false/right
// Returns: depth in grid cells from IF node to end of branch
void insert_if_block_in_connection(int connIndex) {
    save_state_for_undo();
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
    ifNode->height = 0.525f;  // 1.5x larger for diamond shape (0.35 * 1.5)
    ifNode->width = 0.525f;   // 1.5x larger for diamond shape (0.35 * 1.5)
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
    
    // Save state AFTER operation completes (for redo to work correctly)
    save_state_for_undo();
}

// Get all parent IF blocks from a given IF block to the root
// Returns the number of parent IF blocks found (up to maxParents)
// The parents array is filled from immediate parent to root (reverse order)
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    
    // Undo: Ctrl+Z
    if (key == GLFW_KEY_Z && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && !(mods & GLFW_MOD_SHIFT)) {
        perform_undo();
        return;
    }
    
    // Redo: Ctrl+Shift+Z
    if (key == GLFW_KEY_Z && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT)) {
        perform_redo();
        return;
    }
    
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
                scrollOffsetY += scrollSpeed;  // Flipped: up arrow moves down
                break;
            case GLFW_KEY_DOWN:
                scrollOffsetY -= scrollSpeed;  // Flipped: down arrow moves up
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
void insert_cycle_block_in_connection(int connIndex) {
    save_state_for_undo();
    if (nodeCount + 2 >= MAX_NODES || connectionCount + 2 >= MAX_CONNECTIONS || cycleBlockCount >= MAX_CYCLE_BLOCKS) {
        return;
    }
    
    Connection oldConn = connections[connIndex];
    FlowNode *from = &nodes[oldConn.fromNode];
    FlowNode *to = &nodes[oldConn.toNode];
    
    double originalToY = to->y;
    int fromGridY = world_to_grid_y(from->y);
    
    // Calculate branch position (similar to insert_node_in_connection)
    int targetBranchColumn = from->branchColumn;
    double targetX = from->x;
    int cycleOwningIfBlock = from->owningIfBlock;
    int branchType = -1;
    
    // Determine branch position if in an IF branch
    // For nodes already in a branch, use from->x directly (it's already positioned correctly)
    // For nodes directly from an IF, calculate based on IF center and branch widths
    if (from->owningIfBlock >= 0 && from->owningIfBlock < ifBlockCount && from->type != NODE_IF) {
        // Node is already in a branch - use its current X position
        // This works for both top-level and nested IF branches because
        // update_branch_x_positions has already positioned it correctly
        targetX = from->x;
        targetBranchColumn = from->branchColumn;
        cycleOwningIfBlock = from->owningIfBlock;
    } else if (from->type == NODE_IF) {
        // Inserting directly from IF block - determine branch from connection
        branchType = get_if_branch_type(connIndex);
        
        int ifBlockIdx = -1;
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                ifBlockIdx = i;
                break;
            }
        }
        
        if (ifBlockIdx >= 0) {
            double leftWidth = ifBlocks[ifBlockIdx].leftBranchWidth;
            double rightWidth = ifBlocks[ifBlockIdx].rightBranchWidth;
            
            if (branchType == 0) {
                // True branch (left)
                targetBranchColumn = from->branchColumn - 2;
                targetX = from->x - leftWidth;
            } else if (branchType == 1) {
                // False branch (right)
                int falseBranchColumn = from->branchColumn + 2;
                if (falseBranchColumn <= 0) {
                    falseBranchColumn = abs(from->branchColumn) + 2;
                }
                targetBranchColumn = falseBranchColumn;
                targetX = from->x + rightWidth;
            }
            cycleOwningIfBlock = ifBlockIdx;
        }
    }
    
    // Default placement (WHILE/FOR): cycle block then end point one grid below
    int cycleGridY = fromGridY - 1;
    int endGridY = cycleGridY - 1;
    
    // Create cycle block
    int cycleNodeIndex = nodeCount;
    FlowNode *cycleNode = &nodes[nodeCount++];
    cycleNode->x = snap_to_grid_x(targetX);  // Use calculated branch position
    double cycleWorldY = grid_to_world_y(cycleGridY);
    cycleNode->y = snap_to_grid_y(cycleWorldY);
    cycleNode->height = 0.26f;
    cycleNode->width = 0.34f;
    cycleNode->value[0] = '\0';
    cycleNode->type = NODE_CYCLE;
    cycleNode->branchColumn = targetBranchColumn;  // Use calculated branch column
    cycleNode->owningIfBlock = cycleOwningIfBlock;
    
    // Create cycle end point
    int endNodeIndex = nodeCount;
    FlowNode *endNode = &nodes[nodeCount++];
    endNode->x = cycleNode->x;
    double endWorldY = grid_to_world_y(endGridY);
    endNode->y = snap_to_grid_y(endWorldY);
    endNode->height = 0.12f;
    endNode->width = 0.12f;
    endNode->value[0] = '\0';
    endNode->type = NODE_CYCLE_END;
    endNode->branchColumn = targetBranchColumn;  // Use calculated branch column
    endNode->owningIfBlock = cycleOwningIfBlock;
    
    // CRITICAL FIX: Ensure end point is always below cycle block (at least one grid cell)
    // This prevents them from being on top of each other
    // In this coordinate system, Y decreases downward (more negative = lower)
    // So end should be MORE negative (lower) than cycle
    double minEndY = cycleNode->y - GRID_CELL_SIZE;  // End should be at least one grid cell below cycle
    int cycleGridYAfterSnap = world_to_grid_y(cycleNode->y);
    int endGridYAfterSnap = world_to_grid_y(endNode->y);
    
    // Check if they're in the same grid cell or end is too close
    if (endGridYAfterSnap >= cycleGridYAfterSnap) {
        // End is at same or higher grid cell - force it to be one grid cell below
        int requiredEndGridY = cycleGridYAfterSnap - 1;
        endNode->y = snap_to_grid_y(grid_to_world_y(requiredEndGridY));
    }
    
    // CRITICAL FIX: If cycle is in a nested IF branch, ensure end point is above convergence
    // Check if the cycle is in a nested IF (one that has a parent)
    if (cycleOwningIfBlock >= 0 && cycleOwningIfBlock < ifBlockCount) {
        int nestedIfParentIdx = ifBlocks[cycleOwningIfBlock].parentIfIndex;
        if (nestedIfParentIdx >= 0 && nestedIfParentIdx < ifBlockCount) {
            // This is a nested IF - check its convergence point
            int nestedConvergeIdx = ifBlocks[cycleOwningIfBlock].convergeNodeIndex;
            if (nestedConvergeIdx >= 0 && nestedConvergeIdx < nodeCount) {
                double convergeY = nodes[nestedConvergeIdx].y;
                int convergeGridY = world_to_grid_y(convergeY);
                int currentEndGridY = world_to_grid_y(endNode->y);
                
                // Ensure cycle end point is above convergence (convergeY is more negative, so endY should be less negative)
                // But also ensure it's still below the cycle block
                if (currentEndGridY <= convergeGridY) {
                    // End is at or below convergence - position it above convergence with spacing
                    int requiredEndGridY = convergeGridY + 1;  // One grid cell above convergence
                    // But ensure it's still below the cycle block
                    int cycleGridY = world_to_grid_y(cycleNode->y);
                    if (requiredEndGridY >= cycleGridY) {
                        // Can't be above convergence and below cycle - prioritize being below cycle
                        requiredEndGridY = cycleGridY - 1;
                    }
                    endNode->y = snap_to_grid_y(grid_to_world_y(requiredEndGridY));
                }
            }
        }
    }
    
    // Add cycle nodes to IF branch arrays (similar to insert_node_in_connection)
    bool cycleAddedToBranch = false;
    if (from->type == NODE_IF) {
        // Inserting directly from IF block
        for (int i = 0; i < ifBlockCount; i++) {
            if (ifBlocks[i].ifNodeIndex == oldConn.fromNode) {
                int resolvedBranchType = (branchType >= 0) ? branchType : get_if_branch_type(connIndex);
                if (resolvedBranchType == 0) {
                    // True branch (left)
                    if (ifBlocks[i].trueBranchCount + 1 < MAX_NODES) {  // Need space for both cycle and end nodes
                        ifBlocks[i].trueBranchNodes[ifBlocks[i].trueBranchCount] = cycleNodeIndex;
                        ifBlocks[i].trueBranchCount++;
                        // Also add end node to branch array so it's counted in depth calculation
                        ifBlocks[i].trueBranchNodes[ifBlocks[i].trueBranchCount] = endNodeIndex;
                        ifBlocks[i].trueBranchCount++;
                        cycleNode->owningIfBlock = i;
                        endNode->owningIfBlock = i;
                        cycleOwningIfBlock = i;
                        cycleAddedToBranch = true;
                    }
                } else if (resolvedBranchType == 1) {
                    // False branch (right)
                    if (ifBlocks[i].falseBranchCount + 1 < MAX_NODES) {  // Need space for both cycle and end nodes
                        ifBlocks[i].falseBranchNodes[ifBlocks[i].falseBranchCount] = cycleNodeIndex;
                        ifBlocks[i].falseBranchCount++;
                        // Also add end node to branch array so it's counted in depth calculation
                        ifBlocks[i].falseBranchNodes[ifBlocks[i].falseBranchCount] = endNodeIndex;
                        ifBlocks[i].falseBranchCount++;
                        cycleNode->owningIfBlock = i;
                        endNode->owningIfBlock = i;
                        cycleOwningIfBlock = i;
                        cycleAddedToBranch = true;
                    }
                }
                break;
            }
        }
    } else if (from->owningIfBlock >= 0 && from->owningIfBlock < ifBlockCount) {
        // Inserting from a node that's already in a branch
        int relevantIfBlock = from->owningIfBlock;
        
        // CRITICAL FIX: For nested IFs, check which branch array the from node is actually in
        // This is especially important for nested IF branches where branchColumn might be 0
        bool addToTrueBranch = (from->branchColumn < 0);
        
        // Check if from node is actually in a branch array to determine correct branch
        if (from->branchColumn == 0 && from->owningIfBlock >= 0 && from->owningIfBlock < ifBlockCount) {
            // Check which branch array the from node is actually in
            bool foundInTrueBranch = false;
            bool foundInFalseBranch = false;
            
            for (int i = 0; i < ifBlocks[relevantIfBlock].trueBranchCount; i++) {
                if (ifBlocks[relevantIfBlock].trueBranchNodes[i] == oldConn.fromNode) {
                    foundInTrueBranch = true;
                    break;
                }
            }
            if (!foundInTrueBranch) {
                for (int i = 0; i < ifBlocks[relevantIfBlock].falseBranchCount; i++) {
                    if (ifBlocks[relevantIfBlock].falseBranchNodes[i] == oldConn.fromNode) {
                        foundInFalseBranch = true;
                        break;
                    }
                }
            }
            
            if (foundInTrueBranch) {
                addToTrueBranch = true;
            } else if (foundInFalseBranch) {
                addToTrueBranch = false;
            }
        }
        
        if (addToTrueBranch) {
            // Add to true branch
            if (ifBlocks[relevantIfBlock].trueBranchCount + 1 < MAX_NODES) {  // Need space for both cycle and end nodes
                ifBlocks[relevantIfBlock].trueBranchNodes[ifBlocks[relevantIfBlock].trueBranchCount] = cycleNodeIndex;
                ifBlocks[relevantIfBlock].trueBranchCount++;
                // Also add end node to branch array so it's counted in depth calculation
                ifBlocks[relevantIfBlock].trueBranchNodes[ifBlocks[relevantIfBlock].trueBranchCount] = endNodeIndex;
                ifBlocks[relevantIfBlock].trueBranchCount++;
                cycleNode->owningIfBlock = relevantIfBlock;
                endNode->owningIfBlock = relevantIfBlock;
                cycleOwningIfBlock = relevantIfBlock;
                cycleAddedToBranch = true;
            }
        } else if (from->branchColumn > 0) {
            // Add to false branch
            if (ifBlocks[relevantIfBlock].falseBranchCount + 1 < MAX_NODES) {  // Need space for both cycle and end nodes
                ifBlocks[relevantIfBlock].falseBranchNodes[ifBlocks[relevantIfBlock].falseBranchCount] = cycleNodeIndex;
                ifBlocks[relevantIfBlock].falseBranchCount++;
                // Also add end node to branch array so it's counted in depth calculation
                ifBlocks[relevantIfBlock].falseBranchNodes[ifBlocks[relevantIfBlock].falseBranchCount] = endNodeIndex;
                ifBlocks[relevantIfBlock].falseBranchCount++;
                cycleNode->owningIfBlock = relevantIfBlock;
                endNode->owningIfBlock = relevantIfBlock;
                cycleOwningIfBlock = relevantIfBlock;
                cycleAddedToBranch = true;
            }
        }
    }
    
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
    
    // Reposition convergence points for IF blocks that contain the cycle
    // Similar to insert_node_in_connection, we need to reposition the IF's convergence
    // to account for the two new nodes (cycle block and end point) added to the branch
    if (cycleAddedToBranch && cycleOwningIfBlock >= 0 && cycleOwningIfBlock < ifBlockCount) {
        // Reposition the IF block's convergence point to account for the cycle
        reposition_convergence_point(cycleOwningIfBlock, true);
        
        // If this is a nested IF, also reposition its parent IF
        int parentIfIdx = ifBlocks[cycleOwningIfBlock].parentIfIndex;
        if (parentIfIdx >= 0 && parentIfIdx < ifBlockCount) {
            reposition_convergence_point(parentIfIdx, true);
        }
    }
    
    // Recalculate branch widths and positions after insertion
    update_all_branch_positions();
    
    // Save state AFTER operation completes (for redo to work correctly)
    save_state_for_undo();
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
        if (cursor_over_button(buttonX_scaled, closeButtonY, window)) {
            // Red close button clicked - close program (top)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        }
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
        if (cursor_over_button(buttonX_scaled, undoButtonY, window)) {
            // Purple undo button clicked
            perform_undo();
            return;
        }
        if (cursor_over_button(buttonX_scaled, redoButtonY, window)) {
            // Orange redo button clicked
            perform_redo();
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
                            // #endregion
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
        } else {
            // Not clicking on button or menu - start panning
            isPanning = true;
            panStartX = cursorX;
            panStartY = cursorY;
            panStartScrollX = scrollOffsetX;
            panStartScrollY = scrollOffsetY;
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        // Stop panning when left button is released
        isPanning = false;
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
int tinyfd_listDialog(const char* aTitle, const char* aMessage, int numOptions, const char* const* options) {
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


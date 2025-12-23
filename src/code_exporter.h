#ifndef CODE_EXPORTER_H
#define CODE_EXPORTER_H

#include <stdbool.h>

// Forward declarations - actual definitions are in main.c
struct FlowNode;
struct Connection;

// Export flowchart to code
bool export_to_code(const char* filename, const char* language, 
                    struct FlowNode* nodes, int nodeCount,
                    struct Connection* connections, int connectionCount);

#endif


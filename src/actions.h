#ifndef ACTIONS_H
#define ACTIONS_H

#include <GLFW/glfw3.h>
#include "flowchart_state.h"

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void edit_node_value(int nodeIndex);
void insert_node_in_connection(int connIndex, NodeType nodeType);
void insert_if_block_in_connection(int connIndex);
void insert_cycle_block_in_connection(int connIndex);
void delete_node(int nodeIndex);
int tinyfd_listDialog(const char* aTitle, const char* aMessage, int numOptions, const char* const* options);

#endif // ACTIONS_H


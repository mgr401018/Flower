#ifndef DRAWING_H
#define DRAWING_H

#include <GLFW/glfw3.h>

void drawFlowNode(const FlowNode *n);
void drawFlowchart(GLFWwindow* window);
void drawPopupMenu(GLFWwindow* window);
void drawButtons(GLFWwindow* window);

#endif // DRAWING_H


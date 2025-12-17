#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <GLFW/glfw3.h>

// Initialize text renderer (call once at startup)
// fontPath: path to .ttf font file, or NULL to use built-in simple renderer
int init_text_renderer(const char* fontPath);

// Cleanup text renderer
void cleanup_text_renderer(void);

// Draw text at position (x, y) in normalized coordinates (-1 to 1)
// Returns width of rendered text
float draw_text(float x, float y, const char* text, float fontSize, float r, float g, float b);

// Get text width without rendering
float get_text_width(const char* text, float fontSize);

// Set window dimensions (call when window is created or resized)
void text_renderer_set_window_size(int width, int height);

#endif // TEXT_RENDERER_H








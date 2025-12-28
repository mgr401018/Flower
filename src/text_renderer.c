#define STB_TRUETYPE_IMPLEMENTATION
#include "../imports/stb_truetype.h"
#include "text_renderer.h"
#include "embedded_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <GL/gl.h>

#define FONT_TEXTURE_SIZE 512
#define FIRST_CHAR 32
#define NUM_CHARS 96  // ASCII 32..126 is 95 glyphs

static unsigned char ttf_buffer[1 << 20];  // 1MB buffer for font file
static unsigned char font_bitmap[FONT_TEXTURE_SIZE * FONT_TEXTURE_SIZE];
static stbtt_bakedchar cdata[NUM_CHARS];
static GLuint font_texture = 0;
static int window_width = 800;
static int window_height = 600;
static int font_initialized = 0;
static double scroll_offset_x = 0.0;
static double scroll_offset_y = 0.0;
static float aspect_ratio = 1.333f;  // Default to 800/600
static float y_scale = 1.0f;  // Y scaling factor to maintain original proportions
static float flowchart_scale = 1.0f;  // Flowchart scale factor (for block labels only)

// Set window dimensions (call when window is created or resized)
void text_renderer_set_window_size(int width, int height) {
    window_width = width;
    window_height = height;
}

// Set scroll offsets (call before drawing text that should scroll with content)
void text_renderer_set_scroll_offsets(double offsetX, double offsetY) {
    scroll_offset_x = offsetX;
    scroll_offset_y = offsetY;
}

// Set aspect ratio (call when window size changes)
void text_renderer_set_aspect_ratio(float aspectRatio) {
    aspect_ratio = aspectRatio;
}

// Set Y scale (call when window size changes to maintain original proportions)
void text_renderer_set_y_scale(float yScale) {
    y_scale = yScale;
}

// Set flowchart scale (for scaling block labels, not menu/button labels)
void text_renderer_set_flowchart_scale(float scale) {
    flowchart_scale = scale;
}

int init_text_renderer(const char* fontPath) {
    if (font_initialized) {
        cleanup_text_renderer();
    }
    
    const unsigned char* font_data = NULL;
    size_t font_size = 0;
    
    if (fontPath == NULL) {
        // Use embedded font
        font_data = imports_DejaVuSansMono_ttf;
        font_size = imports_DejaVuSansMono_ttf_len;
        
        // Copy to buffer if it fits
        if (font_size <= sizeof(ttf_buffer)) {
            memcpy(ttf_buffer, font_data, font_size);
            font_data = ttf_buffer;
        } else {
            fprintf(stderr, "Warning: Embedded font is too large\n");
            return 0;
        }
    } else {
        // Load from file (for development/testing)
        FILE* font_file = fopen(fontPath, "rb");
        if (!font_file) {
            fprintf(stderr, "Warning: Could not open font file: %s\n", fontPath);
            fprintf(stderr, "Text rendering will not work properly.\n");
            return 0;
        }
        
        size_t bytes_read = fread(ttf_buffer, 1, sizeof(ttf_buffer), font_file);
        fclose(font_file);
        
        if (bytes_read == 0) {
            fprintf(stderr, "Warning: Could not read font file: %s\n", fontPath);
            return 0;
        }
        font_data = ttf_buffer;
        font_size = bytes_read;
    }
    
    // Bake font bitmap
    int result = stbtt_BakeFontBitmap(font_data, 0, 32.0, font_bitmap, 
                                      FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 
                                      FIRST_CHAR, NUM_CHARS, cdata);
    
    if (result <= 0) {
        fprintf(stderr, "Warning: Failed to bake font bitmap\n");
        return 0;
    }
    
    // Create OpenGL texture
    glGenTextures(1, &font_texture);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 
                 0, GL_ALPHA, GL_UNSIGNED_BYTE, font_bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    font_initialized = 1;
    if (fontPath == NULL) {
        printf("Text renderer initialized successfully with embedded font\n");
    } else {
        printf("Text renderer initialized successfully with font: %s\n", fontPath);
    }
    return 1;
}

void cleanup_text_renderer(void) {
    if (font_texture != 0) {
        glDeleteTextures(1, &font_texture);
        font_texture = 0;
    }
    font_initialized = 0;
}

float get_text_width(const char* text, float fontSize) {
    if (!font_initialized || !text) return 0.0f;
    
    // Convert fontSize from normalized coordinates to pixels (same as draw_text)
    // fontSize is in normalized coordinates (typically 0.01-0.1), convert to pixels
    // Normalized coordinates go from -1 to 1, so total range is 2.0
    // For height, we use window_height to convert
    float fontSizeScaled = fontSize * flowchart_scale;
    float fontSizePixels = (fontSizeScaled * window_height) / 2.0f;  // Convert normalized height to pixels
    if (fontSizePixels < 12.0f) fontSizePixels = 18.0f;  // Minimum readable size (same as draw_text)
    
    // Calculate scale based on pixel font size vs baked font size (32 pixels)
    float scale = fontSizePixels / 32.0f;  // 32.0 is the baked font size
    float width = 0.0f;
    
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c >= FIRST_CHAR && *c < FIRST_CHAR + NUM_CHARS) {
            stbtt_bakedchar* b = &cdata[*c - FIRST_CHAR];
            width += b->xadvance * scale;
        } else if (*c == ' ') {
            // Space character
            width += 8.0f * scale;  // Approximate space width
        }
    }
    
    // Convert from pixel coordinates to world coordinates
    // When draw_text converts x to pixels: pixel_x = ((screen_normalized_x / aspect_ratio + 1.0f) / 2.0f) * window_width
    // Where screen_normalized_x = flowchart_scale * x - scroll_offset_x
    // For width (a difference), we need to reverse this conversion:
    // width_pixel = width_world * flowchart_scale / aspect_ratio * window_width / 2.0f
    // Therefore: width_world = width_pixel * 2.0f * aspect_ratio / (flowchart_scale * window_width)
    // But since we're calculating width_pixel from font metrics, we need to convert back:
    return (width / window_width) * 2.0f * aspect_ratio / flowchart_scale;
}

float draw_text(float x, float y, const char* text, float fontSize, float r, float g, float b) {
    if (!font_initialized || !text || font_texture == 0) {
        return 0.0f;
    }
    
    // Convert fontSize from normalized coordinates to pixels
    // fontSize is in normalized coordinates (typically 0.01-0.1), convert to pixels
    // Normalized coordinates go from -1 to 1, so total range is 2.0
    // For height, we use window_height to convert
    // Apply flowchart scale to fontSize for block labels (but not menu/button labels)
    float fontSizeScaled = fontSize * flowchart_scale;
    float fontSizePixels = (fontSizeScaled * window_height) / 2.0f;  // Convert normalized height to pixels
    if (fontSizePixels < 12.0f) fontSizePixels = 18.0f;  // Minimum readable size
    
    // Save current matrices
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    
    // Set up pixel-perfect orthographic projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, window_width, window_height, 0, -1, 1);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Convert normalized coordinates to pixel coordinates
    // Apply the same transformation as OpenGL: screen = FLOWCHART_SCALE * world - scrollOffset
    // x, y are in world coordinates (normalized)
    // scroll_offset_x and scroll_offset_y are already negative (set as -scrollOffsetX in main.c)
    // So: screen = FLOWCHART_SCALE * world - (-scrollOffset) = FLOWCHART_SCALE * world + scrollOffset
    // But we want: screen = FLOWCHART_SCALE * world - scrollOffset
    // So we need to negate the scroll offsets: screen = FLOWCHART_SCALE * world - (-scroll_offset)
    float screen_normalized_x = flowchart_scale * (float)x - (float)scroll_offset_x;
    float screen_normalized_y = flowchart_scale * (float)y - (float)scroll_offset_y;
    // X coordinate is in range [-aspectRatio, aspectRatio]
    // Y coordinate is in range [-1, 1]
    float pixel_x = ((screen_normalized_x / aspect_ratio + 1.0f) / 2.0f) * window_width;
    float pixel_y = ((1.0f - screen_normalized_y) / 2.0f) * window_height;
    
    float scale = fontSizePixels / 32.0f;  // 32.0 is the baked font size
    float start_x = pixel_x;
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    
    // Set texture environment to modulate (multiply texture alpha with vertex color)
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    
    // Set text color - ensure it's white
    // For GL_ALPHA textures, we need to set the color which will be multiplied with the alpha
    glColor4f(r, g, b, 1.0f);  // Use Color4f to ensure alpha is set
    
    // Apply scaling
    glTranslatef(pixel_x, pixel_y, 0);
    glScalef(scale, scale, 1.0f);
    glTranslatef(-pixel_x, -pixel_y, 0);
    
    glBegin(GL_QUADS);
    
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c >= FIRST_CHAR && *c < FIRST_CHAR + NUM_CHARS) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 
                              *c - FIRST_CHAR, &pixel_x, &pixel_y, &q, 1);
            
            glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
            glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
            glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
            glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
        } else if (*c == ' ') {
            // Space character - just advance
            pixel_x += 8.0f;
        }
    }
    
    glEnd();
    
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    
    // Restore matrices - restore in reverse order
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    
    // Return width in normalized coordinates
    float pixel_width = (pixel_x - start_x) * scale;
    return (pixel_width / window_width) * 2.0f;
}


#define STB_TRUETYPE_IMPLEMENTATION
#include "../imports/stb_truetype.h"
#include "text_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// Set window dimensions (call when window is created or resized)
void text_renderer_set_window_size(int width, int height) {
    window_width = width;
    window_height = height;
}

int init_text_renderer(const char* fontPath) {
    if (font_initialized) {
        cleanup_text_renderer();
    }
    
    const char* path = fontPath;
    if (!path) {
        // Default to local DejaVu Sans Mono if available
        path = "imports/DejaVuSansMono.ttf";
    }
    
    FILE* font_file = fopen(path, "rb");
    if (!font_file) {
        fprintf(stderr, "Warning: Could not open font file: %s\n", path);
        fprintf(stderr, "Text rendering will not work properly.\n");
        return 0;
    }
    
    size_t bytes_read = fread(ttf_buffer, 1, sizeof(ttf_buffer), font_file);
    fclose(font_file);
    
    if (bytes_read == 0) {
        fprintf(stderr, "Warning: Could not read font file: %s\n", path);
        return 0;
    }
    
    // Bake font bitmap
    int result = stbtt_BakeFontBitmap(ttf_buffer, 0, 32.0, font_bitmap, 
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
    printf("Text renderer initialized successfully with font: %s\n", path);
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
    
    float scale = fontSize / 32.0f;  // 32.0 is the baked font size
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
    
    // Convert from pixel coordinates to normalized coordinates
    return (width / window_width) * 2.0f;
}

float draw_text(float x, float y, const char* text, float fontSize, float r, float g, float b) {
    if (!font_initialized || !text || font_texture == 0) {
        return 0.0f;
    }
    
    // Convert fontSize from normalized coordinates to pixels
    // fontSize is in normalized coordinates (typically 0.01-0.1), convert to pixels
    // Normalized coordinates go from -1 to 1, so total range is 2.0
    // For height, we use window_height to convert
    float fontSizePixels = (fontSize * window_height) / 2.0f;  // Convert normalized height to pixels
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
    float pixel_x = ((x + 1.0f) / 2.0f) * window_width;
    float pixel_y = ((1.0f - y) / 2.0f) * window_height;
    
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


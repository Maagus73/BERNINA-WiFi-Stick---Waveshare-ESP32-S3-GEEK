#ifndef DISPLAY_SCENE_H
#define DISPLAY_SCENE_H
#include "display_status.h"
#define SCENE_WIDTH 240
#define SCENE_HEIGHT 135
#define SCENE_BODY 39
#define SCENE_COLUMNS 30
#define SCENE_ROWS 12
#define SCENE_CELL 8
typedef struct {
 display_input_t input;
 display_state_t state;
 bool matrix;
 uint8_t heads[SCENE_COLUMNS],phase[SCENE_COLUMNS];
} display_scene_t;
void display_scene_init(display_scene_t *scene);
void display_scene_advance(display_scene_t *scene,unsigned column);
// Writes only the caller's bounded tile, logical RGB565, never a framebuffer.
bool display_scene_render(const display_scene_t *scene,int x,int y,int width,int height,uint16_t *pixels,unsigned capacity);
#endif

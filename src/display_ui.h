#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H
#include "display_status.h"
#include "esp_err.h"
#include <stddef.h>
typedef struct {
 bool attempted,available;esp_err_t error;
 size_t internal_before,internal_after,psram_before,psram_after;
} display_metrics_t;
display_metrics_t display_ui_metrics(void);
// Snapshot callback is observational only; never grants ownership or performs file I/O.
void display_ui_start(void (*snapshot)(display_input_t *));
bool display_ui_matrix_enabled(void);
esp_err_t display_ui_set_matrix(bool enabled);
// Worker-only: bounded render acknowledgement at transfer start/end.
void display_ui_cooperate(void *unused);
void display_ui_transfer(bool active,uint64_t bytes,uint64_t total,bool verifying);
void display_ui_compare(unsigned phase,unsigned done,unsigned known,unsigned entries);
void display_ui_progress(void *unused,uint64_t bytes,uint64_t total,bool verifying);
#endif

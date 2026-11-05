#ifndef DISPLAY_STATUS_H
#define DISPLAY_STATUS_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { DISPLAY_START,DISPLAY_WAIT,DISPLAY_READY,DISPLAY_APP,DISPLAY_SYNC,DISPLAY_ERROR,DISPLAY_RETRY } display_state_t;
typedef struct {
 bool initialized,usb_confirmed,host_ready,app_confirmed,fault,comparing,busy;
 bool retry_waiting;uint32_t retry_seconds;
 bool transferring,verifying,total_known;
 uint64_t bytes,total;
 uint32_t transfer_generation;
 unsigned compare_phase,compare_done,compare_known,compare_entries,compare_pulse;
 unsigned file_index,file_count,succeeded,failed;
 const char *error;
} display_input_t;
typedef struct {display_state_t state;uint32_t ready_since;bool ready_timing,matrix;} display_policy_t;
display_state_t display_classify(const display_input_t *input);
void display_policy_step(display_policy_t *policy,const display_input_t *input,uint32_t now,bool enabled,bool resources_ok);
int display_percent(const display_input_t *input);
#endif

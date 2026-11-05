// Waveshare ESP32-S3-GEEK: schematic V1.1 + official 05_LCD_Button example.
// Independent SPI3 display only. No MSC, SD, HTTP or filesystem access here.
#include "display_ui.h"
#include "display_scene.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "nvs.h"
#define WIDTH SCENE_WIDTH
#define HEIGHT SCENE_HEIGHT
#define TILE_PIXELS 1080
#define LCD_BL 7
#define LCD_DC 8
#define LCD_RST 9
#define LCD_CS 10
#define LCD_MOSI 11
#define LCD_CLK 12
static const char *TAG="display17a";
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static TaskHandle_t task;
static StaticTask_t task_control;
static StackType_t task_stack[6144] __attribute__((aligned(16))); // 6 KiB internal; no dynamic task stack / PSRAM
static DMA_ATTR uint16_t pixels[TILE_PIXELS]; // unchanged 2160-byte DMA buffer, <=4 landscape rows
static void (*read_snapshot)(display_input_t *);
static portMUX_TYPE data_lock=portMUX_INITIALIZER_UNLOCKED;
static bool matrix_enabled,transfer_active,transfer_verifying;
static uint64_t transfer_bytes,transfer_total;
static uint32_t transfer_generation,presented_generation;
static unsigned compare_phase,compare_done,compare_known,compare_entries;
static bool bus_owned;
static display_metrics_t metrics;
static esp_err_t rendering_error;

bool display_ui_matrix_enabled(void)
{
 portENTER_CRITICAL(&data_lock);bool value=matrix_enabled;portEXIT_CRITICAL(&data_lock);return value;
}
esp_err_t display_ui_set_matrix(bool enabled)
{
 if(display_ui_matrix_enabled()==enabled)return ESP_OK;
 nvs_handle_t h;esp_err_t e=nvs_open("bernina_ui",NVS_READWRITE,&h);
 if(e!=ESP_OK)return e;
 e=nvs_set_u8(h,"matrix",enabled?1:0);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);
 if(e==ESP_OK){portENTER_CRITICAL(&data_lock);matrix_enabled=enabled;portEXIT_CRITICAL(&data_lock);}
 return e;
}
static void wait_presented(uint32_t generation)
{
 TickType_t began=xTaskGetTickCount();
 while((TickType_t)(xTaskGetTickCount()-began)<pdMS_TO_TICKS(400)){
  portENTER_CRITICAL(&data_lock);
  bool waiting=metrics.available&&presented_generation!=generation;
  portEXIT_CRITICAL(&data_lock);
  if(!waiting)return;
  vTaskDelay(1); // Let the lower-priority display task finish its real DMA frame.
 }
}
void display_ui_cooperate(void *unused)
{
 (void)unused;static TickType_t last_yield;
 TickType_t now=xTaskGetTickCount();
 portENTER_CRITICAL(&data_lock);bool available=metrics.available;portEXIT_CRITICAL(&data_lock);
 if(available&&(TickType_t)(now-last_yield)>=pdMS_TO_TICKS(50)){
  last_yield=now;vTaskDelay(1); // taskYIELD alone cannot schedule a lower-priority task.
 }
}
void display_ui_transfer(bool active,uint64_t bytes,uint64_t total,bool verifying)
{
 portENTER_CRITICAL(&data_lock);
 bool beginning=active&&!transfer_active,ending=!active&&transfer_active;
 uint32_t previous=transfer_generation;
 portEXIT_CRITICAL(&data_lock);
 if(ending)wait_presented(previous);
 portENTER_CRITICAL(&data_lock);
 if(transfer_active!=active||transfer_bytes!=bytes||transfer_total!=total||transfer_verifying!=verifying)transfer_generation++;
 transfer_active=active;transfer_bytes=bytes;transfer_total=total;transfer_verifying=verifying;
 uint32_t current=transfer_generation;
 portEXIT_CRITICAL(&data_lock);
 if(beginning)wait_presented(current);
 if(active)display_ui_cooperate(NULL);
}
void display_ui_progress(void *unused,uint64_t bytes,uint64_t total,bool verifying)
{
 (void)unused;display_ui_transfer(true,bytes,total,verifying);
}
void display_ui_compare(unsigned phase,unsigned done,unsigned known,unsigned entries)
{
 portENTER_CRITICAL(&data_lock);
 compare_phase=phase;compare_done=done;compare_known=known;compare_entries=entries;
 portEXIT_CRITICAL(&data_lock);
}
static void snapshot(display_input_t *s)
{
 memset(s,0,sizeof(*s));read_snapshot(s);
 portENTER_CRITICAL(&data_lock);
 s->compare_phase=compare_phase;s->compare_done=compare_done;s->compare_known=compare_known;s->compare_entries=compare_entries;
 s->compare_pulse=s->comparing?(unsigned)(xTaskGetTickCount()*portTICK_PERIOD_MS/200)%32:0;
 s->transfer_generation=transfer_generation;
 s->transferring=transfer_active;s->bytes=transfer_bytes;s->total=transfer_total;
 s->total_known=transfer_total>0;s->verifying=transfer_verifying;
 portEXIT_CRITICAL(&data_lock);
}
display_metrics_t display_ui_metrics(void)
{
 portENTER_CRITICAL(&data_lock);display_metrics_t result=metrics;portEXIT_CRITICAL(&data_lock);return result;
}
static bool IRAM_ATTR color_done(esp_lcd_panel_io_handle_t handle,esp_lcd_panel_io_event_data_t *event,void *ctx)
{
 (void)handle;(void)event;(void)ctx;BaseType_t higher=pdFALSE;
 vTaskNotifyGiveFromISR(task,&higher);return higher==pdTRUE;
}
static uint16_t wire(uint16_t rgb){return (uint16_t)((rgb>>8)|(rgb<<8));}
static bool tile(int x,int y,int width,int height)
{
 ulTaskNotifyTake(pdTRUE,0);
 esp_err_t error=esp_lcd_panel_draw_bitmap(panel,x,y,x+width,y+height,pixels);
 if(error!=ESP_OK){rendering_error=error;return false;}
 // Only this low-priority display task waits for DMA. Application never renders.
 // On timeout stop permanently: never overwrite a buffer still owned by DMA.
 if(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50))!=0)return true;
 rendering_error=ESP_ERR_TIMEOUT;return false;
}
static bool render_rect(const display_scene_t *scene,int x,int y,int width,int height)
{
 int step=TILE_PIXELS/width;if(step>8)step=8;
 for(int row=y;row<y+height;row+=step){
  int h=y+height-row;if(h>step)h=step;
  if(!display_scene_render(scene,x,row,width,h,pixels,TILE_PIXELS)){rendering_error=ESP_ERR_INVALID_ARG;return false;}
  for(int n=0;n<width*h;n++)pixels[n]=wire(pixels[n]);
  if(!tile(x,row,width,h))return false;
  taskYIELD();
 }
 return true;
}
static esp_err_t initialize(void)
{
 esp_err_t e;
 gpio_config_t backlight={.pin_bit_mask=1ULL<<LCD_BL,.mode=GPIO_MODE_OUTPUT};
 if((e=gpio_config(&backlight))!=ESP_OK)return e;
 gpio_set_level(LCD_BL,0);
 spi_bus_config_t bus={.mosi_io_num=LCD_MOSI,.miso_io_num=-1,.sclk_io_num=LCD_CLK,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=sizeof(pixels)};
 if((e=spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO))!=ESP_OK)return e;
 bus_owned=true;
 esp_lcd_panel_io_spi_config_t cfg={.cs_gpio_num=LCD_CS,.dc_gpio_num=LCD_DC,.spi_mode=3,.pclk_hz=20*1000*1000,
  .trans_queue_depth=1,.on_color_trans_done=color_done,.lcd_cmd_bits=8,.lcd_param_bits=8};
 if((e=esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST,&cfg,&io))!=ESP_OK)return e;
 esp_lcd_panel_dev_config_t dev={.reset_gpio_num=LCD_RST,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};
 if((e=esp_lcd_new_panel_st7789(io,&dev,&panel))!=ESP_OK)return e;
 if((e=esp_lcd_panel_reset(panel))!=ESP_OK||(e=esp_lcd_panel_init(panel))!=ESP_OK)return e;
 // Rotated 180 degrees from the previous landscape orientation.
 // MV | MX = MADCTL 0x60, landscape offsets (40,53), RGB order unchanged.
 if((e=esp_lcd_panel_swap_xy(panel,true))!=ESP_OK||(e=esp_lcd_panel_mirror(panel,true,false))!=ESP_OK||
    (e=esp_lcd_panel_set_gap(panel,40,53))!=ESP_OK||(e=esp_lcd_panel_invert_color(panel,true))!=ESP_OK||
    (e=esp_lcd_panel_disp_on_off(panel,true))!=ESP_OK)return e;
 return ESP_OK;
}
static void display_task(void *arg)
{
 (void)arg;task=xTaskGetCurrentTaskHandle();
 size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
 esp_err_t e=before<40*1024?ESP_ERR_NO_MEM:initialize();
 size_t after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),psram_after=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
 portENTER_CRITICAL(&data_lock);
 metrics=(display_metrics_t){.attempted=true,.error=e,.internal_before=before,
  .internal_after=after,.psram_before=psram,.psram_after=psram_after};
 portEXIT_CRITICAL(&data_lock);
 ESP_LOGI(TAG,"Init %s; internal heap %u -> %u, PSRAM %u -> %u bytes; static DMA %u, stack %u",
  esp_err_to_name(e),(unsigned)before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)psram,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)sizeof(pixels),(unsigned)sizeof(task_stack));
 if(e!=ESP_OK){
  if(panel)esp_lcd_panel_del(panel);
  if(io)esp_lcd_panel_io_del(io);
  if(bus_owned)spi_bus_free(SPI3_HOST);
  gpio_set_level(LCD_BL,0);vTaskDelete(NULL);return;
 }
 display_scene_t scene;display_scene_init(&scene);
 bool ok=render_rect(&scene,0,0,WIDTH,HEIGHT);
 if(ok){gpio_set_level(LCD_BL,1);portENTER_CRITICAL(&data_lock);metrics.available=true;portEXIT_CRITICAL(&data_lock);}
 display_input_t input={0};display_policy_t policy={.state=DISPLAY_START};unsigned column=0;
 while(ok){
  snapshot(&input);
  display_policy_step(&policy,&input,(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS),display_ui_matrix_enabled(),
   heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=32*1024);
  bool full=scene.state!=policy.state||scene.matrix!=policy.matrix;
  bool changed=scene.input.compare_phase!=input.compare_phase||scene.input.compare_done!=input.compare_done||
   scene.input.compare_known!=input.compare_known||scene.input.compare_entries!=input.compare_entries||scene.input.compare_pulse!=input.compare_pulse||scene.input.bytes!=input.bytes||scene.input.total!=input.total||scene.input.total_known!=input.total_known||
   scene.input.verifying!=input.verifying||scene.input.comparing!=input.comparing||scene.input.error!=input.error||scene.input.file_index!=input.file_index||scene.input.succeeded!=input.succeeded||scene.input.failed!=input.failed;
  scene.input=input;scene.state=policy.state;scene.matrix=policy.matrix;
  if(full)ok=render_rect(&scene,0,0,WIDTH,HEIGHT);
  else if(changed)ok=render_rect(&scene,0,SCENE_BODY,WIDTH,HEIGHT-SCENE_BODY);
  if(!ok)break;
  portENTER_CRITICAL(&data_lock);presented_generation=input.transfer_generation;portEXIT_CRITICAL(&data_lock);
  if(policy.matrix)for(unsigned n=0;n<8&&ok;n++){
   display_input_t latest;snapshot(&latest);
   if(display_classify(&latest)!=DISPLAY_READY||!display_ui_matrix_enabled())break;
   unsigned c=column++%SCENE_COLUMNS;display_scene_advance(&scene,c);
   for(unsigned age=0;age<5&&ok;age++){
    unsigned row=(scene.heads[c]+SCENE_ROWS-age)%SCENE_ROWS;
    ok=render_rect(&scene,(int)c*SCENE_CELL,SCENE_BODY+(int)row*SCENE_CELL,SCENE_CELL,SCENE_CELL);
   }
  }
  vTaskDelay(pdMS_TO_TICKS(100)); // <=10 sparse batches/s; no frame allocation
 }
 portENTER_CRITICAL(&data_lock);metrics.available=false;metrics.error=rendering_error;portEXIT_CRITICAL(&data_lock);
 gpio_set_level(LCD_BL,0);ESP_LOGW(TAG,"Rendering stopped; USB unaffected, display DMA quarantined until reboot");
 // A late DMA interrupt may still notify this task: keep its static TCB alive.
 for(;;)vTaskSuspend(NULL);
}
void display_ui_start(void (*reader)(display_input_t *))
{
 read_snapshot=reader;
 nvs_handle_t h;uint8_t saved=0;esp_err_t e=nvs_open("bernina_ui",NVS_READONLY,&h);
 if(e==ESP_OK){e=nvs_get_u8(h,"matrix",&saved);nvs_close(h);}
 if(e!=ESP_OK&&e!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Matrix setting unavailable; default OFF (%s)",esp_err_to_name(e));
 matrix_enabled=e==ESP_OK&&saved==1;
 task=xTaskCreateStatic(display_task,"display17a",sizeof(task_stack)/sizeof(task_stack[0]),NULL,1,task_stack,&task_control);
 if(!task)ESP_LOGW(TAG,"No display task; USB continues");
}

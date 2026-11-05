idf_component_register(
    SRCS "main.c" "dav_listing.c" "dav_unicode.c" "dav_tree.c" "dry_run.c" "path_search.c" "single_copy.c" "batch_sync.c" "safe_update.c" "mirror_sync.c" "nas_index.c" "sync_rules.c" "fast_sync.c" "remote_plan.c" "bulk_copy.c" "app_settings.c" "sync_retry.c" "display_status.c" "display_scene.c" "display_ui.c"
    INCLUDE_DIRS "../include"
    REQUIRES
        json
        esp_mm
        esp_lcd
        esp_event
        esp_wifi
        esp_netif
        esp_http_client
        esp_http_server
        nvs_flash
        lwip
        mbedtls
        fatfs
        sdmmc
        driver
)
#ifndef BERNINA_SYNC_CONFIG_H
#define BERNINA_SYNC_CONFIG_H
/* Defaults for 1.0.0; runtime interval and pause settings are stored in NVS. */
#define SYNC_AUTOMATIC_ENABLED 1
#define SYNC_CHECK_INTERVAL_SECONDS 600U
/* Full rebuild when DIFFERENCES > threshold; equal remains a delta. */
#define SYNC_REBUILD_THRESHOLD 2500U
#define SYNC_NETWORK_ATTEMPTS 3U
#define SYNC_START_DELAY_SECONDS 10U
/* Bounded multi-file HTTPS stream. */
#define SYNC_BULK_ENABLED 1
#define SYNC_BULK_FILES 64U
#define SYNC_BULK_BYTES (8ULL * 1024ULL * 1024ULL)
#if SYNC_BULK_FILES < 1 || SYNC_BULK_FILES > 128 || SYNC_BULK_BYTES < 65536
#error Invalid bulk transfer configuration
#endif
#if SYNC_CHECK_INTERVAL_SECONDS < 10 || SYNC_REBUILD_THRESHOLD < 1 || SYNC_REBUILD_THRESHOLD > 20000 || SYNC_NETWORK_ATTEMPTS < 1 || SYNC_NETWORK_ATTEMPTS > 5
#error Invalid synchronization configuration
#endif
#endif

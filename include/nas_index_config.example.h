#ifndef NAS_INDEX_CONFIG_H
#define NAS_INDEX_CONFIG_H
// DE: Nach nas_index_config.h kopieren. Produktiver Zugriff nutzt den Indexdienst.
// EN: Copy to nas_index_config.h. Production uses the index service.
#define BERNINA_NAS_INDEX 1
#if BERNINA_NAS_INDEX
#undef WEBDAV_URL
#define WEBDAV_URL "https://nas.home.arpa:5443/Embroidery/"
#endif
#endif

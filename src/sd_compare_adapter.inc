// SD index IO is permitted only during confirmed APP ownership.
static bool comparison_sd_discard(void); // defined after the plan/selection adapters
static bool comparison_sd_owned(void *unused){(void)unused;return app_access_allowed();}
static void comparison_sd_fault(void *unused)
{
 (void)unused;
 if(comparison_result&&comparison_result->tree.disk){comparison_result->complete=false;comparison_result->plan.complete=false;comparison_result->error=DAV_ERR_STORE;comparison_stale=true;comparison_revision++;}
 io_fault=true;app_owned=false;usb_test_phase=3;last_error=EIO;
}
static bool comparison_sd_open(dav_tree_t *tree)
{
 unsigned char hash[32];char binding[65];
 mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
 bool ok=mbedtls_sha256_starts(&sha,0)==0&&mbedtls_sha256_update(&sha,(const unsigned char *)WEBDAV_URL,strlen(WEBDAV_URL))==0&&mbedtls_sha256_finish(&sha,hash)==0;mbedtls_sha256_free(&sha);if(!ok)return false;
 for(unsigned i=0;i<32;i++)snprintf(binding+2*i,3,"%02x",hash[i]);
 const sd_index_ops_t ops={.owned=comparison_sd_owned,.fault=comparison_sd_fault,.cooperate=display_ui_cooperate};
 return app_access_allowed()&&sd_index_open(tree,SD_PATH,binding,esp_random(),&ops);
}

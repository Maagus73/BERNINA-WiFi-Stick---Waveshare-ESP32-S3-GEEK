// One view; /diagnostics remains an alias for existing bookmarks.
static esp_err_t sync_page(httpd_req_t *req,bool diagnostic)
{
 (void)diagnostic;httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
 esp_err_t result=httpd_resp_sendstr_chunk(req,
#include "sync_page_html.inc"
 );
 if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,
#include "sync_page_script.inc"
 );
 if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,"</script></body></html>");
 return result==ESP_OK?httpd_resp_send_chunk(req,NULL,0):result;
}

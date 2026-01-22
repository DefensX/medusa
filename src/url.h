
#ifdef __cplusplus
extern "C"
{
#endif

struct medusa_url;

struct medusa_url * medusa_url_parse (const char *uri);
void medusa_url_destroy (struct medusa_url *url);

const char * medusa_url_get_scheme (struct medusa_url *url);
const char * medusa_url_get_host (struct medusa_url *url);
int medusa_url_get_port (struct medusa_url *url);
int medusa_url_has_port (struct medusa_url *url);
const char * medusa_url_get_path (struct medusa_url *url);
const char * medusa_url_get_username (struct medusa_url *url);
const char * medusa_url_get_password (struct medusa_url *url);

#ifdef __cplusplus
}
#endif

#include "eccdb_c/eccdb.h"

#include <assert.h>
#include <string.h>

int main(void)
{
  uint8_t supported = 0;
  assert(eccdb_supports_lef_def(&supported) == ECCDB_STATUS_OK);

  assert(eccdb_open_binary(NULL, "library.bin", "design.bin", NULL)
         == ECCDB_STATUS_INVALID_ARGUMENT);
  assert(strstr(eccdb_last_error_message(), "invalid open arguments") != NULL);

  eccdb_net_data_t net = {0};
  eccdb_net_data_init(&net);
  eccdb_net_data_free(&net);

  eccdb_instance_data_t instance = {0};
  eccdb_instance_data_init(&instance);
  eccdb_instance_data_free(&instance);
  return 0;
}

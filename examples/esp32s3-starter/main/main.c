#include "device_app.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(device_app_start());
}

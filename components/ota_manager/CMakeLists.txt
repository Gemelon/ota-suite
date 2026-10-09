idf_component_register(SRCS "ota_manager.c" INCLUDE_DIRS "include"
                       REQUIRES esp_https_ota app_update esp_http_client mbedtls
                                esp_timer network_manager http_parser update_check)

idf_component_register(SRCS "update_check.c" "update_policy.c" "update_manifest.c" INCLUDE_DIRS "include"
                       REQUIRES esp_http_client mbedtls esp_timer app_update
                                network_manager espressif__cjson)

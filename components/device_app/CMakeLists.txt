idf_component_register(SRCS "device_app.c" INCLUDE_DIRS "include"
                       REQUIRES network_manager ota_manager web_interface status_led update_check)

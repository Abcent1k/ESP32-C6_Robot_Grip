idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES arduino scservo esp_http_server esp_wifi nvs_flash
    EMBED_FILES "index.html" "chart.js"
)

# Gather all source files
file(GLOB_RECURSE SOURCE_FILES Source/*.cpp)

# Register component
idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "./"
    REQUIRES TactilitySDK esp_http_client newlib
)

# Force C standard
set_target_properties(${COMPONENT_LIB} PROPERTIES C_STANDARD 99)

# Link all required libraries with --whole-archive
target_link_libraries(${COMPONENT_LIB} PRIVATE
    -Wl,--whole-archive esp_http_client newlib TactilityC lvgl -Wl,--no-whole-archive
)

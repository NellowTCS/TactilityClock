# Gather all source files
file(GLOB_RECURSE SOURCE_FILES *.cpp)

# Register component
idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "./"
    REQUIRES TactilitySDK esp_http_client newlib
)

# Force C standard
set_target_properties(${COMPONENT_LIB} PROPERTIES C_STANDARD 99)

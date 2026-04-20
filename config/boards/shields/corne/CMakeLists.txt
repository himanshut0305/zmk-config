if(CONFIG_CORNE_CUSTOM_DISPLAY)
  zephyr_library_named(corne_custom_display)
  zephyr_library_include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
  )
  zephyr_library_sources(widgets/util.c)
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL OR NOT CONFIG_ZMK_SPLIT)
    zephyr_library_sources(widgets/corne_status.c)
  else()
    zephyr_library_sources(widgets/corne_peripheral.c)
  endif()
endif()

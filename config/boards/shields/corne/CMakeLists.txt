if(CONFIG_CORNE_CUSTOM_DISPLAY)
  zephyr_library_named(corne_custom_display)
  zephyr_library_include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  zephyr_library_sources(src/portrait_util.c)
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL OR NOT CONFIG_ZMK_SPLIT)
    zephyr_library_sources(src/portrait_central.c)
  else()
    zephyr_library_sources(src/portrait_peripheral.c)
  endif()
endif()

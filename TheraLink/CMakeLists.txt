# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)

project(MirrorDuo C CXX ASM)

# lwIP (usa lwipopts.h na raiz do projeto)
set(PICO_LWIP_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/lwipopts.h")

pico_sdk_init()

# ------------------ Lib: OLED (SSD1306) ------------------
add_library(ssd1306 STATIC
    src/ssd1306_i2c.c
)
target_link_libraries(ssd1306
    pico_stdlib
    hardware_i2c
)
target_include_directories(ssd1306 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# ------------------ Lib: Sensor de Cor (TCS34725) ------------------
add_library(corlib STATIC
    src/cor.c
)
target_link_libraries(corlib
    pico_stdlib
    hardware_i2c
    hardware_gpio
)
target_include_directories(corlib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# ------------------ Lib: Oxímetro (MAX3010x) ------------------
add_library(oximlib STATIC
    src/oximetro.c
)
target_link_libraries(oximlib
    pico_stdlib
    hardware_i2c
    hardware_gpio
    hardware_irq
    m
)
target_include_directories(oximlib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# ------------------ Lib de rede/AP + stats ------------------
add_library(netlib STATIC
    dhcpserver/dhcpserver.c
    dnsserver/dnsserver.c
    src/web_ap.c
    src/stats.c
)
target_include_directories(netlib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
    ${CMAKE_CURRENT_LIST_DIR}/dnsserver
)
target_link_libraries(netlib
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

# ------------------ Executável principal ------------------
add_executable(main
    main.c
)
target_link_libraries(main
    pico_stdlib
    hardware_i2c
    hardware_adc
    hardware_gpio
    hardware_irq
    ssd1306
    corlib
    oximlib
    netlib
    m
)
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
    ${CMAKE_CURRENT_LIST_DIR}/dnsserver
)

pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 1)

pico_set_program_name(main "main")
pico_set_program_version(main "0.1")
pico_add_extra_outputs(main)

# SPDX-License-Identifier: LGPL-3.0-or-later

if(XRTRANSPORT_BUILD_CLIENT)
    if(XRTRANSPORT_DYNAMIC_MODULES)
        add_library(module_client_handle_exchange SHARED
            client_handle_exchange_linux.cpp
        )
        target_compile_definitions(module_client_handle_exchange PRIVATE
            XRTRANSPORT_EXPORT_API
        )
        install(TARGETS module_client_handle_exchange
            LIBRARY DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
            RUNTIME DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
        )
        if (ANDROID)
            target_link_options(module_client_handle_exchange PRIVATE
                "-Wl,-rpath,$ORIGIN"
            )
        else()
            set_target_properties(module_client_handle_exchange PROPERTIES
                INSTALL_RPATH "$ORIGIN"
            )
        endif()
    else()
        add_library(module_client_handle_exchange STATIC
            client_handle_exchange_linux.cpp
        )
    endif()
    
    target_link_libraries(module_client_handle_exchange PRIVATE
        xrtransport_transport
        xrtransport_serialization
    )

    # make includes available to linkers
    target_include_directories(module_client_handle_exchange PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(XRTRANSPORT_BUILD_SERVER)
    if(XRTRANSPORT_DYNAMIC_MODULES)
        add_library(module_server_handle_exchange SHARED
            server_handle_exchange_linux.cpp
        )
        target_compile_definitions(module_server_handle_exchange PRIVATE
            XRTRANSPORT_EXPORT_API
        )
        install(TARGETS module_server_handle_exchange
            LIBRARY DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH}
            RUNTIME DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH}
        )
        set_target_properties(module_server_handle_exchange PROPERTIES
            INSTALL_RPATH "$ORIGIN"
        )
    else()
        add_library(module_server_handle_exchange STATIC
            server_handle_exchange_linux.cpp
        )
    endif()

    target_link_libraries(module_server_handle_exchange PRIVATE
        xrtransport_transport
        xrtransport_serialization
    )

    # make includes available to linkers
    target_include_directories(module_server_handle_exchange PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

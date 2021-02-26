### Wrapper library
project(vgm)

add_library(${PROJECT_NAME} ${LIBRARY_TYPE})

target_link_libraries(${PROJECT_NAME} PRIVATE vgm-utils_objlib)

if(BUILD_LIBAUDIO)
    target_link_libraries(${PROJECT_NAME} PRIVATE vgm-audio_objlib)
endif()
if(BUILD_LIBEMU)
    target_link_libraries(${PROJECT_NAME} PRIVATE vgm-emu_objlib)
endif()
if(BUILD_LIBPLAYER)
    target_link_libraries(${PROJECT_NAME} PRIVATE vgm-player_objlib)
endif()

install(TARGETS ${PROJECT_NAME}
        EXPORT "${LIBVGM_NAME}"
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	)

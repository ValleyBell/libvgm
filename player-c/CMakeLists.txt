#### File Playback Library ####
project(vgm-player-c)

set(PLAYER_DEFS)
set(PLAYER_FILES
	#dblk_compr.c
	#helper.c
	engine_base.c
	engine_dro.c
	engine_gym.c
	#engine_s98.c
	#engine_vgm_cmdhandler.c
	#engine_vgm.c
	playera.c
)
# export headers
set(PLAYER_HEADERS
	#dblk_compr.h
	#helper.h
	engine_base.hpp
	engine_dro.h
	engine_gym.h
	#engine_s98.h
	#engine_vgm.h
	playera.h
)
set(PLAYER_INCLUDES)
set(PLAYER_LIBS)

set(PLAYER_PC_CFLAGS)
set(PLAYER_PC_LDFLAGS)


add_library(${PROJECT_NAME} ${LIBRARY_TYPE} ${PLAYER_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PLAYER_DEFS})
target_include_directories(${PROJECT_NAME}
	PUBLIC $<BUILD_INTERFACE:${LIBVGM_SOURCE_DIR}> $<INSTALL_INTERFACE:${LIBVGM_INSTALL_INCLUDE_DIR}>
	PRIVATE ${PLAYER_INCLUDES}
)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PLAYER_LIBS} vgm-emu vgm-utils vgm-player)

if(CMAKE_COMPILER_IS_GNUCC OR UNIX)
	# link Math library
	target_link_libraries(${PROJECT_NAME} PRIVATE m)
	set(PLAYER_PC_LDFLAGS "-lm")
endif()


if(FALSE)
pkgcfg_configure("${LIBVGM_SOURCE_DIR}/cmake/libvgm.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/vgm-player.pc"
		NAME "LibVGM Player"
		DESC "LibVGM file playback library"
		VERSION ${LIBVGM_VERSION}
		DEFINES ${PLAYER_DEFS}
		CFLAGS ${PLAYER_PC_CFLAGS}
		LDFLAGS_PRIV ${PLAYER_PC_LDFLAGS}
		LDFLAGS_PUB "-l${PROJECT_NAME}"
		PKGS_PRIV vgm-emu vgm-utils
	)

install(TARGETS ${PROJECT_NAME}
		EXPORT "${LIBVGM_NAME}"
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	)
set(LIBVGM_INSTALL_TARGETS ${LIBVGM_INSTALL_TARGETS} "${PROJECT_NAME}" PARENT_SCOPE)
install(FILES ${PLAYER_HEADERS} DESTINATION "${LIBVGM_INSTALL_INCLUDE_DIR}/player")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vgm-player.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

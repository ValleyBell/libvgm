#### File Playback Library ####
project(vgm-player)

set(PLAYER_DEFS)
set(PLAYER_FILES
	dblk_compr.c
	helper.c
	playerbase.cpp
	droplayer.cpp
	s98player.cpp
	vgmplayer_cmdhandler.cpp
	vgmplayer.cpp
	playera.cpp
)
# export headers
set(PLAYER_HEADERS
	dblk_compr.h
	helper.h
	logging.h
	playerbase.hpp
	droplayer.hpp
	s98player.hpp
	vgmplayer.hpp
	playera.hpp
)
set(PLAYER_INCLUDES)
set(PLAYER_LIBS)

set(PLAYER_PC_CFLAGS)
set(PLAYER_PC_LDFLAGS)


add_library(${PROJECT_NAME}_objlib OBJECT ${PLAYER_FILES})
set_target_properties(${PROJECT_NAME}_objlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(${PROJECT_NAME}_objlib PUBLIC ${PLAYER_DEFS})
target_include_directories(${PROJECT_NAME}_objlib PRIVATE ${PLAYER_INCLUDES})
target_link_libraries(${PROJECT_NAME}_objlib PRIVATE ${PLAYER_LIBS})

add_library(${PROJECT_NAME} ${LIBRARY_TYPE} $<TARGET_OBJECTS:${PROJECT_NAME}_objlib> $<TARGET_OBJECTS:vgm-emu_objlib>)
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PLAYER_DEFS})
target_include_directories(${PROJECT_NAME}
	PUBLIC $<BUILD_INTERFACE:${LIBVGM_SOURCE_DIR}> $<INSTALL_INTERFACE:${LIBVGM_INSTALL_INCLUDE_DIR}>
)
target_link_libraries(${PROJECT_NAME} PRIVATE vgm-utils ${PLAYER_LIBS})

pkgcfg_configure("${LIBVGM_SOURCE_DIR}/cmake/libvgm.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/vgm-player.pc"
		NAME "LibVGM Player"
		DESC "LibVGM file playback library"
		VERSION ${LIBVGM_VERSION}
		DEFINES ${PLAYER_DEFS}
		CFLAGS ${PLAYER_PC_CFLAGS}
		LDFLAGS_PRIV ${PLAYER_PC_LDFLAGS}
		LDFLAGS_PUB "-l${PROJECT_NAME}"
		PKGS_PRIV ${UTILS_PC_REQUIRES} vgm-utils
	)

install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_objlib
		EXPORT "${LIBVGM_NAME}"
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	)
set(LIBVGM_INSTALL_TARGETS ${LIBVGM_INSTALL_TARGETS} "${PROJECT_NAME}" PARENT_SCOPE)
install(FILES ${PLAYER_HEADERS} DESTINATION "${LIBVGM_INSTALL_INCLUDE_DIR}/player")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vgm-player.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

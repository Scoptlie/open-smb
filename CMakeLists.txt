cmake_minimum_required(VERSION 3.21)

project(OpenSMB LANGUAGES C)

file(GLOB_RECURSE OPENSMB_SOURCES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/source/*.c"
)

add_executable(smb
	${OPENSMB_SOURCES}
	"${PROJECT_SOURCE_DIR}/thirdparty/source/glad.c"
)

set_target_properties(smb PROPERTIES
	C_STANDARD 23
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO
)

target_include_directories(smb PRIVATE
	"${PROJECT_SOURCE_DIR}/source"
	"${PROJECT_SOURCE_DIR}/thirdparty/source"
)

if(WIN32)
	target_link_libraries(smb PRIVATE
		"${PROJECT_SOURCE_DIR}/thirdparty/lib/SDL3.lib"
		"${PROJECT_SOURCE_DIR}/thirdparty/lib/OpenAL32.lib"
		opengl32
	)
	
	add_custom_command(TARGET smb POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"${PROJECT_SOURCE_DIR}/thirdparty/bin/SDL3.dll"
			"${PROJECT_SOURCE_DIR}/thirdparty/bin/OpenAL32.dll"
			"$<TARGET_FILE_DIR:smb>"
		VERBATIM
	)
else()
	find_library(SDL3 SDL3 REQUIRED)
	find_library(OPENGL OpenGL opengl OpenGL32 opengl32 GL REQUIRED)
	find_library(OPENAL OpenAL openal OpenAL32 openal32 AL REQUIRED)
	find_library(M m)
	
	target_link_libraries(smb PRIVATE
		${SDL3} ${OPENGL} ${OPENAL}
		${M} ${CMAKE_DL_LIBS}
	)
endif()

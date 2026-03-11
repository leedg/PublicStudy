# =============================================================================
# Server/Tests — AsyncIO unit test executables
# =============================================================================
# English: Builds platform-specific unit tests.
#          Each test uses the same [SKIP] pattern: the binary is always produced
#          but prints "[SKIP] <name>: <platform> only" when run on an unsupported
#          platform, so CI matrices stay simple.
# 한글: 플랫폼별 단위 테스트 빌드.
#       각 테스트는 동일한 [SKIP] 패턴 사용: 실행 파일은 항상 생성되지만
#       미지원 플랫폼에서 실행 시 "[SKIP] <name>: <platform> only"를 출력,
#       CI 매트릭스를 단순하게 유지.

set(TESTS_ENGINE_INCLUDE
    ${CMAKE_SOURCE_DIR}/Server/ServerEngine
    ${CMAKE_SOURCE_DIR}/Server/ServerEngine/Network/Core
)

# -----------------------------------------------------------------------
# EpollTest — Linux only (no extra library; epoll is always available)
# -----------------------------------------------------------------------
add_executable(EpollTest EpollTest/EpollTest.cpp)
target_include_directories(EpollTest PRIVATE ${TESTS_ENGINE_INCLUDE})
target_link_libraries(EpollTest PRIVATE ServerEngine)
target_compile_options(EpollTest PRIVATE -Wall -Wextra -Wno-unused-parameter)

# -----------------------------------------------------------------------
# IOUringTest — Linux + liburing
# -----------------------------------------------------------------------
add_executable(IOUringTest IOUringTest/IOUringTest.cpp)
target_include_directories(IOUringTest PRIVATE ${TESTS_ENGINE_INCLUDE})
target_link_libraries(IOUringTest PRIVATE ServerEngine)
target_compile_options(IOUringTest PRIVATE -Wall -Wextra -Wno-unused-parameter)
# English: HAVE_LIBURING propagates PUBLIC from ServerEngine when liburing was
#          found; no manual define needed here.
# 한글: liburing 발견 시 ServerEngine PUBLIC을 통해 HAVE_LIBURING이 전파됨;
#       여기서 별도 define 불필요.

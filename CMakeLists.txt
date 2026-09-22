cmake_minimum_required(VERSION 3.16)

# 定义工程名称、版本和所用语言。
project(
    seamine
    VERSION 1.0.0
    DESCRIPTION "水面舰船与潜艇电磁场特性仿真"
    LANGUAGES CXX
)

# 全工程统一使用标准 C++17，并禁止编译器私有扩展。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 先确定测试开关，再以源码方式编入 HJC；业务程序不调用 HJC 演示入口。
include(CTest)
set(HJC_BUILD_TESTS ${BUILD_TESTING} CACHE BOOL "构建 HJC 原有回归测试" FORCE)
set(HJC_BUILD_EXAMPLES OFF CACHE BOOL "主工程不构建 HJC 独立演示" FORCE)
set(HJC_BUILD_TOOLS ${BUILD_TESTING} CACHE BOOL "HJC 测试夹具需要数据准备工具" FORCE)
add_subdirectory(HJC)

# 核心模型单独构建为库，便于示例程序和其他工程复用。
add_library(
    target_electric_field_model
    TargetElectricFieldModel.cpp
)
target_include_directories(
    target_electric_field_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 磁场模型独立构建为库，供示例程序、测试和后续服务端接口复用。
add_library(
    target_magnetic_field_model
    TargetMagneticFieldModel.cpp
)
target_include_directories(
    target_magnetic_field_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 水压场模型独立构建为库，提供单点、时序和空间网格仿真接口。
add_library(
    target_pressure_field_model
    TargetPressureFieldModel.cpp
)
target_include_directories(
    target_pressure_field_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 电场适配层把现有目标参数交给 HJC，并输出目标、环境和合成三组电场。
add_library(electric_environment_simulation
    integration/hjc/ElectricFieldSimulation.cpp
    integration/hjc/ElectricEnvironmentConfig.cpp
)
target_include_directories(electric_environment_simulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(electric_environment_simulation PUBLIC target_electric_field_model hjc_field)
if(MSVC)
    target_compile_options(electric_environment_simulation PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(electric_environment_simulation PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 构建水压合成适配层，并为新增代码启用严格警告和中文源文件编码。
add_library(pressure_environment_simulation
    integration/hjc/PressureFieldSimulation.cpp
    integration/hjc/PressureEnvironmentConfig.cpp
)
# 适配层单向依赖 HJC，现有目标水压模型不反向依赖环境模块。
target_include_directories(pressure_environment_simulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pressure_environment_simulation PUBLIC target_pressure_field_model hjc_field)

# 磁场适配层独立于水压流程；目标、环境和合成场由同一次 HJC 求解给出。
add_library(magnetic_environment_simulation
    integration/hjc/MagneticFieldSimulation.cpp
    integration/hjc/MagneticEnvironmentConfig.cpp
)
target_include_directories(magnetic_environment_simulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(magnetic_environment_simulation PUBLIC target_magnetic_field_model hjc_field)
if(MSVC)
    target_compile_options(magnetic_environment_simulation PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(magnetic_environment_simulation PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MSVC)
    target_compile_options(pressure_environment_simulation PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pressure_environment_simulation PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(
        target_electric_field_model
        PRIVATE /W4 /permissive- /utf-8
    )
    target_compile_options(
        target_magnetic_field_model
        PRIVATE /W4 /permissive- /utf-8
    )
    target_compile_options(
        target_pressure_field_model
        PRIVATE /W4 /permissive- /utf-8
    )
else()
    target_compile_options(
        target_electric_field_model
        PRIVATE -Wall -Wextra -Wpedantic
    )
    target_compile_options(
        target_magnetic_field_model
        PRIVATE -Wall -Wextra -Wpedantic
    )
    target_compile_options(
        target_pressure_field_model
        PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

# 构建可直接运行的示例仿真程序。
add_executable(
    seamine_simulator
    main.cpp
)
target_link_libraries(
    seamine_simulator
    PRIVATE
        target_electric_field_model
        target_magnetic_field_model
        target_pressure_field_model
        electric_environment_simulation
        pressure_environment_simulation
        magnetic_environment_simulation
)
# 默认配置随可执行程序放置；运行时也可用命令行指定其他配置文件。
add_custom_command(TARGET seamine_simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/pressure_environment_demo.ini"
        "$<TARGET_FILE_DIR:seamine_simulator>/pressure_environment_demo.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/electric_environment_demo.ini"
        "$<TARGET_FILE_DIR:seamine_simulator>/electric_environment_demo.ini"
    VERBATIM
)
# 磁场演示资源随程序部署，避免运行时依赖源码机器的绝对路径。
add_custom_command(TARGET seamine_simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/magnetic_environment_demo.ini"
        "$<TARGET_FILE_DIR:seamine_simulator>/magnetic_environment_demo.ini"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:seamine_simulator>/data/WMMHR2025"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/HJC/data/WMMHR2025/WMMHR.COF"
        "$<TARGET_FILE_DIR:seamine_simulator>/data/WMMHR2025/WMMHR.COF"
    VERBATIM
)
if(MSVC)
    target_compile_options(seamine_simulator PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(seamine_simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 启用内置测试目标，构建后可通过 CTest 一键验证。
if(BUILD_TESTING)
    # 电场集成测试覆盖三组矢量场、背景分量、单位换算、去重和配置校验。
    add_executable(electric_environment_integration_tests tests/ElectricEnvironmentIntegrationTests.cpp)
    target_link_libraries(electric_environment_integration_tests PRIVATE electric_environment_simulation)
    if(MSVC)
        target_compile_options(electric_environment_integration_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(electric_environment_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME electric_environment_integration_tests COMMAND electric_environment_integration_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/config/electric_environment_demo.ini"
        "${CMAKE_CURRENT_BINARY_DIR}/electric_environment_test_config.ini")

    # 磁场集成回归覆盖三组矢量场、近场拒绝、环境有效期及显式资源缺失。
    add_executable(magnetic_environment_integration_tests tests/MagneticEnvironmentIntegrationTests.cpp)
    target_link_libraries(magnetic_environment_integration_tests PRIVATE magnetic_environment_simulation)
    if(MSVC)
        target_compile_options(magnetic_environment_integration_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(magnetic_environment_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME magnetic_environment_integration_tests COMMAND magnetic_environment_integration_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/config/magnetic_environment_demo.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/HJC")

    # 集成测试覆盖原目标分量保持、背景合成、时序、单位和无效输入。
    add_executable(pressure_environment_integration_tests tests/PressureEnvironmentIntegrationTests.cpp)
    target_link_libraries(pressure_environment_integration_tests PRIVATE pressure_environment_simulation)
    if(MSVC)
        target_compile_options(pressure_environment_integration_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(pressure_environment_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME pressure_environment_integration_tests COMMAND pressure_environment_integration_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/config/pressure_environment_demo.ini"
        "${CMAKE_CURRENT_BINARY_DIR}/pressure_environment_test_config.ini")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pressure_environment_demo")
    add_test(NAME seamine_pressure_environment_demo COMMAND seamine_simulator)
    set_tests_properties(seamine_pressure_environment_demo PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pressure_environment_demo")
    add_executable(
        target_electric_field_model_tests
        tests/TargetElectricFieldModelTests.cpp
    )
    target_link_libraries(
        target_electric_field_model_tests
        PRIVATE target_electric_field_model
    )
    if(MSVC)
        target_compile_options(
            target_electric_field_model_tests
            PRIVATE /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            target_electric_field_model_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
        NAME target_electric_field_model_tests
        COMMAND target_electric_field_model_tests
    )

    # 磁场测试覆盖三分量计算、空间分布、距离衰减和参数校验。
    add_executable(
        target_magnetic_field_model_tests
        tests/TargetMagneticFieldModelTests.cpp
    )
    target_link_libraries(
        target_magnetic_field_model_tests
        PRIVATE target_magnetic_field_model
    )
    if(MSVC)
        target_compile_options(
            target_magnetic_field_model_tests
            PRIVATE /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            target_magnetic_field_model_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
        NAME target_magnetic_field_model_tests
        COMMAND target_magnetic_field_model_tests
    )

    # 水压场测试覆盖压力组成、航速规律、海深影响、运动时序和空间网格。
    add_executable(
        target_pressure_field_model_tests
        tests/TargetPressureFieldModelTests.cpp
    )
    target_link_libraries(
        target_pressure_field_model_tests
        PRIVATE target_pressure_field_model
    )
    if(MSVC)
        target_compile_options(
            target_pressure_field_model_tests
            PRIVATE /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            target_pressure_field_model_tests
            PRIVATE -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(
        NAME target_pressure_field_model_tests
        COMMAND target_pressure_field_model_tests
    )
endif()

# 提供标准安装目标，方便把程序部署到其他目录。
install(
    TARGETS
        seamine_simulator
        target_electric_field_model
        target_magnetic_field_model
        target_pressure_field_model
        electric_environment_simulation
        pressure_environment_simulation
        magnetic_environment_simulation
        hjc_field
        hjc_gsw
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(
    FILES
        TargetElectricFieldModel.h
        TargetMagneticFieldModel.h
        TargetPressureFieldModel.h
    DESTINATION include
)
# 安装适配接口及所需的 HJC 头文件和第三方许可证。
install(FILES integration/hjc/PressureFieldSimulation.h integration/hjc/PressureEnvironmentConfig.h
    DESTINATION include/integration/hjc)
install(FILES integration/hjc/ElectricFieldSimulation.h integration/hjc/ElectricEnvironmentConfig.h
    DESTINATION include/integration/hjc)
install(FILES integration/hjc/MagneticFieldSimulation.h integration/hjc/MagneticEnvironmentConfig.h
    DESTINATION include/integration/hjc)
install(DIRECTORY HJC/include/hjc DESTINATION include)
install(FILES config/pressure_environment_demo.ini DESTINATION bin)
install(FILES config/electric_environment_demo.ini DESTINATION bin)
install(FILES config/magnetic_environment_demo.ini DESTINATION bin)
install(FILES HJC/data/WMMHR2025/WMMHR.COF DESTINATION bin/data/WMMHR2025)
install(FILES HJC/THIRD_PARTY_NOTICES.md HJC/third_party/gsw/LICENSE.txt DESTINATION share/seamine/HJC)

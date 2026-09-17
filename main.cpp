#include "TargetElectricFieldModel.h"
#include "TargetMagneticFieldModel.h"
#include "TargetPressureFieldModel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
/**
 * @brief 配置 Windows 控制台使用 UTF-8，避免中文提示乱码。
 *
 * 非 Windows 平台不执行任何操作。
 */
void configureConsoleEncoding()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

/**
 * @brief 创建一组可直接运行的水面舰船演示参数。
 * @return 已填充几何、运动、环境和等效电源数据的目标参数。
 */
TargetElectricFieldModel::TargetParameter createDemoTarget()
{
    TargetElectricFieldModel::TargetParameter param{};
    param.type = TargetElectricFieldModel::TargetType::SurfaceShip;
    param.tonnage = 5000.0;
    param.length = 100.0;
    param.width = 15.0;
    param.draft = 5.0;
    param.velocity = 8.0;
    param.shaftSpeed = 120.0;
    param.salinity = 35.0;
    param.initialPosition = {-300.0, 40.0, -5.0};
    param.headingDegrees = 0.0;
    param.pitchDegrees = 0.0;
    param.waterTemperature = 18.0;
    param.seaPressureDbar = 30.0;

    // 以下经验值可用目标实测数据替换，以提高场强预测精度。
    param.corrosionCurrentDensity = 1e-4;
    param.coatingDamageRatio = 0.05;
    param.shaftModulationRatio = 0.04;
    param.secondHarmonicRatio = 0.20;
    param.thirdHarmonicRatio = 0.05;
    return param;
}

/**
 * @brief 创建一组可直接生成磁场空间分布的水面舰艇演示参数。
 * @return 已填充尺寸、姿态、地磁和等效磁性数据的目标参数。
 */
TargetMagneticFieldModel::TargetParameter createMagneticDemoTarget()
{
    TargetMagneticFieldModel::TargetParameter param{};
    param.type = TargetMagneticFieldModel::TargetType::SurfaceShip;
    param.length = 100.0;
    param.width = 15.0;
    param.height = 10.0;
    param.center = {0.0, 0.0, -5.0};
    param.headingDegrees = 20.0;
    param.pitchDegrees = 0.0;
    param.rollDegrees = 0.0;

    // 地磁三分量采用东、北、上坐标，负 z 表示磁场指向地球内部。
    param.geomagneticFieldNt = {18000.0, 30000.0, -42000.0};
    param.relativePermeability = 180.0;
    param.magneticMaterialRatio = 0.035;
    param.remanentMagnetizationAm = {7.5, 0.8, -0.4};
    param.minimumDistance = 2.0;
    return param;
}

/**
 * @brief 创建从固定磁传感器侧前方驶过的运动舰艇磁场参数。
 * @return 已设置零时刻位置、航向和航速的完整磁场参数。
 */
TargetMagneticFieldModel::TargetParameter createMovingMagneticDemoTarget()
{
    auto param = createMagneticDemoTarget();
    param.center = {-300.0, 40.0, -5.0};
    param.velocity = 8.0;
    param.headingDegrees = 0.0;
    return param;
}

/**
 * @brief 创建一组从固定水压传感器侧前方驶过的水面舰艇演示参数。
 * @return 已设置尺寸、吃水、海深、初始位置和航速的完整水压场参数。
 */
TargetPressureFieldModel::TargetParameter createPressureDemoTarget()
{
    TargetPressureFieldModel::TargetParameter param{};
    param.type = TargetPressureFieldModel::TargetType::SurfaceShip;
    param.length = 100.0;
    param.width = 15.0;
    param.draft = 5.0;
    param.waterDepth = 60.0;
    param.velocity = 3.0;
    param.initialPosition = {-180.0, 40.0, -2.5};
    param.headingDegrees = 0.0;
    param.waterDensity = 1025.0;
    param.blockCoefficient = 0.68;
    param.minimumDistance = 1.0;
    return param;
}

/**
 * @brief 把完整仿真时序覆盖写入 CSV 文件。
 * @param outputPath CSV 输出文件路径，可以是相对路径或绝对路径。
 * @param signals 按时间排列的电场采样结果，不要求非空。
 * @throws std::runtime_error 无法创建文件或写入过程中发生错误时抛出。
 */
void writeCsv(
    const std::string& outputPath,
    const std::vector<TargetElectricFieldModel::ElectricFieldSignal>& signals)
{
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("无法创建输出文件：" + outputPath);
    }

    output << "time_s,distance_m,target_x_m,target_y_m,target_z_m,"
              "static_ex_uV_m,static_ey_uV_m,static_ez_uV_m,"
              "shaft_ex_uV_m,shaft_ey_uV_m,shaft_ez_uV_m,"
              "total_ex_uV_m,total_ey_uV_m,total_ez_uV_m,"
              "static_magnitude_uV_m,shaft_magnitude_uV_m,"
              "shaft_amplitude_uV_m,total_magnitude_uV_m,frequency_hz\n";
    output << std::setprecision(12);

    for (const auto& signal : signals)
    {
        output
            << signal.time << ','
            << signal.distance << ','
            << signal.targetPosition.x << ','
            << signal.targetPosition.y << ','
            << signal.targetPosition.z << ','
            << signal.staticFieldVector.x << ','
            << signal.staticFieldVector.y << ','
            << signal.staticFieldVector.z << ','
            << signal.shaftFieldVector.x << ','
            << signal.shaftFieldVector.y << ','
            << signal.shaftFieldVector.z << ','
            << signal.totalFieldVector.x << ','
            << signal.totalFieldVector.y << ','
            << signal.totalFieldVector.z << ','
            << signal.staticField << ','
            << signal.shaftField << ','
            << signal.shaftAmplitude << ','
            << signal.totalField << ','
            << signal.frequency << '\n';
    }

    if (!output)
    {
        throw std::runtime_error("写入输出文件失败：" + outputPath);
    }
}

/**
 * @brief 把目标磁场空间分布覆盖写入 CSV 文件。
 * @param outputPath CSV 输出文件路径。
 * @param samples 按规则网格顺序排列的磁场采样结果。
 * @throws std::runtime_error 无法创建或写入输出文件时抛出。
 */
void writeMagneticCsv(
    const std::string& outputPath,
    const std::vector<TargetMagneticFieldModel::MagneticFieldSample>& samples)
{
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("无法创建磁场输出文件：" + outputPath);
    }

    output << "x_m,y_m,z_m,distance_m,"
              "static_bx_nt,static_by_nt,static_bz_nt,"
              "induced_bx_nt,induced_by_nt,induced_bz_nt,"
              "total_bx_nt,total_by_nt,total_bz_nt,"
              "static_magnitude_nt,induced_magnitude_nt,total_magnitude_nt\n";
    output << std::setprecision(12);

    for (const auto& sample : samples)
    {
        output
            << sample.observationPosition.x << ','
            << sample.observationPosition.y << ','
            << sample.observationPosition.z << ','
            << sample.distance << ','
            << sample.staticFieldVector.x << ','
            << sample.staticFieldVector.y << ','
            << sample.staticFieldVector.z << ','
            << sample.inducedFieldVector.x << ','
            << sample.inducedFieldVector.y << ','
            << sample.inducedFieldVector.z << ','
            << sample.totalFieldVector.x << ','
            << sample.totalFieldVector.y << ','
            << sample.totalFieldVector.z << ','
            << sample.staticField << ','
            << sample.inducedField << ','
            << sample.totalField << '\n';
    }

    if (!output)
    {
        throw std::runtime_error("写入磁场输出文件失败：" + outputPath);
    }
}

/**
 * @brief 把运动目标磁场时序覆盖写入 CSV 文件。
 * @param outputPath CSV 输出文件路径。
 * @param samples 按时间升序排列的磁场采样结果。
 * @throws std::runtime_error 无法创建或写入输出文件时抛出。
 */
void writeMagneticTimeSeriesCsv(
    const std::string& outputPath,
    const std::vector<TargetMagneticFieldModel::MagneticFieldSample>& samples)
{
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("无法创建磁场时序输出文件：" + outputPath);
    }

    output << "time_s,target_x_m,target_y_m,target_z_m,"
              "sensor_x_m,sensor_y_m,sensor_z_m,distance_m,"
              "static_bx_nt,static_by_nt,static_bz_nt,"
              "induced_bx_nt,induced_by_nt,induced_bz_nt,"
              "total_bx_nt,total_by_nt,total_bz_nt,"
              "static_magnitude_nt,induced_magnitude_nt,total_magnitude_nt\n";
    output << std::setprecision(12);

    for (const auto& sample : samples)
    {
        output
            << sample.time << ','
            << sample.targetPosition.x << ','
            << sample.targetPosition.y << ','
            << sample.targetPosition.z << ','
            << sample.observationPosition.x << ','
            << sample.observationPosition.y << ','
            << sample.observationPosition.z << ','
            << sample.distance << ','
            << sample.staticFieldVector.x << ','
            << sample.staticFieldVector.y << ','
            << sample.staticFieldVector.z << ','
            << sample.inducedFieldVector.x << ','
            << sample.inducedFieldVector.y << ','
            << sample.inducedFieldVector.z << ','
            << sample.totalFieldVector.x << ','
            << sample.totalFieldVector.y << ','
            << sample.totalFieldVector.z << ','
            << sample.staticField << ','
            << sample.inducedField << ','
            << sample.totalField << '\n';
    }

    if (!output)
    {
        throw std::runtime_error("写入磁场时序输出文件失败：" + outputPath);
    }
}

/** 把水压场工况枚举转换为稳定的 CSV 字符串。 */
const char* pressureFieldRegimeName(
    TargetPressureFieldModel::PressureFieldRegime regime) noexcept
{
    using Regime = TargetPressureFieldModel::PressureFieldRegime;
    switch (regime)
    {
    case Regime::SurfaceShipLowSpeed:
        return "surface_ship_low_speed";
    case Regime::SurfaceShipTransition:
        return "surface_ship_transition";
    case Regime::SurfaceShipShallowSubcritical:
        return "surface_ship_shallow_subcritical";
    case Regime::SubmarineNoWave:
        return "submarine_no_wave";
    case Regime::UnsupportedSurfaceShipDeepWaterWave:
        return "unsupported_surface_ship_deep_water_wave";
    case Regime::UnsupportedSurfaceShipCritical:
        return "unsupported_surface_ship_critical";
    case Regime::UnsupportedSurfaceShipSupercritical:
        return "unsupported_surface_ship_supercritical";
    case Regime::UnsupportedSubmarineFreeSurfaceWave:
        return "unsupported_submarine_free_surface_wave";
    }
    // 枚举新增成员但调用方尚未同步时返回可诊断的兜底文本。
    return "unknown";
}

/**
 * @brief 把运动目标水压场时序覆盖写入 CSV 文件。
 * @param outputPath CSV 输出文件路径。
 * @param samples 按时间升序排列的水压场采样结果。
 * @throws std::runtime_error 无法创建或写入输出文件时抛出。
 */
void writePressureTimeSeriesCsv(
    const std::string& outputPath,
    const std::vector<TargetPressureFieldModel::PressureFieldSample>& samples)
{
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("无法创建水压场时序输出文件：" + outputPath);
    }

    output << "time_s,target_x_m,target_y_m,target_z_m,"
              "sensor_x_m,sensor_y_m,sensor_z_m,distance_m,"
              "longitudinal_offset_m,lateral_offset_m,observation_depth_m,"
              "regime,length_froude_number,depth_froude_number,"
              "wave_attenuation,transition_weight,hydrostatic_pressure_pa,"
              "body_dynamic_pressure_pa,free_surface_correction_pressure_pa,"
              "wave_pressure_pa,seabed_correction_pressure_pa,"
              "dynamic_pressure_pa,"
              "total_gauge_pressure_pa\n";
    output << std::setprecision(12);

    for (const auto& sample : samples)
    {
        output
            << sample.time << ','
            << sample.targetPosition.x << ','
            << sample.targetPosition.y << ','
            << sample.targetPosition.z << ','
            << sample.observationPosition.x << ','
            << sample.observationPosition.y << ','
            << sample.observationPosition.z << ','
            << sample.distance << ','
            << sample.longitudinalOffset << ','
            << sample.lateralOffset << ','
            << sample.observationDepth << ','
            << pressureFieldRegimeName(sample.regime) << ','
            << sample.lengthFroudeNumber << ','
            << sample.depthFroudeNumber << ','
            << sample.waveAttenuation << ','
            << sample.transitionWeight << ','
            << sample.hydrostaticPressure << ','
            << sample.bodyDynamicPressure << ','
            << sample.freeSurfaceCorrectionPressure << ','
            << sample.wavePressure << ','
            << sample.seabedCorrectionPressure << ','
            << sample.dynamicPressure << ','
            << sample.totalGaugePressure << '\n';
    }

    if (!output)
    {
        throw std::runtime_error("写入水压场时序输出文件失败：" + outputPath);
    }
}

/**
 * @brief 比较两个采样结果与传感器的距离。
 * @param lhs 左侧电场采样结果。
 * @param rhs 右侧电场采样结果。
 * @return lhs 的目标距离小于 rhs 时返回 true，否则返回 false。
 */
bool isCloser(
    const TargetElectricFieldModel::ElectricFieldSignal& lhs,
    const TargetElectricFieldModel::ElectricFieldSignal& rhs)
{
    return lhs.distance < rhs.distance;
}

/**
 * @brief 在控制台显示本次仿真的采样数、轴频、最近点和输出路径。
 * @param outputPath 已写入的 CSV 输出文件路径。
 * @param signals 按时间排列的电场采样结果，必须至少包含一个元素。
 */
void printSummary(
    const std::string& outputPath,
    const std::vector<TargetElectricFieldModel::ElectricFieldSignal>& signals)
{
    const auto closest = std::min_element(
        signals.begin(),
        signals.end(),
        isCloser);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "仿真完成\n";
    std::cout << "采样点数：" << signals.size() << '\n';
    std::cout << "轴频基波：" << signals.front().frequency << " Hz\n";
    std::cout << "最近距离：" << closest->distance
              << " m，发生时刻：" << closest->time << " s\n";
    std::cout << "最近点综合电场：" << closest->totalField << " μV/m\n";
    std::cout << "结果文件：" << outputPath << '\n';
}

/** 比较两个磁场采样结果的综合场强。 */
bool isMagneticFieldWeaker(
    const TargetMagneticFieldModel::MagneticFieldSample& lhs,
    const TargetMagneticFieldModel::MagneticFieldSample& rhs)
{
    return lhs.totalField < rhs.totalField;
}

/**
 * @brief 在控制台显示磁场网格点数、最大综合磁场和输出路径。
 * @param outputPath 已写入的磁场 CSV 文件路径。
 * @param samples 磁场网格采样结果，必须至少包含一个元素。
 */
void printMagneticSummary(
    const std::string& outputPath,
    const std::vector<TargetMagneticFieldModel::MagneticFieldSample>& samples)
{
    const auto strongest = std::max_element(
        samples.begin(), samples.end(), isMagneticFieldWeaker);

    std::cout << "磁场空间分布仿真完成\n";
    std::cout << "磁场网格点数：" << samples.size() << '\n';
    std::cout << "最大综合目标异常磁场：" << strongest->totalField << " nT\n";
    std::cout << "最大场强位置：("
              << strongest->observationPosition.x << ", "
              << strongest->observationPosition.y << ", "
              << strongest->observationPosition.z << ") m\n";
    std::cout << "磁场结果文件：" << outputPath << '\n';
}

/**
 * @brief 显示运动目标磁场时序的采样数、峰值时刻和输出路径。
 * @param outputPath 已写入的磁场时序 CSV 文件路径。
 * @param samples 按时间排列的磁场采样结果，必须至少包含一个元素。
 */
void printMagneticTimeSeriesSummary(
    const std::string& outputPath,
    const std::vector<TargetMagneticFieldModel::MagneticFieldSample>& samples)
{
    const auto strongest = std::max_element(
        samples.begin(), samples.end(), isMagneticFieldWeaker);

    std::cout << "运动目标磁场时序仿真完成\n";
    std::cout << "磁场时序采样点数：" << samples.size() << '\n';
    std::cout << "磁场峰值：" << strongest->totalField
              << " nT，发生时刻：" << strongest->time << " s\n";
    std::cout << "磁场时序结果文件：" << outputPath << '\n';
}

/**
 * @brief 显示水压场时序的采样数、最大压力变化及输出路径。
 * @param outputPath 已写入的水压场时序 CSV 路径。
 * @param samples 按时间排列且至少包含一个元素的水压场采样结果。
 */
void printPressureTimeSeriesSummary(
    const std::string& outputPath,
    const std::vector<TargetPressureFieldModel::PressureFieldSample>& samples)
{
    const auto strongest = std::max_element(
        samples.begin(),
        samples.end(),
        [](const auto& lhs, const auto& rhs)
        {
            // 以动态压力绝对值衡量水压异常信号强弱。
            return std::abs(lhs.dynamicPressure) <
                std::abs(rhs.dynamicPressure);
        });

    std::cout << "运动目标水压场时序仿真完成\n";
    std::cout << "水压场时序采样点数：" << samples.size() << '\n';
    std::cout << "水动力工况："
              << pressureFieldRegimeName(samples.front().regime) << '\n';
    std::cout << "船长弗劳德数：" << samples.front().lengthFroudeNumber
              << "，水深弗劳德数：" << samples.front().depthFroudeNumber
              << '\n';
    std::cout << "最大动态压力变化：" << strongest->dynamicPressure
              << " Pa，发生时刻：" << strongest->time << " s\n";
    std::cout << "水压场时序结果文件：" << outputPath << '\n';
}
} // 匿名命名空间

/**
 * @brief 程序入口，生成舰船电场、磁场和水压场仿真 CSV。
 * @param argc 命令行参数总数；最多允许指定四个输出文件路径。
 * @param argv 依次为电场、磁场空间分布、磁场时序和水压场时序输出路径。
 * @return 成功时返回 EXIT_SUCCESS，参数错误或仿真异常时返回 EXIT_FAILURE。
 */
int main(int argc, char* argv[])
{
    configureConsoleEncoding();

    if (argc > 5)
    {
        std::cerr << "用法：seamine_simulator [电场CSV路径] "
                     "[磁场分布CSV路径] [磁场时序CSV路径] "
                     "[水压场时序CSV路径]\n";
        return EXIT_FAILURE;
    }

    const std::string outputPath =
        argc >= 2 ? argv[1] : "electric_field_simulation.csv";
    const std::string magneticOutputPath =
        argc >= 3 ? argv[2] : "magnetic_field_distribution.csv";
    const std::string magneticTimeOutputPath =
        argc >= 4 ? argv[3] : "magnetic_field_time_series.csv";
    const std::string pressureTimeOutputPath =
        argc >= 5 ? argv[4] : "pressure_field_time_series.csv";

    try
    {
        const TargetElectricFieldModel model(createDemoTarget());

        // 固定传感器位于水下三十米，目标从其侧前方匀速通过。
        const TargetElectricFieldModel::Vector3 sensorPosition{
            0.0,
            0.0,
            -30.0};
        constexpr double startTimeSeconds = 0.0;
        constexpr double durationSeconds = 75.0;
        constexpr double sampleRateHz = 20.0;

        const auto signals = model.simulate(
            sensorPosition,
            startTimeSeconds,
            durationSeconds,
            sampleRateHz);
        writeCsv(outputPath, signals);
        printSummary(outputPath, signals);

        // 在目标下方固定深度的水平面生成静磁场和感应磁场三分量分布。
        const TargetMagneticFieldModel magneticModel(createMagneticDemoTarget());
        TargetMagneticFieldModel::GridParameter magneticGrid{};
        magneticGrid.minimum = {-250.0, -150.0, -30.0};
        magneticGrid.maximum = {250.0, 150.0, -30.0};
        magneticGrid.xCount = 101U;
        magneticGrid.yCount = 61U;
        magneticGrid.zCount = 1U;

        const auto magneticSamples = magneticModel.simulateGrid(magneticGrid);
        writeMagneticCsv(magneticOutputPath, magneticSamples);
        printMagneticSummary(magneticOutputPath, magneticSamples);

        // 固定磁传感器，计算舰艇从侧前方匀速通过时的连续磁场信号。
        const TargetMagneticFieldModel movingMagneticModel(
            createMovingMagneticDemoTarget());
        const TargetMagneticFieldModel::Vector3 magneticSensorPosition{
            0.0,
            0.0,
            -30.0};
        const auto magneticTimeSamples = movingMagneticModel.simulate(
            magneticSensorPosition,
            startTimeSeconds,
            durationSeconds,
            sampleRateHz);
        writeMagneticTimeSeriesCsv(
            magneticTimeOutputPath, magneticTimeSamples);
        printMagneticTimeSeriesSummary(
            magneticTimeOutputPath, magneticTimeSamples);

        // 固定水压传感器位于海床上方，生成目标匀速通过时的连续水压异常。
        const TargetPressureFieldModel pressureModel(
            createPressureDemoTarget());
        const TargetPressureFieldModel::Vector3 pressureSensorPosition{
            0.0,
            0.0,
            -55.0};
        const auto pressureTimeSamples = pressureModel.simulate(
            pressureSensorPosition,
            startTimeSeconds,
            durationSeconds,
            sampleRateHz);
        writePressureTimeSeriesCsv(
            pressureTimeOutputPath, pressureTimeSamples);
        printPressureTimeSeriesSummary(
            pressureTimeOutputPath, pressureTimeSamples);
    }
    catch (const std::exception& exception)
    {
        std::cerr << "仿真失败：" << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

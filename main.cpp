#include "TargetElectricFieldModel.h"
#include "TargetMagneticFieldModel.h"
#include "TargetPressureFieldModel.h"
#include "integration/hjc/ElectricEnvironmentConfig.h"
#include "integration/hjc/ElectricFieldSimulation.h"
#include "integration/hjc/MagneticFieldSimulation.h"
#include "integration/hjc/MagneticEnvironmentConfig.h"
#include "integration/hjc/PressureFieldSimulation.h"
#include "integration/hjc/PressureEnvironmentConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
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
    param.dipoleArrayLongitudinalCount = 7U;
    param.dipoleArrayTransverseCount = 3U;
    param.dipoleArrayVerticalCount = 3U;
    param.localCorrectionStrength = 0.65;
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

/** 将 HJC 同次求得的目标、背景和合成电场按 ENU 三分量写出。 */
void writeCombinedElectricTimeSeriesCsv(
    const std::string& outputPath, const hjc::field::ElectricSimulationOutput& result)
{
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建电场合成输出文件：" + outputPath);
    output << "time_s,signal_ex_uV_m,signal_ey_uV_m,signal_ez_uV_m,"
              "environment_ex_uV_m,environment_ey_uV_m,environment_ez_uV_m,"
              "total_ex_uV_m,total_ey_uV_m,total_ez_uV_m,"
              "signal_magnitude_uV_m,environment_magnitude_uV_m,total_magnitude_uV_m,scalar_anomaly_uV_m,"
              "target_static_ex_uV_m,target_static_ey_uV_m,target_static_ez_uV_m,"
              "target_shaft_ex_uV_m,target_shaft_ey_uV_m,target_shaft_ez_uV_m,"
              "motional_ex_uV_m,motional_ey_uV_m,motional_ez_uV_m,"
              "shipping_ex_uV_m,shipping_ey_uV_m,shipping_ez_uV_m,"
              "local_ex_uV_m,local_ey_uV_m,local_ez_uV_m\n";
    output << std::setprecision(17);
    constexpr double voltsToMicrovolts = 1.0e6;
    const auto& components = *result.components;
    const auto magnitude = [](const hjc::field::ElectricSeriesVm& series, std::size_t index)
    {
        return std::hypot(series.x[index], series.y[index], series.z[index]);
    };
    const auto writeVector = [&](const hjc::field::ElectricSeriesVm& series, std::size_t index)
    {
        output << series.x[index] * voltsToMicrovolts << ','
               << series.y[index] * voltsToMicrovolts << ','
               << series.z[index] * voltsToMicrovolts;
    };
    for (std::size_t index = 0; index < result.timeSeconds.size(); ++index)
    {
        const double signalMagnitude = magnitude(result.signalOnly, index) * voltsToMicrovolts;
        const double environmentMagnitude = magnitude(result.environmentOnly, index) * voltsToMicrovolts;
        const double totalMagnitude = magnitude(result.totalField, index) * voltsToMicrovolts;
        output << result.timeSeconds[index] << ',';
        writeVector(result.signalOnly, index);
        output << ',';
        writeVector(result.environmentOnly, index);
        output << ',';
        writeVector(result.totalField, index);
        output << ',' << signalMagnitude << ',' << environmentMagnitude << ',' << totalMagnitude << ','
               << totalMagnitude - environmentMagnitude << ',';
        writeVector(components.staticField, index);
        output << ',';
        writeVector(components.shaftField, index);
        output << ',';
        writeVector(components.motional, index);
        output << ',';
        writeVector(components.shipping, index);
        output << ',';
        writeVector(components.local, index);
        output << '\n';
    }
    if (!output) throw std::runtime_error("写入电场合成输出文件失败：" + outputPath);
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
              "static_magnitude_nt,induced_magnitude_nt,total_magnitude_nt,"
              "macro_bx_nt,macro_by_nt,macro_bz_nt,"
              "local_correction_bx_nt,local_correction_by_nt,local_correction_bz_nt,"
              "dipole_array_node_count\n";
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
            << sample.totalField << ','
            << sample.macroFieldVector.x << ','
            << sample.macroFieldVector.y << ','
            << sample.macroFieldVector.z << ','
            << sample.localCorrectionFieldVector.x << ','
            << sample.localCorrectionFieldVector.y << ','
            << sample.localCorrectionFieldVector.z << ','
            << sample.dipoleArrayNodeCount << '\n';
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
              "static_magnitude_nt,induced_magnitude_nt,total_magnitude_nt,"
              "macro_bx_nt,macro_by_nt,macro_bz_nt,"
              "local_correction_bx_nt,local_correction_by_nt,local_correction_bz_nt,"
              "dipole_array_node_count\n";
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
            << sample.totalField << ','
            << sample.macroFieldVector.x << ','
            << sample.macroFieldVector.y << ','
            << sample.macroFieldVector.z << ','
            << sample.localCorrectionFieldVector.x << ','
            << sample.localCorrectionFieldVector.y << ','
            << sample.localCorrectionFieldVector.z << ','
            << sample.dipoleArrayNodeCount << '\n';
    }

    if (!output)
    {
        throw std::runtime_error("写入磁场时序输出文件失败：" + outputPath);
    }
}

/** 将 HJC 同次求得的目标、背景及合成磁场按 ENU 三分量分别写出。 */
void writeCombinedMagneticTimeSeriesCsv(
    const std::string& outputPath, const hjc::field::MagneticSimulationOutput& result)
{
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建磁场合成输出文件：" + outputPath);
    output << "time_s,signal_bx_nt,signal_by_nt,signal_bz_nt,"
              "environment_bx_nt,environment_by_nt,environment_bz_nt,"
              "total_bx_nt,total_by_nt,total_bz_nt,"
              "signal_magnitude_nt,environment_magnitude_nt,total_magnitude_nt,scalar_anomaly_nt,"
              "target_macro_bx_nt,target_macro_by_nt,target_macro_bz_nt,"
              "target_local_correction_bx_nt,target_local_correction_by_nt,target_local_correction_bz_nt,"
              "main_bx_nt,main_by_nt,main_bz_nt,"
              "crustal_bx_nt,crustal_by_nt,crustal_bz_nt,crustal_scalar_nt,"
              "fluctuation_bx_nt,fluctuation_by_nt,fluctuation_bz_nt,"
              "local_bx_nt,local_by_nt,local_bz_nt,"
              "motional_bx_nt,motional_by_nt,motional_bz_nt\n";
    output << std::setprecision(17);
    const auto& components = *result.components;
    const auto magnitude = [](const hjc::field::MagneticSeriesNt& series, std::size_t index)
    {
        return std::hypot(series.x[index], series.y[index], series.z[index]);
    };
    for (std::size_t index = 0; index < result.timeSeconds.size(); ++index)
    {
        // 标量异常是合成矢量模长与背景矢量模长之差，不是目标模长。
        const double signalMagnitude = magnitude(result.signalOnly, index);
        const double environmentMagnitude = magnitude(result.environmentOnly, index);
        const double totalMagnitude = magnitude(result.totalField, index);
        output << result.timeSeconds[index] << ','
               << result.signalOnly.x[index] << ',' << result.signalOnly.y[index] << ',' << result.signalOnly.z[index] << ','
               << result.environmentOnly.x[index] << ',' << result.environmentOnly.y[index] << ',' << result.environmentOnly.z[index] << ','
               << result.totalField.x[index] << ',' << result.totalField.y[index] << ',' << result.totalField.z[index] << ','
               << signalMagnitude << ',' << environmentMagnitude << ',' << totalMagnitude << ','
               << totalMagnitude - environmentMagnitude << ','
               << components.targetMacro.x[index] << ',' << components.targetMacro.y[index] << ','
               << components.targetMacro.z[index] << ','
               << components.targetLocalCorrection.x[index] << ','
               << components.targetLocalCorrection.y[index] << ','
               << components.targetLocalCorrection.z[index] << ','
               << components.mainField.x[index] << ',' << components.mainField.y[index] << ',' << components.mainField.z[index] << ','
               << components.crustalAnomaly.x[index] << ',' << components.crustalAnomaly.y[index] << ','
               << components.crustalAnomaly.z[index] << ',' << components.crustalAnomalyScalar[index] << ','
               << components.fluctuation.x[index] << ',' << components.fluctuation.y[index] << ',' << components.fluctuation.z[index] << ','
               << components.localAnomaly.x[index] << ',' << components.localAnomaly.y[index] << ',' << components.localAnomaly.z[index] << ','
               << components.motional.x[index] << ',' << components.motional.y[index] << ',' << components.motional.z[index] << '\n';
    }
    if (!output) throw std::runtime_error("写入磁场合成输出文件失败：" + outputPath);
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
 * @param result 包含目标、背景与总场的水压时序及实际环境参数。
 * @throws std::runtime_error 无法创建或写入输出文件时抛出。
 */
void writePressureTimeSeriesCsv(
    const std::string& outputPath,
    const PressureFieldSimulation::Output& result)
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
              "total_gauge_pressure_pa,signal_only_pa,environment_only_pa,total_field_pa,"
              "total_dynamic_pressure_pa,environment_tide_pressure_pa,"
              "environment_wave_pressure_pa,environment_shipping_pressure_pa,"
              "water_depth_m,water_density_kg_m3\n";
    // 保留旧列的物理含义；新总场另列输出，17 位有效数字支持逐点复核。
    output << std::setprecision(17);

    for (const auto& combined : result.samples)
    {
        const auto& sample = combined.target;
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
            << sample.totalGaugePressure << ','
            << combined.signalOnly << ','
            << combined.environmentOnly << ','
            << combined.totalField << ','
            << combined.totalDynamicPressure << ','
            << combined.tidePressure << ','
            << combined.environmentWavePressure << ','
            << combined.shippingPressure << ','
            << result.waterDepthM << ','
            << result.waterDensityKgM3 << '\n';
    }

    if (!output)
    {
        throw std::runtime_error("写入水压场时序输出文件失败：" + outputPath);
    }
}

/** 保存环境配置原文和质量信息，避免合成演示结果被误认为实测海况。 */
void writePressureEnvironmentMetadata(const std::string& outputPath,
    const PressureEnvironmentConfig& config, const hjc::field::QualityInfo& quality)
{
    std::ofstream output(outputPath + ".environment.txt", std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建水压环境说明文件");
    output << "水压环境配置与计算记录\n"
           << "status=" << hjc::field::toString(quality.status) << '\n'
           << "method=" << quality.method << '\n'
           << "model_version=" << quality.modelVersion << '\n'
           << "source=" << quality.sourceUri << '\n';
    for (const auto& flag : quality.flags) output << "flag=" << flag << '\n';
    for (const auto& issue : quality.issues)
        output << "issue=" << issue.fieldPath << ": " << issue.message << '\n';
    output << "\n以下为原始配置；TAI 纳秒、ENU 坐标，压力单位 Pa。\n" << config.sourceText;
    if (!output) throw std::runtime_error("写入水压环境说明文件失败");
}

/** 保留磁场数据来源和质量标志，区分演示、模型背景与实测数据。 */
void writeMagneticEnvironmentMetadata(const std::string& outputPath,
    const MagneticEnvironmentConfig& config, const hjc::field::QualityInfo& quality)
{
    std::ofstream output(outputPath + ".environment.txt", std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建磁场环境说明文件");
    output << "磁场环境配置与计算记录\n"
           << "status=" << hjc::field::toString(quality.status) << '\n'
           << "method=" << quality.method << '\n'
           << "model_version=" << quality.modelVersion << '\n'
           << "source=" << quality.sourceUri << '\n';
    for (const auto& flag : quality.flags) output << "flag=" << flag << '\n';
    for (const auto& issue : quality.issues)
        output << "issue=" << issue.fieldPath << ": " << issue.message << '\n';
    output << "\n字段为 ENU 三分量、单位 nT；scalar_anomaly=|totalField|-|environmentOnly|。\n"
           << "以下为原始配置；时间采用 TAI 纳秒基准。\n" << config.sourceText;
    if (!output) throw std::runtime_error("写入磁场环境说明文件失败");
}

/** 保存电场环境配置和质量标志，明确 HJC 内部与 CSV 的单位换算。 */
void writeElectricEnvironmentMetadata(const std::string& outputPath,
    const ElectricEnvironmentConfig& config, const hjc::field::QualityInfo& quality)
{
    std::ofstream output(outputPath + ".environment.txt", std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建电场环境说明文件");
    output << "电场环境配置与计算记录\n"
           << "status=" << hjc::field::toString(quality.status) << '\n'
           << "method=" << quality.method << '\n'
           << "model_version=" << quality.modelVersion << '\n'
           << "source=" << quality.sourceUri << '\n';
    for (const auto& flag : quality.flags) output << "flag=" << flag << '\n';
    for (const auto& issue : quality.issues)
        output << "issue=" << issue.fieldPath << ": " << issue.message << '\n';
    output << "\nHJC 内部使用 V/m；CSV 为便于与原目标模型对照，统一输出 μV/m。\n"
           << "以下为原始配置；时间采用 TAI 纳秒基准，坐标采用 ENU。\n" << config.sourceText;
    if (!output) throw std::runtime_error("写入电场环境说明文件失败");
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

/** 显示目标、背景和合成电场的峰值及输出路径。 */
void printCombinedElectricSummary(
    const std::string& outputPath, const hjc::field::ElectricSimulationOutput& result)
{
    constexpr double voltsToMicrovolts = 1.0e6;
    std::size_t strongestIndex = 0U;
    double strongestMagnitude = -1.0;
    for (std::size_t index = 0; index < result.timeSeconds.size(); ++index)
    {
        const double value = std::hypot(result.totalField.x[index],
            result.totalField.y[index], result.totalField.z[index]);
        if (value > strongestMagnitude)
        {
            strongestMagnitude = value;
            strongestIndex = index;
        }
    }
    std::cout << "HJC 目标电场与环境背景合成完成\n"
              << "合成电场采样点数：" << result.timeSeconds.size() << '\n'
              << "合成场峰值：" << strongestMagnitude * voltsToMicrovolts
              << " μV/m，发生时刻：" << result.timeSeconds[strongestIndex] << " s\n"
              << "合成电场结果文件：" << outputPath << '\n';
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
    std::cout << "有效多偶极子节点数：" << samples.front().dipoleArrayNodeCount << '\n';
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
    std::cout << "有效多偶极子节点数：" << samples.front().dipoleArrayNodeCount << '\n';
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
    const std::vector<PressureFieldSimulation::Sample>& samples)
{
    const auto strongest = std::max_element(
        samples.begin(),
        samples.end(),
        [](const auto& lhs, const auto& rhs)
        {
            // 以动态压力绝对值衡量水压异常信号强弱。
            return std::abs(lhs.totalDynamicPressure) <
                std::abs(rhs.totalDynamicPressure);
        });

    std::cout << "目标水压与 HJC 环境背景合成完成\n";
    std::cout << "水压场时序采样点数：" << samples.size() << '\n';
    std::cout << "水动力工况："
              << pressureFieldRegimeName(samples.front().target.regime) << '\n';
    std::cout << "船长弗劳德数：" << samples.front().target.lengthFroudeNumber
              << "，水深弗劳德数：" << samples.front().target.depthFroudeNumber
              << '\n';
    std::cout << "合成动态压力绝对值峰值对应数值：" << strongest->totalDynamicPressure
              << " Pa，发生时刻：" << strongest->target.time << " s\n";
    std::cout << "水压场时序结果文件：" << outputPath << '\n';
}
} // 匿名命名空间

/**
 * @brief 程序入口，生成舰船电场、磁场和水压场仿真 CSV。
 * @param argc 命令行参数总数。
 * @param argv 六个可选输出路径，以及三类环境配置文件路径。
 * @return 成功时返回 EXIT_SUCCESS，参数错误或仿真异常时返回 EXIT_FAILURE。
 */
int main(int argc, char* argv[])
{
    configureConsoleEncoding();

    try
    {
        // 保持原有位置参数顺序，并追加磁场、电场合成结果及三份环境配置。
        std::vector<std::string> paths;
        const auto resourceRoot = std::filesystem::absolute(argv[0]).parent_path();
        auto environmentPath = resourceRoot / "pressure_environment_demo.ini";
        auto magneticEnvironmentPath = resourceRoot / "magnetic_environment_demo.ini";
        auto electricEnvironmentPath = resourceRoot / "electric_environment_demo.ini";
        bool hasEnvironmentPath = false;
        bool hasMagneticEnvironmentPath = false;
        bool hasElectricEnvironmentPath = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--help")
            {
                std::cout << "用法：seamine_simulator [目标电场CSV] [磁场分布CSV] [磁场时序CSV] [水压CSV] "
                             "[合成磁场CSV] [合成电场CSV] [--pressure-environment 水压配置.ini] "
                             "[--magnetic-environment 磁场配置.ini] [--electric-environment 电场配置.ini]\n"
                             "默认使用程序旁明确标记为合成场景的演示配置。\n";
                return EXIT_SUCCESS;
            }
            if (argument == "--pressure-environment")
            {
                if (hasEnvironmentPath || index + 1 >= argc)
                    throw std::invalid_argument("--pressure-environment 必须且只能指定一个配置路径");
                environmentPath = std::filesystem::u8path(argv[++index]);
                hasEnvironmentPath = true;
            }
            else if (argument == "--magnetic-environment")
            {
                if (hasMagneticEnvironmentPath || index + 1 >= argc)
                    throw std::invalid_argument("--magnetic-environment 必须且只能指定一个配置路径");
                magneticEnvironmentPath = std::filesystem::u8path(argv[++index]);
                hasMagneticEnvironmentPath = true;
            }
            else if (argument == "--electric-environment")
            {
                if (hasElectricEnvironmentPath || index + 1 >= argc)
                    throw std::invalid_argument("--electric-environment 必须且只能指定一个配置路径");
                electricEnvironmentPath = std::filesystem::u8path(argv[++index]);
                hasElectricEnvironmentPath = true;
            }
            else if (argument.compare(0, 2, "--") == 0)
                throw std::invalid_argument("未知命令行参数：" + argument);
            else paths.push_back(argument);
        }
        if (paths.size() > 6) throw std::invalid_argument("最多允许指定六个输出文件路径，请使用 --help 查看用法");
        const std::string outputPath = paths.size() >= 1 ? paths[0] : "electric_field_simulation.csv";
        const std::string magneticOutputPath = paths.size() >= 2 ? paths[1] : "magnetic_field_distribution.csv";
        const std::string magneticTimeOutputPath = paths.size() >= 3 ? paths[2] : "magnetic_field_time_series.csv";
        const std::string pressureTimeOutputPath = paths.size() >= 4 ? paths[3] : "pressure_field_time_series.csv";
        const std::string combinedMagneticOutputPath = paths.size() >= 5 ? paths[4] : "magnetic_field_combined_time_series.csv";
        const std::string combinedElectricOutputPath = paths.size() >= 6 ? paths[5] : "electric_field_combined_time_series.csv";
        const auto pressureEnvironment = loadPressureEnvironmentConfig(environmentPath);
        const auto magneticEnvironment = loadMagneticEnvironmentConfig(magneticEnvironmentPath, resourceRoot);
        const auto electricEnvironment = loadElectricEnvironmentConfig(electricEnvironmentPath);

        // 固定传感器位于水下三十米，目标从其侧前方匀速通过。
        const TargetElectricFieldModel::Vector3 sensorPosition{
            0.0,
            0.0,
            -30.0};
        constexpr double startTimeSeconds = 0.0;
        constexpr double durationSeconds = 75.0;
        constexpr double sampleRateHz = 20.0;

        // 电导率由同一环境快照提供，使独立目标曲线和 HJC 合成链采用同一介质参数。
        auto electricTarget = createDemoTarget();
        electricTarget.conductivity = *hjc::field::sampleField(
            electricEnvironment.environment.conductivitySM,
            {sensorPosition.x, sensorPosition.y, sensorPosition.z});
        const TargetElectricFieldModel model(electricTarget);

        const auto signals = model.simulate(
            sensorPosition,
            startTimeSeconds,
            durationSeconds,
            sampleRateHz);
        writeCsv(outputPath, signals);
        printSummary(outputPath, signals);

        // HJC 在同一时间轴上计算目标静态/轴频电场、环境背景和逐分量总场。
        ElectricFieldSimulation::Request electricRequest;
        electricRequest.target = electricTarget;
        electricRequest.targetSourceId = 1;
        electricRequest.targetLineageId = "main-electric-target";
        electricRequest.environment = electricEnvironment.environment;
        electricRequest.sampling = {{sensorPosition.x, sensorPosition.y, sensorPosition.z},
            startTimeSeconds, durationSeconds, sampleRateHz};
        const auto combinedElectric = ElectricFieldSimulation::simulate(electricRequest);
        writeCombinedElectricTimeSeriesCsv(combinedElectricOutputPath, combinedElectric);
        writeElectricEnvironmentMetadata(combinedElectricOutputPath,
            electricEnvironment, combinedElectric.quality);
        printCombinedElectricSummary(combinedElectricOutputPath, combinedElectric);
        std::cout << "电场环境配置：" << electricEnvironmentPath.u8string() << '\n';

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

        // 同一目标另由 HJC 求目标异常、传感器环境背景和逐分量合成；独立结果保持不变。
        MagneticFieldSimulation::Request magneticRequest;
        magneticRequest.target = createMovingMagneticDemoTarget();
        magneticRequest.environment = magneticEnvironment.environment;
        magneticRequest.sampling = {{magneticSensorPosition.x, magneticSensorPosition.y,
            magneticSensorPosition.z}, startTimeSeconds, durationSeconds, sampleRateHz};
        const auto combinedMagnetic = MagneticFieldSimulation::simulate(magneticRequest);
        writeCombinedMagneticTimeSeriesCsv(combinedMagneticOutputPath, combinedMagnetic);
        writeMagneticEnvironmentMetadata(combinedMagneticOutputPath,
            magneticEnvironment, combinedMagnetic.quality);
        std::cout << "HJC 磁场合成完成：" << combinedMagneticOutputPath << '\n'
                  << "磁场环境配置：" << magneticEnvironmentPath.u8string() << '\n'
                  << "磁场质量状态：" << hjc::field::toString(combinedMagnetic.quality.status) << '\n';

        // 固定水压传感器位于海床上方，生成目标匀速通过时的连续水压异常。
        PressureFieldSimulation::Request pressureRequest;
        pressureRequest.target = createPressureDemoTarget();
        pressureRequest.targetSourceId = 1;
        pressureRequest.targetLineageId = "main-pressure-target";
        pressureRequest.environment = pressureEnvironment.environment;
        pressureRequest.sampling = {{0.0, 0.0, -55.0}, startTimeSeconds, durationSeconds, sampleRateHz};
        pressureRequest.imageLayerCount = pressureEnvironment.imageLayerCount;
        const auto pressureOutput = PressureFieldSimulation::simulate(pressureRequest);
        writePressureTimeSeriesCsv(
            pressureTimeOutputPath, pressureOutput);
        writePressureEnvironmentMetadata(pressureTimeOutputPath, pressureEnvironment, pressureOutput.quality);
        std::cout << "水压环境配置：" << environmentPath.u8string() << '\n'
                  << "环境场景：" << pressureEnvironment.environment.scenarioId << '\n'
                  << "环境数据说明：" << pressureEnvironment.environment.fieldProvenance.at("pressure_environment").dataset << '\n'
                  << "环境来源：" << pressureEnvironment.environment.quality.sourceUri << '\n';
        printPressureTimeSeriesSummary(
            pressureTimeOutputPath, pressureOutput.samples);
    }
    catch (const std::exception& exception)
    {
        std::cerr << "仿真失败：" << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

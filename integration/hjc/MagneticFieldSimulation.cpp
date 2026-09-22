#include "MagneticFieldSimulation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace
{
/** 汇总 HJC 的结构化校验原因，保留字段路径供调用方定位。 */
std::string describeIssues(const std::vector<hjc::field::ValidationIssue>& issues)
{
    std::string message;
    for (const auto& issue : issues)
    {
        message += "; " + issue.fieldPath + ": " + issue.message;
    }
    return message;
}

/** 逐个检查三轴数组长度，防止部分结果被当成完整时序。 */
bool hasCount(const hjc::field::MagneticSeriesNt& series, std::size_t count)
{
    return series.x.size() == count && series.y.size() == count && series.z.size() == count;
}

/** 在浮点换算误差范围内检查目标与环境的逐分量叠加。 */
bool matchesSum(double signal, double environment, double total)
{
    const double scale = std::max({1.0, std::abs(signal), std::abs(environment), std::abs(total)});
    return std::isfinite(signal) && std::isfinite(environment) && std::isfinite(total) &&
        std::abs(signal + environment - total) <= 1.0e-10 * scale;
}
} // 匿名命名空间

hjc::field::MagneticSimulationOutput MagneticFieldSimulation::simulate(const Request& request)
{
    using namespace hjc::field;
    // 环境快照必须覆盖整个请求；HJC 当前未完整强制这项上层契约。
    if (!std::isfinite(request.environment.validDurationS) ||
        !std::isfinite(request.sampling.startTimeSeconds) ||
        !std::isfinite(request.sampling.durationSeconds) ||
        request.sampling.startTimeSeconds < 0.0 ||
        request.sampling.durationSeconds < 0.0 ||
        request.sampling.startTimeSeconds > request.environment.validDurationS ||
        request.sampling.durationSeconds >
            request.environment.validDurationS - request.sampling.startTimeSeconds)
    {
        throw std::invalid_argument("磁场采样时间超出环境快照有效期");
    }

    MagneticSimulationInput input;
    const auto& target = request.target;
    input.target.type = target.type == TargetMagneticFieldModel::TargetType::SurfaceShip
        ? TargetType::SurfaceShip : TargetType::Submarine;
    input.target.length = target.length;
    input.target.width = target.width;
    input.target.height = target.height;
    input.target.center = {target.center.x, target.center.y, target.center.z};
    input.target.velocity = target.velocity;
    input.target.headingDegrees = target.headingDegrees;
    input.target.pitchDegrees = target.pitchDegrees;
    input.target.rollDegrees = target.rollDegrees;
    // 旧地磁参数仅供 HJC 兼容性告警；实际感应磁矩由目标处 WMM/EMAG2 场决定。
    input.target.geomagneticFieldNt = {target.geomagneticFieldNt.x,
        target.geomagneticFieldNt.y, target.geomagneticFieldNt.z};
    input.target.relativePermeability = target.relativePermeability;
    input.target.magneticMaterialRatio = target.magneticMaterialRatio;
    input.target.remanentMagnetizationAm = {target.remanentMagnetizationAm.x,
        target.remanentMagnetizationAm.y, target.remanentMagnetizationAm.z};
    input.target.minimumDistance = target.minimumDistance;
    input.target.dipoleArrayLongitudinalCount = target.dipoleArrayLongitudinalCount;
    input.target.dipoleArrayTransverseCount = target.dipoleArrayTransverseCount;
    input.target.dipoleArrayVerticalCount = target.dipoleArrayVerticalCount;
    input.target.localCorrectionStrength = target.localCorrectionStrength;
    input.environment = request.environment;
    input.sampling = request.sampling;
    input.keepComponents = true;
    input.mode = MagneticSolverMode::EllipsoidDipoleArray;
    // 业务地磁唯一来源是环境模型，不把旧演示值差异误报成求解质量问题。
    input.checkLegacyGeomagneticField = false;

    // 先验证边界，再求同一时刻的目标场、背景场及其矢量和。
    const PropagationEnvironmentModule module;
    const auto validation = module.validateMagnetic(input);
    if (!validation.accepted)
    {
        throw std::invalid_argument("HJC 磁场输入被拒绝" + describeIssues(validation.issues));
    }
    auto output = module.simulateMagnetic(input);
    if (output.quality.status != QualityStatus::Valid &&
        output.quality.status != QualityStatus::PartiallyValid)
    {
        throw std::runtime_error("HJC 磁场计算失败：" + toString(output.quality.status) +
            describeIssues(output.quality.issues));
    }

    const auto expectedAxis = makeTimeAxis(request.sampling, request.environment.validTimeTaiNs);
    const std::size_t count = output.timeSeconds.size();
    if (!expectedAxis || count == 0 || count != expectedAxis->count ||
        !hasCount(output.signalOnly, count) ||
        !hasCount(output.environmentOnly, count) || !hasCount(output.totalField, count) ||
        !output.components)
    {
        throw std::runtime_error("HJC 磁场时序或背景分量缺失");
    }
    const auto& components = *output.components;
    if (!hasCount(components.targetMacro, count) ||
        !hasCount(components.targetLocalCorrection, count) ||
        !hasCount(components.mainField, count) ||
        !hasCount(components.crustalAnomaly, count) ||
        components.crustalAnomalyScalar.size() != count ||
        !hasCount(components.fluctuation, count) ||
        !hasCount(components.localAnomaly, count) ||
        !hasCount(components.motional, count))
    {
        throw std::runtime_error("HJC 磁场背景分量长度不一致");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        // 同时核对时间轴与三个方向；场强模值应在下游由矢量计算。
        const double expectedTime = request.sampling.startTimeSeconds +
            static_cast<double>(index) / request.sampling.sampleRateHz;
        if (!std::isfinite(output.timeSeconds[index]) ||
            std::abs(output.timeSeconds[index] - expectedTime) >
                1.0e-10 * std::max(1.0, std::abs(expectedTime)) ||
            !matchesSum(output.signalOnly.x[index], output.environmentOnly.x[index], output.totalField.x[index]) ||
            !matchesSum(output.signalOnly.y[index], output.environmentOnly.y[index], output.totalField.y[index]) ||
            !matchesSum(output.signalOnly.z[index], output.environmentOnly.z[index], output.totalField.z[index]))
        {
            throw std::runtime_error("HJC 磁场时间轴或目标与背景矢量合成不一致");
        }
    }
    return output;
}

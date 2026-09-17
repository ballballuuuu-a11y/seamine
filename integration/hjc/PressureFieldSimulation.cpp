#include "PressureFieldSimulation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
/** 将 HJC 的结构化原因保留在异常信息中，便于主程序报告失败。 */
std::string describeQuality(const hjc::field::QualityInfo& quality)
{
    std::string message = hjc::field::toString(quality.status);
    for (const auto& issue : quality.issues)
    {
        message += "; " + issue.fieldPath + ": " + issue.message;
    }
    return message;
}
} // 匿名命名空间

PressureFieldSimulation::Output PressureFieldSimulation::simulate(const Request& request)
{
    using namespace hjc::field;
    // HJC 的潮汐和海浪核目前统一使用标准重力，禁止混用不同重力常数。
    if (!std::isfinite(request.target.gravityAcceleration) ||
        std::abs(request.target.gravityAcceleration - 9.80665) > 1.0e-12)
    {
        throw std::invalid_argument("HJC 水压背景目前要求 gravityAcceleration=9.80665 m/s²");
    }
    for (const auto& source : request.environment.shippingPressureSources)
    {
        if ((request.targetSourceId != 0 && source.sourceId == request.targetSourceId) ||
            (!request.targetLineageId.empty() && source.lineageId == request.targetLineageId))
        {
            throw std::invalid_argument("主目标不得再次列入其他船舶背景压力源");
        }
    }

    // 背景独立求解，不构造虚假目标，也不调用 HJC 的目标水压公式。
    PressureBackgroundInput backgroundInput;
    backgroundInput.environment = request.environment;
    backgroundInput.sampling = request.sampling;
    backgroundInput.imageLayerCount = request.imageLayerCount;
    const PropagationEnvironmentModule module;
    const auto background = module.simulatePressureBackground(backgroundInput);
    if (background.quality.status != QualityStatus::Valid &&
        background.quality.status != QualityStatus::PartiallyValid)
    {
        throw std::runtime_error("HJC 水压背景计算失败：" + describeQuality(background.quality));
    }

    // 环境快照是水深和密度的唯一来源，旧目标参数中的兼容值在副本中同步。
    auto target = request.target;
    target.waterDepth = *sampleField(request.environment.waterDepthM,
        request.sampling.observationPositionM);
    target.waterDensity = *sampleField(request.environment.densityKgM3,
        request.sampling.observationPositionM);
    const auto& position = request.sampling.observationPositionM;
    const auto targetSamples = TargetPressureFieldModel(target).simulate(
        {position.x, position.y, position.z}, request.sampling.startTimeSeconds,
        request.sampling.durationSeconds, request.sampling.sampleRateHz);
    const std::size_t count = targetSamples.size();
    if (count == 0 || background.timeSeconds.size() != count ||
        background.environmentOnly.values.size() != count ||
        background.components.hydrostatic.values.size() != count ||
        background.components.tide.values.size() != count ||
        background.components.wave.values.size() != count ||
        background.components.shipping.values.size() != count)
    {
        throw std::runtime_error("目标水压与 HJC 背景的采样数组长度不一致");
    }

    Output output;
    output.quality = background.quality;
    output.quality.method = "TargetPressureFieldModel+" + background.quality.method;
    output.waterDepthM = target.waterDepth;
    output.waterDensityKgM3 = target.waterDensity;
    output.samples.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& targetSample = targetSamples[index];
        if (!std::isfinite(background.timeSeconds[index]) ||
            std::abs(targetSample.time - background.timeSeconds[index]) >
                1.0e-10 * std::max(1.0, std::abs(targetSample.time)))
        {
            throw std::runtime_error("目标水压与 HJC 背景的采样时刻不一致");
        }
        Sample sample;
        sample.target = targetSample;
        sample.signalOnly = targetSample.dynamicPressure;
        sample.environmentOnly = background.environmentOnly.values[index];
        sample.hydrostaticPressure = background.components.hydrostatic.values[index];
        sample.tidePressure = background.components.tide.values[index];
        sample.environmentWavePressure = background.components.wave.values[index];
        sample.shippingPressure = background.components.shipping.values[index];
        // 只叠加目标动态压力，避免将旧模型中的静水基线再次计入。
        sample.totalField = sample.signalOnly + sample.environmentOnly;
        // 直接合成动态项，避免从很大的静水基线相减损失有效数字。
        sample.totalDynamicPressure = sample.signalOnly + sample.tidePressure +
            sample.environmentWavePressure + sample.shippingPressure;
        if (!std::isfinite(sample.totalField) || !std::isfinite(sample.totalDynamicPressure) ||
            !std::isfinite(sample.hydrostaticPressure))
        {
            throw std::runtime_error("水压场合成结果包含非有限数值");
        }
        output.samples.push_back(sample);
    }
    return output;
}

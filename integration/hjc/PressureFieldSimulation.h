#pragma once

#include "TargetPressureFieldModel.h"
#include "hjc/field/module.h"

#include <cstdint>
#include <string>
#include <vector>

/** 将现有目标水压模型与 HJC 环境背景在固定观测点合成。 */
class PressureFieldSimulation
{
public:
    /** 所有坐标采用 ENU，所有压力采用 Pa，时间相对环境快照起点。 */
    struct Request
    {
        TargetPressureFieldModel::TargetParameter target{}; // 目标几何和运动参数。
        hjc::field::OceanEnvironmentSnapshot environment{}; // 目标与背景共用的环境。
        hjc::field::SamplingRequest sampling{}; // 固定观测点及采样时间轴。
        int imageLayerCount{8}; // 其他船舶的 HJC 有限水深镜像层数。
        std::uint64_t targetSourceId{0}; // 非零时用于排除重复列入背景的主目标。
        std::string targetLineageId; // 可选的主目标来源标识。
    };

    /** 保留原目标分量，并明确区分环境波浪和目标自身兴波。 */
    struct Sample
    {
        TargetPressureFieldModel::PressureFieldSample target{}; // 原目标模型的完整结果。
        double signalOnly{0.0}; // 目标动态压力，不含静水基线。
        double environmentOnly{0.0}; // 静水、潮汐、环境海浪及其他船舶压力。
        double totalField{0.0}; // 目标与背景合成的总表压。
        double totalDynamicPressure{0.0}; // 合成总场去除静水基线后的动态压力。
        double hydrostaticPressure{0.0}; // 只计入一次的静水表压。
        double tidePressure{0.0}; // 环境潮汐压力。
        double environmentWavePressure{0.0}; // 环境海浪压力，与目标兴波分开保存。
        double shippingPressure{0.0}; // 其他船舶造成的背景压力。
    };

    struct Output
    {
        std::vector<Sample> samples; // 按时间升序排列的合成结果。
        hjc::field::QualityInfo quality; // HJC 状态、方法、数据来源与限制。
        double waterDepthM{0.0}; // 本次观测点采用的环境水深，单位 m。
        double waterDensityKgM3{0.0}; // 本次观测点采用的密度，单位 kg/m³。
    };

    /** 环境或目标无效时抛出异常，不用零曲线代替失败；不会修改输入请求。 */
    static Output simulate(const Request& request);
};

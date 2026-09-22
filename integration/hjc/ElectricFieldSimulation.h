#pragma once

#include "TargetElectricFieldModel.h"
#include "hjc/field/module.h"

#include <cstdint>
#include <string>

/** 将现有目标电场参数转换为 HJC 输入，并合成环境电场。 */
class ElectricFieldSimulation
{
public:
    /** 坐标采用 ENU，时间相对环境快照起点，HJC 输出单位为 V/m。 */
    struct Request
    {
        TargetElectricFieldModel::TargetParameter target{}; // 现有目标几何、运动和电源参数。
        hjc::field::OceanEnvironmentSnapshot environment{}; // 电导率及环境电场来源。
        hjc::field::SamplingRequest sampling{}; // 固定传感器位置和采样时间轴。
        std::uint64_t targetSourceId{0}; // 非零时用于排除重复列入背景的主目标。
        std::string targetLineageId; // 可选的主目标来源标识。
    };

    /** 校验输入、分量长度及逐分量恒等式；失败时不返回零曲线。 */
    static hjc::field::ElectricSimulationOutput simulate(const Request& request);
};

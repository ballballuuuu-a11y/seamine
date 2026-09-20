#pragma once

#include "TargetMagneticFieldModel.h"
#include "hjc/field/module.h"

/** 将现有目标参数转换为 HJC 输入，并取得同一求解中的目标、环境与合成磁场。 */
class MagneticFieldSimulation
{
public:
    /** 所有位置采用局部 ENU，时间相对环境快照起点，磁场输出单位为 nT。 */
    struct Request
    {
        TargetMagneticFieldModel::TargetParameter target{}; // 现有目标的几何、运动和磁性参数。
        hjc::field::OceanEnvironmentSnapshot environment{}; // 地理位置、时间和地磁数据来源。
        hjc::field::SamplingRequest sampling{}; // 固定传感器位置与采样时间轴。
    };

    /** 校验环境有效期及 HJC 质量状态，不以零曲线掩盖失败。 */
    static hjc::field::MagneticSimulationOutput simulate(const Request& request);
};

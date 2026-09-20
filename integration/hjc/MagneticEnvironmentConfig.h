#pragma once

#include "hjc/field/types.h"

#include <filesystem>
#include <string>

/** 从显式配置构造磁场环境，不下载数据，也不在资源缺失时切换合成场。 */
struct MagneticEnvironmentConfig
{
    hjc::field::OceanEnvironmentSnapshot environment{}; // 地磁资源、地理原点及时间基准。
    std::string sourceText; // 原始配置文本，用于保存计算来源。
};

/** 相对资源路径以程序资源目录为根；拒绝缺失、重复或未知配置字段。 */
MagneticEnvironmentConfig loadMagneticEnvironmentConfig(
    const std::filesystem::path& configPath,
    const std::filesystem::path& resourceRoot);

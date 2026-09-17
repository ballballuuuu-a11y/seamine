#pragma once

#include "hjc/field/types.h"

#include <filesystem>
#include <string>

/** 从显式配置构造水压环境；不自动下载数据或回退到合成场景。 */
struct PressureEnvironmentConfig
{
    hjc::field::OceanEnvironmentSnapshot environment{}; // 均匀水深和密度，以及背景源。
    int imageLayerCount{8}; // 其他船舶使用的镜像层数。
    std::string sourceText; // 原始配置，随结果保存以便复现。
};

/** 读取 UTF-8 的 key=value 配置，拒绝缺失字段、重复字段和拼写错误。 */
PressureEnvironmentConfig loadPressureEnvironmentConfig(const std::filesystem::path& path);

#pragma once

#include "hjc/field/types.h"

#include <filesystem>
#include <string>

/** 从显式配置构造电场环境；不下载数据，也不把缺失背景默认为零。 */
struct ElectricEnvironmentConfig
{
    hjc::field::OceanEnvironmentSnapshot environment{}; // 电导率、运动电场、干扰偶极子和局部场。
    std::string sourceText; // 原始配置文本，随结果保存以便复现。
};

/** 读取 UTF-8 的 key=value 配置，拒绝缺失、重复和未知字段。 */
ElectricEnvironmentConfig loadElectricEnvironmentConfig(const std::filesystem::path& path);

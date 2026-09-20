#include "MagneticEnvironmentConfig.h"

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace
{
/** 去除配置字段两端的空白，同时兼容 Windows 换行。 */
std::string trim(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

/** 必填字段读取后移除，保证最后可检测所有未知键。 */
std::string take(std::map<std::string, std::string>& fields, const std::string& key)
{
    const auto it = fields.find(key);
    if (it == fields.end() || it->second.empty())
        throw std::invalid_argument("磁场环境配置缺少字段：" + key);
    const std::string value = it->second;
    fields.erase(it);
    return value;
}

/** 可选字段允许显式留空，以便关闭 EMAG2 或磁扰动。 */
std::string takeOptional(std::map<std::string, std::string>& fields, const std::string& key)
{
    const auto it = fields.find(key);
    if (it == fields.end()) return {};
    const std::string value = it->second;
    fields.erase(it);
    return value;
}

/** 数值字段必须完全转换且保持有限，不能忽略单位或尾随文本。 */
double takeNumber(std::map<std::string, std::string>& fields, const std::string& key)
{
    const std::string value = take(fields, key);
    std::size_t parsed = 0;
    const double number = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(number))
        throw std::invalid_argument("磁场环境数值字段无效：" + key);
    return number;
}

/** TAI 纳秒必须是完整的十进制整数。 */
std::int64_t takeTaiNs(std::map<std::string, std::string>& fields, const std::string& key)
{
    const std::string value = take(fields, key);
    std::size_t parsed = 0;
    const auto number = std::stoll(value, &parsed);
    if (parsed != value.size()) throw std::invalid_argument("TAI 纳秒字段无效：" + key);
    return static_cast<std::int64_t>(number);
}

/** 随机种子仅控制可重复磁扰动，不使用进程全局随机状态。 */
std::uint64_t takeSeed(std::map<std::string, std::string>& fields, const std::string& key)
{
    const std::string value = take(fields, key);
    std::size_t parsed = 0;
    const auto number = std::stoull(value, &parsed);
    if (parsed != value.size() || value.front() == '-')
        throw std::invalid_argument("磁扰动种子无效：" + key);
    return static_cast<std::uint64_t>(number);
}

/** 在部署资源根目录中定位相对文件，绝不回退到源码绝对路径。 */
std::filesystem::path resourcePath(const std::string& value, const std::filesystem::path& root)
{
    const auto path = std::filesystem::u8path(value);
    return std::filesystem::absolute(path.is_absolute() ? path : root / path).lexically_normal();
}
} // 匿名命名空间

MagneticEnvironmentConfig loadMagneticEnvironmentConfig(
    const std::filesystem::path& configPath, const std::filesystem::path& resourceRoot)
{
    std::ifstream file(configPath);
    if (!file) throw std::runtime_error("无法读取磁场环境配置：" + configPath.u8string());
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) throw std::runtime_error("读取磁场环境配置失败：" + configPath.u8string());

    // 解析唯一的 key=value，拒绝重复键及无法识别的配置。
    MagneticEnvironmentConfig config;
    config.sourceText = contents.str();
    std::map<std::string, std::string> fields;
    std::istringstream lines(config.sourceText);
    std::string line;
    while (std::getline(lines, line))
    {
        const std::string clean = trim(line);
        if (clean.empty() || clean.front() == '#') continue;
        const auto equals = clean.find('=');
        if (equals == std::string::npos || trim(clean.substr(0, equals)).empty())
            throw std::invalid_argument("磁场环境配置行缺少 key=value");
        const std::string key = trim(clean.substr(0, equals));
        if (!fields.emplace(key, trim(clean.substr(equals + 1))).second)
            throw std::invalid_argument("磁场环境配置重复字段：" + key);
    }

    auto& environment = config.environment;
    environment.scenarioId = take(fields, "scenario_id");
    environment.validTimeTaiNs = takeTaiNs(fields, "valid_time_tai_ns");
    environment.validDurationS = takeNumber(fields, "valid_duration_s");
    environment.origin.latitudeDeg = takeNumber(fields, "origin_latitude_deg");
    environment.origin.longitudeDeg = takeNumber(fields, "origin_longitude_deg");
    environment.origin.heightM = takeNumber(fields, "origin_height_m");
    environment.geomagnetic.decimalYear = takeNumber(fields, "decimal_year");
    environment.geomagnetic.randomSeed = takeSeed(fields, "magnetic_random_seed");
    environment.geomagnetic.wmmCoefficientPath = resourcePath(
        take(fields, "wmm_coefficient_path"), resourceRoot);
    const auto emag2Path = takeOptional(fields, "emag2_grid_path");
    if (!emag2Path.empty())
        environment.geomagnetic.emag2GridPath = resourcePath(emag2Path, resourceRoot);
    environment.geomagnetic.emag2ExpectedSha256 = takeOptional(fields, "emag2_expected_sha256");
    if (!environment.geomagnetic.emag2GridPath &&
        !environment.geomagnetic.emag2ExpectedSha256.empty())
        throw std::invalid_argument("未指定 EMAG2 格网时不能填写校验和");
    // 一组可选 PSD 样本用于演示确定性磁扰动；省略频率则不生成扰动。
    const auto psdFrequency = takeOptional(fields, "magnetic_psd_frequency_hz");
    if (!psdFrequency.empty())
    {
        std::size_t parsed = 0;
        const double frequency = std::stod(psdFrequency, &parsed);
        if (parsed != psdFrequency.size() || !std::isfinite(frequency) || frequency <= 0.0)
            throw std::invalid_argument("磁扰动 PSD 频率无效");
        const hjc::field::Vec3d density{
            takeNumber(fields, "magnetic_psd_east_t2_per_hz"),
            takeNumber(fields, "magnetic_psd_north_t2_per_hz"),
            takeNumber(fields, "magnetic_psd_up_t2_per_hz")};
        if (density.x < 0.0 || density.y < 0.0 || density.z < 0.0)
            throw std::invalid_argument("磁扰动 PSD 不能为负值");
        environment.geomagnetic.fluctuationPsd.push_back({frequency, density});
    }

    // 来源标签由配置显式给出，演示数据始终标为合成验证数据。
    const auto natureText = take(fields, "data_nature");
    hjc::field::DataProvenance provenance;
    bool recognized = false;
    for (const auto nature : {hjc::field::EnvironmentalDataNature::SyntheticValidationData,
        hjc::field::EnvironmentalDataNature::ModelCoefficient,
        hjc::field::EnvironmentalDataNature::CompiledGrid,
        hjc::field::EnvironmentalDataNature::Climatology,
        hjc::field::EnvironmentalDataNature::DerivedField})
    {
        if (natureText == hjc::field::toString(nature))
        {
            provenance.nature = nature;
            recognized = true;
            break;
        }
    }
    if (!recognized) throw std::invalid_argument("未知磁场环境数据性质：" + natureText);
    provenance.dataset = take(fields, "source_description");
    provenance.sourceUri = take(fields, "source_uri");
    provenance.transformation = "配置的 WMM 地磁主场及可选 EMAG2/磁扰动";
    environment.fieldProvenance["geomagnetic"] = provenance;
    environment.quality.sourceUri = provenance.sourceUri;
    environment.quality.flags = {natureText};

    if (!fields.empty()) throw std::invalid_argument("未知磁场环境字段：" + fields.begin()->first);
    if (environment.validDurationS <= 0.0 ||
        environment.origin.latitudeDeg < -90.0 || environment.origin.latitudeDeg > 90.0 ||
        environment.origin.longitudeDeg < -180.0 || environment.origin.longitudeDeg > 180.0)
        throw std::invalid_argument("磁场环境有效期或地理原点超出范围");
    return config;
}

#include "PressureEnvironmentConfig.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace
{
/** 去除行首行尾空白，保留值内部的中文和空格。 */
std::string trim(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? std::string{} :
        value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

/** 消费式配置读取器，读取后剩余的键视为拼写错误。 */
class Fields
{
public:
    explicit Fields(const std::string& text)
    {
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
        {
            // 接受常见编辑器保存的 UTF-8 BOM。
            if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                throw std::invalid_argument("环境配置行缺少等号：" + line);
            const auto key = trim(line.substr(0, separator));
            const auto value = trim(line.substr(separator + 1));
            if (key.empty() || value.empty() || !m_values.emplace(key, value).second)
                throw std::invalid_argument("环境配置键为空、值为空或重复：" + key);
        }
    }

    /** 读取必需字段并删除，缺失时报告字段名称。 */
    std::string take(const std::string& key)
    {
        const auto item = m_values.find(key);
        if (item == m_values.end()) throw std::invalid_argument("缺少环境配置字段：" + key);
        const std::string value = item->second;
        m_values.erase(item);
        return value;
    }

    /** 完整解析有限浮点值，禁止悄悄接受带错误单位的数字。 */
    double number(const std::string& key)
    {
        const auto value = take(key);
        std::size_t end = 0;
        try
        {
            const double result = std::stod(value, &end);
            if (end == value.size() && std::isfinite(result)) return result;
        }
        catch (const std::exception&) {}
        throw std::invalid_argument("环境配置必须是有限数值：" + key);
    }

    /** 纳秒时间和随机种子按整数解析，避免浮点转换丢失精度。 */
    std::int64_t integer(const std::string& key, std::int64_t maximum)
    {
        const auto value = take(key);
        std::size_t end = 0;
        try
        {
            const auto result = std::stoll(value, &end);
            if (end == value.size() && result >= 0 && result <= maximum) return result;
        }
        catch (const std::exception&) {}
        throw std::invalid_argument("环境配置必须是范围内的非负整数：" + key);
    }

    /** 防止未识别字段被忽略后意外使用另一组参数。 */
    void finish() const
    {
        if (!m_values.empty()) throw std::invalid_argument("未知环境配置字段：" + m_values.begin()->first);
    }

private:
    std::map<std::string, std::string> m_values; // 尚未消费的字段及原始值。
};
} // 匿名命名空间

PressureEnvironmentConfig loadPressureEnvironmentConfig(const std::filesystem::path& path)
{
    using namespace hjc::field;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("无法读取水压环境配置：" + path.u8string());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) throw std::runtime_error("读取水压环境配置失败：" + path.u8string());
    PressureEnvironmentConfig config;
    config.sourceText = buffer.str();
    Fields fields(config.sourceText);
    auto& environment = config.environment;
    environment.scenarioId = fields.take("scenario_id");
    environment.validTimeTaiNs = fields.integer("valid_time_tai_ns", std::numeric_limits<std::int64_t>::max());
    environment.validDurationS = fields.number("valid_duration_s");
    environment.bathymetryVerticalDatum = fields.take("vertical_datum");
    environment.waterDepthM = {UniformGeometry{}, {fields.number("water_depth_m")}, "m"};
    environment.densityKgM3 = {UniformGeometry{}, {fields.number("density_kg_m3")}, "kg/m3"};
    // 数据性质只记录调用方声明的来源，不据此把演示数据当作实测海况。
    const auto natureText = fields.take("data_nature");
    DataProvenance provenance;
    bool recognized = false;
    for (const auto nature : {EnvironmentalDataNature::SyntheticValidationData,
         EnvironmentalDataNature::Climatology, EnvironmentalDataNature::CompiledGrid,
         EnvironmentalDataNature::ModelCoefficient, EnvironmentalDataNature::DerivedField})
    {
        if (natureText == toString(nature))
        {
            provenance.nature = nature;
            recognized = true;
        }
    }
    if (!recognized) throw std::invalid_argument("未知 data_nature：" + natureText);
    provenance.dataset = fields.take("source_description");
    provenance.sourceUri = fields.take("source_uri");
    provenance.transformation = "显式场景参数，均匀水深/密度；海浪采用 JONSWAP 合成";
    environment.quality.sourceUri = provenance.sourceUri;
    environment.quality.flags = {natureText};
    environment.fieldProvenance["pressure_environment"] = provenance;

    auto& waves = environment.waves;
    constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;
    waves.projectSeaState = static_cast<int>(fields.integer("sea_state", 10));
    waves.significantHeightM = fields.number("wave_hs_m");
    waves.peakPeriodS = fields.number("wave_tp_s");
    waves.meanDirectionRad = fields.number("wave_direction_deg") * radiansPerDegree;
    waves.jonswapGamma = fields.number("wave_gamma");
    waves.directionSpreadExponent = fields.number("wave_spread");
    waves.randomSeed = static_cast<std::uint64_t>(fields.integer("wave_seed", std::numeric_limits<std::int64_t>::max()));
    waves.frequencyComponentCount = static_cast<std::size_t>(fields.integer("wave_frequency_count", 1024));
    waves.directionComponentCount = static_cast<std::size_t>(fields.integer("wave_direction_count", 1024));
    config.imageLayerCount = static_cast<int>(fields.integer("image_layer_count", 64));

    // 每个潮汐分量都明确记录相位参考时刻，不能把 UTC 纳秒当成 TAI。
    const auto tideCount = fields.integer("tide_count", 128);
    for (std::int64_t index = 0; index < tideCount; ++index)
    {
        const std::string prefix = "tide." + std::to_string(index) + ".";
        TideConstituent tide;
        tide.name = fields.take(prefix + "name");
        tide.amplitudeM = fields.number(prefix + "amplitude_m");
        tide.periodS = fields.number(prefix + "period_s");
        tide.phaseRad = fields.number(prefix + "phase_deg") * radiansPerDegree;
        tide.referenceTaiNs = fields.integer(prefix + "reference_tai_ns", std::numeric_limits<std::int64_t>::max());
        tide.verticalDatum = environment.bathymetryVerticalDatum;
        tide.provenance = provenance;
        environment.tides.push_back(tide);
    }

    // 此配置格式中的其他船舶为水面舰艇；背景来源不得包含主目标。
    const auto shippingCount = fields.integer("shipping_count", 128);
    for (std::int64_t index = 0; index < shippingCount; ++index)
    {
        const std::string prefix = "shipping." + std::to_string(index) + ".";
        PressureTargetParameter source;
        source.sourceId = static_cast<std::uint64_t>(fields.integer(prefix + "source_id", std::numeric_limits<std::int64_t>::max()));
        source.lineageId = fields.take(prefix + "lineage_id");
        source.length = fields.number(prefix + "length_m");
        source.width = fields.number(prefix + "width_m");
        source.draft = fields.number(prefix + "draft_m");
        source.initialPosition = {fields.number(prefix + "x_m"), fields.number(prefix + "y_m"), fields.number(prefix + "z_m")};
        source.velocity = fields.number(prefix + "speed_m_s");
        source.headingDegrees = fields.number(prefix + "heading_deg");
        source.blockCoefficient = fields.number(prefix + "block_coefficient");
        source.minimumDistance = fields.number(prefix + "minimum_distance_m");
        source.waterDepth = environment.waterDepthM.values.front();
        source.waterDensity = environment.densityKgM3.values.front();
        environment.shippingPressureSources.push_back(source);
    }
    fields.finish();
    return config;
}

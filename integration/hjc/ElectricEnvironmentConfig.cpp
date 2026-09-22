#include "ElectricEnvironmentConfig.h"

#include <cmath>
#include <complex>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
constexpr double kMicrovoltsToVolts = 1.0e-6;
constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;

/** 去除字段两端空白，同时保留值内部空格和中文。 */
std::string trim(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? std::string{} :
        value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

/** 消费式配置读取器；未消费字段会作为拼写错误报告。 */
class Fields
{
public:
    explicit Fields(const std::string& text)
    {
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line))
        {
            // 接受常见编辑器写入的 UTF-8 BOM。
            if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            line = trim(line);
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos)
                throw std::invalid_argument("电场环境配置行缺少等号：" + line);
            const auto key = trim(line.substr(0, separator));
            const auto value = trim(line.substr(separator + 1));
            if (key.empty() || value.empty() || !values_.emplace(key, value).second)
                throw std::invalid_argument("电场环境配置键为空、值为空或重复：" + key);
        }
    }

    std::string take(const std::string& key)
    {
        const auto item = values_.find(key);
        if (item == values_.end()) throw std::invalid_argument("缺少电场环境配置字段：" + key);
        const auto value = item->second;
        values_.erase(item);
        return value;
    }

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
        throw std::invalid_argument("电场环境配置必须是有限数值：" + key);
    }

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
        throw std::invalid_argument("电场环境配置必须是范围内的非负整数：" + key);
    }

    bool boolean(const std::string& key)
    {
        const auto value = take(key);
        if (value == "true") return true;
        if (value == "false") return false;
        throw std::invalid_argument("电场环境布尔字段必须为 true 或 false：" + key);
    }

    void finish() const
    {
        if (!values_.empty())
            throw std::invalid_argument("未知电场环境配置字段：" + values_.begin()->first);
    }

private:
    std::map<std::string, std::string> values_; // 尚未被读取的字段。
};

/** 把配置中的统一相位和三个有符号幅值转换为复电流偶极矩。 */
hjc::field::ComplexVec3 complexMoment(Fields& fields, const std::string& prefix)
{
    const double phase = fields.number(prefix + "phase_deg") * kRadiansPerDegree;
    const std::complex<double> phasor{std::cos(phase), std::sin(phase)};
    return {
        fields.number(prefix + "moment_east_a_m") * phasor,
        fields.number(prefix + "moment_north_a_m") * phasor,
        fields.number(prefix + "moment_up_a_m") * phasor};
}
} // 匿名命名空间

ElectricEnvironmentConfig loadElectricEnvironmentConfig(const std::filesystem::path& path)
{
    using namespace hjc::field;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("无法读取电场环境配置：" + path.u8string());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) throw std::runtime_error("读取电场环境配置失败：" + path.u8string());

    ElectricEnvironmentConfig config;
    config.sourceText = buffer.str();
    Fields fields(config.sourceText);
    auto& environment = config.environment;
    environment.scenarioId = fields.take("scenario_id");
    environment.validTimeTaiNs = fields.integer(
        "valid_time_tai_ns", std::numeric_limits<std::int64_t>::max());
    environment.validDurationS = fields.number("valid_duration_s");
    const double conductivity = fields.number("conductivity_s_m");
    if (!(environment.validDurationS >= 0.0) || !(conductivity > 0.0))
        throw std::invalid_argument("电场环境有效期必须非负且电导率必须为正数");
    environment.conductivitySM = {UniformGeometry{}, {conductivity}, "S/m"};

    // 保留调用方声明的数据性质和来源，不把演示参数误标为实测数据。
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
    provenance.transformation = "显式 ENU 电场配置；微伏每米输入统一换算为伏每米";
    environment.quality.sourceUri = provenance.sourceUri;
    environment.quality.flags = {natureText};
    environment.fieldProvenance["electric_environment"] = provenance;

    auto& background = environment.electric;
    const auto motionalMode = fields.take("motional_mode");
    if (motionalMode == "none")
    {
        background.motionalMode = MotionalElectricMode::None;
    }
    else if (motionalMode == "constant")
    {
        background.motionalMode = MotionalElectricMode::Prescribed;
        // 单值在主工程适配层中扩展到整个采样时间轴。
        background.prescribedMotionalFieldVm.push_back({
            fields.number("motional_east_uV_m") * kMicrovoltsToVolts,
            fields.number("motional_north_uV_m") * kMicrovoltsToVolts,
            fields.number("motional_up_uV_m") * kMicrovoltsToVolts});
    }
    else
    {
        throw std::invalid_argument("motional_mode 只支持 none 或 constant");
    }

    if (fields.boolean("local_field_enabled"))
    {
        RealVectorField local;
        local.geometry = UniformGeometry{};
        local.unit = "V/m";
        local.values.push_back({
            fields.number("local_east_uV_m") * kMicrovoltsToVolts,
            fields.number("local_north_uV_m") * kMicrovoltsToVolts,
            fields.number("local_up_uV_m") * kMicrovoltsToVolts});
        background.localElectricFieldVm = std::move(local);
        background.localFieldLineageIds.push_back(fields.take("local_lineage_id"));
    }

    const auto interferenceCount = fields.integer("interference_count", 128);
    for (std::int64_t index = 0; index < interferenceCount; ++index)
    {
        const std::string prefix = "interference." + std::to_string(index) + ".";
        ElectricDipole source;
        source.sourceId = static_cast<std::uint64_t>(fields.integer(
            prefix + "source_id", std::numeric_limits<std::int64_t>::max()));
        source.lineageId = fields.take(prefix + "lineage_id");
        source.positionM = {fields.number(prefix + "x_m"), fields.number(prefix + "y_m"),
            fields.number(prefix + "z_m")};
        source.velocityMS = {fields.number(prefix + "vx_m_s"), fields.number(prefix + "vy_m_s"),
            fields.number(prefix + "vz_m_s")};
        source.momentAm = complexMoment(fields, prefix);
        source.frequencyHz = fields.number(prefix + "frequency_hz");
        background.interferenceDipoles.push_back(std::move(source));
    }
    fields.finish();
    return config;
}

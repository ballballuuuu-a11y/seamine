#include "integration/hjc/ElectricEnvironmentConfig.h"
#include "integration/hjc/ElectricFieldSimulation.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
/** 检查测试前提，失败时抛出带中文原因的异常。 */
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

/** 浮点断言同时拒绝非有限数值。 */
void near(double actual, double expected, double tolerance, const std::string& message)
{
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= tolerance, message);
}

/** 无效输入必须显式失败，不能退化成全零背景。 */
void rejects(const std::function<void()>& action, const std::string& message)
{
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

/** 创建包含三类环境分量的短时电场请求。 */
ElectricFieldSimulation::Request makeRequest()
{
    using namespace hjc::field;
    ElectricFieldSimulation::Request request;
    auto& target = request.target;
    target.type = TargetElectricFieldModel::TargetType::SurfaceShip;
    target.tonnage = 5000.0;
    target.length = 100.0;
    target.width = 15.0;
    target.draft = 5.0;
    target.velocity = 4.0;
    target.shaftSpeed = 120.0;
    target.initialPosition = {-100.0, 35.0, -5.0};
    target.staticDipoleMoment = 0.35;
    target.conductivity = 4.54;
    target.minimumDistance = 1.0;
    request.targetSourceId = 1U;
    request.targetLineageId = "main-electric-target";
    request.sampling = {{0.0, 0.0, -30.0}, 0.0, 1.0, 20.0};

    auto& environment = request.environment;
    environment.scenarioId = "electric-integration-test";
    environment.validTimeTaiNs = 1735689637000000000LL;
    environment.validDurationS = 5.0;
    environment.conductivitySM = {UniformGeometry{}, {4.54}, "S/m"};
    environment.electric.motionalMode = MotionalElectricMode::Prescribed;
    // 单值用于验证适配层会把恒定运动电场扩展到完整时序。
    environment.electric.prescribedMotionalFieldVm = {{1.0e-8, -2.0e-8, 3.0e-8}};
    environment.electric.localElectricFieldVm = RealVectorField{
        UniformGeometry{}, {{4.0e-8, 1.0e-8, -1.0e-8}}, "V/m"};
    environment.electric.localFieldLineageIds = {"local-grid"};

    ElectricDipole interference;
    interference.sourceId = 4001U;
    interference.lineageId = "other-ship-electric";
    interference.positionM = {-50.0, -80.0, -8.0};
    interference.velocityMS = {1.0, 0.0, 0.0};
    interference.momentAm = {{0.8, 0.0}, {0.2, 0.0}, {-0.1, 0.0}};
    interference.frequencyHz = 0.4;
    environment.electric.interferenceDipoles = {interference};
    return request;
}

/** 目标链保持原模型结果，同时环境三分量与总场逐点守恒。 */
void testCompositionAndTargetRegression()
{
    const auto request = makeRequest();
    const auto output = ElectricFieldSimulation::simulate(request);
    const auto& components = *output.components;
    const auto& point = request.sampling.observationPositionM;
    const auto target = TargetElectricFieldModel(request.target).simulate(
        {point.x, point.y, point.z}, request.sampling.startTimeSeconds,
        request.sampling.durationSeconds, request.sampling.sampleRateHz);
    require(output.timeSeconds.size() == 21U && target.size() == 21U, "电场采样数量错误");
    for (std::size_t index = 0; index < output.timeSeconds.size(); ++index)
    {
        constexpr double microvoltsToVolts = 1.0e-6;
        near(output.signalOnly.x[index], target[index].totalFieldVector.x * microvoltsToVolts,
            1.0e-18, "HJC 目标电场东向分量改变");
        near(output.signalOnly.y[index], target[index].totalFieldVector.y * microvoltsToVolts,
            1.0e-18, "HJC 目标电场北向分量改变");
        near(output.signalOnly.z[index], target[index].totalFieldVector.z * microvoltsToVolts,
            1.0e-18, "HJC 目标电场垂向分量改变");
        near(output.environmentOnly.x[index], components.motional.x[index] +
            components.shipping.x[index] + components.local.x[index], 1.0e-20,
            "环境电场东向分量合成错误");
        near(output.signalOnly.x[index] + output.environmentOnly.x[index],
            output.totalField.x[index], 1.0e-20, "目标与背景东向分量未守恒");
        near(output.signalOnly.y[index] + output.environmentOnly.y[index],
            output.totalField.y[index], 1.0e-20, "目标与背景北向分量未守恒");
        near(output.signalOnly.z[index] + output.environmentOnly.z[index],
            output.totalField.z[index], 1.0e-20, "目标与背景垂向分量未守恒");
    }
    require(std::abs(components.motional.x.front()) > 0.0, "运动电场没有进入背景");
    require(std::abs(components.shipping.x.front()) > 0.0, "干扰船舶电场没有进入背景");
    require(std::abs(components.local.x.front()) > 0.0, "局部电场没有进入背景");

    auto partialRequest = request;
    partialRequest.environment.quality.status = hjc::field::QualityStatus::PartiallyValid;
    require(ElectricFieldSimulation::simulate(partialRequest).quality.status ==
        hjc::field::QualityStatus::PartiallyValid, "环境受限状态没有传播到电场结果");
}

/** 主目标不得通过来源标识再次列入背景干扰源。 */
void testDuplicateTargetRejection()
{
    auto request = makeRequest();
    request.environment.electric.interferenceDipoles.front().sourceId = request.targetSourceId;
    rejects([&] { static_cast<void>(ElectricFieldSimulation::simulate(request)); },
        "重复主目标 sourceId 未被拒绝");
    request = makeRequest();
    request.environment.electric.interferenceDipoles.front().lineageId = request.targetLineageId;
    rejects([&] { static_cast<void>(ElectricFieldSimulation::simulate(request)); },
        "重复主目标 lineageId 未被拒绝");
}

/** 时间覆盖、电导率、单位和不支持的派生模式必须明确报错。 */
void testInvalidInputs()
{
    const auto baseline = makeRequest();
    const std::vector<std::function<void(ElectricFieldSimulation::Request&)>> changes{
        [](auto& request) { request.environment.validDurationS = 0.5; },
        [](auto& request) { request.environment.conductivitySM.values.clear(); },
        [](auto& request) { request.environment.conductivitySM.unit = "mS/cm"; },
        [](auto& request) { request.environment.electric.motionalMode = hjc::field::MotionalElectricMode::Derived; },
        [](auto& request) { request.environment.electric.localElectricFieldVm->unit = "uV/m"; },
        [](auto& request) { request.sampling.sampleRateHz = 10.0; },
        [](auto& request) { request.environment.quality.status = hjc::field::QualityStatus::MissingInput; },
        [](auto& request) { request.target.minimumDistance = std::numeric_limits<double>::quiet_NaN(); }
    };
    for (const auto& change : changes)
    {
        auto request = baseline;
        change(request);
        rejects([&] { static_cast<void>(ElectricFieldSimulation::simulate(request)); },
            "非法电场输入未被拒绝");
    }
}

/** 配置文件必须保持整数精度，并拒绝未知键和重复键。 */
void testConfig(const std::filesystem::path& source, const std::filesystem::path& temporary)
{
    const auto config = loadElectricEnvironmentConfig(source);
    require(config.environment.validTimeTaiNs == 1735689637000000000LL, "电场配置 TAI 纳秒精度损失");
    require(config.environment.electric.interferenceDipoles.size() == 1U, "电场干扰源数量错误");
    require(config.environment.electric.localElectricFieldVm.has_value(), "电场局部背景缺失");

    auto request = makeRequest();
    request.environment = config.environment;
    request.target.initialPosition = {-300.0, 40.0, -5.0};
    request.target.velocity = 8.0;
    request.target.conductivity = config.environment.conductivitySM.values.front();
    request.sampling = {{0.0, 0.0, -30.0}, 0.0, 75.0, 20.0};
    require(ElectricFieldSimulation::simulate(request).timeSeconds.size() == 1501U,
        "电场演示配置无法运行");

    for (const std::string& suffix : {std::string("\nunknown_key=1\n"),
         std::string("\nconductivity_s_m=5\n")})
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << config.sourceText << suffix;
        output.close();
        rejects([&] { static_cast<void>(loadElectricEnvironmentConfig(temporary)); },
            "电场配置未拒绝未知键或重复键");
    }
}
} // 匿名命名空间

/** 集成测试入口，参数依次为演示配置和临时配置路径。 */
int main(int argc, char* argv[])
{
    try
    {
        if (argc != 3) throw std::invalid_argument("测试需要电场演示配置和临时配置路径");
        testCompositionAndTargetRegression();
        testDuplicateTargetRejection();
        testInvalidInputs();
        testConfig(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
    }
    catch (const std::exception& exception)
    {
        std::cerr << "电场环境集成测试失败：" << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "电场环境集成测试全部通过\n";
    return EXIT_SUCCESS;
}

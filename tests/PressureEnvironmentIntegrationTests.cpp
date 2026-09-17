#include "integration/hjc/PressureFieldSimulation.h"
#include "integration/hjc/PressureEnvironmentConfig.h"
#include "hjc/field/scenarios.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
/** 检查测试前提或离散状态。 */
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

/** 浮点断言同时拒绝 NaN，防止非有限结果通过比较。 */
void near(double actual, double expected, double tolerance, const std::string& message)
{
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

/** 无效输入必须显式失败，不能悄悄生成零背景。 */
void rejects(const std::function<void()>& action, const std::string& message)
{
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

/** 创建仅供测试的低速目标和静水环境，未提供任何电磁或底质数据。 */
PressureFieldSimulation::Request calmRequest()
{
    using namespace hjc::field;
    PressureFieldSimulation::Request request;
    auto& target = request.target;
    target.length = 100.0;
    target.width = 15.0;
    target.draft = 5.0;
    target.waterDepth = 60.0;
    target.waterDensity = 1025.0;
    target.velocity = 3.0;
    target.initialPosition = {-180.0, 40.0, -2.5};
    target.blockCoefficient = 0.68;
    request.targetSourceId = 1;
    request.sampling = {{0.0, 0.0, -55.0}, 0.5, 5.0, 20.0};
    auto& environment = request.environment;
    environment.validTimeTaiNs = 1735689637000000000LL;
    environment.validDurationS = 75.0;
    environment.waterDepthM = {UniformGeometry{}, {60.0}, "m"};
    environment.densityKgM3 = {UniformGeometry{}, {1025.0}, "kg/m3"};
    environment.waves.peakPeriodS = 6.0;
    return request;
}

/** 静水背景合成应逐点恢复旧结果，验证静水只计入一次和目标模型未替换。 */
void testCalmAndRegimes()
{
    for (int mode = 0; mode < 3; ++mode)
    {
        auto request = calmRequest();
        if (mode == 1)
        {
            // 原模型独有的浅水过渡工况必须完整保留。
            request.environment.waterDepthM.values = {15.0};
            request.target.waterDepth = 15.0;
            request.sampling.observationPositionM.z = -10.0;
        }
        else if (mode == 2)
        {
            // 潜艇使用独立垂向高度，不能被 HJC 较简化的 draft 接口覆盖。
            request.target.type = TargetPressureFieldModel::TargetType::Submarine;
            request.target.initialPosition.z = -20.0;
            request.target.height = 8.0;
        }
        const auto result = PressureFieldSimulation::simulate(request);
        const auto& point = request.sampling.observationPositionM;
        const auto expected = TargetPressureFieldModel(request.target).simulate(
            {point.x, point.y, point.z}, request.sampling.startTimeSeconds,
            request.sampling.durationSeconds, request.sampling.sampleRateHz);
        require(result.samples.size() == 101 && expected.size() == result.samples.size(), "时序长度错误");
        for (std::size_t i = 0; i < result.samples.size(); ++i)
        {
            const auto& sample = result.samples[i];
            near(sample.signalOnly, expected[i].dynamicPressure, 0.0, "目标动态压力改变");
            near(sample.totalField, expected[i].totalGaugePressure, 1.0e-9, "静水基线被重复计入");
            near(sample.environmentOnly, expected[i].hydrostaticPressure, 0.0, "纯静水背景错误");
            require(sample.target.regime == expected[i].regime, "目标工况丢失");
        }
    }
}

/** 共享环境必须同时改变目标和背景，而不能只影响其中一条分支。 */
void testSharedEnvironment()
{
    auto request = calmRequest();
    request.environment.densityKgM3.values = {1030.0};
    request.environment.waterDepthM.values = {65.0};
    auto expectedTarget = request.target;
    expectedTarget.waterDensity = 1030.0;
    expectedTarget.waterDepth = 65.0;
    const auto expected = TargetPressureFieldModel(expectedTarget).calculate({0, 0, -55}, 0.5);
    const auto result = PressureFieldSimulation::simulate(request);
    near(result.samples.front().signalOnly, expected.dynamicPressure, 0.0, "目标没有使用环境密度和水深");
    near(result.samples.front().hydrostaticPressure, 1030.0 * 9.80665 * 55.0, 1.0e-9, "环境静水压力错误");
    near(request.target.waterDensity, 1025.0, 0.0, "调用修改了输入目标");
}

/** 独立背景接口必须复现 HJC 原接口的环境分量，构成跨接口回归基线。 */
void testHjcBackgroundRegression()
{
    using namespace hjc::field;
    const auto scenario = SyntheticScenarioFactory::makeFullBackgroundScenario();
    const PropagationEnvironmentModule module;
    const auto original = module.simulatePressure(scenario.pressure);
    const auto background = module.simulatePressureBackground(
        {scenario.pressure.environment, scenario.pressure.sampling, scenario.pressure.imageLayerCount});
    std::string reason = "独立背景被拒绝";
    for (const auto& issue : background.quality.issues) reason += "; " + issue.fieldPath + ": " + issue.message;
    require(background.quality.status == QualityStatus::Valid, reason);
    require(original.environmentOnly.values == background.environmentOnly.values, "HJC 原背景数值改变");
    require(original.components->wave.values == background.components.wave.values, "HJC 海浪分量改变");
    require(original.components->shipping.values == background.components.shipping.values, "HJC 航运分量改变");
}

/** 验证潮汐相位、非零开始时刻、海浪复现和三组场合成。 */
void testCompositionAndTime()
{
    using namespace hjc::field;
    auto request = calmRequest();
    request.environment.waves.projectSeaState = 4;
    request.environment.waves.significantHeightM = 1.0;
    request.environment.waves.randomSeed = 17;
    TideConstituent tide;
    tide.amplitudeM = 0.5;
    tide.periodS = 44714.0;
    tide.phaseRad = 0.3;
    tide.referenceTaiNs = request.environment.validTimeTaiNs - 1000000000LL;
    request.environment.tides = {tide};
    const auto first = PressureFieldSimulation::simulate(request);
    const auto repeat = PressureFieldSimulation::simulate(request);
    const auto wave = PropagationEnvironmentModule().simulateWave({request.environment, request.sampling});
    for (std::size_t i = 0; i < first.samples.size(); ++i)
    {
        const auto& sample = first.samples[i];
        const double time = 0.5 + static_cast<double>(i) / 20.0;
        near(sample.tidePressure, 1025.0 * 9.80665 * 0.5 * std::cos(2.0 * kPi * (time + 1.0) / 44714.0 + 0.3),
            1.0e-9, "潮汐绝对参考时刻偏移错误");
        near(sample.environmentWavePressure, wave.pressureAtObservationPa[i], 0.0, "海浪时间或位置没有对齐");
        near(sample.signalOnly + sample.environmentOnly, sample.totalField, 0.0, "三组场合成失败");
        near(sample.totalField - sample.hydrostaticPressure, sample.totalDynamicPressure, 1.0e-9, "去静水压力错误");
        near(sample.totalField, repeat.samples[i].totalField, 0.0, "相同海浪种子无法复现");
    }
    request.environment.waves.randomSeed = 18;
    const auto changed = PressureFieldSimulation::simulate(request);
    require(first.samples.front().environmentWavePressure != changed.samples.front().environmentWavePressure, "随机种子没有影响海浪");
    near(first.samples.front().signalOnly, changed.samples.front().signalOnly, 0.0, "改变海浪影响了独立目标分量");
}

/** 输入缺失、单位错误和不支持工况都必须明确失败。 */
void testInvalidInputs()
{
    using namespace hjc::field;
    const auto baseline = calmRequest();
    const std::vector<std::function<void(PressureFieldSimulation::Request&)>> changes{
        [](auto& r) { r.environment.densityKgM3.values.clear(); },
        [](auto& r) { r.environment.waterDepthM.unit = "km"; },
        [](auto& r) { r.environment.validDurationS = 1.0; },
        [](auto& r) { r.environment.validTimeTaiNs = std::numeric_limits<std::int64_t>::max(); },
        [](auto& r) { r.sampling.startTimeSeconds = -1.0; },
        [](auto& r) { r.sampling.observationPositionM.z = -61.0; },
        [](auto& r) { r.environment.quality.status = QualityStatus::MissingInput; },
        [](auto& r) { r.environment.waves.meanDirectionRad = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& r) { r.target.gravityAcceleration = 10.0; },
        [](auto& r) { r.target.velocity = 8.0; },
        [](auto& r) { r.environment.waves.projectSeaState = 4; r.environment.waves.significantHeightM = 1.0; r.sampling.sampleRateHz = 0.5; }
    };
    for (const auto& change : changes)
    {
        auto request = baseline;
        change(request);
        rejects([&] { PressureFieldSimulation::simulate(request); }, "非法配置未报错");
    }
    auto partial = baseline;
    partial.environment.quality.status = QualityStatus::PartiallyValid;
    partial.environment.quality.flags = {"test_partial_source"};
    require(PressureFieldSimulation::simulate(partial).quality.status == QualityStatus::PartiallyValid, "来源质量限制丢失");
}

/** 检查航运来源去重和船体经过观测点时的整段失败语义。 */
void testShipping()
{
    using namespace hjc::field;
    auto request = calmRequest();
    auto source = SyntheticScenarioFactory::makeFullBackgroundScenario().pressure.environment.shippingPressureSources.front();
    request.environment.shippingPressureSources = {source};
    const auto result = PressureFieldSimulation::simulate(request);
    require(std::abs(result.samples.front().shippingPressure) > 1.0e-12, "其他船舶没有贡献背景");
    request.environment.shippingPressureSources.push_back(source);
    rejects([&] { PressureFieldSimulation::simulate(request); }, "重复船舶未被拒绝");
    request.environment.shippingPressureSources = {source};
    request.targetSourceId = source.sourceId;
    rejects([&] { PressureFieldSimulation::simulate(request); }, "主目标重复计入背景");
    source.initialPosition = {0, 0, -55};
    source.velocity = 0;
    const auto independent = PropagationEnvironmentModule().simulatePressureBackground(
        {request.environment, request.sampling, 8});
    // 上面的背景仍有效；独立接口不需要也不判断主目标身份。
    require(independent.quality.status == QualityStatus::Valid, "独立背景误依赖主目标");
    request.environment.shippingPressureSources = {source};
    const auto crossing = PropagationEnvironmentModule().simulatePressureBackground(
        {request.environment, request.sampling, 8});
    require(crossing.quality.status == QualityStatus::OutOfRange && crossing.timeSeconds.empty(), "航运越界没有清空失败结果");
}

/** 配置文件必须保留整数精度，且拒绝不存在、拼错和重复的字段。 */
void testConfig(const std::filesystem::path& source, const std::filesystem::path& temporary)
{
    const auto config = loadPressureEnvironmentConfig(source);
    require(config.environment.validTimeTaiNs == 1735689637000000000LL, "TAI 纳秒精度损失");
    auto request = calmRequest();
    request.environment = config.environment;
    request.imageLayerCount = config.imageLayerCount;
    require(PressureFieldSimulation::simulate(request).samples.size() == 101, "演示配置无法运行");
    for (const std::string& suffix : {std::string("\nunknown_key=1\n"), std::string("\nwave_hs_m=2\n")})
    {
        std::ofstream output(temporary);
        output << config.sourceText << suffix;
        output.close();
        rejects([&] { loadPressureEnvironmentConfig(temporary); }, "配置错误被忽略");
    }
    rejects([&] { loadPressureEnvironmentConfig(temporary.string() + ".missing"); }, "缺失文件回退到了演示配置");
}
} // 匿名命名空间

/** 在统一测试进程中报告每个物理或接口契约的结果。 */
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"calm_and_original_regimes", testCalmAndRegimes},
        {"shared_environment", testSharedEnvironment},
        {"hjc_background_regression", testHjcBackgroundRegression},
        {"composition_and_time", testCompositionAndTime},
        {"invalid_inputs", testInvalidInputs},
        {"shipping_and_duplicate_sources", testShipping},
        {"configuration", [&] { testConfig(argv[1], argv[2]); }}
    };
    int failures = 0;
    for (const auto& test : tests)
    {
        try { test.second(); std::cout << "[PASS] " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}

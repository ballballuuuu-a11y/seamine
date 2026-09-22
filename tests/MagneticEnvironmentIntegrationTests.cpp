#include "integration/hjc/MagneticEnvironmentConfig.h"
#include "integration/hjc/MagneticFieldSimulation.h"
#include "hjc/field/models.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
/** 失败时提供可读原因，避免测试静默通过。 */
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

/** 对有限值进行带尺度的数值比较。 */
void near(double actual, double expected, double tolerance, const std::string& message)
{
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= tolerance * std::max({1.0, std::abs(actual), std::abs(expected)}),
        message);
}

/** 对非法输入要求显式报错，不允许返回看似有效的零时序。 */
void rejects(const std::function<void()>& action, const std::string& message)
{
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

/** 使用与演示一致的 ENU 目标和传感器，独立构造集成请求。 */
MagneticFieldSimulation::Request makeRequest(const MagneticEnvironmentConfig& config)
{
    MagneticFieldSimulation::Request request;
    request.environment = config.environment;
    request.target.type = TargetMagneticFieldModel::TargetType::SurfaceShip;
    request.target.length = 100.0;
    request.target.width = 15.0;
    request.target.height = 10.0;
    request.target.center = {-300.0, 40.0, -5.0};
    request.target.velocity = 8.0;
    request.target.relativePermeability = 180.0;
    request.target.magneticMaterialRatio = 0.035;
    request.target.remanentMagnetizationAm = {7.5, 0.8, -0.4};
    request.target.geomagneticFieldNt = {18000.0, 30000.0, -42000.0};
    request.target.minimumDistance = 2.0;
    request.sampling = {{0.0, 0.0, -30.0}, 0.0, 2.0, 4.0};
    return request;
}

/** 每个时刻逐分量核对合成恒等式、背景分量和标量观测定义。 */
void testCombinedField(const MagneticEnvironmentConfig& config)
{
    auto request = makeRequest(config);
    // 使用明确给定的一纳特磁扰动，验证它只属于环境背景。
    request.environment.geomagnetic.prescribedFluctuationT.assign(9, {1.0e-9, 0.0, 0.0});
    const auto output = MagneticFieldSimulation::simulate(request);
    require(output.timeSeconds.size() == 9 && output.components.has_value(), "合成磁场时序或分量缺失");
    require(output.quality.status == hjc::field::QualityStatus::Valid,
        "WMM 演示不应因旧地磁对照值不同而降级");
    for (std::size_t index = 0; index < output.timeSeconds.size(); ++index)
    {
        near(output.timeSeconds[index], static_cast<double>(index) / 4.0, 1.0e-12, "磁场采样时间错误");
        near(output.components->fluctuation.x[index], 1.0, 1.0e-12, "给定磁扰动未进入背景");
        near(output.signalOnly.x[index] + output.environmentOnly.x[index],
            output.totalField.x[index], 1.0e-12, "东向合成错误");
        near(output.signalOnly.y[index] + output.environmentOnly.y[index],
            output.totalField.y[index], 1.0e-12, "北向合成错误");
        near(output.signalOnly.z[index] + output.environmentOnly.z[index],
            output.totalField.z[index], 1.0e-12, "上向合成错误");
        near(output.components->targetMacro.x[index] +
                output.components->targetLocalCorrection.x[index],
            output.signalOnly.x[index], 1.0e-12, "目标东向宏观场与局部修整合成错误");
        near(output.components->targetMacro.y[index] +
                output.components->targetLocalCorrection.y[index],
            output.signalOnly.y[index], 1.0e-12, "目标北向宏观场与局部修整合成错误");
        near(output.components->targetMacro.z[index] +
                output.components->targetLocalCorrection.z[index],
            output.signalOnly.z[index], 1.0e-12, "目标上向宏观场与局部修整合成错误");
        require(std::hypot(output.environmentOnly.x[index], output.environmentOnly.y[index],
            output.environmentOnly.z[index]) > 10000.0, "WMM 环境背景未参与计算");
    }
}

/** 无磁性目标时，背景仍存在且合成场逐分量等于背景。 */
void testNoTargetContribution(const MagneticEnvironmentConfig& config)
{
    auto request = makeRequest(config);
    request.target.remanentMagnetizationAm = {0.0, 0.0, 0.0};
    request.target.relativePermeability = 1.0;
    const auto output = MagneticFieldSimulation::simulate(request);
    for (std::size_t index = 0; index < output.timeSeconds.size(); ++index)
    {
        near(output.signalOnly.x[index], 0.0, 0.0, "无磁性目标仍产生东向异常");
        near(output.signalOnly.y[index], 0.0, 0.0, "无磁性目标仍产生北向异常");
        near(output.signalOnly.z[index], 0.0, 0.0, "无磁性目标仍产生上向异常");
        near(output.totalField.x[index], output.environmentOnly.x[index], 0.0, "背景被重复计入");
    }
}

/** 目标穿过传感器时返回越界状态；缺资源和时间越界也不能回退合成值。 */
void testRejectedInputs(const MagneticEnvironmentConfig& config)
{
    auto request = makeRequest(config);
    // 相距十米且超过 minimumDistance，但仍在半长五十米的椭球内部。
    request.target.center = {-10.0, 0.0, -30.0};
    hjc::field::MagneticSimulationInput direct;
    direct.target.length = request.target.length;
    direct.target.width = request.target.width;
    direct.target.height = request.target.height;
    direct.target.center = {-10.0, 0.0, -30.0};
    direct.target.relativePermeability = 180.0;
    direct.target.magneticMaterialRatio = 0.035;
    direct.environment = config.environment;
    direct.sampling = request.sampling;
    const auto nearField = hjc::field::PropagationEnvironmentModule{}.simulateMagnetic(direct);
    require(nearField.quality.status == hjc::field::QualityStatus::OutOfRange &&
        nearField.totalField.x.empty(), "HJC 未拒绝椭球内部传感器");
    rejects([&] { static_cast<void>(MagneticFieldSimulation::simulate(request)); },
        "适配层未拒绝近场观测");

    request = makeRequest(config);
    request.sampling.durationSeconds = config.environment.validDurationS + 1.0;
    rejects([&] { static_cast<void>(MagneticFieldSimulation::simulate(request)); },
        "环境有效期外仍返回磁场");
    request = makeRequest(config);
    request.environment.geomagnetic.wmmCoefficientPath = "missing-WMM.COF";
    rejects([&] { static_cast<void>(MagneticFieldSimulation::simulate(request)); },
        "WMM 缺失时发生隐式数据回退");
    request = makeRequest(config);
    request.environment.geomagnetic.emag2GridPath = "missing-EMAG2.tif";
    rejects([&] { static_cast<void>(MagneticFieldSimulation::simulate(request)); },
        "EMAG2 缺失时发生隐式数据回退");
    request = makeRequest(config);
    request.target.pitchDegrees = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { static_cast<void>(MagneticFieldSimulation::simulate(request)); },
        "非法目标姿态仍返回磁场");
    request = makeRequest(config);
    request.target.dipoleArrayLongitudinalCount = 0U;
    rejects([&] { static_cast<void>(MagneticFieldSimulation::simulate(request)); },
        "空的多偶极子阵列仍返回磁场");
}

/** 明确锁定 HJC 正俯仰使舰首抬升的姿态约定，避免旧实现差异被忽略。 */
void testPitchConvention()
{
    hjc::field::MagneticTargetParameter target;
    target.length = 100.0;
    target.width = 15.0;
    target.height = 10.0;
    target.velocity = 10.0;
    target.pitchDegrees = 10.0;
    require(hjc::field::targetPosition(target, 1.0).z > target.center.z,
        "HJC 正俯仰应使目标航迹向上");
}
} // 匿名命名空间

/** 从 CTest 传入显式资源目录，不使用 HJC 合成场景工厂。 */
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::invalid_argument("测试需要配置文件与资源根目录");
        const auto config = loadMagneticEnvironmentConfig(
            std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]));
        require(std::filesystem::exists(config.environment.geomagnetic.wmmCoefficientPath),
            "WMM 测试资源不存在");
        testCombinedField(config);
        testNoTargetContribution(config);
        testRejectedInputs(config);
        testPitchConvention();
        std::cout << "HJC 磁场目标、环境与合成场集成测试通过\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "HJC 磁场集成测试失败：" << exception.what() << '\n';
        return 1;
    }
}

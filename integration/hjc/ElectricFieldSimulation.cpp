#include "ElectricFieldSimulation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
/** 汇总 HJC 的结构化校验信息，保留字段路径。 */
std::string describeIssues(const std::vector<hjc::field::ValidationIssue>& issues)
{
    std::string message;
    for (const auto& issue : issues) message += "; " + issue.fieldPath + ": " + issue.message;
    return message;
}

/** 检查一个三轴电场时序是否覆盖全部采样点。 */
bool hasCount(const hjc::field::ElectricSeriesVm& series, std::size_t count)
{
    return series.x.size() == count && series.y.size() == count && series.z.size() == count;
}

/** 在相对浮点误差范围内检查目标、环境和总场恒等式。 */
bool matchesSum(double signal, double environment, double total)
{
    const double scale = std::max({1.0, std::abs(signal), std::abs(environment), std::abs(total)});
    return std::isfinite(signal) && std::isfinite(environment) && std::isfinite(total) &&
        std::abs(signal + environment - total) <= 1.0e-11 * scale;
}
} // 匿名命名空间

hjc::field::ElectricSimulationOutput ElectricFieldSimulation::simulate(const Request& request)
{
    using namespace hjc::field;
    if (!std::isfinite(request.environment.validDurationS) ||
        !std::isfinite(request.sampling.startTimeSeconds) ||
        !std::isfinite(request.sampling.durationSeconds) ||
        request.sampling.startTimeSeconds < 0.0 || request.sampling.durationSeconds < 0.0 ||
        request.sampling.startTimeSeconds > request.environment.validDurationS ||
        request.sampling.durationSeconds >
            request.environment.validDurationS - request.sampling.startTimeSeconds)
    {
        throw std::invalid_argument("电场采样时间超出环境快照有效期");
    }
    if (!request.environment.conductivitySM.unit.empty() &&
        request.environment.conductivitySM.unit != "S/m")
    {
        throw std::invalid_argument("电场环境电导率必须使用 S/m");
    }
    if (request.environment.quality.status != QualityStatus::Valid &&
        request.environment.quality.status != QualityStatus::PartiallyValid)
    {
        throw std::invalid_argument("电场环境快照质量状态无效：" +
            toString(request.environment.quality.status));
    }
    for (const auto& source : request.environment.electric.interferenceDipoles)
    {
        if ((request.targetSourceId != 0 && source.sourceId == request.targetSourceId) ||
            (!request.targetLineageId.empty() && source.lineageId == request.targetLineageId))
        {
            throw std::invalid_argument("主目标不得再次列入环境电场干扰源");
        }
    }

    // 用现有目标模型得到最终采用的偶极矩，防止两条链路各自重复估算。
    const TargetElectricFieldModel targetModel(request.target);
    ElectricSimulationInput input;
    input.target.type = request.target.type == TargetElectricFieldModel::TargetType::SurfaceShip
        ? TargetType::SurfaceShip : TargetType::Submarine;
    input.target.sourceId = request.targetSourceId;
    input.target.lineageId = request.targetLineageId;
    input.target.tonnage = request.target.tonnage;
    input.target.length = request.target.length;
    input.target.width = request.target.width;
    input.target.draft = request.target.draft;
    input.target.velocity = request.target.velocity;
    input.target.shaftSpeedRpm = request.target.shaftSpeed;
    input.target.initialPosition = {request.target.initialPosition.x,
        request.target.initialPosition.y, request.target.initialPosition.z};
    input.target.headingDegrees = request.target.headingDegrees;
    input.target.pitchDegrees = request.target.pitchDegrees;
    input.target.shaftPhaseDegrees = request.target.shaftPhaseDegrees;
    input.target.staticDipoleMomentAm = targetModel.effectiveStaticDipoleMoment();
    input.target.corrosionCurrentDensityAm2 = request.target.corrosionCurrentDensity;
    input.target.coatingDamageRatio = request.target.coatingDamageRatio;
    input.target.electrodeSeparationM = request.target.electrodeSeparation;
    input.target.shaftModulationRatio = request.target.shaftModulationRatio;
    input.target.secondHarmonicRatio = request.target.secondHarmonicRatio;
    input.target.thirdHarmonicRatio = request.target.thirdHarmonicRatio;
    input.target.minimumDistance = request.target.minimumDistance;
    input.environment = request.environment;
    input.sampling = request.sampling;
    input.keepComponents = true;
    input.mode = ElectricSolverMode::HomogeneousConductiveDipoleL0;

    const auto timeAxis = makeTimeAxis(input.sampling, input.environment.validTimeTaiNs);
    if (!timeAxis) throw std::invalid_argument("电场采样时间轴无效");
    auto& motional = input.environment.electric.prescribedMotionalFieldVm;
    if (input.environment.electric.motionalMode == MotionalElectricMode::Prescribed &&
        motional.size() == 1U && timeAxis->count > 1U)
    {
        // 配置文件中的单个运动电场表示整段时序上的常量。
        motional.assign(timeAxis->count, motional.front());
    }

    const PropagationEnvironmentModule module;
    const auto validation = module.validateElectric(input);
    if (!validation.accepted)
        throw std::invalid_argument("HJC 电场输入被拒绝" + describeIssues(validation.issues));
    auto output = module.simulateElectric(input);
    if (output.quality.status != QualityStatus::Valid &&
        output.quality.status != QualityStatus::PartiallyValid)
    {
        throw std::runtime_error("HJC 电场计算失败：" + toString(output.quality.status) +
            describeIssues(output.quality.issues));
    }

    const std::size_t count = output.timeSeconds.size();
    if (count == 0U || count != timeAxis->count || !output.components ||
        !hasCount(output.signalOnly, count) || !hasCount(output.environmentOnly, count) ||
        !hasCount(output.totalField, count) || !hasCount(output.components->staticField, count) ||
        !hasCount(output.components->shaftField, count) || !hasCount(output.components->motional, count) ||
        !hasCount(output.components->shipping, count) || !hasCount(output.components->local, count))
    {
        throw std::runtime_error("HJC 电场时序或分量缺失");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        const double expectedTime = request.sampling.startTimeSeconds +
            static_cast<double>(index) / request.sampling.sampleRateHz;
        if (!std::isfinite(output.timeSeconds[index]) ||
            std::abs(output.timeSeconds[index] - expectedTime) >
                1.0e-10 * std::max(1.0, std::abs(expectedTime)) ||
            !matchesSum(output.signalOnly.x[index], output.environmentOnly.x[index], output.totalField.x[index]) ||
            !matchesSum(output.signalOnly.y[index], output.environmentOnly.y[index], output.totalField.y[index]) ||
            !matchesSum(output.signalOnly.z[index], output.environmentOnly.z[index], output.totalField.z[index]))
        {
            throw std::runtime_error("HJC 电场时间轴或目标与背景矢量合成不一致");
        }
    }
    if (request.environment.quality.status == QualityStatus::PartiallyValid)
    {
        // 上游环境受限时不得把数值可计算误标为完整有效。
        output.quality.status = QualityStatus::PartiallyValid;
        output.quality.flags.push_back("environment_partially_valid");
    }
    return output;
}

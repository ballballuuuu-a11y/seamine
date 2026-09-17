#include "TargetElectricFieldModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
// 数学与单位换算常量。
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegreesToRadians = kPi / 180.0;
constexpr double kMicrovoltsPerVolt = 1e6;

// 标准海水在实用盐度 35、15 摄氏度和零海压附近的参考电导率。
constexpr double kReferenceConductivity = 4.2914;

// 用排水吨位估算排水体积时采用的典型海水密度。
constexpr double kSeawaterDensityTonPerCubicMeter = 1.025;

// 单次时序仿真的最大采样点数，防止错误参数导致内存耗尽。
constexpr std::size_t kMaximumSampleCount = 10'000'000U;

/**
 * @brief 判断浮点数是否既不是无穷大也不是非数值。
 * @param value 需要检查的浮点数。
 * @return value 为普通有限数时返回 true，否则返回 false。
 */
bool isFinite(double value) noexcept
{
    return std::isfinite(value);
}
} // 匿名命名空间

/**
 * @brief 使用完整参数构造一个可立即计算的电场模型。
 * @param param 目标几何、运动、海水环境和等效电源参数。
 * @throws std::invalid_argument param 中存在非法参数时抛出。
 */
TargetElectricFieldModel::TargetElectricFieldModel(
    const TargetParameter& param)
{
    setTarget(param);
}

/**
 * @brief 校验并保存新的目标参数。
 * @param param 需要替换当前配置的完整目标参数。
 * @throws std::invalid_argument param 中存在非法参数时抛出，原配置保持不变。
 */
void TargetElectricFieldModel::setTarget(const TargetParameter& param)
{
    // 先完整校验临时参数，保证设置失败时原模型状态不变。
    validateTarget(param);
    m_target = param;
    m_configured = true;
}

/**
 * @brief 返回当前生效的目标参数。
 * @return 当前参数对象的只读引用。
 * @throws std::logic_error 尚未配置模型时抛出。
 */
const TargetElectricFieldModel::TargetParameter&
TargetElectricFieldModel::target() const
{
    ensureConfigured();
    return m_target;
}

/**
 * @brief 计算指定传感器位置和指定时刻的三维瞬时电场。
 * @param sensorPosition 传感器全局坐标，单位 m。
 * @param timeSeconds 相对仿真零时刻的时间，单位 s。
 * @return 当前时刻的目标位置、距离及各电场分量。
 * @throws std::logic_error 尚未配置模型时抛出。
 * @throws std::invalid_argument 坐标或时间不是有限值时抛出。
 * @throws std::domain_error 传感器距离小于允许的最小距离时抛出。
 */
TargetElectricFieldModel::ElectricFieldSignal
TargetElectricFieldModel::calculate(
    const Vector3& sensorPosition,
    double timeSeconds) const
{
    //检查模型是否已经设置目标
    ensureConfigured();
    //检查传感器坐标和时间
    if (!isFinite(sensorPosition.x) ||
        !isFinite(sensorPosition.y) ||
        !isFinite(sensorPosition.z) ||
        !isFinite(timeSeconds))
    {
        throw std::invalid_argument("传感器坐标和仿真时刻必须是有限值");
    }

    ElectricFieldSignal signal{};
    signal.time = timeSeconds;
    signal.frequency = m_target.shaftSpeed / 60.0;
    //计算目标当前的位置
    signal.targetPosition = calculateTargetPosition(timeSeconds);

    // 观测矢量始终由当前目标中心指向固定传感器。
    //计算目标到传感器的相对位置 相对位置 = 传感器位置 - 目标当前位置
    const Vector3 relativePosition =
        subtract(sensorPosition, signal.targetPosition);
    //计算目标到传感器的距离
    signal.distance = magnitude(relativePosition);

    if (signal.distance < m_target.minimumDistance)
    {
        throw std::domain_error("传感器距离小于偶极子模型允许的最小距离");
    }
    //海水电导率
    const double conductivity = calculateConductivity();
    //目标纵轴单位向量
    const Vector3 targetAxis = calculateTargetAxis();
    //静态等效电流偶极矩大小
    const double staticMoment = calculateStaticDipoleMoment();

    // 静态偶极矩沿目标纵轴方向，输出为三维静电场矢量。
    const Vector3 staticDipoleMoment = scale(targetAxis, staticMoment);
    //计算静电场向量
    signal.staticFieldVector = calculateDipoleField(
        relativePosition, staticDipoleMoment, conductivity);

    // 轴频场使用静态偶极矩的周期调制近似，并保留二、三次谐波。
    //判断是否需要计算轴频电场
    if (signal.frequency > 0.0 && m_target.shaftModulationRatio > 0.0)
    {
        //计算轴转相位
        const double phase =
            2.0 * kPi * signal.frequency * timeSeconds +
            m_target.shaftPhaseDegrees * kDegreesToRadians;
        //生成轴频波形
        const double waveform =
            std::cos(phase) +
            m_target.secondHarmonicRatio * std::cos(2.0 * phase) +
            m_target.thirdHarmonicRatio * std::cos(3.0 * phase);
        //构造轴频偶极矩幅值
        const Vector3 shaftDipoleMoment = scale(
            targetAxis,
            staticMoment * m_target.shaftModulationRatio);
        const Vector3 shaftAmplitudeVector = calculateDipoleField(
            relativePosition, shaftDipoleMoment, conductivity);
        //基波空间幅值
        signal.shaftAmplitude = magnitude(shaftAmplitudeVector);
        //得到当前时刻的轴频电场
        signal.shaftFieldVector = scale(shaftAmplitudeVector, waveform);
    }

    // 综合瞬时电场按矢量直接叠加，不再使用无明确含义的平方和。
    signal.totalFieldVector =
        add(signal.staticFieldVector, signal.shaftFieldVector);
    signal.staticField = magnitude(signal.staticFieldVector);
    signal.shaftField = magnitude(signal.shaftFieldVector);
    signal.totalField = magnitude(signal.totalFieldVector);

    return signal;
}

/**
 * @brief 通过目标初始中心到传感器的一维距离计算零时刻电场。
 * @param distance 目标初始中心到传感器的距离，单位 m。
 * @return 零时刻全局 x 轴正方向观测点处的电场结果。
 * @throws std::logic_error 尚未配置模型时抛出。
 * @throws std::invalid_argument distance 无效或小于最小距离时抛出。
 */
TargetElectricFieldModel::ElectricFieldSignal
TargetElectricFieldModel::calculate(double distance) const
{
    ensureConfigured();

    if (!isFinite(distance) || distance < m_target.minimumDistance)
    {
        throw std::invalid_argument("观测距离必须是不小于最小距离的有限正数");
    }

    // 兼容接口采用零时刻目标位置，并把传感器放在全局 x 轴正方向。
    const Vector3 sensorPosition{
        m_target.initialPosition.x + distance,
        m_target.initialPosition.y,
        m_target.initialPosition.z};
    return calculate(sensorPosition, 0.0);
}

/**
 * @brief 按固定采样率生成一段连续电场时序。
 * @param sensorPosition 固定传感器的全局坐标，单位 m。
 * @param startTimeSeconds 第一个采样点的仿真时刻，单位 s。
 * @param durationSeconds 需要生成的时序长度，单位 s。
 * @param sampleRateHz 每秒采样次数，单位 Hz。
 * @return 按时刻升序排列的电场结果集合。
 * @throws std::invalid_argument 时长或采样率不合法时抛出。
 * @throws std::length_error 计算得到的采样点数量过大时抛出。
 */
std::vector<TargetElectricFieldModel::ElectricFieldSignal>
TargetElectricFieldModel::simulate(
    const Vector3& sensorPosition,
    double startTimeSeconds,
    double durationSeconds,
    double sampleRateHz) const
{
    ensureConfigured();

    if (!isFinite(startTimeSeconds) ||
        !isFinite(durationSeconds) ||
        !isFinite(sampleRateHz) ||
        durationSeconds < 0.0 ||
        sampleRateHz <= 0.0)
    {
        throw std::invalid_argument("仿真时长必须非负且采样率必须为有限正数");
    }

    const double rawSampleCount = durationSeconds * sampleRateHz;
    if (!isFinite(rawSampleCount) ||
        rawSampleCount + 1.0 > static_cast<double>(kMaximumSampleCount))
    {
        throw std::length_error("请求的仿真采样点数量过大");
    }

    // 包含起点和终点；非整数采样周期的末端只保留最后一个完整采样点。
    const std::size_t sampleCount =
        static_cast<std::size_t>(std::floor(rawSampleCount)) + 1U;
    std::vector<ElectricFieldSignal> signals;
    signals.reserve(sampleCount);

    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        const double timeSeconds =
            startTimeSeconds + static_cast<double>(index) / sampleRateHz;
        signals.push_back(calculate(sensorPosition, timeSeconds));
    }

    return signals;
}

/**
 * @brief 对全部目标和环境参数执行集中合法性校验。
 * @param param 需要校验的完整参数对象。
 * @throws std::invalid_argument 任一参数超出模型支持范围时抛出。
 */
void TargetElectricFieldModel::validateTarget(
    const TargetParameter& param)
{
    const bool finiteGeometry =
        isFinite(param.tonnage) &&
        isFinite(param.length) &&
        isFinite(param.width) &&
        isFinite(param.draft);
    if (!finiteGeometry ||
        param.tonnage <= 0.0 ||
        param.length <= 0.0 ||
        param.width <= 0.0 ||
        param.draft <= 0.0)
    {
        throw std::invalid_argument("吨位、长度、宽度和吃水必须为有限正数");
    }

    if (!isFinite(param.initialPosition.x) ||
        !isFinite(param.initialPosition.y) ||
        !isFinite(param.initialPosition.z) ||
        !isFinite(param.velocity) ||
        !isFinite(param.headingDegrees) ||
        !isFinite(param.pitchDegrees) ||
        !isFinite(param.shaftSpeed) ||
        !isFinite(param.shaftPhaseDegrees) ||
        param.velocity < 0.0 ||
        param.shaftSpeed < 0.0)
    {
        throw std::invalid_argument("目标位置、姿态、航速和轴转速参数无效");
    }

    if (!isFinite(param.salinity) ||
        !isFinite(param.waterTemperature) ||
        !isFinite(param.seaPressureDbar) ||
        !isFinite(param.conductivity) ||
        param.salinity <= 0.0 ||
        param.salinity > 42.0 ||
        param.waterTemperature < -2.0 ||
        param.waterTemperature > 40.0 ||
        param.seaPressureDbar < 0.0 ||
        param.conductivity < 0.0)
    {
        throw std::invalid_argument("盐度、温度、压力或电导率参数超出支持范围");
    }

    if (!isFinite(param.staticDipoleMoment) ||
        !isFinite(param.corrosionCurrentDensity) ||
        !isFinite(param.coatingDamageRatio) ||
        !isFinite(param.electrodeSeparation) ||
        param.staticDipoleMoment < 0.0 ||
        param.corrosionCurrentDensity < 0.0 ||
        param.coatingDamageRatio < 0.0 ||
        param.coatingDamageRatio > 1.0 ||
        param.electrodeSeparation < 0.0)
    {
        throw std::invalid_argument("腐蚀静电场等效参数无效");
    }

    if (!isFinite(param.shaftModulationRatio) ||
        !isFinite(param.secondHarmonicRatio) ||
        !isFinite(param.thirdHarmonicRatio) ||
        param.shaftModulationRatio < 0.0 ||
        param.secondHarmonicRatio < 0.0 ||
        param.thirdHarmonicRatio < 0.0)
    {
        throw std::invalid_argument("轴频调制及谐波参数必须为有限非负数");
    }

    if (!isFinite(param.minimumDistance) || param.minimumDistance <= 0.0)
    {
        throw std::invalid_argument("最小观测距离必须为有限正数");
    }
}

/**
 * @brief 确认模型已经保存有效目标参数。
 * @throws std::logic_error 模型仍处于未配置状态时抛出。
 */
void TargetElectricFieldModel::ensureConfigured() const
{
    if (!m_configured)
    {
        throw std::logic_error("尚未设置有效的目标参数");
    }
}

/**
 * @brief 获取实测电导率或根据温度、盐度和压力进行近似估算。
 * @return 当前海水环境的电导率，单位 S/m。
 */
double TargetElectricFieldModel::calculateConductivity() const
{
    // 非零直接输入优先，便于使用实测电导率替代经验估算。
    if (m_target.conductivity > 0.0)
    {
        return m_target.conductivity;
    }

    // 这是面向快速仿真的近似式，并非完整的 PSS-78 或 TEOS-10 实现。
    const double salinityFactor = m_target.salinity / 35.0;
    const double temperatureFactor = std::max(
        0.05,
        1.0 + 0.019 * (m_target.waterTemperature - 15.0));
    const double pressureFactor =
        1.0 + 2.5e-5 * m_target.seaPressureDbar;

    return kReferenceConductivity *
           salinityFactor *
           temperatureFactor *
           pressureFactor;
}

/**
 * @brief 根据尺寸、吨位和目标类型估算浸水表面积。
 * @return 目标船体浸水表面积，单位 m²。
 */
double TargetElectricFieldModel::calculateWetArea() const
{
    // 先计算长方体近似的底面及侧面面积，确保结果量纲为 m²。
    const double boxWetArea =
        m_target.length * m_target.width +
        2.0 * m_target.length * m_target.draft +
        2.0 * m_target.width * m_target.draft;

    // 排水体积与包络体积的比例用于反映船体丰满度，并限制异常输入影响。
    const double displacedVolume =
        m_target.tonnage / kSeawaterDensityTonPerCubicMeter;
    const double envelopeVolume =
        m_target.length * m_target.width * m_target.draft;
    const double blockCoefficient = std::clamp(
        displacedVolume / envelopeVolume,
        0.20,
        1.00);

    const double typeCoefficient =
        m_target.type == TargetType::SurfaceShip ? 0.78 : 0.90;
    const double fullnessCorrection =
        0.80 + 0.20 * std::cbrt(blockCoefficient);

    return boxWetArea * typeCoefficient * fullnessCorrection;
}

/**
 * @brief 获取实测静态偶极矩或根据腐蚀参数进行估算。
 * @return 静态电流偶极矩标量，单位 A·m。
 */
double TargetElectricFieldModel::calculateStaticDipoleMoment() const
{
    // 用户提供的实测或反演偶极矩优先级最高。
    if (m_target.staticDipoleMoment > 0.0)
    {
        return m_target.staticDipoleMoment;
    }

    const double corrosionCurrent =
        m_target.corrosionCurrentDensity *
        calculateWetArea() *
        m_target.coatingDamageRatio;
    const double separation =
        m_target.electrodeSeparation > 0.0
            ? m_target.electrodeSeparation
            : m_target.length *
                  (m_target.type == TargetType::SurfaceShip ? 0.35 : 0.25);

    return corrosionCurrent * separation;
}

/**
 * @brief 使用匀速直线运动关系计算目标中心位置。
 * @param timeSeconds 相对目标初始位置的仿真时间，单位 s。
 * @return 指定时刻的目标中心全局坐标，单位 m。
 */
TargetElectricFieldModel::Vector3
TargetElectricFieldModel::calculateTargetPosition(
    double timeSeconds) const
{
    // 匀速直线运动模型便于生成稳定、可重复的目标通过传感器时序。
    return add(
        m_target.initialPosition,
        scale(calculateTargetAxis(), m_target.velocity * timeSeconds));
}

/**
 * @brief 把航向角和俯仰角转换为目标纵轴单位矢量。
 * @return 指向目标运动方向且模长为一的三维矢量。
 */
TargetElectricFieldModel::Vector3
TargetElectricFieldModel::calculateTargetAxis() const
{
    const double heading =
        m_target.headingDegrees * kDegreesToRadians;
    const double pitch =
        m_target.pitchDegrees * kDegreesToRadians;
    const double horizontalFactor = std::cos(pitch);

    return Vector3{
        horizontalFactor * std::cos(heading),
        horizontalFactor * std::sin(heading),
        std::sin(pitch)};
}

/**
 * @brief 根据电流偶极子远场公式计算传感器位置处的三维电场。
 * @param relativePosition 从目标中心指向传感器的位移，单位 m。
 * @param dipoleMoment 电流偶极矩矢量，单位 A·m。
 * @param conductivity 海水电导率，单位 S/m。
 * @return 三维电场矢量，单位 μV/m。
 */
TargetElectricFieldModel::Vector3
TargetElectricFieldModel::calculateDipoleField(
    const Vector3& relativePosition,
    const Vector3& dipoleMoment,
    double conductivity)
{
    const double distance = magnitude(relativePosition);
    const Vector3 direction = scale(relativePosition, 1.0 / distance);
    const double directionalMoment = dot(dipoleMoment, direction);

    // 电流偶极子远场：E=(3*r_hat*(p·r_hat)-p)/(4*pi*sigma*r³)。
    const Vector3 numerator = subtract(
        scale(direction, 3.0 * directionalMoment),
        dipoleMoment);
    const double denominator =
        4.0 * kPi * conductivity *
        distance * distance * distance;

    return scale(numerator, kMicrovoltsPerVolt / denominator);
}

/**
 * @brief 计算两个三维矢量的点积。
 * @param lhs 左侧三维矢量。
 * @param rhs 右侧三维矢量。
 * @return 两个矢量的标量点积。
 */
double TargetElectricFieldModel::dot(
    const Vector3& lhs,
    const Vector3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

/**
 * @brief 计算三维矢量的欧几里得模值。
 * @param value 需要计算模值的三维矢量。
 * @return value 的非负模值。
 */
double TargetElectricFieldModel::magnitude(
    const Vector3& value) noexcept
{
    return std::sqrt(dot(value, value));
}

/**
 * @brief 对两个三维矢量逐分量相加。
 * @param lhs 加法左侧矢量。
 * @param rhs 加法右侧矢量。
 * @return 两个矢量相加后的结果。
 */
TargetElectricFieldModel::Vector3
TargetElectricFieldModel::add(
    const Vector3& lhs,
    const Vector3& rhs) noexcept
{
    return Vector3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

/**
 * @brief 对两个三维矢量逐分量相减。
 * @param lhs 被减矢量。
 * @param rhs 减数矢量。
 * @return lhs 减去 rhs 后的结果。
 */
TargetElectricFieldModel::Vector3
TargetElectricFieldModel::subtract(
    const Vector3& lhs,
    const Vector3& rhs) noexcept
{
    return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

/**
 * @brief 使用标量统一缩放三维矢量的所有分量。
 * @param value 需要缩放的三维矢量。
 * @param factor 乘到每个矢量分量上的缩放系数。
 * @return 缩放后的三维矢量。
 */
TargetElectricFieldModel::Vector3
TargetElectricFieldModel::scale(
    const Vector3& value,
    double factor) noexcept
{
    return Vector3{value.x * factor, value.y * factor, value.z * factor};
}

#include "TargetMagneticFieldModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
constexpr double kPi = 3.14159265358979323846;          // 圆周率。
constexpr double kVacuumPermeability = 4.0e-7 * kPi;   // 真空磁导率，单位 H/m。
constexpr double kNanoteslaPerTesla = 1.0e9;            // 特斯拉到纳特的换算系数。
constexpr std::size_t kIntegrationSteps = 4096U;        // 退磁因子数值积分步数。

/** 判断浮点数是否为有限值。 */
bool isFinite(double value) noexcept
{
    return std::isfinite(value);
}

/** 判断三维矢量的全部分量是否为有限值。 */
bool isFiniteVector(const TargetMagneticFieldModel::Vector3& value) noexcept
{
    return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

/** 计算三维矢量点积。 */
double dot(const TargetMagneticFieldModel::Vector3& lhs,
           const TargetMagneticFieldModel::Vector3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

/** 计算三维矢量模值。 */
double magnitude(const TargetMagneticFieldModel::Vector3& value) noexcept
{
    return std::sqrt(dot(value, value));
}

/** 计算角度对应的弧度值。 */
double degreesToRadians(double degrees) noexcept
{
    return degrees * kPi / 180.0;
}

/**
 * @brief 把目标坐标系矢量按航向、俯仰和横滚旋转至全局坐标系。
 */
TargetMagneticFieldModel::Vector3 bodyToGlobal(
    const TargetMagneticFieldModel::Vector3& value,
    const TargetMagneticFieldModel::TargetParameter& target) noexcept
{
    const double heading = degreesToRadians(target.headingDegrees);
    const double pitch = degreesToRadians(target.pitchDegrees);
    const double roll = degreesToRadians(target.rollDegrees);
    const double ch = std::cos(heading);
    const double sh = std::sin(heading);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);

    // 使用 Z-Y-X 欧拉角旋转，确保航向角与现有电场模型约定一致。
    return {
        ch * cp * value.x + (ch * sp * sr - sh * cr) * value.y +
            (ch * sp * cr + sh * sr) * value.z,
        sh * cp * value.x + (sh * sp * sr + ch * cr) * value.y +
            (sh * sp * cr - ch * sr) * value.z,
        -sp * value.x + cp * sr * value.y + cp * cr * value.z};
}

/**
 * @brief 把全局坐标系矢量旋转至目标坐标系。
 */
TargetMagneticFieldModel::Vector3 globalToBody(
    const TargetMagneticFieldModel::Vector3& value,
    const TargetMagneticFieldModel::TargetParameter& target) noexcept
{
    const double heading = degreesToRadians(target.headingDegrees);
    const double pitch = degreesToRadians(target.pitchDegrees);
    const double roll = degreesToRadians(target.rollDegrees);
    const double ch = std::cos(heading);
    const double sh = std::sin(heading);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);

    // 旋转矩阵为正交矩阵，因此使用正向矩阵的转置完成逆旋转。
    return {
        ch * cp * value.x + sh * cp * value.y - sp * value.z,
        (ch * sp * sr - sh * cr) * value.x +
            (sh * sp * sr + ch * cr) * value.y + cp * sr * value.z,
        (ch * sp * cr + sh * sr) * value.x +
            (sh * sp * cr - ch * sr) * value.y + cp * cr * value.z};
}

/**
 * @brief 计算观测点相对于等效三轴椭球的无量纲坐标。
 *
 * 返回值小于等于 1 表示观测点位于椭球内部或表面。计算前先按照目标
 * 航向、俯仰和横滚把全局位移转换到目标坐标系，因此判定会随姿态旋转。
 */
double calculateEllipsoidCoordinate(
    const TargetMagneticFieldModel::Vector3& displacement,
    const TargetMagneticFieldModel::TargetParameter& target) noexcept
{
    const TargetMagneticFieldModel::Vector3 bodyDisplacement =
        globalToBody(displacement, target);
    const double semiLength = target.length * 0.5;
    const double semiWidth = target.width * 0.5;
    const double semiHeight = target.height * 0.5;

    // 三个归一化分量的平方和定义等效三轴椭球边界。
    const double normalizedX = bodyDisplacement.x / semiLength;
    const double normalizedY = bodyDisplacement.y / semiWidth;
    const double normalizedZ = bodyDisplacement.z / semiHeight;
    return normalizedX * normalizedX +
           normalizedY * normalizedY +
           normalizedZ * normalizedZ;
}

/**
 * @brief 数值计算三轴椭球沿指定半轴方向的退磁因子。
 */
double calculateDemagnetizingFactor(double axisSquared,
                                    double aSquared,
                                    double bSquared,
                                    double cSquared,
                                    double abc) noexcept
{
    const double scale = std::max({aSquared, bSquared, cSquared});
    const double step = 1.0 / static_cast<double>(kIntegrationSteps);
    double integral = 0.0;

    // 使用中点积分并通过 s=scale*u/(1-u) 把无穷区间映射到 (0,1)。
    for (std::size_t index = 0; index < kIntegrationSteps; ++index)
    {
        const double u = (static_cast<double>(index) + 0.5) * step;
        const double oneMinusU = 1.0 - u;
        const double s = scale * u / oneMinusU;
        const double dsDu = scale / (oneMinusU * oneMinusU);
        const double delta = std::sqrt(
            (s + aSquared) * (s + bSquared) * (s + cSquared));
        integral += dsDu / ((s + axisSquared) * delta);
    }

    return 0.5 * abc * integral * step;
}

/** 计算并归一化三轴椭球退磁因子，保证三分量之和严格为 1。 */
std::array<double, 3U> calculateDemagnetizingFactors(double length,
                                                     double width,
                                                     double height) noexcept
{
    const double a = length * 0.5;
    const double b = width * 0.5;
    const double c = height * 0.5;
    const double aSquared = a * a;
    const double bSquared = b * b;
    const double cSquared = c * c;
    const double abc = a * b * c;

    std::array<double, 3U> factors{
        calculateDemagnetizingFactor(aSquared, aSquared, bSquared, cSquared, abc),
        calculateDemagnetizingFactor(bSquared, aSquared, bSquared, cSquared, abc),
        calculateDemagnetizingFactor(cSquared, aSquared, bSquared, cSquared, abc)};
    const double sum = factors[0] + factors[1] + factors[2];

    // 数值积分会产生轻微截断误差，归一化后满足磁静力学恒等式 Nx+Ny+Nz=1。
    for (double& factor : factors)
    {
        factor /= sum;
    }
    return factors;
}

/** 使用磁偶极子公式计算指定观测位置的异常磁场，结果单位为 nT。 */
TargetMagneticFieldModel::Vector3 calculateDipoleField(
    const TargetMagneticFieldModel::Vector3& moment,
    const TargetMagneticFieldModel::Vector3& displacement,
    double distance) noexcept
{
    const TargetMagneticFieldModel::Vector3 direction{
        displacement.x / distance,
        displacement.y / distance,
        displacement.z / distance};
    const double projection = dot(moment, direction);
    const double coefficient =
        (kVacuumPermeability / (4.0 * kPi)) * kNanoteslaPerTesla /
        (distance * distance * distance);

    return {
        coefficient * (3.0 * projection * direction.x - moment.x),
        coefficient * (3.0 * projection * direction.y - moment.y),
        coefficient * (3.0 * projection * direction.z - moment.z)};
}

/** 根据采样点序号在线性区间内计算坐标，单点轴取最小坐标。 */
double gridCoordinate(double minimum,
                      double maximum,
                      std::size_t count,
                      std::size_t index) noexcept
{
    if (count == 1U)
    {
        return minimum;
    }
    return minimum + (maximum - minimum) *
        static_cast<double>(index) / static_cast<double>(count - 1U);
}
} // 匿名命名空间

TargetMagneticFieldModel::TargetMagneticFieldModel(const TargetParameter& param)
{
    setTarget(param);
}

void TargetMagneticFieldModel::setTarget(const TargetParameter& param)
{
    // 先在临时状态上完成校验，避免非法参数破坏已经可用的模型。
    validateTarget(param);
    m_target = param;
    rebuildDipoleMoments();
    m_configured = true;
}

const TargetMagneticFieldModel::TargetParameter&
TargetMagneticFieldModel::target() const
{
    ensureConfigured();
    return m_target;
}

TargetMagneticFieldModel::MagneticFieldSample
TargetMagneticFieldModel::calculate(const Vector3& observationPosition) const
{
    // 保持原空间分布接口语义，未指定时刻时统一使用零时刻目标位置。
    return calculate(observationPosition, 0.0);
}

TargetMagneticFieldModel::MagneticFieldSample
TargetMagneticFieldModel::calculate(const Vector3& observationPosition,
                                    double timeSeconds) const
{
    ensureConfigured();
    if (!isFiniteVector(observationPosition) || !isFinite(timeSeconds))
    {
        throw std::invalid_argument("观测点坐标和仿真时间必须为有限数值");
    }

    // 目标沿自身纵轴匀速运动，航向和俯仰决定全局运动方向。
    const Vector3 movement = bodyToGlobal(
        {m_target.velocity * timeSeconds, 0.0, 0.0}, m_target);
    const Vector3 targetPosition{
        m_target.center.x + movement.x,
        m_target.center.y + movement.y,
        m_target.center.z + movement.z};
    if (!isFiniteVector(targetPosition))
    {
        throw std::invalid_argument("目标运动位置超出可计算数值范围");
    }

    const Vector3 displacement{
        observationPosition.x - targetPosition.x,
        observationPosition.y - targetPosition.y,
        observationPosition.z - targetPosition.z};
    const double distance = magnitude(displacement);
    const double ellipsoidCoordinate =
        calculateEllipsoidCoordinate(displacement, m_target);
    if (!isFinite(distance) || !isFinite(ellipsoidCoordinate) ||
        distance < m_target.minimumDistance || ellipsoidCoordinate <= 1.0)
    {
        throw std::domain_error(
            "观测点位于等效舰体椭球内部、表面或距离目标中心过近，超出磁偶极子模型适用范围");
    }

    MagneticFieldSample sample{};
    sample.observationPosition = observationPosition;
    sample.targetPosition = targetPosition;
    sample.distance = distance;
    sample.time = timeSeconds;
    //
    sample.staticFieldVector =
        calculateDipoleField(m_staticDipoleMoment, displacement, distance);
    sample.inducedFieldVector =
        calculateDipoleField(m_inducedDipoleMoment, displacement, distance);
    sample.totalFieldVector = {
        sample.staticFieldVector.x + sample.inducedFieldVector.x,
        sample.staticFieldVector.y + sample.inducedFieldVector.y,
        sample.staticFieldVector.z + sample.inducedFieldVector.z};
    sample.staticField = magnitude(sample.staticFieldVector);
    sample.inducedField = magnitude(sample.inducedFieldVector);
    sample.totalField = magnitude(sample.totalFieldVector);
    return sample;
}

std::vector<TargetMagneticFieldModel::MagneticFieldSample>
TargetMagneticFieldModel::simulate(
    const Vector3& observationPosition,
    double startTimeSeconds,
    double durationSeconds,
    double sampleRateHz) const
{
    ensureConfigured();
    if (!isFiniteVector(observationPosition) ||
        !isFinite(startTimeSeconds) ||
        !isFinite(durationSeconds) ||
        !isFinite(sampleRateHz) ||
        durationSeconds < 0.0 ||
        sampleRateHz <= 0.0)
    {
        throw std::invalid_argument(
            "传感器坐标必须有限，仿真时长必须非负且采样率必须为有限正数");
    }

    const double rawSampleCount = durationSeconds * sampleRateHz;
    if (!isFinite(rawSampleCount) ||
        rawSampleCount + 1.0 > static_cast<double>(kMaximumTimeSampleCount))
    {
        throw std::length_error("请求的磁场时序采样点数量过大");
    }

    // 与电场时序保持一致：包含起点，并保留持续时间内的最后一个完整采样点。
    const std::size_t sampleCount =
        static_cast<std::size_t>(std::floor(rawSampleCount)) + 1U;
    std::vector<MagneticFieldSample> samples;
    samples.reserve(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        const double timeSeconds =
            startTimeSeconds + static_cast<double>(index) / sampleRateHz;
        samples.push_back(calculate(observationPosition, timeSeconds));
    }
    return samples;
}

std::vector<TargetMagneticFieldModel::MagneticFieldSample>
TargetMagneticFieldModel::simulateGrid(const GridParameter& grid) const
{
    ensureConfigured();
    validateGrid(grid);

    if (grid.xCount > kMaximumGridPointCount / grid.yCount ||
        grid.xCount * grid.yCount > kMaximumGridPointCount / grid.zCount)
    {
        throw std::length_error("请求的磁场网格点数量过大");
    }
    const std::size_t pointCount = grid.xCount * grid.yCount * grid.zCount;

    std::vector<MagneticFieldSample> samples;
    samples.reserve(pointCount);
    for (std::size_t zIndex = 0; zIndex < grid.zCount; ++zIndex)
    {
        for (std::size_t yIndex = 0; yIndex < grid.yCount; ++yIndex)
        {
            for (std::size_t xIndex = 0; xIndex < grid.xCount; ++xIndex)
            {
                // 展开顺序固定为 z、y、x，便于前端直接恢复规则网格。
                const Vector3 position{
                    gridCoordinate(grid.minimum.x, grid.maximum.x,
                                   grid.xCount, xIndex),
                    gridCoordinate(grid.minimum.y, grid.maximum.y,
                                   grid.yCount, yIndex),
                    gridCoordinate(grid.minimum.z, grid.maximum.z,
                                   grid.zCount, zIndex)};
                samples.push_back(calculate(position));
            }
        }
    }
    return samples;
}

void TargetMagneticFieldModel::validateTarget(const TargetParameter& param)
{
    if (!isFinite(param.length) || !isFinite(param.width) ||
        !isFinite(param.height) || param.length <= 0.0 ||
        param.width <= 0.0 || param.height <= 0.0)
    {
        throw std::invalid_argument("目标长度、宽度和高度必须为有限正数");
    }
    if (!isFiniteVector(param.center) ||
        !isFinite(param.velocity) || param.velocity < 0.0 ||
        !isFinite(param.headingDegrees) ||
        !isFinite(param.pitchDegrees) ||
        !isFinite(param.rollDegrees))
    {
        throw std::invalid_argument("目标位置、非负航速和姿态角必须为有限数值");
    }
    if (!isFiniteVector(param.geomagneticFieldNt) ||
        !isFiniteVector(param.remanentMagnetizationAm))
    {
        throw std::invalid_argument("地磁场和剩磁三分量必须为有限数值");
    }
    if (!isFinite(param.relativePermeability) ||
        param.relativePermeability < 1.0)
    {
        throw std::invalid_argument("等效相对磁导率必须是不小于1的有限数值");
    }
    if (!isFinite(param.magneticMaterialRatio) ||
        param.magneticMaterialRatio <= 0.0 ||
        param.magneticMaterialRatio > 1.0)
    {
        throw std::invalid_argument("磁性材料等效体积占比必须在(0,1]范围内");
    }
    if (!isFinite(param.minimumDistance) || param.minimumDistance <= 0.0)
    {
        throw std::invalid_argument("最小观测距离必须为有限正数");
    }
}

void TargetMagneticFieldModel::validateGrid(const GridParameter& grid)
{
    if (!isFiniteVector(grid.minimum) || !isFiniteVector(grid.maximum))
    {
        throw std::invalid_argument("磁场网格边界必须为有限数值");
    }
    if (grid.minimum.x > grid.maximum.x ||
        grid.minimum.y > grid.maximum.y ||
        grid.minimum.z > grid.maximum.z)
    {
        throw std::invalid_argument("磁场网格最小坐标不能大于最大坐标");
    }
    if (grid.xCount == 0U || grid.yCount == 0U || grid.zCount == 0U)
    {
        throw std::invalid_argument("磁场网格各方向采样点数必须大于0");
    }
}

void TargetMagneticFieldModel::rebuildDipoleMoments()
{
    // 用三轴椭球体积乘材料占比，得到参与磁化的等效铁磁体积。
    //计算几何体积
    const double ellipsoidVolume =
        (kPi / 6.0) * m_target.length * m_target.width * m_target.height;
    //计算参与磁化的等效磁性体积
    const double magneticVolume =
        ellipsoidVolume * m_target.magneticMaterialRatio;
    //剩磁如何变成静态磁矩
    //1、等效磁性体积×剩磁强度
    const Vector3 staticMomentBody{
        magneticVolume * m_target.remanentMagnetizationAm.x,
        magneticVolume * m_target.remanentMagnetizationAm.y,
        magneticVolume * m_target.remanentMagnetizationAm.z};
    //2、转为全局坐标系
    m_staticDipoleMoment = bodyToGlobal(staticMomentBody, m_target);

    const auto factors = calculateDemagnetizingFactors(
        m_target.length, m_target.width, m_target.height);
    const Vector3 geomagneticBody = globalToBody(
        m_target.geomagneticFieldNt, m_target);
    const double susceptibility = m_target.relativePermeability - 1.0;

    // 地磁 B 先换算为 H，再按三个主轴的退磁因子计算感应磁矩。
    const Vector3 magneticFieldStrength{
        geomagneticBody.x / (kNanoteslaPerTesla * kVacuumPermeability),
        geomagneticBody.y / (kNanoteslaPerTesla * kVacuumPermeability),
        geomagneticBody.z / (kNanoteslaPerTesla * kVacuumPermeability)};
    const Vector3 inducedMomentBody{
        magneticVolume * susceptibility * magneticFieldStrength.x /
            (1.0 + factors[0] * susceptibility),
        magneticVolume * susceptibility * magneticFieldStrength.y /
            (1.0 + factors[1] * susceptibility),
        magneticVolume * susceptibility * magneticFieldStrength.z /
            (1.0 + factors[2] * susceptibility)};
    m_inducedDipoleMoment = bodyToGlobal(inducedMomentBody, m_target);
}

void TargetMagneticFieldModel::ensureConfigured() const
{
    if (!m_configured)
    {
        throw std::logic_error("磁场模型尚未配置目标参数");
    }
}

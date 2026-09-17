#include "TargetPressureFieldModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
constexpr double kPi = 3.14159265358979323846; // 圆周率。
constexpr int kImageLayerCount = 8;            // 上下边界镜像偶极子的截断层数。

/** 判断浮点数是否为有限值。 */
bool isFinite(double value) noexcept
{
    return std::isfinite(value);
}

/** 判断三维坐标的全部分量是否为有限值。 */
bool isFiniteVector(const TargetPressureFieldModel::Vector3& value) noexcept
{
    return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

/** 计算角度对应的弧度值。 */
double degreesToRadians(double degrees) noexcept
{
    return degrees * kPi / 180.0;
}

/** 计算三维矢量的欧氏距离。 */
double magnitude(const TargetPressureFieldModel::Vector3& value) noexcept
{
    return std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z);
}

/**
 * @brief 把水平全局位移旋转至目标坐标系。
 */
TargetPressureFieldModel::Vector3 globalToBody(
    const TargetPressureFieldModel::Vector3& value,
    double headingDegrees) noexcept
{
    const double heading = degreesToRadians(headingDegrees);
    const double cosine = std::cos(heading);
    const double sine = std::sin(heading);

    // 航向只改变水平纵横轴，垂向坐标保持全局 z 方向。
    return {
        cosine * value.x + sine * value.y,
        -sine * value.x + cosine * value.y,
        value.z};
}

/** 根据采样序号在线性区间内计算坐标，单点轴取最小坐标。 */
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

/**
 * @brief 计算等效旋转椭球纵向退极化因子。
 *
 * 该无量纲因子用于近似细长目标沿纵轴运动时的附加质量效应。
 */
double calculateLongitudinalFactor(double length,
                                   double width,
                                   double draft) noexcept
{
    const double longitudinalRadius = length * 0.5;
    const double transverseRadius = 0.5 * std::sqrt(width * draft);
    const double radiusRatio = transverseRadius / longitudinalRadius;

    if (radiusRatio >= 1.0 - 1.0e-8)
    {
        // 球形极限下纵向退极化因子为 1/3。
        return 1.0 / 3.0;
    }

    const double eccentricity =
        std::sqrt(std::max(0.0, 1.0 - radiusRatio * radiusRatio));
    const double logarithm =
        std::log((1.0 + eccentricity) / (1.0 - eccentricity));
    //描述目标沿纵轴推动水体时的几何特征
    return (1.0 - eccentricity * eccentricity) /
        (2.0 * eccentricity * eccentricity * eccentricity) *
        (logarithm - 2.0 * eccentricity);
}

/**
 * @brief 计算单个水平运动势流偶极子在观测点产生的动态压力。
 */
double calculateImagePressure(
    const TargetPressureFieldModel::Vector3& observationPosition,
    const TargetPressureFieldModel::Vector3& imagePosition,
    double headingX,
    double headingY,
    double minimumDistance,
    double pressureCoefficient)
{
    //计算观测点到当前镜像点的距离
    const TargetPressureFieldModel::Vector3 displacement{
        observationPosition.x - imagePosition.x,
        observationPosition.y - imagePosition.y,
        observationPosition.z - imagePosition.z};
    const double distance = magnitude(displacement);
    if (!isFinite(distance) || distance < minimumDistance)
    {
        throw std::domain_error("观测点距离目标或边界镜像点过近，超出势流偶极子模型适用范围");
    }
    //计算相对位置在航向上的投影
    const double longitudinalProjection =
        displacement.x * headingX + displacement.y * headingY;
    const double cosineSquared =
        longitudinalProjection * longitudinalProjection /
        (distance * distance);

    // 线性化非定常伯努利方程给出 (3cos²θ-1)/r³ 的压力空间分布。
    return pressureCoefficient * (3.0 * cosineSquared - 1.0) /
        (distance * distance * distance);
}

/**
 * @brief 计算刚盖自由液面与海床共同作用下的有限水深压力。
 *
 * 周期镜像保留原模型的截断方式，用于低速无兴波工况。
 */
double calculateRigidLidTotalPressure(
    const TargetPressureFieldModel::Vector3& observationPosition,
    const TargetPressureFieldModel::Vector3& targetPosition,
    double waterDepth,
    double headingX,
    double headingY,
    double minimumDistance,
    double pressureCoefficient)
{
    double totalPressure = 0.0;
    for (int layer = -kImageLayerCount;
         layer <= kImageLayerCount;
         ++layer)
    {
        // 周期平移镜像用于同时满足上下刚性边界的无穿透条件。
        const double verticalPeriod =
            2.0 * static_cast<double>(layer) * waterDepth;
        const TargetPressureFieldModel::Vector3 directImage{
            targetPosition.x,
            targetPosition.y,
            targetPosition.z + verticalPeriod};
        const TargetPressureFieldModel::Vector3 reflectedImage{
            targetPosition.x,
            targetPosition.y,
            -targetPosition.z - 2.0 * waterDepth + verticalPeriod};

        totalPressure += calculateImagePressure(
            observationPosition,
            directImage,
            headingX,
            headingY,
            minimumDistance,
            pressureCoefficient);
        totalPressure += calculateImagePressure(
            observationPosition,
            reflectedImage,
            headingX,
            headingY,
            minimumDistance,
            pressureCoefficient);
    }
    return totalPressure;
}

/**
 * @brief 计算浅水亚临界条件下的线性深度平均兴波压力。
 *
 * 采用亚临界坐标伸缩 beta=sqrt(1-FrH^2) 的二维偶极子近似。
 * 长波压力在水深方向按静水近似保持一致，横向尺度用船宽限制奇点。
 */
double calculateShallowSubcriticalWavePressure(
    double longitudinalOffset,
    double lateralOffset,
    double width,
    double waterDepth,
    double minimumDistance,
    double depthFroudeNumber,
    double waterDensity,
    double dipoleStrengthPerSpeed,
    double velocity)
{
    const double betaSquared =
        std::max(1.0e-12,
                 1.0 - depthFroudeNumber * depthFroudeNumber);
    const double beta = std::sqrt(betaSquared);
    const double stretchedLongitudinalOffset = longitudinalOffset / beta;
    const double coreRadius = std::max(minimumDistance, width * 0.5);
    const double rawRadiusSquared =
        stretchedLongitudinalOffset * stretchedLongitudinalOffset +
        lateralOffset * lateralOffset;
    const double radiusSquared =
        std::max(rawRadiusSquared, coreRadius * coreRadius);

    // 二维偶极子的角向项在纵向为正、横向为负，并按水平距离平方衰减。
    const double angularTerm =
        (stretchedLongitudinalOffset * stretchedLongitudinalOffset -
         lateralOffset * lateralOffset) /
        (radiusSquared * radiusSquared);
    const double pressureScale =
        waterDensity * dipoleStrengthPerSpeed * velocity * velocity /
        (waterDepth * beta);
    return pressureScale * angularTerm;
}
} // 匿名命名空间

TargetPressureFieldModel::TargetPressureFieldModel(
    const TargetParameter& param)
{
    setTarget(param);
}

void TargetPressureFieldModel::setTarget(const TargetParameter& param)
{
    // 先完成全部校验，再修改成员，保证非法设置不会破坏原有可用配置。
    validateTarget(param);
    m_target = param;
    rebuildHydrodynamicParameters();
    m_configured = true;
}

const TargetPressureFieldModel::TargetParameter&
TargetPressureFieldModel::target() const
{
    ensureConfigured();
    return m_target;
}

TargetPressureFieldModel::PressureFieldRegime
TargetPressureFieldModel::regime() const
{
    ensureConfigured();
    return selectRegime();
}

TargetPressureFieldModel::PressureFieldSample
TargetPressureFieldModel::calculate(
    const Vector3& observationPosition) const
{
    // 未指定时刻时保持与其他场模型一致，统一计算零时刻结果。
    return calculate(observationPosition, 0.0);
}

TargetPressureFieldModel::PressureFieldSample
TargetPressureFieldModel::calculate(
    const Vector3& observationPosition,
    double timeSeconds) const
{
    // 检查模型是否已经配置。
    ensureConfigured();
    // 检查输入是否为正常有限数值。
    if (!isFiniteVector(observationPosition) || !isFinite(timeSeconds))
    {
        throw std::invalid_argument("观测点坐标和仿真时间必须为有限数值");
    }
    // 检查观测点是否位于有效水体范围内。
    if (observationPosition.z > 0.0 ||
        observationPosition.z < -m_target.waterDepth)
    {
        throw std::domain_error("观测点必须位于静水面与海床之间");
    }

    // 计算航向单位矢量和当前时刻目标位置。
    const double heading = degreesToRadians(m_target.headingDegrees);
    const double headingX = std::cos(heading);
    const double headingY = std::sin(heading);
    const double traveledDistance = m_target.velocity * timeSeconds;
    const Vector3 targetPosition{
        m_target.initialPosition.x + headingX * traveledDistance,
        m_target.initialPosition.y + headingY * traveledDistance,
        m_target.initialPosition.z};
    if (!isFiniteVector(targetPosition))
    {
        throw std::invalid_argument("目标运动位置超出可计算数值范围");
    }
    // 计算目标到观测点的相对位置及目标坐标系分量。
    const Vector3 displacement{
        observationPosition.x - targetPosition.x,
        observationPosition.y - targetPosition.y,
        observationPosition.z - targetPosition.z};
    const Vector3 bodyDisplacement =
        globalToBody(displacement, m_target.headingDegrees);
    const double distance = magnitude(displacement);

    // 使用按类型解释后的垂向尺度判断观测点是否进入等效回转体内部。
    const double a = m_target.length * 0.5;
    const double b = m_target.width * 0.5;
    const double c = effectiveVerticalSize() * 0.5;
    const double ellipsoidCoordinate =
        bodyDisplacement.x * bodyDisplacement.x / (a * a) +
        bodyDisplacement.y * bodyDisplacement.y / (b * b) +
        bodyDisplacement.z * bodyDisplacement.z / (c * c);
    // 小于等于 1 表示观测点位于椭球内部或表面，偶极子近似不再适用。
    if (ellipsoidCoordinate <= 1.0)
    {
        throw std::domain_error("观测点不能位于目标等效排水体内部");
    }
    // 检查观测距离是否超过点偶极子模型的最小适用距离。
    if (!isFinite(distance) || distance < m_target.minimumDistance)
    {
        throw std::domain_error("观测点距离目标中心过近，超出势流偶极子模型适用范围");
    }
    const PressureFieldRegime selectedRegime = selectRegime();
    switch (selectedRegime)
    {
    case PressureFieldRegime::UnsupportedSurfaceShipDeepWaterWave:
        throw std::domain_error(
            "当前水面舰艇工况已超过低速范围，但 H/L 大于浅水阈值，深水兴波模型尚未实现");
    case PressureFieldRegime::UnsupportedSurfaceShipCritical:
        throw std::domain_error(
            "当前水面舰艇水深弗劳德数位于临界区，浅水亚临界近似不适用");
    case PressureFieldRegime::UnsupportedSurfaceShipSupercritical:
        throw std::domain_error(
            "当前水面舰艇属于浅水超临界工况，超临界模型尚未实现");
    case PressureFieldRegime::UnsupportedSubmarineFreeSurfaceWave:
        throw std::domain_error(
            "当前潜艇近水面高速航行的自由液面兴波不可忽略，对应模型尚未实现");
    default:
        break;
    }

    // 计算动态压力公共系数 rho*q*U^2。
    const double pressureCoefficient =
        m_target.waterDensity * m_dipoleStrengthPerSpeed *
        m_target.velocity * m_target.velocity;

    PressureFieldSample sample{};
    sample.observationPosition = observationPosition;
    sample.targetPosition = targetPosition;
    sample.time = timeSeconds;
    sample.distance = distance;
    sample.longitudinalOffset = bodyDisplacement.x;
    sample.lateralOffset = bodyDisplacement.y;
    sample.observationDepth = -observationPosition.z;
    sample.regime = selectedRegime;
    sample.lengthFroudeNumber = lengthFroudeNumber();
    sample.depthFroudeNumber = depthFroudeNumber();
    sample.waveAttenuation = submarineWaveAttenuation();
    if (selectedRegime == PressureFieldRegime::SurfaceShipTransition)
    {
        sample.transitionWeight = transitionWeight();
    }
    else if (selectedRegime ==
             PressureFieldRegime::SurfaceShipShallowSubcritical)
    {
        sample.transitionWeight = 1.0;
    }
    sample.hydrostaticPressure =
        m_target.waterDensity * m_target.gravityAcceleration *
        sample.observationDepth;

    // 本体压力始终由目标真实位置处的三维偶极子给出。
    sample.bodyDynamicPressure = calculateImagePressure(
        observationPosition,
        targetPosition,
        headingX,
        headingY,
        m_target.minimumDistance,
        pressureCoefficient);

    const Vector3 surfaceImage{
        targetPosition.x,
        targetPosition.y,
        -targetPosition.z};
    const Vector3 seabedImage{
        targetPosition.x,
        targetPosition.y,
        -2.0 * m_target.waterDepth - targetPosition.z};

    if (selectedRegime == PressureFieldRegime::SurfaceShipLowSpeed ||
        (selectedRegime == PressureFieldRegime::SubmarineNoWave &&
         sample.lengthFroudeNumber <=
             m_target.modelOptions.lowSpeedLengthFroudeThreshold))
    {
        // 低速时采用刚盖自由液面；其余周期镜像统一归入海床边界修正。
        sample.freeSurfaceCorrectionPressure = calculateImagePressure(
            observationPosition,
            surfaceImage,
            headingX,
            headingY,
            m_target.minimumDistance,
            pressureCoefficient);
        const double rigidLidTotalPressure = calculateRigidLidTotalPressure(
            observationPosition,
            targetPosition,
            m_target.waterDepth,
            headingX,
            headingY,
            m_target.minimumDistance,
            pressureCoefficient);
        sample.seabedCorrectionPressure =
            rigidLidTotalPressure -
            sample.bodyDynamicPressure -
            sample.freeSurfaceCorrectionPressure;
    }
    else if (selectedRegime == PressureFieldRegime::SurfaceShipTransition ||
             selectedRegime ==
                 PressureFieldRegime::SurfaceShipShallowSubcritical)
    {
        // 先计算浅水亚临界模型的自由液面兴波和一阶海床修正。
        const double shallowWavePressure =
            calculateShallowSubcriticalWavePressure(
            sample.longitudinalOffset,
            sample.lateralOffset,
            m_target.width,
            m_target.waterDepth,
            m_target.minimumDistance,
            sample.depthFroudeNumber,
            m_target.waterDensity,
            m_dipoleStrengthPerSpeed,
            m_target.velocity);
        const double shallowSeabedPressure = calculateImagePressure(
            observationPosition,
            seabedImage,
            headingX,
            headingY,
            m_target.minimumDistance,
            pressureCoefficient);

        if (selectedRegime == PressureFieldRegime::SurfaceShipTransition)
        {
            // 过渡带只混合边界项，公共本体压力保持一次计算，避免重复计入。
            const double lowSurfacePressure = calculateImagePressure(
                observationPosition,
                surfaceImage,
                headingX,
                headingY,
                m_target.minimumDistance,
                pressureCoefficient);
            const double lowTotalPressure = calculateRigidLidTotalPressure(
                observationPosition,
                targetPosition,
                m_target.waterDepth,
                headingX,
                headingY,
                m_target.minimumDistance,
                pressureCoefficient);
            const double lowSeabedPressure =
                lowTotalPressure -
                sample.bodyDynamicPressure -
                lowSurfacePressure;
            const double lowWeight = 1.0 - sample.transitionWeight;

            sample.wavePressure =
                sample.transitionWeight * shallowWavePressure;
            sample.freeSurfaceCorrectionPressure =
                lowWeight * lowSurfacePressure + sample.wavePressure;
            sample.seabedCorrectionPressure =
                lowWeight * lowSeabedPressure +
                sample.transitionWeight * shallowSeabedPressure;
        }
        else
        {
            // 完全进入浅水亚临界区后只使用兴波自由液面项。
            sample.wavePressure = shallowWavePressure;
            sample.freeSurfaceCorrectionPressure = shallowWavePressure;
            sample.seabedCorrectionPressure = shallowSeabedPressure;
        }
    }
    else
    {
        // 深潜高速但兴波已充分衰减时，仅保留本体和一阶海床镜像。
        sample.seabedCorrectionPressure = calculateImagePressure(
            observationPosition,
            seabedImage,
            headingX,
            headingY,
            m_target.minimumDistance,
            pressureCoefficient);
    }

    sample.dynamicPressure =
        sample.bodyDynamicPressure +
        sample.freeSurfaceCorrectionPressure +
        sample.seabedCorrectionPressure;
    sample.totalGaugePressure =
        sample.hydrostaticPressure + sample.dynamicPressure;
    return sample;
}

std::vector<TargetPressureFieldModel::PressureFieldSample>
TargetPressureFieldModel::simulate(
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
            "观测点坐标必须有限，仿真时长必须非负且采样率必须为有限正数");
    }

    const double rawSampleCount = durationSeconds * sampleRateHz;
    if (!isFinite(rawSampleCount) ||
        rawSampleCount + 1.0 >
            static_cast<double>(kMaximumTimeSampleCount))
    {
        throw std::length_error("请求的水压场时序采样点数量过大");
    }

    const std::size_t sampleCount =
        static_cast<std::size_t>(std::floor(rawSampleCount)) + 1U;
    std::vector<PressureFieldSample> samples;
    samples.reserve(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        // 时序包含起点，并保留持续时间内最后一个完整采样点。
        const double timeSeconds =
            startTimeSeconds + static_cast<double>(index) / sampleRateHz;
        samples.push_back(calculate(observationPosition, timeSeconds));
    }
    return samples;
}

std::vector<TargetPressureFieldModel::PressureFieldSample>
TargetPressureFieldModel::simulateGrid(
    const GridParameter& grid,
    double timeSeconds) const
{
    ensureConfigured();
    validateGrid(grid);
    if (!isFinite(timeSeconds))
    {
        throw std::invalid_argument("水压场网格仿真时刻必须为有限数值");
    }
    if (grid.xCount > kMaximumGridPointCount / grid.yCount ||
        grid.xCount * grid.yCount >
            kMaximumGridPointCount / grid.zCount)
    {
        throw std::length_error("请求的水压场网格点数量过大");
    }
    const std::size_t pointCount =
        grid.xCount * grid.yCount * grid.zCount;

    std::vector<PressureFieldSample> samples;
    samples.reserve(pointCount);
    for (std::size_t zIndex = 0; zIndex < grid.zCount; ++zIndex)
    {
        for (std::size_t yIndex = 0; yIndex < grid.yCount; ++yIndex)
        {
            for (std::size_t xIndex = 0; xIndex < grid.xCount; ++xIndex)
            {
                // 固定使用 z、y、x 展开顺序，便于调用方直接恢复规则网格。
                const Vector3 position{
                    gridCoordinate(
                        grid.minimum.x, grid.maximum.x,
                        grid.xCount, xIndex),
                    gridCoordinate(
                        grid.minimum.y, grid.maximum.y,
                        grid.yCount, yIndex),
                    gridCoordinate(
                        grid.minimum.z, grid.maximum.z,
                        grid.zCount, zIndex)};
                samples.push_back(calculate(position, timeSeconds));
            }
        }
    }
    return samples;
}

double TargetPressureFieldModel::effectiveVerticalSize() const noexcept
{
    // 潜艇新接口优先采用 height，零值时回退到旧版 draft 字段。
    if (m_target.type == TargetType::Submarine && m_target.height > 0.0)
    {
        return m_target.height;
    }
    return m_target.draft;
}

double TargetPressureFieldModel::lengthFroudeNumber() const noexcept
{
    // 参数已在 setTarget 中校验为正数，因此分母不会为零。
    return m_target.velocity /
        std::sqrt(m_target.gravityAcceleration * m_target.length);
}

double TargetPressureFieldModel::depthFroudeNumber() const noexcept
{
    // 水深弗劳德数用于区分浅水亚临界、临界和超临界状态。
    return m_target.velocity /
        std::sqrt(m_target.gravityAcceleration * m_target.waterDepth);
}

double TargetPressureFieldModel::submarineWaveAttenuation() const noexcept
{
    if (m_target.type != TargetType::Submarine)
    {
        return 0.0;
    }
    if (m_target.velocity == 0.0)
    {
        // 静止目标没有运动兴波，直接返回完全衰减。
        return 0.0;
    }

    // 使用艇体中心潜深估算稳态重力波传到自由液面时的指数衰减。
    const double centerDepth = -m_target.initialPosition.z;
    const double exponent =
        -m_target.gravityAcceleration * centerDepth /
        (m_target.velocity * m_target.velocity);
    return std::exp(exponent);
}

TargetPressureFieldModel::PressureFieldRegime
TargetPressureFieldModel::selectRegime() const noexcept
{
    const double lengthFroude = lengthFroudeNumber();
    const double depthFroude = depthFroudeNumber();
    const ModelOptions& options = m_target.modelOptions;

    if (m_target.type == TargetType::Submarine)
    {
        // 低速或深潜导致的兴波衰减任一成立时，均使用无兴波潜艇模型。
        if (lengthFroude <= options.lowSpeedLengthFroudeThreshold ||
            submarineWaveAttenuation() <= options.waveAttenuationTolerance)
        {
            return PressureFieldRegime::SubmarineNoWave;
        }
        return PressureFieldRegime::UnsupportedSubmarineFreeSurfaceWave;
    }

    const double transitionLowerBound =
        options.lowSpeedLengthFroudeThreshold -
        options.lengthFroudeTransitionHalfWidth;
    const double transitionUpperBound =
        options.lowSpeedLengthFroudeThreshold +
        options.lengthFroudeTransitionHalfWidth;
    if (lengthFroude <= transitionLowerBound)
    {
        return PressureFieldRegime::SurfaceShipLowSpeed;
    }

    const double waterDepthToLength =
        m_target.waterDepth / m_target.length;
    if (waterDepthToLength > options.shallowWaterRatioThreshold)
    {
        // 深水兴波模型尚未实现，因此在名义低速阈值内仍保持低速模型。
        if (lengthFroude <= options.lowSpeedLengthFroudeThreshold)
        {
            return PressureFieldRegime::SurfaceShipLowSpeed;
        }
        return PressureFieldRegime::UnsupportedSurfaceShipDeepWaterWave;
    }

    const double lowerCriticalBound =
        1.0 - options.criticalDepthFroudeMargin;
    const double upperCriticalBound =
        1.0 + options.criticalDepthFroudeMargin;
    if (depthFroude < lowerCriticalBound)
    {
        if (lengthFroude < transitionUpperBound)
        {
            return PressureFieldRegime::SurfaceShipTransition;
        }
        return PressureFieldRegime::SurfaceShipShallowSubcritical;
    }
    if (depthFroude <= upperCriticalBound)
    {
        return PressureFieldRegime::UnsupportedSurfaceShipCritical;
    }
    return PressureFieldRegime::UnsupportedSurfaceShipSupercritical;
}

double TargetPressureFieldModel::transitionWeight() const noexcept
{
    const ModelOptions& options = m_target.modelOptions;
    const double lowerBound =
        options.lowSpeedLengthFroudeThreshold -
        options.lengthFroudeTransitionHalfWidth;
    const double upperBound =
        options.lowSpeedLengthFroudeThreshold +
        options.lengthFroudeTransitionHalfWidth;
    const double normalized = std::clamp(
        (lengthFroudeNumber() - lowerBound) /
            (upperBound - lowerBound),
        0.0,
        1.0);

    // 三次平滑函数在过渡带两端的一阶导数均为零。
    return normalized * normalized * (3.0 - 2.0 * normalized);
}

void TargetPressureFieldModel::validateTarget(
    const TargetParameter& param)
{
    const double verticalSize =
        param.type == TargetType::Submarine && param.height > 0.0
            ? param.height
            : param.draft;
    if (!isFinite(param.length) ||
        !isFinite(param.width) ||
        !isFinite(param.draft) ||
        !isFinite(param.height) ||
        param.length <= 0.0 ||
        param.width <= 0.0 ||
        (param.type == TargetType::SurfaceShip && param.draft <= 0.0) ||
        (param.type == TargetType::Submarine &&
         param.draft <= 0.0 && param.height <= 0.0) ||
        param.height < 0.0 ||
        param.length < param.width ||
        param.length < verticalSize)
    {
        throw std::invalid_argument(
            "目标长度、宽度和垂向尺度必须合法，且长度不能小于宽度或垂向尺度");
    }
    if (!isFinite(param.waterDepth) ||
        param.waterDepth <= 0.0 ||
        param.waterDepth < verticalSize)
    {
        throw std::invalid_argument("海深必须为有限正数且不小于目标垂向尺度");
    }
    if (!isFiniteVector(param.initialPosition) ||
        !isFinite(param.velocity) ||
        param.velocity < 0.0 ||
        !isFinite(param.headingDegrees))
    {
        throw std::invalid_argument("目标初始位置、非负航速和航向角必须为有限数值");
    }
    if (!isFinite(param.waterDensity) ||
        param.waterDensity <= 0.0 ||
        !isFinite(param.gravityAcceleration) ||
        param.gravityAcceleration <= 0.0)
    {
        throw std::invalid_argument("海水密度和重力加速度必须为有限正数");
    }
    if (!isFinite(param.blockCoefficient) ||
        param.blockCoefficient <= 0.0 ||
        param.blockCoefficient > 1.0)
    {
        throw std::invalid_argument("等效排水体积方形系数必须在(0,1]范围内");
    }
    if (!isFinite(param.minimumDistance) ||
        param.minimumDistance <= 0.0)
    {
        throw std::invalid_argument("最小观测距离必须为有限正数");
    }

    const ModelOptions& options = param.modelOptions;
    if (!isFinite(options.lowSpeedLengthFroudeThreshold) ||
        options.lowSpeedLengthFroudeThreshold <= 0.0 ||
        options.lowSpeedLengthFroudeThreshold >= 1.0)
    {
        throw std::invalid_argument("低速船长弗劳德数阈值必须位于(0,1)范围内");
    }
    if (!isFinite(options.lengthFroudeTransitionHalfWidth) ||
        options.lengthFroudeTransitionHalfWidth <= 0.0 ||
        options.lengthFroudeTransitionHalfWidth >=
            options.lowSpeedLengthFroudeThreshold ||
        options.lowSpeedLengthFroudeThreshold +
            options.lengthFroudeTransitionHalfWidth >= 1.0)
    {
        throw std::invalid_argument(
            "船长弗劳德数过渡带半宽必须为正，且过渡带必须完整位于(0,1)范围内");
    }
    if (!isFinite(options.shallowWaterRatioThreshold) ||
        options.shallowWaterRatioThreshold <= 0.0 ||
        options.shallowWaterRatioThreshold > 1.0)
    {
        throw std::invalid_argument("浅水 H/L 阈值必须位于(0,1]范围内");
    }
    if (!isFinite(options.criticalDepthFroudeMargin) ||
        options.criticalDepthFroudeMargin <= 0.0 ||
        options.criticalDepthFroudeMargin >= 1.0)
    {
        throw std::invalid_argument("水深弗劳德数临界区半宽必须位于(0,1)范围内");
    }
    if (!isFinite(options.waveAttenuationTolerance) ||
        options.waveAttenuationTolerance <= 0.0 ||
        options.waveAttenuationTolerance > 1.0)
    {
        throw std::invalid_argument("潜艇兴波衰减阈值必须位于(0,1]范围内");
    }

    if (param.type == TargetType::SurfaceShip)
    {
        // 水面舰艇等效回转体中心固定在半吃水处，使顶部与静水面相切。
        const double expectedCenterZ = -param.draft * 0.5;
        const double waterlineTolerance =
            std::max(1.0e-8, param.draft * 1.0e-8);
        if (std::abs(param.initialPosition.z - expectedCenterZ) >
            waterlineTolerance)
        {
            throw std::invalid_argument(
                "水面舰艇等效回转体中心 z 坐标必须等于 -draft/2");
        }
    }
    else
    {
        // 潜艇必须完整浸没，并且不能穿过海床。
        const double targetTop =
            param.initialPosition.z + verticalSize * 0.5;
        const double targetBottom =
            param.initialPosition.z - verticalSize * 0.5;
        if (targetTop > 0.0 || targetBottom < -param.waterDepth)
        {
            throw std::invalid_argument(
                "潜艇等效回转体必须完整位于静水面与海床之间");
        }
    }
}

void TargetPressureFieldModel::validateGrid(
    const GridParameter& grid) const
{
    if (!isFiniteVector(grid.minimum) ||
        !isFiniteVector(grid.maximum))
    {
        throw std::invalid_argument("水压场网格边界必须为有限数值");
    }
    if (grid.minimum.x > grid.maximum.x ||
        grid.minimum.y > grid.maximum.y ||
        grid.minimum.z > grid.maximum.z)
    {
        throw std::invalid_argument("水压场网格最小坐标不能大于最大坐标");
    }
    if (grid.minimum.z < -m_target.waterDepth ||
        grid.maximum.z > 0.0)
    {
        throw std::invalid_argument("水压场网格必须完整位于静水面与海床之间");
    }
    if (grid.xCount == 0U ||
        grid.yCount == 0U ||
        grid.zCount == 0U)
    {
        throw std::invalid_argument("水压场网格各方向采样点数必须大于0");
    }
}

void TargetPressureFieldModel::rebuildHydrodynamicParameters()
{
    const double verticalSize = effectiveVerticalSize();
    // 根据水面舰艇吃水或潜艇高度计算等效排水体积。
    const double displacedVolume =
        m_target.blockCoefficient *
        m_target.length * m_target.width * verticalSize;
    // 使用细长回转体纵向因子修正单位航速偶极子强度。
    const double longitudinalFactor = calculateLongitudinalFactor(
        m_target.length, m_target.width, verticalSize);

    // 以球形精确解为基准，将纵向附加质量因子归一化为球形时等于 1。
    const double shapeFactor =
        2.0 * longitudinalFactor / (1.0 - longitudinalFactor);
    // 得到单位航速偶极子强度。
    m_dipoleStrengthPerSpeed =
        (3.0 / (8.0 * kPi)) * displacedVolume * shapeFactor;
}

void TargetPressureFieldModel::ensureConfigured() const
{
    if (!m_configured)
    {
        throw std::logic_error("水压场模型尚未配置目标参数");
    }
}

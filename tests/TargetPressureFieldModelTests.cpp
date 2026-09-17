#include "TargetPressureFieldModel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
/** 检查测试条件，失败时抛出带中文说明的异常。 */
void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/** 判断两个浮点数是否满足给定的相对误差要求。 */
bool nearlyEqual(double lhs,
                 double rhs,
                 double relativeTolerance = 1.0e-9)
{
    const double scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= relativeTolerance * scale;
}

/** 创建一组水面舰艇水压场测试共享的合法参数。 */
TargetPressureFieldModel::TargetParameter createValidParameter()
{
    TargetPressureFieldModel::TargetParameter param{};
    param.type = TargetPressureFieldModel::TargetType::SurfaceShip;
    param.length = 120.0;
    param.width = 18.0;
    param.draft = 6.0;
    param.waterDepth = 60.0;
    param.velocity = 3.0;
    param.initialPosition = {-160.0, 20.0, -3.0};
    param.headingDegrees = 0.0;
    param.blockCoefficient = 0.68;
    param.minimumDistance = 1.0;
    return param;
}

/** 验证静水压力、动态压力和总表压的基本关系。 */
void testPressureComponents()
{
    const TargetPressureFieldModel model(createValidParameter());
    const auto sample = model.calculate({0.0, 0.0, -30.0});

    require(std::isfinite(sample.dynamicPressure),
            "动态压力必须为有限值");
    require(sample.hydrostaticPressure > 0.0,
            "水下观测点的静水压力必须大于0");
    require(nearlyEqual(
                sample.totalGaugePressure,
                sample.hydrostaticPressure + sample.dynamicPressure),
            "总表压必须等于静水压力与动态压力之和");
    require(nearlyEqual(
                sample.dynamicPressure,
                sample.bodyDynamicPressure +
                    sample.freeSurfaceCorrectionPressure +
                    sample.seabedCorrectionPressure),
            "动态压力必须等于本体、自由液面和海床修正分量之和");
    require(nearlyEqual(sample.wavePressure, 0.0),
            "低速回转体工况不应生成兴波压力分量");
    require(nearlyEqual(sample.observationDepth, 30.0),
            "观测点水深计算不正确");
}

/** 验证静止目标不产生运动动态压力。 */
void testStationaryTarget()
{
    auto param = createValidParameter();
    param.velocity = 0.0;
    const TargetPressureFieldModel model(param);
    const auto sample = model.calculate({0.0, 0.0, -30.0});

    require(nearlyEqual(sample.dynamicPressure, 0.0),
            "静止目标的动态压力应为0");
    require(nearlyEqual(
                sample.totalGaugePressure, sample.hydrostaticPressure),
            "静止目标总表压应等于静水压力");
}

/** 验证势流模型的动态压力与航速平方成正比。 */
void testVelocitySquaredScaling()
{
    auto slowParam = createValidParameter();
    slowParam.velocity = 1.5;
    const TargetPressureFieldModel slowModel(slowParam);
    const double slowPressure =
        slowModel.calculate({0.0, 0.0, -30.0}).dynamicPressure;

    auto fastParam = slowParam;
    fastParam.velocity = 3.0;
    const TargetPressureFieldModel fastModel(fastParam);
    const double fastPressure =
        fastModel.calculate({0.0, 0.0, -30.0}).dynamicPressure;

    require(nearlyEqual(fastPressure / slowPressure, 4.0, 1.0e-8),
            "动态压力没有按航速平方规律变化");
}

/** 验证海深通过边界镜像项改变动态压力结果。 */
void testWaterDepthSensitivity()
{
    auto shallowParam = createValidParameter();
    shallowParam.waterDepth = 40.0;
    const TargetPressureFieldModel shallowModel(shallowParam);
    const double shallowPressure =
        shallowModel.calculate({0.0, 0.0, -30.0}).dynamicPressure;

    auto deepParam = createValidParameter();
    deepParam.waterDepth = 120.0;
    const TargetPressureFieldModel deepModel(deepParam);
    const double deepPressure =
        deepModel.calculate({0.0, 0.0, -30.0}).dynamicPressure;

    require(!nearlyEqual(shallowPressure, deepPressure, 1.0e-5),
            "改变海深后动态压力应发生变化");
}

/** 验证完全潜航的潜艇目标可以在指定潜深生成水压场。 */
void testSubmarineTarget()
{
    auto param = createValidParameter();
    param.type = TargetPressureFieldModel::TargetType::Submarine;
    param.length = 80.0;
    param.width = 10.0;
    param.draft = 0.0;
    param.height = 10.0;
    param.waterDepth = 100.0;
    param.initialPosition = {-100.0, 0.0, -35.0};
    param.blockCoefficient = 0.55;
    const TargetPressureFieldModel model(param);
    const auto sample = model.calculate({0.0, 20.0, -80.0});

    require(std::isfinite(sample.dynamicPressure),
            "潜艇目标动态压力必须为有限值");
    require(nearlyEqual(sample.targetPosition.z, -35.0),
            "潜艇目标应保持设定潜深");
    require(sample.regime ==
                TargetPressureFieldModel::PressureFieldRegime::SubmarineNoWave,
            "深潜潜艇应选择无兴波工况");
    require(nearlyEqual(sample.wavePressure, 0.0),
            "深潜潜艇不应生成自由液面兴波压力");
}

/** 验证默认浅水判据为 H/L 不超过 0.3，且边界值进入亚临界模型。 */
void testShallowSubcriticalRegime()
{
    auto param = createValidParameter();
    param.waterDepth = 36.0;
    param.velocity = 8.0;
    const TargetPressureFieldModel model(param);

    require(nearlyEqual(
                param.modelOptions.shallowWaterRatioThreshold, 0.30),
            "默认浅水 H/L 阈值必须为0.3");
    require(model.regime() ==
                TargetPressureFieldModel::PressureFieldRegime::
                    SurfaceShipShallowSubcritical,
            "H/L 等于0.3的较高速舰艇应选择浅水亚临界模型");

    const auto sample = model.calculate({0.0, 20.0, -25.0});
    require(sample.lengthFroudeNumber >
                param.modelOptions.lowSpeedLengthFroudeThreshold,
            "浅水亚临界测试航速必须超过低速阈值");
    require(sample.depthFroudeNumber < 1.0,
            "浅水亚临界测试的水深弗劳德数必须小于1");
    require(!nearlyEqual(sample.wavePressure, 0.0),
            "浅水亚临界模型必须生成独立的兴波压力分量");
    require(nearlyEqual(
                sample.freeSurfaceCorrectionPressure,
                sample.wavePressure),
            "浅水亚临界自由液面修正必须来自兴波压力");
}

/** 验证 FrL=0.08～0.12 过渡带按三次平滑权重连续混合两套边界模型。 */
void testSurfaceShipTransitionRegime()
{
    auto middleParam = createValidParameter();
    middleParam.waterDepth = 36.0;
    middleParam.velocity =
        middleParam.modelOptions.lowSpeedLengthFroudeThreshold *
        std::sqrt(middleParam.gravityAcceleration * middleParam.length);
    const TargetPressureFieldModel middleModel(middleParam);

    require(nearlyEqual(
                middleParam.modelOptions.lengthFroudeTransitionHalfWidth,
                0.02),
            "默认船长弗劳德数过渡带半宽必须为0.02");
    require(middleModel.regime() ==
                TargetPressureFieldModel::PressureFieldRegime::
                    SurfaceShipTransition,
            "FrL=0.10的浅水亚临界舰艇必须进入平滑过渡工况");

    const TargetPressureFieldModel::Vector3 observation{0.0, 20.0, -25.0};
    const auto middleSample = middleModel.calculate(observation);
    require(nearlyEqual(middleSample.transitionWeight, 0.5, 1.0e-12),
            "过渡带中心的兴波模型权重必须为0.5");
    require(!nearlyEqual(middleSample.wavePressure, 0.0),
            "过渡带中心必须包含加权后的兴波压力");

    const double transitionCenter =
        middleParam.modelOptions.lowSpeedLengthFroudeThreshold;
    const double transitionHalfWidth =
        middleParam.modelOptions.lengthFroudeTransitionHalfWidth;
    const double speedScale =
        std::sqrt(middleParam.gravityAcceleration * middleParam.length);

    auto lowerEdgeParam = middleParam;
    lowerEdgeParam.velocity =
        (transitionCenter - transitionHalfWidth) * speedScale;
    const TargetPressureFieldModel lowerEdgeModel(lowerEdgeParam);
    const auto lowerEdgeSample = lowerEdgeModel.calculate(observation);

    auto justAboveLowerParam = middleParam;
    justAboveLowerParam.velocity =
        (transitionCenter - transitionHalfWidth + 1.0e-6) * speedScale;
    const TargetPressureFieldModel justAboveLowerModel(justAboveLowerParam);
    const auto justAboveLowerSample =
        justAboveLowerModel.calculate(observation);
    require(nearlyEqual(
                lowerEdgeSample.dynamicPressure,
                justAboveLowerSample.dynamicPressure,
                1.0e-4),
            "低速模型与过渡带下边界的动态压力必须连续");

    auto justBelowUpperParam = middleParam;
    justBelowUpperParam.velocity =
        (transitionCenter + transitionHalfWidth - 1.0e-6) * speedScale;
    const TargetPressureFieldModel justBelowUpperModel(justBelowUpperParam);
    const auto justBelowUpperSample =
        justBelowUpperModel.calculate(observation);

    auto upperEdgeParam = middleParam;
    upperEdgeParam.velocity =
        (transitionCenter + transitionHalfWidth) * speedScale;
    const TargetPressureFieldModel upperEdgeModel(upperEdgeParam);
    const auto upperEdgeSample = upperEdgeModel.calculate(observation);
    require(nearlyEqual(
                justBelowUpperSample.dynamicPressure,
                upperEdgeSample.dynamicPressure,
                1.0e-4),
            "过渡带上边界与浅水兴波模型的动态压力必须连续");
}

/** 验证尚未覆盖的深水兴波、临界和超临界工况会被明确识别。 */
void testUnsupportedSurfaceShipRegimes()
{
    auto deepWaterParam = createValidParameter();
    deepWaterParam.velocity = 8.0;
    const TargetPressureFieldModel deepWaterModel(deepWaterParam);
    require(deepWaterModel.regime() ==
                TargetPressureFieldModel::PressureFieldRegime::
                    UnsupportedSurfaceShipDeepWaterWave,
            "较高速深水舰艇必须识别为尚未支持的深水兴波工况");

    bool rejected = false;
    try
    {
        static_cast<void>(deepWaterModel.calculate({0.0, 0.0, -30.0}));
    }
    catch (const std::domain_error&)
    {
        rejected = true;
    }
    require(rejected, "尚未支持的深水兴波工况必须拒绝计算");

    auto criticalParam = createValidParameter();
    criticalParam.waterDepth = 30.0;
    criticalParam.velocity = std::sqrt(
        criticalParam.gravityAcceleration * criticalParam.waterDepth);
    const TargetPressureFieldModel criticalModel(criticalParam);
    require(criticalModel.regime() ==
                TargetPressureFieldModel::PressureFieldRegime::
                    UnsupportedSurfaceShipCritical,
            "FrH 等于1的舰艇必须识别为临界工况");

    auto supercriticalParam = criticalParam;
    supercriticalParam.velocity *= 1.10;
    const TargetPressureFieldModel supercriticalModel(supercriticalParam);
    require(supercriticalModel.regime() ==
                TargetPressureFieldModel::PressureFieldRegime::
                    UnsupportedSurfaceShipSupercritical,
            "FrH 超过临界区的舰艇必须识别为超临界工况");
}

/** 验证近水面高速潜艇不会误用无兴波回转体模型。 */
void testUnsupportedSubmarineWaveRegime()
{
    auto param = createValidParameter();
    param.type = TargetPressureFieldModel::TargetType::Submarine;
    param.length = 80.0;
    param.width = 10.0;
    param.draft = 0.0;
    param.height = 10.0;
    param.waterDepth = 100.0;
    param.velocity = 8.0;
    param.initialPosition = {-100.0, 0.0, -6.0};
    const TargetPressureFieldModel model(param);

    require(model.regime() ==
                TargetPressureFieldModel::PressureFieldRegime::
                    UnsupportedSubmarineFreeSurfaceWave,
            "近水面高速潜艇必须识别为自由液面兴波工况");

    bool rejected = false;
    try
    {
        static_cast<void>(model.calculate({0.0, 20.0, -40.0}));
    }
    catch (const std::domain_error&)
    {
        rejected = true;
    }
    require(rejected, "尚未实现的潜艇自由液面兴波工况必须拒绝计算");
}

/** 验证运动时序的采样数量、时间和目标位置。 */
void testMotionAndSampling()
{
    auto param = createValidParameter();
    param.initialPosition = {-80.0, 10.0, -3.0};
    param.velocity = 3.0;
    const TargetPressureFieldModel model(param);
    const auto samples = model.simulate(
        {0.0, 0.0, -30.0}, 2.0, 4.0, 2.0);

    require(samples.size() == 9U,
            "4秒、2Hz的闭区间时序应包含9个采样点");
    require(nearlyEqual(samples.front().time, 2.0) &&
                nearlyEqual(samples.back().time, 6.0),
            "时序起止时刻不正确");
    require(nearlyEqual(samples.front().targetPosition.x, -74.0) &&
                nearlyEqual(samples.back().targetPosition.x, -62.0),
            "目标匀速运动位置不正确");
}

/** 验证规则空间网格的数量和展开顺序。 */
void testSpatialGrid()
{
    const TargetPressureFieldModel model(createValidParameter());
    TargetPressureFieldModel::GridParameter grid{};
    grid.minimum = {0.0, -20.0, -40.0};
    grid.maximum = {20.0, 20.0, -30.0};
    grid.xCount = 3U;
    grid.yCount = 2U;
    grid.zCount = 2U;
    const auto samples = model.simulateGrid(grid, 0.0);

    require(samples.size() == 12U,
            "3×2×2水压场网格应包含12个采样点");
    require(nearlyEqual(samples.front().observationPosition.x, 0.0) &&
                nearlyEqual(samples.front().observationPosition.y, -20.0) &&
                nearlyEqual(samples.front().observationPosition.z, -40.0),
            "网格首点坐标不正确");
    require(nearlyEqual(samples.back().observationPosition.x, 20.0) &&
                nearlyEqual(samples.back().observationPosition.y, 20.0) &&
                nearlyEqual(samples.back().observationPosition.z, -30.0),
            "网格末点坐标不正确");
}

/** 验证非法目标、观测点和仿真参数会被拒绝。 */
void testValidation()
{
    bool rejected = false;
    auto invalidParam = createValidParameter();
    invalidParam.waterDepth = 2.0;
    try
    {
        const TargetPressureFieldModel model(invalidParam);
        static_cast<void>(model);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "小于目标吃水的海深必须被拒绝");

    rejected = false;
    auto invalidTransitionParam = createValidParameter();
    invalidTransitionParam.modelOptions.lengthFroudeTransitionHalfWidth =
        invalidTransitionParam.modelOptions.lowSpeedLengthFroudeThreshold;
    try
    {
        const TargetPressureFieldModel invalidTransitionModel(
            invalidTransitionParam);
        static_cast<void>(invalidTransitionModel);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "越过零点的船长弗劳德数过渡带必须被拒绝");

    const TargetPressureFieldModel model(createValidParameter());
    rejected = false;
    try
    {
        static_cast<void>(model.calculate({0.0, 0.0, 1.0}));
    }
    catch (const std::domain_error&)
    {
        rejected = true;
    }
    require(rejected, "静水面以上的观测点必须被拒绝");

    rejected = false;
    try
    {
        static_cast<void>(model.simulate(
            {0.0, 0.0, -30.0}, 0.0, 10.0, 0.0));
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "非正采样率必须被拒绝");
}
} // 匿名命名空间

/**
 * @brief 依次运行全部水压场模型测试。
 * @return 全部通过时返回 EXIT_SUCCESS，否则返回 EXIT_FAILURE。
 */
int main()
{
    try
    {
        testPressureComponents();
        testStationaryTarget();
        testVelocitySquaredScaling();
        testWaterDepthSensitivity();
        testSubmarineTarget();
        testShallowSubcriticalRegime();
        testSurfaceShipTransitionRegime();
        testUnsupportedSurfaceShipRegimes();
        testUnsupportedSubmarineWaveRegime();
        testMotionAndSampling();
        testSpatialGrid();
        testValidation();
        std::cout << "TargetPressureFieldModel 全部测试通过\n";
    }
    catch (const std::exception& exception)
    {
        std::cerr << "TargetPressureFieldModel 测试失败："
                  << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

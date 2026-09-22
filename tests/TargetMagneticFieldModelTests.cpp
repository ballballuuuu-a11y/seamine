#include "TargetMagneticFieldModel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
bool nearlyEqual(double lhs, double rhs, double relativeTolerance = 1e-9)
{
    const double scale = std::max({1.0, std::abs(lhs), std::abs(rhs)});
    return std::abs(lhs - rhs) <= relativeTolerance * scale;
}

/** 创建一组舰船磁场测试共享的合法参数。 */
TargetMagneticFieldModel::TargetParameter createValidParameter()
{
    TargetMagneticFieldModel::TargetParameter param{};
    param.type = TargetMagneticFieldModel::TargetType::SurfaceShip;
    param.length = 120.0;
    param.width = 18.0;
    param.height = 12.0;
    param.center = {0.0, 0.0, -6.0};
    param.geomagneticFieldNt = {18000.0, 30000.0, -42000.0};
    param.relativePermeability = 180.0;
    param.magneticMaterialRatio = 0.035;
    param.remanentMagnetizationAm = {7.5, 0.8, -0.4};
    param.minimumDistance = 2.0;
    return param;
}

/** 验证单点结果同时包含有限的静磁场、感应磁场和综合磁场三分量。 */
void testThreeComponentCalculation()
{
    const TargetMagneticFieldModel model(createValidParameter());
    const auto sample = model.calculate({80.0, 45.0, -35.0});

    require(std::isfinite(sample.staticFieldVector.x) &&
                std::isfinite(sample.staticFieldVector.y) &&
                std::isfinite(sample.staticFieldVector.z),
            "静磁场三分量必须为有限值");
    require(std::isfinite(sample.inducedFieldVector.x) &&
                std::isfinite(sample.inducedFieldVector.y) &&
                std::isfinite(sample.inducedFieldVector.z),
            "感应磁场三分量必须为有限值");
    require(sample.staticField > 0.0, "静磁场模值必须大于0");
    require(sample.inducedField > 0.0, "感应磁场模值必须大于0");
    require(nearlyEqual(sample.totalFieldVector.x,
                        sample.staticFieldVector.x + sample.inducedFieldVector.x),
            "综合磁场 x 分量叠加不正确");
    require(nearlyEqual(sample.totalFieldVector.y,
                        sample.staticFieldVector.y + sample.inducedFieldVector.y),
            "综合磁场 y 分量叠加不正确");
    require(nearlyEqual(sample.totalFieldVector.z,
                        sample.staticFieldVector.z + sample.inducedFieldVector.z),
            "综合磁场 z 分量叠加不正确");
}

/** 验证地磁场为零时不会产生感应磁场。 */
void testZeroGeomagneticField()
{
    auto param = createValidParameter();
    param.geomagneticFieldNt = {0.0, 0.0, 0.0};
    const TargetMagneticFieldModel model(param);
    const auto sample = model.calculate({100.0, 20.0, -30.0});

    require(nearlyEqual(sample.inducedField, 0.0),
            "零地磁场下感应磁场应为零");
    require(nearlyEqual(sample.totalField, sample.staticField),
            "零地磁场下综合磁场应等于静磁场");
}

/** 验证磁偶极子静磁场按距离三次方反比衰减。 */
void testInverseCubeAttenuation()
{
    auto param = createValidParameter();
    param.center = {0.0, 0.0, 0.0};
    param.geomagneticFieldNt = {0.0, 0.0, 0.0};
    param.remanentMagnetizationAm = {8.0, 0.0, 0.0};
    // 关闭局部修整后，纯椭球宏观总磁矩应严格保持偶极子远场规律。
    param.localCorrectionStrength = 0.0;
    const TargetMagneticFieldModel model(param);

    const auto nearSample = model.calculate({100.0, 0.0, 0.0});
    const auto farSample = model.calculate({200.0, 0.0, 0.0});
    require(nearlyEqual(nearSample.staticField / farSample.staticField, 8.0),
            "静磁场没有按距离三次方反比衰减");
}

/** 验证阵列矩阵只修整局部场，并在远场收敛到椭球宏观总磁矩。 */
void testDipoleArrayMatrixCorrection()
{
    auto param = createValidParameter();
    param.center = {0.0, 0.0, 0.0};
    param.geomagneticFieldNt = {0.0, 0.0, 0.0};
    param.remanentMagnetizationAm = {8.0, 1.0, -0.5};
    const TargetMagneticFieldModel model(param);

    const auto nearSample = model.calculate({80.0, 25.0, -20.0});
    require(nearSample.dipoleArrayNodeCount > 1U,
            "默认多偶极子阵列没有生成多个有效节点");
    require(std::hypot(nearSample.localCorrectionFieldVector.x,
                       nearSample.localCorrectionFieldVector.y,
                       nearSample.localCorrectionFieldVector.z) > 0.0,
            "多偶极子阵列没有产生局部场修整量");
    require(nearlyEqual(nearSample.totalFieldVector.x,
                        nearSample.macroFieldVector.x +
                            nearSample.localCorrectionFieldVector.x) &&
                nearlyEqual(nearSample.totalFieldVector.y,
                            nearSample.macroFieldVector.y +
                                nearSample.localCorrectionFieldVector.y) &&
                nearlyEqual(nearSample.totalFieldVector.z,
                            nearSample.macroFieldVector.z +
                                nearSample.localCorrectionFieldVector.z),
            "宏观场与局部修整量的矩阵合成不正确");

    const auto farSample = model.calculate({5000.0, 1700.0, -900.0});
    const double farMacro = std::hypot(farSample.macroFieldVector.x,
        farSample.macroFieldVector.y, farSample.macroFieldVector.z);
    const double farCorrection = std::hypot(farSample.localCorrectionFieldVector.x,
        farSample.localCorrectionFieldVector.y,
        farSample.localCorrectionFieldVector.z);
    require(farCorrection / farMacro < 1.0e-3,
            "多偶极子局部修整量没有在远场收敛到椭球宏观场");
}

/** 验证目标尺寸和航向变化会改变地磁感应三分量结果。 */
void testGeometryAndHeadingSensitivity()
{
    auto baseParam = createValidParameter();
    baseParam.remanentMagnetizationAm = {0.0, 0.0, 0.0};
    baseParam.center = {0.0, 0.0, 0.0};
    const TargetMagneticFieldModel baseModel(baseParam);
    const auto baseSample = baseModel.calculate({180.0, 70.0, -50.0});

    auto longerParam = baseParam;
    longerParam.length *= 1.5;
    const TargetMagneticFieldModel longerModel(longerParam);
    const auto longerSample = longerModel.calculate({180.0, 70.0, -50.0});
    require(!nearlyEqual(longerSample.inducedField, baseSample.inducedField, 1e-4),
            "目标长度变化没有影响感应磁场");

    auto rotatedParam = baseParam;
    rotatedParam.headingDegrees = 90.0;
    const TargetMagneticFieldModel rotatedModel(rotatedParam);
    const auto rotatedSample = rotatedModel.calculate({180.0, 70.0, -50.0});
    require(!nearlyEqual(rotatedSample.inducedFieldVector.x,
                         baseSample.inducedFieldVector.x,
                         1e-4),
            "目标航向变化没有影响感应磁场三分量");
}

/** 验证目标匀速运动、固定采样数量和磁场随时间变化。 */
void testMotionAndTimeSampling()
{
    auto param = createValidParameter();
    param.center = {-100.0, 20.0, -8.0};
    param.velocity = 10.0;
    param.headingDegrees = 0.0;
    param.pitchDegrees = 0.0;
    const TargetMagneticFieldModel model(param);
    const TargetMagneticFieldModel::Vector3 sensor{0.0, 0.0, -35.0};

    // 从零时刻开始仿真 5 秒，2 Hz 应生成包含起点和终点的 11 个样本。
    const auto samples = model.simulate(sensor, 0.0, 5.0, 2.0);
    require(samples.size() == 11U, "运动磁场时序采样点数量不正确");
    require(nearlyEqual(samples.front().time, 0.0) &&
                nearlyEqual(samples.back().time, 5.0),
            "运动磁场时序的起止时刻不正确");
    require(nearlyEqual(samples.front().targetPosition.x, -100.0) &&
                nearlyEqual(samples.back().targetPosition.x, -50.0),
            "目标没有按航向和航速更新位置");
    require(!nearlyEqual(samples.front().totalField,
                         samples.back().totalField,
                         1e-4),
            "运动目标磁场没有随时间发生变化");

    // 旧单点接口必须继续等价于显式计算零时刻。
    const auto legacySample = model.calculate(sensor);
    const auto zeroTimeSample = model.calculate(sensor, 0.0);
    require(nearlyEqual(legacySample.totalField, zeroTimeSample.totalField),
            "原单点磁场接口与零时刻结果不一致");
}

/** 验证规则平面网格数量、展开顺序和端点坐标。 */
void testSpatialGrid()
{
    auto param = createValidParameter();
    param.center = {0.0, 0.0, 0.0};
    const TargetMagneticFieldModel model(param);

    TargetMagneticFieldModel::GridParameter grid{};
    grid.minimum = {-100.0, -50.0, -30.0};
    grid.maximum = {100.0, 50.0, -30.0};
    grid.xCount = 5U;
    grid.yCount = 3U;
    grid.zCount = 1U;

    const auto samples = model.simulateGrid(grid);
    require(samples.size() == 15U, "二维磁场分布网格点数不正确");
    require(nearlyEqual(samples.front().observationPosition.x, -100.0) &&
                nearlyEqual(samples.front().observationPosition.y, -50.0),
            "磁场网格起点坐标不正确");
    require(nearlyEqual(samples.back().observationPosition.x, 100.0) &&
                nearlyEqual(samples.back().observationPosition.y, 50.0),
            "磁场网格终点坐标不正确");
}

/** 验证模型拒绝非法目标尺寸、非法网格和进入等效舰体椭球的观测点。 */
void testValidation()
{
    auto invalidTarget = createValidParameter();
    invalidTarget.height = 0.0;
    bool rejectedTarget = false;
    try
    {
        static_cast<void>(TargetMagneticFieldModel(invalidTarget));
    }
    catch (const std::invalid_argument&)
    {
        rejectedTarget = true;
    }
    require(rejectedTarget, "没有拒绝零高度目标");

    invalidTarget = createValidParameter();
    invalidTarget.geomagneticFieldNt.x =
        std::numeric_limits<double>::quiet_NaN();
    rejectedTarget = false;
    try
    {
        static_cast<void>(TargetMagneticFieldModel(invalidTarget));
    }
    catch (const std::invalid_argument&)
    {
        rejectedTarget = true;
    }
    require(rejectedTarget, "没有拒绝非法地磁场分量");

    invalidTarget = createValidParameter();
    invalidTarget.velocity = -1.0;
    rejectedTarget = false;
    try
    {
        static_cast<void>(TargetMagneticFieldModel(invalidTarget));
    }
    catch (const std::invalid_argument&)
    {
        rejectedTarget = true;
    }
    require(rejectedTarget, "没有拒绝负航速");

    invalidTarget = createValidParameter();
    invalidTarget.localCorrectionStrength = 1.1;
    rejectedTarget = false;
    try
    {
        static_cast<void>(TargetMagneticFieldModel(invalidTarget));
    }
    catch (const std::invalid_argument&)
    {
        rejectedTarget = true;
    }
    require(rejectedTarget, "没有拒绝越界的局部阵列修整系数");

    const TargetMagneticFieldModel model(createValidParameter());
    bool rejectedDistance = false;
    try
    {
        static_cast<void>(model.calculate(model.target().center));
    }
    catch (const std::domain_error&)
    {
        rejectedDistance = true;
    }
    require(rejectedDistance, "没有拒绝过近的磁场观测点");

    auto rotatedTarget = createValidParameter();
    rotatedTarget.center = {0.0, 0.0, 0.0};
    rotatedTarget.headingDegrees = 90.0;
    const TargetMagneticFieldModel rotatedModel(rotatedTarget);

    // 该点距中心 30 m，明显大于 minimumDistance，但沿旋转后的舰体纵轴
    // 位于长半轴 60 m 内，必须由尺寸和姿态联合判定为无效。
    bool rejectedInsideEllipsoid = false;
    try
    {
        static_cast<void>(rotatedModel.calculate({0.0, 30.0, 0.0}));
    }
    catch (const std::domain_error&)
    {
        rejectedInsideEllipsoid = true;
    }
    require(rejectedInsideEllipsoid,
            "没有拒绝位于旋转后等效舰体椭球内部的观测点");

    // 与上一个点中心距离相同，但该点位于横向且已超出宽半轴，应允许计算。
    const auto outsideEllipsoidSample =
        rotatedModel.calculate({30.0, 0.0, 0.0});
    require(nearlyEqual(outsideEllipsoidSample.distance, 30.0),
            "等效舰体椭球外部观测点被错误拒绝");

    TargetMagneticFieldModel::GridParameter invalidGrid{};
    invalidGrid.xCount = 0U;
    bool rejectedGrid = false;
    try
    {
        static_cast<void>(model.simulateGrid(invalidGrid));
    }
    catch (const std::invalid_argument&)
    {
        rejectedGrid = true;
    }
    require(rejectedGrid, "没有拒绝零采样点数网格");

    bool rejectedSampling = false;
    try
    {
        static_cast<void>(model.simulate({0.0, 0.0, -50.0},
                                         0.0,
                                         10.0,
                                         0.0));
    }
    catch (const std::invalid_argument&)
    {
        rejectedSampling = true;
    }
    require(rejectedSampling, "没有拒绝零磁场时序采样率");
}
} // 匿名命名空间

/** 测试程序入口，依次运行磁场模型的全部无外部依赖测试。 */
int main()
{
    try
    {
        testThreeComponentCalculation();
        testZeroGeomagneticField();
        testInverseCubeAttenuation();
        testDipoleArrayMatrixCorrection();
        testGeometryAndHeadingSensitivity();
        testMotionAndTimeSampling();
        testSpatialGrid();
        testValidation();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "磁场模型测试失败：" << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "磁场模型所有测试均已通过\n";
    return EXIT_SUCCESS;
}

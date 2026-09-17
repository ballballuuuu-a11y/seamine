#include "TargetElectricFieldModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
/**
 * @brief 检查测试条件，失败时终止当前测试流程。
 * @param condition 需要成立的布尔条件。
 * @param message 条件不成立时写入异常的中文错误说明。
 * @throws std::runtime_error condition 为 false 时抛出。
 */
void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/**
 * @brief 判断两个浮点数是否在指定绝对误差内相等。
 * @param lhs 需要比较的左侧数值。
 * @param rhs 需要比较的右侧数值。
 * @param tolerance 允许的最大绝对误差，默认值为 1e-10。
 * @return 两数绝对差小于或等于 tolerance 时返回 true。
 */
bool nearlyEqual(double lhs, double rhs, double tolerance = 1e-10)
{
    return std::abs(lhs - rhs) <= tolerance;
}

/**
 * @brief 创建各测试共享的一组合法水面舰船参数。
 * @return 可直接构造电场模型的完整目标参数。
 */
TargetElectricFieldModel::TargetParameter createValidParameter()
{
    // 前八个字段保持旧版顺序，顺便验证原有聚合初始化方式仍可编译。
    TargetElectricFieldModel::TargetParameter param{
        TargetElectricFieldModel::TargetType::SurfaceShip,
        5000.0,
        100.0,
        15.0,
        5.0,
        5.0,
        120.0,
        35.0};
    param.initialPosition = {-200.0, 0.0, -10.0};
    return param;
}

/**
 * @brief 演示并验证从设置目标参数到生成连续电场时序的完整调用过程。
 * @throws std::runtime_error 采样数量、轴频、运动位置或电场结果不正确时抛出。
 */
void testCompleteUsageExample()
{
    // 设置水面舰船的几何尺寸、运动状态和轴转速。
    TargetElectricFieldModel::TargetParameter param{};
    param.type = TargetElectricFieldModel::TargetType::SurfaceShip;
    param.tonnage = 5000.0;              // 排水吨位，单位 t。
    param.length = 100.0;                // 目标长度，单位 m。
    param.width = 15.0;                  // 目标宽度，单位 m。
    param.draft = 5.0;                   // 吃水，单位 m。
    param.velocity = 8.0;                // 航速，单位 m/s。
    param.shaftSpeed = 120.0;            // 轴转速，对应轴频 2 Hz。
    param.initialPosition = {
        -300.0,
        40.0,
        -5.0};                           // 仿真零时刻目标中心位置，单位 m。
    param.headingDegrees = 0.0;          // 沿全局 x 轴正方向运动。
    param.pitchDegrees = 0.0;            // 保持水平航行。

    // 设置海水环境，电导率保持为零时由模型根据以下参数自动估算。
    param.salinity = 35.0;               // 实用盐度。
    param.waterTemperature = 18.0;       // 海水温度，单位摄氏度。
    param.seaPressureDbar = 30.0;        // 海水表压，单位 dbar。

    // 构造函数会集中校验以上参数，非法参数会抛出异常。
    const TargetElectricFieldModel model(param);

    // 固定传感器位于全局原点下方三十米处。
    const TargetElectricFieldModel::Vector3 sensorPosition{
        0.0,
        0.0,
        -30.0};

    // 从零时刻开始仿真 75 秒，按 20 Hz 生成连续电场采样结果。
    const auto signals = model.simulate(
        sensorPosition,
        0.0,                              // 开始时刻，单位 s。
        75.0,                             // 仿真持续时间，单位 s。
        20.0);                            // 每秒采样次数，单位 Hz。

    require(signals.size() == 1501U,
            "完整示例的采样点数量不正确");
    require(nearlyEqual(signals.front().frequency, 2.0),
            "完整示例的轴频换算不正确");
    require(nearlyEqual(signals.back().targetPosition.x, 300.0),
            "完整示例的目标末位置不正确");

    // 逐点检查静电场、轴频场和综合场，保证完整序列没有无穷大或非数值。
    for (const auto& signal : signals)
    {
        require(std::isfinite(signal.staticField),
                "完整示例出现非法静电场结果");
        require(std::isfinite(signal.shaftField),
                "完整示例出现非法轴频场结果");
        require(std::isfinite(signal.totalField),
                "完整示例出现非法综合电场结果");
    }
}

/**
 * @brief 验证目标匀速运动、轴频换算和固定采样点数量。
 * @throws std::runtime_error 任一验证条件不成立时抛出。
 */
void testMotionAndSampling()
{
    const TargetElectricFieldModel model(createValidParameter());
    const TargetElectricFieldModel::Vector3 sensor{0.0, 50.0, -20.0};
    const auto signals = model.simulate(sensor, 0.0, 1.0, 20.0);

    require(signals.size() == 21U, "采样点数量不正确");
    require(nearlyEqual(signals.front().frequency, 2.0), "轴频换算不正确");
    require(nearlyEqual(signals.front().targetPosition.x, -200.0),
            "初始目标位置不正确");
    require(nearlyEqual(signals.back().targetPosition.x, -195.0),
            "目标运动位置不正确");
    require(std::isfinite(signals.front().totalField),
            "综合电场必须为有限值");
}

/**
 * @brief 验证轴频基波在四分之一周期后从峰值变化到零点。
 * @throws std::runtime_error 轴频场没有按时间变化时抛出。
 */
void testShaftFieldChangesWithTime()
{
    auto param = createValidParameter();
    param.velocity = 0.0;
    param.secondHarmonicRatio = 0.0;
    param.thirdHarmonicRatio = 0.0;
    const TargetElectricFieldModel model(param);
    const TargetElectricFieldModel::Vector3 sensor{100.0, 0.0, -10.0};

    const auto peak = model.calculate(sensor, 0.0);
    const auto zeroCrossing = model.calculate(sensor, 0.125);
    require(peak.shaftField > zeroCrossing.shaftField,
            "轴频电场没有随相位变化");
    require(zeroCrossing.shaftField < 1e-10,
            "基波四分之一周期处应接近零");
}

/**
 * @brief 验证模型能够拒绝零长度目标和零观测距离。
 * @throws std::runtime_error 模型没有抛出预期的参数异常时抛出。
 */
void testValidation()
{
    auto invalidParam = createValidParameter();
    invalidParam.length = 0.0;

    bool rejectedParameter = false;
    try
    {
        static_cast<void>(TargetElectricFieldModel(invalidParam));
    }
    catch (const std::invalid_argument&)
    {
        rejectedParameter = true;
    }
    require(rejectedParameter, "没有拒绝非法目标尺寸");

    const TargetElectricFieldModel model(createValidParameter());
    bool rejectedDistance = false;
    try
    {
        static_cast<void>(model.calculate(0.0));
    }
    catch (const std::invalid_argument&)
    {
        rejectedDistance = true;
    }
    require(rejectedDistance, "没有拒绝零观测距离");
}
} // 匿名命名空间

/**
 * @brief 测试程序入口，依次运行全部无外部依赖的单元测试。
 * @return 所有测试通过时返回 EXIT_SUCCESS，否则返回 EXIT_FAILURE。
 */
int main()
{
    try
    {
        testCompleteUsageExample();
        testMotionAndSampling();
        testShaftFieldChangesWithTime();
        testValidation();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "测试失败：" << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "所有测试均已通过\n";
    return EXIT_SUCCESS;
}

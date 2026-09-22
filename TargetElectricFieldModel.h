#pragma once

#include <cstddef>
#include <vector>

/**
 * 舰船与潜艇水下电场的简化仿真模型。
 *
 * 模型采用均匀无限导电海水中的等效电流偶极子近似，适合生成算法验证用的
 * 静电场和轴频电场数据。若用于工程预测，应使用实测数据标定偶极矩及调制参数。
 */
class TargetElectricFieldModel
{
public:
    /** 目标类型，用于选择不同的船体几何修正系数。 */
    enum class TargetType
    {
        SurfaceShip,
        Submarine
    };

    /** 三维坐标或三维矢量，单位由使用场景决定。 */
    struct Vector3
    {
        double x{0.0};
        double y{0.0};
        double z{0.0};
    };

    /**
     * 目标、运动、环境和等效电源参数。
     * 坐标约定：x 轴向东、y 轴向北、z 轴向上，水下 z 为负值。
     */
    struct TargetParameter
    {
        // 目标基本参数。
        TargetType type{TargetType::SurfaceShip};
        double tonnage{0.0};                 // 排水吨位，单位 t。
        double length{0.0};                  // 目标长度，单位 m。
        double width{0.0};                   // 目标宽度，单位 m。
        double draft{0.0};                   // 吃水或等效垂向尺度，单位 m。
        double velocity{0.0};                // 沿目标纵轴的航速，单位 m/s。
        double shaftSpeed{0.0};              // 轴转速，单位 r/min。
        double salinity{35.0};               // 实用盐度，建议范围 0～42。

        // 扩展运动参数；放在旧版字段之后以兼容原有聚合初始化顺序。
        Vector3 initialPosition{};           // 仿真零时刻目标中心位置，单位 m。
        double headingDegrees{0.0};          // 航向角，x 轴正向为零度，逆时针为正。
        double pitchDegrees{0.0};            // 俯仰角，向上为正，单位度。
        double shaftPhaseDegrees{0.0};       // 轴频基波初相位，单位度。

        // 海水环境参数。
        double waterTemperature{15.0};       // 现场海水温度，单位摄氏度。
        double seaPressureDbar{0.0};         // 海水表压，单位 dbar。
        double conductivity{0.0};            // 直接指定电导率，单位 S/m；零表示自动估算。

        // 腐蚀静电场等效参数。
        double staticDipoleMoment{0.0};      // 等效静态电流偶极矩，单位 A·m；零表示自动估算。
        double corrosionCurrentDensity{1e-4}; // 等效腐蚀电流密度，单位 A/m²。
        double coatingDamageRatio{0.05};     // 涂层等效破损比例，取值范围 0～1。
        double electrodeSeparation{0.0};     // 等效电极间距，单位 m；零表示按目标长度估算。

        // 轴频场等效调制参数。
        double shaftModulationRatio{0.04};   // 轴频偶极矩基波相对静态偶极矩的比例。
        double secondHarmonicRatio{0.20};    // 二次谐波相对基波的幅值比例。
        double thirdHarmonicRatio{0.05};     // 三次谐波相对基波的幅值比例。

        // 数值安全参数。
        double minimumDistance{1.0};         // 偶极子模型允许的最小观测距离，单位 m。
    };

    /** 单个时刻、单个传感器位置处的电场计算结果。 */
    struct ElectricFieldSignal
    {
        Vector3 targetPosition{};            // 当前目标中心位置，单位 m。
        Vector3 staticFieldVector{};         // 静电场矢量，单位 μV/m。
        Vector3 shaftFieldVector{};          // 轴频瞬时电场矢量，单位 μV/m。
        Vector3 totalFieldVector{};          // 静态与轴频瞬时矢量和，单位 μV/m。
        double staticField{0.0};             // 静电场矢量模值，单位 μV/m。
        double shaftField{0.0};              // 轴频瞬时电场矢量模值，单位 μV/m。
        double shaftAmplitude{0.0};          // 轴频基波电场幅值，单位 μV/m。
        double totalField{0.0};              // 综合瞬时电场矢量模值，单位 μV/m。
        double frequency{0.0};               // 轴频基波频率，单位 Hz。
        double distance{0.0};                // 目标中心到传感器的距离，单位 m。
        double time{0.0};                    // 当前仿真时刻，单位 s。
    };

    /**
     * @brief 创建尚未配置目标参数的空模型。
     *
     * 调用方必须先调用 setTarget()，才能执行电场计算或连续仿真。
     */
    TargetElectricFieldModel() = default;

    /**
     * @brief 创建模型并立即校验、设置目标参数。
     * @param param 目标几何、运动状态、海水环境及等效电源参数。
     * @throws std::invalid_argument param 中任一参数无效或超出支持范围时抛出。
     */
    explicit TargetElectricFieldModel(const TargetParameter& param);

    /**
     * @brief 校验并替换模型当前使用的全部目标参数。
     * @param param 新的目标几何、运动状态、海水环境及等效电源参数。
     * @throws std::invalid_argument param 中任一参数无效或超出支持范围时抛出。
     */
    void setTarget(const TargetParameter& param);

    /**
     * @brief 获取当前模型正在使用的目标参数。
     * @return 当前 TargetParameter 的常量引用，引用生命周期与模型对象相同。
     * @throws std::logic_error 模型尚未调用 setTarget() 配置有效参数时抛出。
     */
    const TargetParameter& target() const;

    /**
     * @brief 返回当前参数对应的有效海水电导率。
     * @return 实测输入或由盐度、温度和压力估算的电导率，单位 S/m。
     * @throws std::logic_error 模型尚未配置有效目标参数时抛出。
     */
    double effectiveConductivity() const;

    /**
     * @brief 返回当前参数对应的有效静态电流偶极矩。
     * @return 实测输入或由船体几何及腐蚀参数估算的偶极矩，单位 A·m。
     * @throws std::logic_error 模型尚未配置有效目标参数时抛出。
     */
    double effectiveStaticDipoleMoment() const;

    /**
     * @brief 计算指定三维传感器位置和指定时刻的瞬时电场。
     * @param sensorPosition 固定传感器的全局三维坐标，单位 m。
     * @param timeSeconds 相对仿真零时刻的时间，单位 s；允许使用负值回算位置。
     * @return 包含目标位置、距离、静态场、轴频场和综合场的完整结果。
     * @throws std::logic_error 模型尚未配置有效目标参数时抛出。
     * @throws std::invalid_argument 传感器坐标或仿真时刻不是有限值时抛出。
     * @throws std::domain_error 传感器与目标中心距离小于 minimumDistance 时抛出。
     */
    ElectricFieldSignal calculate(const Vector3& sensorPosition,
                                  double timeSeconds) const;

    /**
     * @brief 使用一维距离计算零时刻电场，兼容旧版调用接口。
     * @param distance 目标初始中心到传感器的距离，单位 m；传感器位于全局 x 轴正方向。
     * @return 零时刻对应位置的完整电场结果。
     * @throws std::logic_error 模型尚未配置有效目标参数时抛出。
     * @throws std::invalid_argument distance 不是有限值或小于 minimumDistance 时抛出。
     */
    ElectricFieldSignal calculate(double distance) const;

    /**
     * @brief 按固定采样率生成目标运动过程中传感器位置处的连续电场序列。
     * @param sensorPosition 固定传感器的全局三维坐标，单位 m。
     * @param startTimeSeconds 序列第一个采样点相对仿真零时刻的时间，单位 s。
     * @param durationSeconds 仿真持续时间，单位 s，必须大于或等于零。
     * @param sampleRateHz 采样率，单位 Hz，必须为有限正数。
     * @return 按时间升序排列的电场结果；包含起点以及不超过终点的完整采样点。
     * @throws std::logic_error 模型尚未配置有效目标参数时抛出。
     * @throws std::invalid_argument 时间或采样率参数无效时抛出。
     * @throws std::length_error 请求的采样点超过模型允许的上限时抛出。
     * @throws std::domain_error 任一采样点与目标距离小于 minimumDistance 时抛出。
     */
    std::vector<ElectricFieldSignal> simulate(
        const Vector3& sensorPosition,
        double startTimeSeconds,
        double durationSeconds,
        double sampleRateHz) const;

private:
    /**
     * @brief 校验目标几何、运动、环境和等效电源参数。
     * @param param 需要检查的完整目标参数。
     * @throws std::invalid_argument 任一参数不是有限值、为负数或超出支持范围时抛出。
     */
    static void validateTarget(const TargetParameter& param);

    /**
     * @brief 检查模型是否已经通过构造函数或 setTarget() 设置有效目标。
     * @throws std::logic_error 模型尚未配置时抛出。
     */
    void ensureConfigured() const;

    /**
     * @brief 获取当前环境的海水电导率。
     * @return 电导率，单位 S/m；优先返回实测输入，否则根据盐度、温度和压力估算。
     */
    double calculateConductivity() const;

    /**
     * @brief 根据目标类型、尺寸和排水吨位估算船体浸水表面积。
     * @return 估算的浸水表面积，单位 m²。
     */
    double calculateWetArea() const;

    /**
     * @brief 获取或估算腐蚀静电场的等效电流偶极矩。
     * @return 等效电流偶极矩标量，单位 A·m；方向由目标纵轴决定。
     */
    double calculateStaticDipoleMoment() const;

    /**
     * @brief 根据初始位置、航速、航向和俯仰角计算目标中心位置。
     * @param timeSeconds 相对仿真零时刻的时间，单位 s。
     * @return 指定时刻的目标中心全局三维坐标，单位 m。
     */
    Vector3 calculateTargetPosition(double timeSeconds) const;

    /**
     * @brief 根据航向角和俯仰角计算目标纵轴方向。
     * @return 模长为一的目标纵轴三维方向矢量。
     */
    Vector3 calculateTargetAxis() const;

    /**
     * @brief 计算均匀无限导电介质中电流偶极子产生的远场电场。
     * @param relativePosition 从偶极子中心指向传感器的三维位移，单位 m，模值必须大于零。
     * @param dipoleMoment 电流偶极矩三维矢量，单位 A·m。
     * @param conductivity 均匀介质电导率，单位 S/m，必须大于零。
     * @return 传感器位置处的三维电场矢量，单位 μV/m。
     */
    static Vector3 calculateDipoleField(
        const Vector3& relativePosition,
        const Vector3& dipoleMoment,
        double conductivity);

    /**
     * @brief 计算两个三维矢量的点积。
     * @param lhs 点积左侧矢量。
     * @param rhs 点积右侧矢量。
     * @return lhs 与 rhs 的标量点积。
     */
    static double dot(const Vector3& lhs, const Vector3& rhs) noexcept;

    /**
     * @brief 计算三维矢量的欧几里得模值。
     * @param value 需要计算模值的三维矢量。
     * @return value 的非负模值。
     */
    static double magnitude(const Vector3& value) noexcept;

    /**
     * @brief 对两个三维矢量逐分量相加。
     * @param lhs 加法左侧矢量。
     * @param rhs 加法右侧矢量。
     * @return lhs 与 rhs 的三维矢量和。
     */
    static Vector3 add(const Vector3& lhs, const Vector3& rhs) noexcept;

    /**
     * @brief 对两个三维矢量逐分量相减。
     * @param lhs 被减三维矢量。
     * @param rhs 需要减去的三维矢量。
     * @return lhs 减去 rhs 后的三维矢量。
     */
    static Vector3 subtract(const Vector3& lhs, const Vector3& rhs) noexcept;

    /**
     * @brief 将三维矢量的每个分量乘以同一个标量。
     * @param value 需要缩放的三维矢量。
     * @param factor 缩放倍数，可为正数、零或负数。
     * @return 缩放后的三维矢量。
     */
    static Vector3 scale(const Vector3& value, double factor) noexcept;

private:
    TargetParameter m_target{};               // 当前目标及环境参数。
    bool m_configured{false};                 // 是否已经设置并校验有效参数。
};

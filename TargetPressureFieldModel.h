#ifndef TARGET_PRESSURE_FIELD_MODEL_H
#define TARGET_PRESSURE_FIELD_MODEL_H

#include <cstddef>
#include <vector>

/**
 * @brief 舰艇、潜艇运动引起的有限水深水压场特性仿真模型。
 *
 * 模型根据目标类型、船长弗劳德数、水深弗劳德数和潜深自动选择计算工况。
 * 低速工况使用等效回转体势流偶极子与刚盖边界近似；水面舰艇在浅水亚临界
 * 工况下增加线性深度平均兴波压力。坐标约定为 x 向东、y 向北、z 向上，
 * 静水面为 z=0，海床为 z=-waterDepth。
 */
class TargetPressureFieldModel
{
public:
    /** 目标类型，用于上层区分水面舰艇和潜艇。 */
    enum class TargetType
    {
        SurfaceShip,
        Submarine
    };

    /** 本次计算实际采用或识别出的水动力工况。 */
    enum class PressureFieldRegime
    {
        SurfaceShipLowSpeed,                    // 水面舰艇低速回转体工况。
        SurfaceShipTransition,                  // 水面舰艇低速与兴波模型平滑过渡工况。
        SurfaceShipShallowSubcritical,          // 水面舰艇浅水亚临界工况。
        SubmarineNoWave,                        // 潜艇深潜或低速无兴波工况。
        UnsupportedSurfaceShipDeepWaterWave,   // 尚未实现的水面舰艇深水兴波工况。
        UnsupportedSurfaceShipCritical,        // 浅水临界区，当前近似不适用。
        UnsupportedSurfaceShipSupercritical,   // 尚未实现的浅水超临界工况。
        UnsupportedSubmarineFreeSurfaceWave    // 尚未实现的潜艇近水面兴波工况。
    };

    /** 三维坐标或三维矢量，坐标单位统一为 m。 */
    struct Vector3
    {
        double x{0.0}; // 东向坐标或分量。
        double y{0.0}; // 北向坐标或分量。
        double z{0.0}; // 上向坐标或分量。
    };

    /** 水动力工况自动选择与适用域控制参数。 */
    struct ModelOptions
    {
        double lowSpeedLengthFroudeThreshold{0.10}; // 低速与兴波过渡带的中心值。
        double lengthFroudeTransitionHalfWidth{0.02}; // 模型过渡带相对阈值的半宽。
        double shallowWaterRatioThreshold{0.30};    // 浅水判据 H/L 上限。
        double criticalDepthFroudeMargin{0.05};     // 水深弗劳德数临界区半宽。
        double waveAttenuationTolerance{0.01};      // 潜艇兴波可忽略的衰减阈值。
    };

    /** 目标几何、运动状态和海洋环境参数。 */
    struct TargetParameter
    {
        TargetType type{TargetType::SurfaceShip}; // 水面舰艇或潜艇类型。
        double length{0.0};                       // 目标长度，单位 m。
        double width{0.0};                        // 目标宽度，单位 m。
        double draft{0.0};                        // 水面舰艇吃水；潜艇旧接口等效高度，单位 m。
        double waterDepth{0.0};                   // 静水面至海床的海深，单位 m。
        double velocity{0.0};                     // 沿目标纵轴的航速，单位 m/s。
        Vector3 initialPosition{};                 // 零时刻目标几何中心，单位 m。
        double headingDegrees{0.0};               // 航向角，东向为 0°，逆时针为正。
        double waterDensity{1025.0};              // 海水密度，单位 kg/m³。
        double gravityAcceleration{9.80665};      // 重力加速度，单位 m/s²。
        double blockCoefficient{0.70};            // 等效排水体积方形系数，范围 (0,1]。
        double minimumDistance{1.0};              // 点偶极子允许的最小中心距离，单位 m。
        double height{0.0};                       // 潜艇垂向高度，0 表示兼容使用 draft，单位 m。
        ModelOptions modelOptions{};              // 工况判定阈值和适用域配置。
    };

    /** 单个时刻、单个水下空间位置的水压场计算结果。 */
    struct PressureFieldSample
    {
        Vector3 observationPosition{};            // 固定观测点全局坐标，单位 m。
        Vector3 targetPosition{};                 // 当前时刻目标几何中心，单位 m。
        double time{0.0};                         // 当前仿真时刻，单位 s。
        double distance{0.0};                     // 观测点至目标中心距离，单位 m。
        double longitudinalOffset{0.0};           // 目标坐标系纵向相对距离，单位 m。
        double lateralOffset{0.0};                // 目标坐标系横向相对距离，单位 m。
        double observationDepth{0.0};             // 观测点水深，单位 m。
        PressureFieldRegime regime{PressureFieldRegime::SurfaceShipLowSpeed}; // 实际工况。
        double lengthFroudeNumber{0.0};            // 船长弗劳德数 U/sqrt(gL)。
        double depthFroudeNumber{0.0};             // 水深弗劳德数 U/sqrt(gH)。
        double waveAttenuation{0.0};               // 潜艇自由液面兴波估算衰减量。
        double transitionWeight{0.0};               // 兴波模型在平滑过渡中的权重。
        double hydrostaticPressure{0.0};          // 相对海面的静水表压，单位 Pa。
        double bodyDynamicPressure{0.0};          // 目标本体运动产生的压力分量，单位 Pa。
        double freeSurfaceCorrectionPressure{0.0}; // 刚盖或兴波自由液面修正，单位 Pa。
        double wavePressure{0.0};                 // 自由液面兴波压力分量，单位 Pa。
        double seabedCorrectionPressure{0.0};     // 海床及重复边界镜像修正，单位 Pa。
        double dynamicPressure{0.0};              // 各动态压力分量之和，单位 Pa。
        double totalGaugePressure{0.0};           // 静水表压与动态压力之和，单位 Pa。
    };

    /** 规则空间网格参数，三个方向均包含起点和终点。 */
    struct GridParameter
    {
        Vector3 minimum{};                         // 网格最小坐标，单位 m。
        Vector3 maximum{};                         // 网格最大坐标，单位 m。
        std::size_t xCount{1U};                    // x 方向采样点数。
        std::size_t yCount{1U};                    // y 方向采样点数。
        std::size_t zCount{1U};                    // z 方向采样点数。
    };

    /** 创建尚未配置参数的空水压场模型。 */
    TargetPressureFieldModel() = default;

    /**
     * @brief 使用完整目标参数创建可立即计算的水压场模型。
     * @param param 目标几何、运动状态和海洋环境参数。
     * @throws std::invalid_argument 参数不合法时抛出。
     */
    explicit TargetPressureFieldModel(const TargetParameter& param);

    /**
     * @brief 校验并设置目标参数，同时更新等效排水偶极子强度。
     * @param param 需要生效的新参数。
     * @throws std::invalid_argument 参数不合法时抛出，原配置保持不变。
     */
    void setTarget(const TargetParameter& param);

    /**
     * @brief 返回当前生效的目标参数。
     * @return 当前参数的常量引用。
     * @throws std::logic_error 模型尚未配置时抛出。
     */
    const TargetParameter& target() const;

    /**
     * @brief 根据当前参数返回自动识别的水动力工况。
     * @return 已支持或尚未支持的工况枚举，便于调用方提前诊断。
     */
    PressureFieldRegime regime() const;

    /**
     * @brief 计算零时刻指定水下位置的水压场。
     * @param observationPosition 观测点全局坐标，单位 m。
     * @return 静水压力、动态压力和总表压。
     */
    PressureFieldSample calculate(const Vector3& observationPosition) const;

    /**
     * @brief 计算运动目标在指定时刻经过固定观测点时的水压场。
     * @param observationPosition 固定观测点全局坐标，单位 m。
     * @param timeSeconds 相对零时刻的仿真时间，单位 s。
     * @return 指定时刻的完整水压场结果。
     * @throws std::domain_error 观测点位于水体外部或目标内部时抛出。
     */
    PressureFieldSample calculate(const Vector3& observationPosition,
                                  double timeSeconds) const;

    /**
     * @brief 在固定观测点生成运动目标的连续水压场时序。
     * @param observationPosition 固定观测点全局坐标，单位 m。
     * @param startTimeSeconds 仿真开始时刻，单位 s。
     * @param durationSeconds 仿真持续时间，单位 s。
     * @param sampleRateHz 采样率，单位 Hz。
     * @return 包含起点和最后一个完整采样点的水压场时序。
     */
    std::vector<PressureFieldSample> simulate(
        const Vector3& observationPosition,
        double startTimeSeconds,
        double durationSeconds,
        double sampleRateHz) const;

    /**
     * @brief 生成指定时刻规则二维或三维水压场分布网格。
     * @param grid 网格边界和三个方向的采样数量。
     * @param timeSeconds 需要计算的仿真时刻，单位 s。
     * @return 按 z、y、x 顺序展开的水压场采样结果。
     */
    std::vector<PressureFieldSample> simulateGrid(
        const GridParameter& grid,
        double timeSeconds = 0.0) const;

private:
    /** 单次网格仿真允许生成的最大点数，避免意外耗尽内存。 */
    static constexpr std::size_t kMaximumGridPointCount = 1000000U;

    /** 单次时序仿真允许生成的最大点数，避免意外耗尽内存。 */
    static constexpr std::size_t kMaximumTimeSampleCount = 1000000U;

    TargetParameter m_target{};                    // 当前已校验的目标参数。
    double m_dipoleStrengthPerSpeed{0.0};          // 单位航速的等效势流偶极矩，单位 m³。
    bool m_configured{false};                      // 是否已成功设置目标参数。

    /** 返回当前目标使用的垂向尺度，潜艇优先使用 height。 */
    double effectiveVerticalSize() const noexcept;

    /** 计算当前目标的船长弗劳德数。 */
    double lengthFroudeNumber() const noexcept;

    /** 计算当前目标的水深弗劳德数。 */
    double depthFroudeNumber() const noexcept;

    /** 估算潜艇兴波传到自由液面时的指数衰减量。 */
    double submarineWaveAttenuation() const noexcept;

    /** 按目标类型和无量纲参数自动选择水动力工况。 */
    PressureFieldRegime selectRegime() const noexcept;

    /** 根据船长弗劳德数计算兴波模型的三次平滑权重。 */
    double transitionWeight() const noexcept;

    /** 校验完整目标参数和目标在水体中的初始位置。 */
    static void validateTarget(const TargetParameter& param);

    /** 按当前海深校验规则空间网格参数。 */
    void validateGrid(const GridParameter& grid) const;

    /** 根据几何尺寸和排水体积更新等效偶极子参数。 */
    void rebuildHydrodynamicParameters();

    /** 确保模型已经配置。 */
    void ensureConfigured() const;
};

#endif // TARGET_PRESSURE_FIELD_MODEL_H

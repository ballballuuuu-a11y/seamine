#ifndef TARGET_MAGNETIC_FIELD_MODEL_H
#define TARGET_MAGNETIC_FIELD_MODEL_H

#include <cstddef>
#include <vector>

/**
 * @brief 舰艇、潜艇目标的静磁场与感应磁场三分量仿真模型。
 *
 * 模型先由三轴椭球退磁因子计算宏观剩磁和感应磁矩，再使用舰体内部
 * 多偶极子阵列矩阵修整局部空间分布。坐标约定为 x 向东、y 向北、z 向上。
 */
class TargetMagneticFieldModel
{
public:
    /** 目标类型，保留目标语义并便于上层业务分类。 */
    enum class TargetType
    {
        SurfaceShip,
        Submarine
    };

    /** 三维坐标或三维矢量，具体单位由成员所在结构说明。 */
    struct Vector3
    {
        double x{0.0}; // 东向坐标或分量。
        double y{0.0}; // 北向坐标或分量。
        double z{0.0}; // 上向坐标或分量。
    };

    /** 目标几何、姿态、地磁环境和磁性参数。 */
    struct TargetParameter
    {
        TargetType type{TargetType::SurfaceShip}; // 舰艇或潜艇类型。
        double length{0.0};                       // 目标长度，单位 m。
        double width{0.0};                        // 目标宽度，单位 m。
        double height{0.0};                       // 目标高度，单位 m。
        Vector3 center{};                         // 零时刻目标几何中心，单位 m。
        double velocity{0.0};                     // 沿目标纵轴的航速，单位 m/s。

        double headingDegrees{0.0};               // 航向角，x 轴正向为 0°，逆时针为正。
        double pitchDegrees{0.0};                 // 俯仰角，抬头为正，单位度。
        double rollDegrees{0.0};                  // 横滚角，绕目标纵轴旋转，单位度。

        Vector3 geomagneticFieldNt{0.0, 30000.0, -40000.0}; // 地磁场三分量，单位 nT。
        double relativePermeability{200.0};       // 船体等效相对磁导率，必须不小于 1。
        double magneticMaterialRatio{0.04};       // 磁性材料等效体积占比，范围 (0, 1]。
        Vector3 remanentMagnetizationAm{8.0, 0.0, 0.0}; // 目标坐标系剩磁强度，单位 A/m。

        double minimumDistance{1.0};              // 目标中心最小观测距离；同时禁止进入等效舰体椭球，单位 m。
        std::size_t dipoleArrayLongitudinalCount{7U}; // 舰体纵向候选偶极子节点数。
        std::size_t dipoleArrayTransverseCount{3U};   // 舰体横向候选偶极子节点数。
        std::size_t dipoleArrayVerticalCount{3U};     // 舰体垂向候选偶极子节点数。
        double localCorrectionStrength{0.65};         // 局部阵列修整系数，范围 [0,1]。
    };

    /** 单个时刻、单个空间观测点的磁场计算结果。 */
    struct MagneticFieldSample
    {
        Vector3 observationPosition{};            // 观测点全局坐标，单位 m。
        Vector3 targetPosition{};                 // 当前时刻目标中心坐标，单位 m。
        Vector3 staticFieldVector{};               // 目标剩磁产生的静磁场三分量，单位 nT。
        Vector3 inducedFieldVector{};              // 地磁感应产生的磁场三分量，单位 nT。
        Vector3 totalFieldVector{};                // 静磁场与感应磁场矢量和，单位 nT。
        Vector3 macroFieldVector{};                // 椭球宏观总磁矩产生的三分量，单位 nT。
        Vector3 localCorrectionFieldVector{};      // 多偶极子阵列相对宏观场的局部修整量，单位 nT。
        double staticField{0.0};                   // 静磁场模值，单位 nT。
        double inducedField{0.0};                  // 感应磁场模值，单位 nT。
        double totalField{0.0};                    // 综合目标异常磁场模值，单位 nT。
        double distance{0.0};                      // 目标中心至观测点距离，单位 m。
        double time{0.0};                          // 当前仿真时刻，单位 s。
        std::size_t dipoleArrayNodeCount{0U};       // 实际落在椭球内部并参与计算的偶极子节点数。
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

    /** 创建尚未配置参数的空磁场模型。 */
    TargetMagneticFieldModel() = default;

    /**
     * @brief 使用完整目标参数创建可立即计算的模型。
     * @param param 目标几何、姿态、地磁与磁性参数。
     * @throws std::invalid_argument 参数不合法时抛出。
     */
    explicit TargetMagneticFieldModel(const TargetParameter& param);

    /**
     * @brief 校验并设置目标参数，同时重新计算等效磁矩。
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
     * @brief 计算零时刻指定空间位置的静磁场、感应磁场和综合磁场。
     * @param observationPosition 观测点全局坐标，单位 m。
     * @return 包含三类磁场三分量及模值的结果。
     * @throws std::domain_error 观测点进入等效舰体椭球或距离目标中心过近时抛出。
     */
    MagneticFieldSample calculate(const Vector3& observationPosition) const;

    /**
     * @brief 计算运动目标在指定时刻经过固定观测点时的磁场。
     * @param observationPosition 固定传感器全局坐标，单位 m。
     * @param timeSeconds 相对零时刻的仿真时间，单位 s。
     * @return 包含目标位置及磁场三分量的瞬时结果。
     * @throws std::invalid_argument 坐标或时间不是有限值时抛出。
     * @throws std::domain_error 目标运动使观测点进入等效舰体椭球或距中心过近时抛出。
     */
    MagneticFieldSample calculate(const Vector3& observationPosition,
                                  double timeSeconds) const;

    /**
     * @brief 在固定传感器处生成运动目标的连续磁场时序。
     * @param observationPosition 固定传感器全局坐标，单位 m。
     * @param startTimeSeconds 仿真开始时刻，单位 s。
     * @param durationSeconds 仿真持续时间，单位 s。
     * @param sampleRateHz 采样率，单位 Hz。
     * @return 包含起点及最后一个完整采样点的磁场时序。
     * @throws std::invalid_argument 时间或采样率不合法时抛出。
     * @throws std::length_error 采样点数超过安全上限时抛出。
     */
    std::vector<MagneticFieldSample> simulate(
        const Vector3& observationPosition,
        double startTimeSeconds,
        double durationSeconds,
        double sampleRateHz) const;

    /**
     * @brief 生成零时刻规则二维或三维磁场分布网格。
     * @param grid 网格边界和三个方向的采样数量。
     * @return 按 z、y、x 顺序展开的磁场采样结果。
     * @throws std::invalid_argument 网格参数不合法时抛出。
     * @throws std::length_error 网格点数超过安全上限时抛出。
     */
    std::vector<MagneticFieldSample> simulateGrid(const GridParameter& grid) const;

private:
    /** 单次网格仿真允许生成的最大点数，避免意外耗尽内存。 */
    static constexpr std::size_t kMaximumGridPointCount = 1000000U;

    /** 单次时序仿真允许生成的最大点数，避免意外耗尽内存。 */
    static constexpr std::size_t kMaximumTimeSampleCount = 1000000U;

    /** 单个目标允许使用的最大候选阵列节点数，限制每个采样点的计算量。 */
    static constexpr std::size_t kMaximumDipoleArrayNodeCount = 4096U;

    TargetParameter m_target{};                    // 当前已校验的目标参数。
    Vector3 m_staticDipoleMoment{};                // 全局坐标系等效剩磁偶极矩，单位 A·m²。
    Vector3 m_inducedDipoleMoment{};               // 全局坐标系等效感应偶极矩，单位 A·m²。
    std::vector<Vector3> m_dipoleOffsetsGlobal{};  // 相对目标中心的阵列节点全局偏移，单位 m。
    std::vector<double> m_dipoleWeights{};         // 阵列节点归一化磁矩权重，总和为 1。
    bool m_configured{false};                      // 是否已成功设置目标参数。

    /** 校验完整目标参数。 */
    static void validateTarget(const TargetParameter& param);

    /** 校验规则空间网格参数。 */
    static void validateGrid(const GridParameter& grid);

    /** 按当前目标参数计算静态与感应等效磁矩。 */
    void rebuildDipoleMoments();

    /** 在椭球内部建立中心对称且总权重为 1 的多偶极子阵列。 */
    void rebuildDipoleArray();

    /** 确保模型已经配置。 */
    void ensureConfigured() const;
};

#endif // TARGET_MAGNETIC_FIELD_MODEL_H

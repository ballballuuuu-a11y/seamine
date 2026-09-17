# 水压场前端数据接口约定

## 1. 功能范围

水压场模型用于计算舰艇、潜艇匀速运动过程中，固定水下传感器或规则空间网格上的压力变化。模型输出：

- 静水表压 `hydrostaticPressure`；
- 目标本体压力 `bodyDynamicPressure`；
- 自由液面修正 `freeSurfaceCorrectionPressure` 和其中的兴波分量 `wavePressure`；
- 海床及重复镜像修正 `seabedCorrectionPressure`；
- 目标运动引起的动态压力变化 `dynamicPressure`；
- 两者相加后的总表压 `totalGaugePressure`；
- 当前目标位置、观测点位置、相对纵横距离和时间。

模型会根据目标类型、船长弗劳德数和水深弗劳德数自动选择工况。低速工况采用有限水深回转体势流偶极子和刚盖边界近似；水面舰艇在 `H/L <= 0.3` 且处于亚临界状态时采用线性深度平均浅水兴波近似。它适合前端交互预览、算法验证和合成数据生成；工程级预报应使用目标实测压力特征进行标定。

## 2. 坐标与单位

- 全局坐标：`x` 向东、`y` 向北、`z` 向上。
- 静水面：`z = 0`。
- 海床：`z = -waterDepth`。
- 位置、长度、宽度、吃水、海深：`m`。
- 航速：`m/s`。
- 航向角：度，东向为 `0°`，逆时针为正。
- 压力：`Pa`。
- 时间：`s`。
- 采样率：`Hz`。

## 3. 目标参数

| 字段 | 类型 | 必填 | 约束 | 说明 |
|---|---|---:|---|---|
| `type` | string | 是 | `surfaceShip` / `submarine` | 目标类型 |
| `length` | number | 是 | `> 0` | 目标长度 |
| `width` | number | 是 | `> 0` 且不大于长度 | 目标宽度 |
| `draft` | number | 舰艇必填 | `> 0` 且不大于长度 | 水面舰艇吃水；潜艇旧接口兼容高度 |
| `height` | number | 潜艇建议填写 | `> 0` 且不大于长度 | 潜艇垂向高度；为 `0` 时回退使用 `draft` |
| `waterDepth` | number | 是 | 不小于吃水 | 静水面至海床距离 |
| `velocity` | number | 是 | `>= 0` | 沿目标纵轴的航速 |
| `initialPosition` | object | 是 | 三分量有限 | 零时刻目标几何中心 |
| `headingDegrees` | number | 是 | 有限 | 航向角 |
| `waterDensity` | number | 否 | `> 0`，默认 `1025` | 海水密度 |
| `gravityAcceleration` | number | 否 | `> 0`，默认 `9.80665` | 重力加速度 |
| `blockCoefficient` | number | 否 | `(0,1]`，默认 `0.70` | 等效排水体积方形系数 |
| `minimumDistance` | number | 否 | `> 0`，默认 `1` | 点偶极子模型最小中心距离 |
| `modelOptions.lowSpeedLengthFroudeThreshold` | number | 否 | `(0,1)`，默认 `0.10` | 低速与兴波过渡带中心 |
| `modelOptions.lengthFroudeTransitionHalfWidth` | number | 否 | 默认 `0.02` | 过渡带半宽，默认形成 `0.08～0.12` 区间 |
| `modelOptions.shallowWaterRatioThreshold` | number | 否 | `(0,1]`，默认 `0.30` | 浅水判据 `H/L` 上限 |
| `modelOptions.criticalDepthFroudeMargin` | number | 否 | `(0,1)`，默认 `0.05` | `FrH=1` 两侧的临界区半宽 |
| `modelOptions.waveAttenuationTolerance` | number | 否 | `(0,1]`，默认 `0.01` | 潜艇兴波可忽略的衰减阈值 |

水面舰艇等效回转体中心必须设置为 `z = -draft / 2`。潜艇必须完整位于 `-waterDepth <= z <= 0` 的水体范围内，中心高度按实际潜深设置。

参数示例：

```json
{
  "type": "surfaceShip",
  "length": 100.0,
  "width": 15.0,
  "draft": 5.0,
  "waterDepth": 60.0,
  "velocity": 3.0,
  "initialPosition": {"x": -180.0, "y": 40.0, "z": -2.5},
  "headingDegrees": 0.0,
  "waterDensity": 1025.0,
  "gravityAcceleration": 9.80665,
  "blockCoefficient": 0.68,
  "minimumDistance": 1.0
}
```

## 4. 单点与时序结果

固定位置时序请求除目标参数外，还需要：

| 字段 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `observationPosition` | object | 位于水体内且不在目标内部 | 固定传感器位置 |
| `startTimeSeconds` | number | 有限 | 起始时刻 |
| `durationSeconds` | number | `>= 0` | 持续时间 |
| `sampleRateHz` | number | `> 0` | 采样率 |

单个采样结果：

```json
{
  "time": 37.5,
  "observationPosition": {"x": 0.0, "y": 0.0, "z": -55.0},
  "targetPosition": {"x": 0.0, "y": 40.0, "z": -2.5},
  "distance": 66.0038,
  "longitudinalOffset": 0.0,
  "lateralOffset": -40.0,
  "observationDepth": 55.0,
  "regime": "surface_ship_low_speed",
  "lengthFroudeNumber": 0.0958,
  "depthFroudeNumber": 0.1237,
  "waveAttenuation": 0.0,
  "transitionWeight": 0.0,
  "hydrostaticPressure": 552749.9,
  "bodyDynamicPressure": -0.92,
  "freeSurfaceCorrectionPressure": -0.31,
  "wavePressure": 0.0,
  "seabedCorrectionPressure": -0.05,
  "dynamicPressure": -1.28,
  "totalGaugePressure": 552748.6
}
```

前端绘制目标特征曲线时，建议以 `time` 为横轴、`dynamicPressure` 为纵轴；总表压包含较大的静水压力基线，不适合直接观察微弱目标异常。

## 5. 空间网格

网格包含 `minimum`、`maximum`、`xCount`、`yCount` 和 `zCount`。结果固定按 `z -> y -> x` 的三重循环展开，其中 `x` 变化最快。需要二维切片时，把固定方向的最小值与最大值设为相同，并把该方向采样数设为 `1`。

前端热力图建议使用：

- 颜色字段：`dynamicPressure`；
- 发散色带中心：`0 Pa`；
- 正值：压力升高；
- 负值：压力降低；
- 提示信息：位置、目标相对纵横距离、动态压力和总表压。

## 6. 模型适用范围

- 假设海水不可压、无黏，目标保持恒定深度和航向并匀速直线运动。
- 水面舰艇低速工况和近水面低速潜艇采用刚盖自由液面近似，不计算兴波。
- 浅水亚临界水面舰艇在默认 `0.08 < FrL < 0.12` 区间平滑混合低速和兴波边界项；`transitionWeight` 表示兴波模型权重。
- 深潜或兴波衰减量不大于配置阈值的潜艇采用无兴波模型。
- 水面舰艇只有在超过低速阈值、`H/L <= 0.3` 且 `FrH < 1-criticalDepthFroudeMargin` 时采用浅水亚临界模型。
- 浅水亚临界兴波项采用线性深度平均二维偶极子近似，并以船宽限制近场奇点；工程应用需要实测标定。
- 深水高速舰艇、浅水临界或超临界舰艇、近水面高速潜艇会返回明确的不支持错误，不会静默套用其他模型。
- 模型未包含螺旋桨脉动、湍流和空化。
- 观测点不能位于目标等效椭球内部，也不应紧贴目标表面。
- `blockCoefficient` 可用实测排水量换算；若有目标实测压力曲线，应进一步进行幅值标定。

过渡带使用三次平滑权重：

\[
s=\frac{Fr_L-0.08}{0.12-0.08},\qquad
w=s^2(3-2s)
\]

只对自由液面和海床边界项进行混合，公共本体压力保持一次计算：

\[
p_{dynamic}=p_{body}+(1-w)p_{low-boundary}+wp_{wave-boundary}
\]

自动工况选择使用：

\[
Fr_L=\frac{U}{\sqrt{gL}},\qquad
Fr_H=\frac{U}{\sqrt{gH}}
\]

潜艇自由液面兴波衰减估算为：

\[
E=\exp\left(-\frac{gh}{U^2}\right)
\]

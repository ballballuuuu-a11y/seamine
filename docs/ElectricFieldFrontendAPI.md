# 水下目标电场前端接口说明

这份文档只回答三个问题：

1. 前端怎么请求电场数据？
2. 后端会返回什么？
3. 画不同的图应该用哪些字段？

> 当前 C++ 工程已经完成电场计算，但还没有实现 HTTP 服务。下面的接口地址是建议的前后端数据格式，后续可以用任意 Web 框架进行封装。

## 一、最快使用方法

如果前端需要画目标运动过程中的电场曲线，调用：

```text
POST /api/v1/electric-field/simulate
```

前端需要告诉后端：

- 目标有多大，例如吨位、长度、宽度和吃水。
- 目标怎么运动，例如初始位置、速度和航向。
- 螺旋桨轴转多快。
- 海水盐度和温度。
- 传感器放在哪里。
- 要仿真多长时间，每秒采样多少次。

后端返回一个 `samples` 数组。数组中的每一项，就是某一时刻的目标位置和电场结果。

## 二、接口有哪些

| 接口 | 用途 |
|---|---|
| `POST /api/v1/electric-field/simulate` | 生成一段连续电场数据，主要用于画曲线和航迹 |
| `POST /api/v1/electric-field/calculate` | 只计算某一个时刻的电场 |

C++ 中对应的函数是：

| 接口 | C++ 函数 |
|---|---|
| 连续仿真 | `model.simulate(sensorPosition, startTime, duration, sampleRate)` |
| 单点计算 | `model.calculate(sensorPosition, timeSeconds)` |

## 三、坐标怎么理解

- x：向东为正。
- y：向北为正。
- z：向上为正。
- 水下位置的 z 是负数，例如水下30米写成 `z: -30`。
- 航向角为 0° 时，目标沿 x 轴正方向运动。

一个水下30米的传感器位置写成：

```json
{
  "x": 0.0,
  "y": 0.0,
  "z": -30.0
}
```

## 四、生成连续电场数据

### 1. 请求地址

```text
POST /api/v1/electric-field/simulate
Content-Type: application/json
```

### 2. 完整请求示例

下面表示一艘5000吨、长100米的水面舰船，从传感器侧前方驶过：

```json
{
  "target": {
    "type": "surfaceShip",
    "tonnageT": 5000.0,
    "lengthM": 100.0,
    "widthM": 15.0,
    "draftM": 5.0,
    "initialPosition": {
      "x": -300.0,
      "y": 40.0,
      "z": -5.0
    },
    "velocityMps": 8.0,
    "headingDegrees": 0.0,
    "pitchDegrees": 0.0,
    "shaftSpeedRpm": 120.0
  },
  "environment": {
    "salinity": 35.0,
    "waterTemperatureC": 18.0,
    "seaPressureDbar": 30.0
  },
  "sensorPosition": {
    "x": 0.0,
    "y": 0.0,
    "z": -30.0
  },
  "sampling": {
    "startTimeSeconds": 0.0,
    "durationSeconds": 75.0,
    "sampleRateHz": 20.0
  }
}
```

### 3. 请求参数怎么理解

#### 目标参数 `target`

| 字段 | 必填 | 通俗解释 |
|---|---:|---|
| `type` | 否 | `surfaceShip` 表示水面舰船，`submarine` 表示潜艇 |
| `tonnageT` | 是 | 排水吨位，单位为吨 |
| `lengthM` | 是 | 目标长度，单位为米 |
| `widthM` | 是 | 目标宽度，单位为米 |
| `draftM` | 是 | 吃水或目标垂向尺寸，单位为米 |
| `initialPosition` | 否 | 仿真开始时目标在哪里 |
| `velocityMps` | 否 | 航速，每秒前进多少米 |
| `headingDegrees` | 否 | 航向角，0° 表示沿 x 轴正方向 |
| `pitchDegrees` | 否 | 俯仰角，普通水面舰船一般填 0 |
| `shaftSpeedRpm` | 否 | 螺旋桨轴每分钟转多少圈 |

吨位、长度、宽度和吃水必须大于0。其他参数没有传时，可以使用后端默认值。

#### 海水参数 `environment`

| 字段 | 默认值 | 通俗解释 |
|---|---:|---|
| `salinity` | 35 | 海水盐度 |
| `waterTemperatureC` | 15 | 海水温度，单位为摄氏度 |
| `seaPressureDbar` | 0 | 海水压力，单位为 dbar |
| `conductivitySPerM` | 0 | 实测电导率；填0时由后端自动估算 |

#### 传感器位置 `sensorPosition`

表示电场传感器固定放在哪里，x、y、z 的单位都是米。

#### 采样参数 `sampling`

| 字段 | 通俗解释 |
|---|---|
| `startTimeSeconds` | 从第几秒开始计算 |
| `durationSeconds` | 一共计算多少秒 |
| `sampleRateHz` | 每秒生成多少个采样点 |

例如持续75秒、每秒20个点，返回的采样点数量是：

```text
75 × 20 + 1 = 1501 个点
```

### 4. 可选的电场标定参数

没有实测数据时，可以不传 `source`，由模型使用默认值估算。

有实测或试验数据时，可以传：

```json
{
  "source": {
    "staticDipoleMomentAm": 0.0,
    "corrosionCurrentDensityApm2": 0.0001,
    "coatingDamageRatio": 0.05,
    "electrodeSeparationM": 0.0,
    "shaftModulationRatio": 0.04,
    "secondHarmonicRatio": 0.20,
    "thirdHarmonicRatio": 0.05,
    "minimumDistanceM": 1.0
  }
}
```

这些参数的简单含义是：

| 字段 | 通俗解释 |
|---|---|
| `staticDipoleMomentAm` | 已知的静电场源强；填0时自动估算 |
| `corrosionCurrentDensityApm2` | 船体腐蚀产生电流的强弱 |
| `coatingDamageRatio` | 船体涂层破损比例，范围为0～1 |
| `electrodeSeparationM` | 等效电场源之间的距离；填0时自动估算 |
| `shaftModulationRatio` | 轴频电场相对静电场有多强 |
| `secondHarmonicRatio` | 二次谐波有多强 |
| `thirdHarmonicRatio` | 三次谐波有多强 |
| `minimumDistanceM` | 目标与传感器允许的最小距离 |

## 五、后端返回什么

### 1. 响应示例

```json
{
  "metadata": {
    "sampleCount": 1501,
    "sampleRateHz": 20.0,
    "shaftFrequencyHz": 2.0,
    "fieldUnit": "uV/m",
    "distanceUnit": "m",
    "timeUnit": "s",
    "sensorPosition": {
      "x": 0.0,
      "y": 0.0,
      "z": -30.0
    }
  },
  "samples": [
    {
      "timeSeconds": 0.0,
      "distanceM": 303.685693,
      "targetPosition": {
        "x": -300.0,
        "y": 40.0,
        "z": -5.0
      },
      "staticField": {
        "x": 0.000425,
        "y": -0.000086,
        "z": -0.000054,
        "magnitude": 0.000437
      },
      "shaftField": {
        "x": 0.000021,
        "y": -0.000004,
        "z": -0.000003,
        "magnitude": 0.000022,
        "amplitude": 0.000017
      },
      "totalField": {
        "x": 0.000446,
        "y": -0.000090,
        "z": -0.000056,
        "magnitude": 0.000459
      }
    }
  ]
}
```

### 2. `metadata` 是什么

`metadata` 是整次仿真的公共信息，不需要在每个采样点中重复：

| 字段 | 含义 |
|---|---|
| `sampleCount` | 一共返回多少个采样点 |
| `sampleRateHz` | 每秒多少个点 |
| `shaftFrequencyHz` | 轴频基波频率，例如120转/分钟对应2 Hz |
| `fieldUnit` | 电场单位，固定为 `uV/m`，也就是 μV/m |
| `distanceUnit` | 距离单位，固定为米 |
| `timeUnit` | 时间单位，固定为秒 |
| `sensorPosition` | 传感器固定位置 |

### 3. `samples` 是什么

`samples` 是按时间顺序排列的数据点。前端画曲线时主要使用这里的数据。

| 字段 | 含义 |
|---|---|
| `timeSeconds` | 当前是第几秒 |
| `distanceM` | 当前目标离传感器多远 |
| `targetPosition` | 当前目标在哪里 |
| `staticField` | 静电场 |
| `shaftField` | 随轴转动周期变化的轴频电场 |
| `totalField` | 静电场与轴频电场相加后的综合电场 |

每种电场中：

- `x`、`y`、`z` 是三个方向的电场分量，可以为正数或负数。
- `magnitude` 是电场总强度，始终大于或等于0。
- `shaftField.amplitude` 是当前位置的轴频基波幅值。

## 六、前端画图该用哪些字段

### 1. 画电场强度随时间变化

横轴使用：

```text
samples[].timeSeconds
```

纵轴可以画三条曲线：

```text
samples[].staticField.magnitude
samples[].shaftField.magnitude
samples[].totalField.magnitude
```

### 2. 画真正的轴频波形

横轴使用：

```text
samples[].timeSeconds
```

纵轴选择一个有正负号的方向分量：

```text
samples[].shaftField.x
```

也可以画 `y` 或 `z` 分量。

> 不要用 `shaftField.magnitude` 画轴频正弦波。`magnitude` 没有负数，会把波形的负半周期翻到上面，导致波形和频谱不正确。

### 3. 画目标航迹

二维俯视图使用：

```text
samples[].targetPosition.x
samples[].targetPosition.y
```

三维航迹再加上：

```text
samples[].targetPosition.z
```

传感器位置使用：

```text
metadata.sensorPosition
```

### 4. 画距离和场强的关系

横轴使用：

```text
samples[].distanceM
```

纵轴可以使用：

```text
samples[].staticField.magnitude
samples[].shaftField.amplitude
samples[].totalField.magnitude
```

### 5. 画频谱图

前端做 FFT 时需要：

- 采样率：`metadata.sampleRateHz`
- 轴频分量：例如 `samples[].shaftField.x`

同样不要使用 `shaftField.magnitude` 做 FFT，否则可能产生错误的倍频。

## 七、只计算一个时刻

如果不需要一整段曲线，只想知道某一秒的电场，调用：

```text
POST /api/v1/electric-field/calculate
```

请求参数和连续仿真基本相同，只需要把 `sampling` 换成：

```json
{
  "sensorPosition": {
    "x": 0.0,
    "y": 0.0,
    "z": -30.0
  },
  "timeSeconds": 10.0
}
```

响应只返回一个 `sample`，字段含义与 `samples` 中的单个元素相同。

## 八、出错时返回什么

统一返回：

```json
{
  "error": {
    "code": "INVALID_PARAMETER",
    "message": "目标长度必须大于0",
    "field": "target.lengthM"
  }
}
```

常见错误：

| HTTP 状态码 | 含义 |
|---:|---|
| 400 | 参数没有填写、格式不对或数值超出范围 |
| 413 | 请求生成的采样点太多 |
| 422 | 目标运动过程中离传感器太近，偶极子模型不再适用 |
| 500 | 后端发生了没有预料到的错误 |

## 九、C++ 字段和前端字段对应关系

| C++ 结果 | 前端 JSON |
|---|---|
| `signal.time` | `timeSeconds` |
| `signal.distance` | `distanceM` |
| `signal.targetPosition` | `targetPosition` |
| `signal.staticFieldVector` | `staticField.x/y/z` |
| `signal.staticField` | `staticField.magnitude` |
| `signal.shaftFieldVector` | `shaftField.x/y/z` |
| `signal.shaftField` | `shaftField.magnitude` |
| `signal.shaftAmplitude` | `shaftField.amplitude` |
| `signal.totalFieldVector` | `totalField.x/y/z` |
| `signal.totalField` | `totalField.magnitude` |
| `signal.frequency` | `metadata.shaftFrequencyHz` |

## 十、关于空间热力图

当前 `simulate()` 计算的是“一个固定传感器随时间接收到的电场”，适合画时间曲线和目标航迹。

如果要画某一片海域的电场热力图，后端需要：

1. 在海域中生成很多网格点。
2. 对每个网格点调用一次 `calculate()`。
3. 把每个点的坐标和场强传给前端。

这属于另一个功能，建议以后增加独立接口：

```text
POST /api/v1/electric-field/spatial-grid
```

前端只画普通曲线和航迹时，不需要这个接口。

# 电场环境背景接入说明

## 合成关系

主工程保留原 `TargetElectricFieldModel` 的纯目标输出，并新增 HJC 合成电场：

```text
signalOnly      = targetStatic + targetShaft
environmentOnly = motional + shipping + local
totalField       = signalOnly + environmentOnly
```

三组场都采用 ENU 三分量。HJC 内部单位为 `V/m`，主程序 CSV 为兼容原电场输出统一换算为 `μV/m`。标量异常定义为：

```text
scalar_anomaly_uV_m = |totalField| - |environmentOnly|
```

它不是目标电场模值，可能因目标矢量与背景矢量方向相反而为负。

## 环境分量

- `motional`：上游已经求解的规定运动电场。演示配置中的 `constant` 会扩展到完整采样时序；HJC 当前不自动求解海流引起的派生运动电场。
- `shipping`：其他船舶或设备的解析电流偶极子场，可包含直流或规定频率。
- `local`：覆盖传感器位置的局部电场格网；演示配置使用均匀矢量。

适配层使用环境快照中的电导率计算目标和背景传播。主程序还把同一电导率写入独立目标模型，因此两份目标结果可以逐分量对照。

## 配置和运行

默认配置为 `config/electric_environment_demo.ini`，其来源明确标记为 `SyntheticValidationData`。可以通过以下参数替换：

```powershell
.\build\Release\seamine_simulator.exe --electric-environment config/electric_environment_demo.ini
```

相对时间必须完全位于环境快照的 `valid_duration_s` 内。主目标的 `source_id` 或 `lineage_id` 不得再次出现在干扰偶极子中。

## 输出文件

默认合成文件为 `electric_field_combined_time_series.csv`，并生成同名 `.environment.txt` 来源说明。主要字段包括：

- `signal_e*_uV_m`：目标静态场与轴频场之和；
- `environment_e*_uV_m`：环境背景三分量；
- `total_e*_uV_m`：传感器合成三分量；
- `target_static_e*_uV_m`、`target_shaft_e*_uV_m`：目标分解；
- `motional_e*_uV_m`、`shipping_e*_uV_m`、`local_e*_uV_m`：背景分解；
- `signal_magnitude_uV_m`、`environment_magnitude_uV_m`、`total_magnitude_uV_m`：三组矢量模值。

`plotElectricFieldResults.m` 在检测到合成文件后，会额外生成目标、环境和传感器总场对比图。

# HJC 磁场环境接入说明

主程序现保留原有 `magnetic_field_distribution.csv` 和
`magnetic_field_time_series.csv`，它们仍由 `TargetMagneticFieldModel` 生成，
仅表示目标剩磁与感应磁场。新增 `magnetic_field_combined_time_series.csv`
由 HJC 在固定传感器位置同次计算目标异常、地磁背景和合成总场，
不把原模型输出再次加入 HJC 的目标场。

## 配置与运行

构建后，`magnetic_environment_demo.ini` 与 WMMHR2025 系数随程序复制；
运行时不会联网，也不会在资源缺失时退回合成场。默认配置的地理原点
为北纬 30°、东经 122°，时间为 2025-01-01 00:00:00 UTC 对应的 TAI，
场景明确标记为 `SyntheticValidationData`，不代表实测海况。

```text
seamine_simulator [电场CSV] [磁场分布CSV] [旧磁场时序CSV] [水压CSV] [合成磁场CSV]
    [--magnetic-environment 磁场配置.ini] [--pressure-environment 水压配置.ini]
```

相对 `wmm_coefficient_path` 和可选 `emag2_grid_path` 以可执行程序所在资源目录为根，
也可以写绝对路径。启用 EMAG2 时，HJC 使用 WMMHR 1～15 阶主场与 EMAG2 数值格网，
不会重复计入 WMMHR 高阶地壳项。可按配置注释提供一组 PSD 频率、三个方向谱密度
及随机种子；不配置时扰动为零。局部异常、规定扰动等更复杂来源可由适配层请求的
`OceanEnvironmentSnapshot::geomagnetic` 显式提供。

## 输出物理意义

- `signal_b*_nt`：HJC 用目标处 WMM/EMAG2 地磁计算感应磁矩后，得到的目标异常场。
- `environment_b*_nt`：传感器处的地磁主场、可选地壳异常及磁扰动等背景。
- `total_b*_nt`：上述两个矢量的同方向分量之和。
- `signal_magnitude_nt`：目标异常矢量的模长，不是磁力仪标量异常。
- `total_magnitude_nt`：传感器处合成矢量的模长。
- `scalar_anomaly_nt`：`total_magnitude_nt - environment_magnitude_nt`。

主场、地壳异常、扰动、局部异常和规定运动场另列三分量；
`*.environment.txt` 保留质量状态、方法、来源和原始配置。`PartiallyValid`
必须连同质量标志解释，其他失败状态不会生成零曲线。现阶段只接入固定传感器时序；
原空间热力图及剩磁／感应分量曲线仍保留旧模型定义。

工程根目录的 `plotMagneticFieldResults.m` 会自动读取同一结果目录内的三份磁场 CSV，
分别绘制旧目标模型、HJC 三组矢量场和有符号标量异常；可设置脚本中的
`resultDirectory` 指向自定义结果目录。`magneticCSV.m` 仍只绘制旧模型空间网格，
不会把目标异常热力图误标为含环境背景的总场。

HJC 当前实现静态／准静态磁偶极近似，不包含多层海水磁扩散。正俯仰按 HJC
“舰首向上”约定；旧目标模型的非零俯仰旋转实现方向不同，比较两组结果时需留意。

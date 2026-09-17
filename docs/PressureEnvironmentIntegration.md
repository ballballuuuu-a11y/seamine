# 水压场与 HJC 环境背景接入

主程序使用 `PressureFieldSimulation` 调用现有 `TargetPressureFieldModel` 和新增的
HJC `simulatePressureBackground`，在同一固定观测点和时间轴上合成压力。
目标模型的低速、浅水过渡、浅水亚临界与潜艇工况保持原有定义；HJC 独立计算
静水、潮汐、方向 JONSWAP 环境海浪和其他船舶势流水压。

## 构建与运行

```powershell
# 编译主工程及 HJC 源码，测试默认启用。
cmake -S . -B build
cmake --build build --config Release
# 运行原有测试、HJC 测试、压力集成测试和主程序端到端测试。
ctest --test-dir build -C Release --output-on-failure
# 使用明确标记为合成数据的演示配置。
.\build\Release\seamine_simulator.exe --pressure-environment config\pressure_environment_demo.ini
```

不带 `--pressure-environment` 时读取可执行程序旁的 `pressure_environment_demo.ini`。
该文件在构建和安装时复制到程序旁。指定的配置缺失或无效时返回错误，不回退到默认场景。
原有四个位置参数仍依次表示电场、磁场分布、磁场时序和水压时序的 CSV 路径。

## 配置与来源

`config/pressure_environment_demo.ini` 是有中文说明的完整模板。每行使用 `key=value`，
允许空行和以 `#` 开头的整行注释，不允许重复键、未知键或省略必需字段。

- `water_depth_m`、`density_kg_m3` 是目标和背景共用的水深、密度。它们会同步到目标参数的副本。
- `valid_time_tai_ns` 是 TAI 纳秒起点，不能直接填写未经转换的 UTC Unix 纳秒。
  `valid_duration_s` 必须覆盖完整采样区间。当前主程序采样 0～75 秒、20 Hz，传感器深度 55 m。
- `wave_hs_m`、`wave_tp_s`、`wave_direction_deg` 定义海浪统计特征；波向按东向为 0°、北向为 90°解释。
  `wave_gamma`、`wave_spread` 定义谱形，`wave_seed` 确定可复现的随机相位。
  项目 `sea_state` 编号为 1～10，并与 Hs 范围保持一致；海浪合成的最高频率小于 3/Tp，采样率必须大于 6/Tp。
- `tide_count` 和连续的 `tide.0.*` 等条目定义潮汐幅值、周期、相位和参考时刻。
  配置中的潮汐使用与水深一致的 `vertical_datum`。
- `shipping_count` 和连续的 `shipping.0.*` 等条目定义其他水面舰艇。
  主程序目标身份是 `source_id=1`、`lineage_id=main-pressure-target`，不得重复加入背景。
- `scenario_id`、`data_nature`、`source_description`、`source_uri` 明确记录配置来源。
  演示配置全部是 `SyntheticValidationData`，不代表实测海况；修改标签并不能改变数据性质。

关闭环境海浪时设置 `sea_state=1`、`wave_hs_m=0`，其余海浪字段保留有效值。
关闭潮汐或航运时将相应 `*_count` 设为 0，并删除该组条目。静水基线始终保留。
独立背景 API 可直接接收调用方构造的 `OceanEnvironmentSnapshot`；本次命令行接口读取显式配置，
没有接入在线数据库或自动下载。密度和水深以观测点的取值近似整段局部环境。

## 输出字段

原水压 CSV 的列保持原有含义，新增列如下，压力均为 Pa：

| 字段 | 含义 |
| --- | --- |
| `signal_only_pa` | 目标动态压力，等于原 `dynamic_pressure_pa` |
| `environment_only_pa` | 静水＋潮汐＋环境海浪＋其他船舶压力 |
| `total_field_pa` | 目标贡献＋环境贡献，包含一次静水基线 |
| `total_dynamic_pressure_pa` | 目标＋潮汐＋环境海浪＋其他船舶，不含静水基线 |
| `environment_tide_pressure_pa` | 潮汐压力 |
| `environment_wave_pressure_pa` | 环境海浪压力，不是目标自身兴波 |
| `environment_shipping_pressure_pa` | 其他船舶压力 |
| `water_depth_m`、`water_density_kg_m3` | 本次计算实际使用的环境水深和密度 |

原 `wave_pressure_pa` 仍为目标兴波，原 `total_gauge_pressure_pa` 仍为目标＋静水。
需要新的含背景总场时，应读取 `total_field_pa`。电场和磁场的计算和输出列没有变化。
CSV 使用 17 位有效数字；同目录的 `<水压CSV>.environment.txt` 保存配置原文、模型方法、
质量状态、来源和限制。`plotPressureFieldResults.m` 会为新版 CSV 另开一个 HJC 背景与总场图窗。

## 接口约定与适用范围

`integration/hjc/PressureFieldSimulation.h` 中的 `Request` 接收目标、环境、采样请求和
可选目标来源标识；`simulate()` 返回含目标原分量、背景分量和三组场的样点数组。
环境背景不通过虚假目标生成，也不会先计算 HJC 总场再重复添加目标信号。

仅消费 HJC 的 `Valid` 或 `PartiallyValid` 结果；后者保留 flags 和 issues。
其他状态、缺失字段、数组错位、非有限值、单位错误和目标不支持的工况均显式失败。
输入单位采用 m、kg/m3（也接受等价的 kg m-3）、Pa，坐标为 ENU，重力固定为 HJC 数值核使用的 9.80665 m/s²。
API 不会因为缺少无关的电导率、声速、地磁或底质字段而拒绝水压计算。

背景海浪使用线性随机波模型，其他船舶使用 HJC 势流偶极及镜像近似。
本次合成不包含海浪改变目标运动的双向耦合、传感器噪声或传感器响应。
原 HJC 综合接口保留；新增独立接口复用其数值函数，集成测试逐点比较两者的背景分量。

# 水下目标电磁场仿真

这是一个可独立编译运行的 C++17 工程，用于生成水面舰船或潜艇的电场时序、静磁场和地磁感应磁场三分量空间分布、运动目标磁场时序，以及有限水深条件下的运动目标水压场时序和空间分布。

电场模型采用均匀无限导电海水中的等效电流偶极子近似。磁场模型把目标等效为三轴磁化椭球体，根据长度、宽度、高度、姿态、地磁三分量、相对磁导率和剩磁计算等效磁矩，再用磁偶极子模型计算目标外部异常磁场。水压场模型根据目标类型和弗劳德数自动区分潜艇无兴波、水面舰艇低速回转体以及水面舰艇浅水亚临界工况；浅水判据默认采用 `H/L <= 0.3`，并在 `0.08 < FrL < 0.12` 内平滑混合低速和兴波边界项。输出分别给出静水压力、本体动态压力、自由液面修正、兴波压力、海床修正和总表压。各模型适合算法验证和合成数据生成；工程级场强预测仍需使用实测数据标定。

## 工程结构

- `TargetElectricFieldModel.h`：模型公开接口和参数定义。
- `TargetElectricFieldModel.cpp`：三维运动、电导率、静电场和轴频场实现。
- `TargetMagneticFieldModel.h`：磁场目标参数、单点计算和空间网格接口。
- `TargetMagneticFieldModel.cpp`：剩磁、地磁感应、退磁因子和三分量磁场实现。
- `TargetPressureFieldModel.h`：水压场目标参数、单点、时序和空间网格接口。
- `TargetPressureFieldModel.cpp`：有限水深边界镜像、静水压力和动态压力实现。
- `main.cpp`：可直接运行的电场、磁场与水压场仿真示例。
- `runPressureFieldRegimeSimulation.m`：独立运行低速、过渡、浅水亚临界和深潜潜艇水压场仿真并生成结果图。
- `plotElectricFieldResults.m`：读取电场 CSV，绘制目标通过曲线、三分量、频谱和航迹。
- `tests/TargetElectricFieldModelTests.cpp`：参数校验、运动和轴频测试。
- `tests/TargetMagneticFieldModelTests.cpp`：磁场三分量、空间网格、衰减规律和校验测试。
- `tests/TargetPressureFieldModelTests.cpp`：压力组成、航速规律、海深影响、运动时序和网格测试。
- `CMakeLists.txt`：跨平台 CMake 构建配置。
- `docs/ElectricFieldFrontendAPI.md`：前后端电场数据接口、字段和绘图约定。
- `docs/静电场与轴频电场仿真数据结构与函数说明.md`：电场仿真的数据结构、参数含义、函数输入输出和调用示例。
- `docs/舰艇静电场与轴频电场仿真数据结构与函数说明.docx`：适合阅读、评审和交付的 Word 版电场接口手册。
- `docs/MagneticFieldFrontendAPI.md`：磁场参数、空间网格接口和热力图字段约定。
- `docs/PressureFieldFrontendAPI.md`：水压场参数、时序、空间网格和绘图字段约定。

## 接口文档

水压场已接入 HJC 环境背景：主程序额外输出目标、背景、合成总场及潮汐、海浪、其他船舶分量。
默认配置为明确标记的合成海况。配置、运行和字段说明见 [水压环境接入说明](docs/PressureEnvironmentIntegration.md)。

电场数据结构和函数说明请参阅 [`docs/静电场与轴频电场仿真数据结构与函数说明.md`](docs/静电场与轴频电场仿真数据结构与函数说明.md)；前端需要绘制电场时序、航迹或频谱时，请参阅 [`docs/ElectricFieldFrontendAPI.md`](docs/ElectricFieldFrontendAPI.md)；需要绘制静磁场分布、感应磁场三分量或综合磁场热力图时，请参阅 [`docs/MagneticFieldFrontendAPI.md`](docs/MagneticFieldFrontendAPI.md)；需要绘制运动目标水压异常时序或水下空间压力热力图时，请参阅 [`docs/PressureFieldFrontendAPI.md`](docs/PressureFieldFrontendAPI.md)。

## 编译

需要 CMake 3.16 或更高版本，以及支持 C++17 的编译器。

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## 运行测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## 运行示例

Visual Studio 多配置生成器：

```powershell
.\build\Release\seamine_simulator.exe
```

单配置生成器：

```powershell
.\build\seamine_simulator.exe
```

默认在当前目录生成 `electric_field_simulation.csv`、`magnetic_field_distribution.csv`、`magnetic_field_time_series.csv` 和 `pressure_field_time_series.csv`。也可以依次指定四份输出文件：

```powershell
.\build\Release\seamine_simulator.exe electric_result.csv magnetic_grid.csv magnetic_time.csv pressure_time.csv
```

可通过 `--pressure-environment config/pressure_environment_demo.ini` 指定水压环境配置，
无需重新编译。未指定时读取程序旁的同名演示配置；配置缺失时会报错。
新水压总场列为 `total_field_pa`，原 `total_gauge_pressure_pa` 保持“目标＋静水”的含义。
水压 CSV 同时生成 `.environment.txt` 来源说明文件。

磁场 CSV 包含空间坐标、距离、静磁场三分量、感应磁场三分量、综合磁场三分量及各自模值，磁场单位统一为 `nT`。

## 使用 MATLAB 绘制电场结果

先运行 C++ 示例生成 `electric_field_simulation.csv`，也可以直接使用
`build/validation_electric.csv`。然后在 MATLAB 命令窗口执行：

```matlab
run('plotElectricFieldResults.m')
```

脚本会自动选择现有电场 CSV，绘制目标距离和电场强度通过曲线、静电场三分量、
轴频电场三分量、综合电场三分量、综合电场分量频谱及目标航迹，并把 PNG 图片保存到
`build/electric_field_figures`。

## 使用 MATLAB 运行水压场分工况仿真

在 MATLAB 命令窗口切换到项目目录并执行：

```matlab
results = runPressureFieldRegimeSimulation();
```

该流程兼容 MATLAB R2017a/R2017b，不依赖预先生成的 C++ CSV，会直接复现当前水压模型，展示水面舰艇低速回转体、`FrL=0.10` 平滑过渡、浅水亚临界以及深潜潜艇无兴波四种工况。默认结果保存在 `matlab_pressure_regime_results`，包括4个时序 CSV、汇总 CSV、MAT 数据和3张 PNG。无界面运行时可用：

```matlab
results = runPressureFieldRegimeSimulation('输出目录', false);
```

## 在其他代码中使用

创建 `TargetElectricFieldModel::TargetParameter`，设置目标和环境参数后构造模型。单点求值使用 `calculate(sensorPosition, timeSeconds)`，连续仿真使用 `simulate(sensorPosition, startTime, duration, sampleRate)`。

内部电场单位统一为 `μV/m`，位置单位为 `m`，时间单位为 `s`，转速单位为 `r/min`。

磁场单点求值使用 `TargetMagneticFieldModel::calculate(observationPosition)`；指定运动时刻使用 `calculate(observationPosition, timeSeconds)`；固定传感器连续采样使用 `simulate(observationPosition, startTime, duration, sampleRate)`；二维平面或三维空间分布使用 `simulateGrid(grid)`。将某个方向的网格最小值、最大值设为相同，并把采样点数设为 1，即可生成二维平面。

水压场单点求值、固定传感器时序和空间网格分别使用 `TargetPressureFieldModel::calculate`、`simulate` 和 `simulateGrid`，使用 `regime` 可在计算前查看自动识别的水动力工况。结果同时给出静水表压、目标本体压力、自由液面与海床修正、目标动态压力和总表压；目标特征检测通常使用去除静水基线后的 `dynamicPressure`。尚未实现的深水高速、临界、超临界或潜艇近水面高速兴波工况会明确报错，不会静默退回低速模型。

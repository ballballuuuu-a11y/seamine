# MATLAB 水下光学成像三分量模型演示

这个示例用 MATLAB 完整展示以下过程：

1. 生成清晰彩色目标和逐像素距离图；
2. 计算直接分量 `E_D`；
3. 用距离相关高斯点扩散函数近似前向散射 `E_FS`；
4. 计算随距离趋于背景光的后向散射 `E_BS`；
5. 合成带颜色衰减、模糊、幕光和噪声的水下观测图像；
6. 分别使用两分量模型和已知前向散射的三分量模型复原图像。

## 一键运行

在 MATLAB 命令窗口中执行：

```matlab
cd('C:\Users\Administrator\Desktop\seamine\matlab_underwater_demo');
run_demo;
```

也可以直接调用主函数：

```matlab
outputDir = fullfile(pwd, 'output');
results = underwater_optical_model_demo(outputDir, true);
```

`showFigures` 设为 `false` 时只生成文件、不显示图窗，适合批处理：

```matlab
results = underwater_optical_model_demo(fullfile(pwd, 'output'), false);
```

## 模型表达

直接分量：

```latex
L_D(x,k)=J(x,k)\exp[-\beta_k^D z(x)]
```

前向散射近似：

```latex
L_{FS}(x,k)\approx\alpha_k\,[(Jt_k)*g_{\sigma(z,k)}]
```

后向散射：

```latex
L_{BS}(x,k)=B_k^{\infty}\,[1-\exp(-\beta_k^B z(x))]
```

水下观测图像：

```latex
I=L_D+L_{FS}+L_{BS}+n
```

两分量复原忽略 `L_FS`；三分量示范复原使用已知 `L_FS`，用于显示完整模型的理想复原上限。

## 输出文件

运行后，`output` 目录包含：

- `underwater_model_overview.png`：九宫格原理总览；
- `underwater_model_curves.png`：透射率、后向散射和分量变化曲线；
- `01_clear_scene.png`：清晰合成场景；
- `02_depth_map.png`：归一化距离图；
- `03_observed_underwater.png`：水下观测图像；
- `04_restored_two_component.png`：两分量复原结果；
- `05_restored_three_component.png`：三分量复原结果；
- `underwater_model_demo_data.mat`：全部中间变量、参数和评价指标。

## 兼容性

- 主程序按 MATLAB R2017 兼容语法编写，并已在 MATLAB R2021b 回归运行；
- 绘图采用 `subplot`，未使用 `tiledlayout`、`nexttile`、`sgtitle` 或 `turbo`；
- 通道矩阵运算采用 `bsxfun`，不依赖隐式扩展；
- 不使用 `imshow`、`imgaussfilt`、`padarray` 等图像处理工具箱函数；
- 高斯模糊由 `conv2` 实现，基础 MATLAB 即可运行。

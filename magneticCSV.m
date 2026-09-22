%% 椭球宏观场与多偶极子局部修整的磁场空间分布
% 当前空间网格尚未接入 HJC；本图只表示目标剩磁与感应磁场，不含环境背景。
% 下列参数是当前 CSV 对应的仿真条件；重新生成 CSV 后如有修改，应同步更新。

% 舰艇几何、姿态和磁性基本参数。
simulationParameter.targetType = '水面舰艇';
simulationParameter.lengthM = 100.0;
simulationParameter.widthM = 15.0;
simulationParameter.heightM = 10.0;
simulationParameter.centerM = [0.0, 0.0, -5.0];
simulationParameter.headingDegrees = 20.0;
simulationParameter.pitchDegrees = 0.0;
simulationParameter.rollDegrees = 0.0;
simulationParameter.geomagneticFieldNt = [18000.0, 30000.0, -42000.0];
simulationParameter.relativePermeability = 180.0;
simulationParameter.magneticMaterialRatio = 0.035;
simulationParameter.remanentMagnetizationAm = [7.5, 0.8, -0.4];
simulationParameter.dipoleArraySize = [7, 3, 3];
simulationParameter.localCorrectionStrength = 0.65;

% 空间网格参数：在水下 30 m 的水平面上计算磁场。
simulationParameter.xRangeM = [-250.0, 250.0];
simulationParameter.yRangeM = [-150.0, 150.0];
simulationParameter.observationDepthM = -30.0;
simulationParameter.xCount = 101;
simulationParameter.yCount = 61;

% 优先读取主程序结果，其次读取 CTest 演示结果；不依赖开发机器绝对路径。
projectRoot = fileparts(mfilename('fullpath'));
filePath = fullfile(projectRoot, 'magnetic_field_distribution.csv');
if exist(filePath, 'file') ~= 2
    filePath = fullfile(projectRoot, 'build', 'pressure_environment_demo', ...
        'magnetic_field_distribution.csv');
end
if exist(filePath, 'file') ~= 2
    error('找不到目标磁场空间分布文件，请先运行 seamine_simulator。');
end
data = readtable(filePath);

% 在命令窗口输出本图对应的基本仿真参数。
fprintf('\n========== 磁场空间分布仿真参数 ==========\n');
fprintf('目标：%s，尺寸 %.0f m × %.0f m × %.0f m\n', ...
    simulationParameter.targetType, simulationParameter.lengthM, ...
    simulationParameter.widthM, simulationParameter.heightM);
fprintf('中心位置：(%.0f, %.0f, %.0f) m，航向 %.0f°\n', ...
    simulationParameter.centerM, simulationParameter.headingDegrees);
fprintf('独立目标模型固定地磁 ENU：(%.0f, %.0f, %.0f) nT\n', ...
    simulationParameter.geomagneticFieldNt);
fprintf('相对磁导率：%.0f，磁性材料比例：%.3f\n', ...
    simulationParameter.relativePermeability, ...
    simulationParameter.magneticMaterialRatio);
fprintf('剩磁强度：(%.1f, %.1f, %.1f) A/m\n', ...
    simulationParameter.remanentMagnetizationAm);
fprintf('多偶极子候选阵列：%d×%d×%d，局部修整系数：%.2f\n', ...
    simulationParameter.dipoleArraySize, ...
    simulationParameter.localCorrectionStrength);
fprintf('观测面：z = %.0f m，网格 %d × %d\n\n', ...
    simulationParameter.observationDepthM, simulationParameter.xCount, ...
    simulationParameter.yCount);

% 获取规则网格坐标。
x = unique(data.x_m);
y = unique(data.y_m);
nx = numel(x);
ny = numel(y);

% CSV 中 x 方向变化最快，因此按 nx×ny 重组后转置。
totalField = reshape(data.total_magnitude_nt, nx, ny).';
macroField = reshape(sqrt(data.macro_bx_nt.^2 + data.macro_by_nt.^2 + ...
    data.macro_bz_nt.^2), nx, ny).';
localCorrection = reshape(sqrt(data.local_correction_bx_nt.^2 + ...
    data.local_correction_by_nt.^2 + data.local_correction_bz_nt.^2), nx, ny).';

% 并列显示宏观场、局部修整量和最终目标异常场。
figure('Name', '椭球宏观场与多偶极子局部修整', 'Color', 'w');
subplot(1, 3, 1);
imagesc(x, y, macroField);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('椭球宏观场模值');

subplot(1, 3, 2);
imagesc(x, y, localCorrection);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('多偶极子局部修整量模值');

subplot(1, 3, 3);
imagesc(x, y, totalField);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
parameterText = sprintf([ ...
    '舰艇 %.0f×%.0f×%.0f m，中心(%.0f, %.0f, %.0f) m，航向 %.0f°；' ...
    '观测面 z=%.0f m'], ...
    simulationParameter.lengthM, simulationParameter.widthM, ...
    simulationParameter.heightM, simulationParameter.centerM, ...
    simulationParameter.headingDegrees, ...
    simulationParameter.observationDepthM);
title({'修整后的综合目标异常场（不含 HJC 背景）', parameterText}, ...
    'Interpreter', 'none');

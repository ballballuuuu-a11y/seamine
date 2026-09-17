%% 舰艇磁场空间分布快速绘图
% 本文件读取 validation_magnetic.csv，并显示综合目标异常磁场热力图。
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

% 空间网格参数：在水下 30 m 的水平面上计算磁场。
simulationParameter.xRangeM = [-250.0, 250.0];
simulationParameter.yRangeM = [-150.0, 150.0];
simulationParameter.observationDepthM = -30.0;
simulationParameter.xCount = 101;
simulationParameter.yCount = 61;

% 读取磁场仿真结果。
filePath = 'C:\Users\Administrator\Desktop\seamine\build\validation_magnetic.csv';
data = readtable(filePath);

% 在命令窗口输出本图对应的基本仿真参数。
fprintf('\n========== 磁场空间分布仿真参数 ==========\n');
fprintf('目标：%s，尺寸 %.0f m × %.0f m × %.0f m\n', ...
    simulationParameter.targetType, simulationParameter.lengthM, ...
    simulationParameter.widthM, simulationParameter.heightM);
fprintf('中心位置：(%.0f, %.0f, %.0f) m，航向 %.0f°\n', ...
    simulationParameter.centerM, simulationParameter.headingDegrees);
fprintf('地磁 ENU：(%.0f, %.0f, %.0f) nT\n', ...
    simulationParameter.geomagneticFieldNt);
fprintf('相对磁导率：%.0f，磁性材料比例：%.3f\n', ...
    simulationParameter.relativePermeability, ...
    simulationParameter.magneticMaterialRatio);
fprintf('剩磁强度：(%.1f, %.1f, %.1f) A/m\n', ...
    simulationParameter.remanentMagnetizationAm);
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

% 绘制综合异常磁场热力图。
figure;
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
title({'综合目标异常磁场分布', parameterText}, ...
    'Interpreter', 'none');

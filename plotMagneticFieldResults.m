%% 舰艇磁场空间分布与运动时序绘图
% 本脚本读取 C++ 仿真程序输出的两份 CSV，并生成常用磁场图形。

clear;
clc;
close all;

%% 一、当前 CSV 对应的仿真参数
% 这些参数与 C++ 演示程序生成 validation_magnetic*.csv 时的配置一致。
% 如果重新生成 CSV 时修改了仿真条件，应同步修改本节，避免图注与数据不一致。

% 两组磁场数据共用的舰艇几何、姿态和磁性参数。
commonParameter.targetType = '水面舰艇';
commonParameter.lengthM = 100.0;
commonParameter.widthM = 15.0;
commonParameter.heightM = 10.0;
commonParameter.pitchDegrees = 0.0;
commonParameter.rollDegrees = 0.0;
commonParameter.geomagneticFieldNt = [18000.0, 30000.0, -42000.0];
commonParameter.relativePermeability = 180.0;
commonParameter.magneticMaterialRatio = 0.035;
commonParameter.remanentMagnetizationAm = [7.5, 0.8, -0.4];
commonParameter.minimumDistanceM = 2.0;

% 空间分布图对应的静止目标和规则网格参数。
spatialParameter.centerM = [0.0, 0.0, -5.0];
spatialParameter.headingDegrees = 20.0;
spatialParameter.velocityMps = 0.0;
spatialParameter.xRangeM = [-250.0, 250.0];
spatialParameter.yRangeM = [-150.0, 150.0];
spatialParameter.observationDepthM = -30.0;
spatialParameter.xCount = 101;
spatialParameter.yCount = 61;

% 运动时序图对应的目标运动、固定传感器和采样参数。
motionParameter.initialCenterM = [-300.0, 40.0, -5.0];
motionParameter.headingDegrees = 0.0;
motionParameter.velocityMps = 8.0;
motionParameter.sensorPositionM = [0.0, 0.0, -30.0];
motionParameter.startTimeS = 0.0;
motionParameter.durationS = 75.0;
motionParameter.sampleRateHz = 20.0;
motionParameter.sampleCount = 1501;

% 为图窗标题准备简洁参数说明，详细参数同时输出到命令窗口。
spatialFigureText = sprintf([ ...
    '舰艇 %.0f×%.0f×%.0f m，中心(%.0f, %.0f, %.0f) m，航向 %.0f°，' ...
    '观测面 z=%.0f m'], ...
    commonParameter.lengthM, commonParameter.widthM, ...
    commonParameter.heightM, spatialParameter.centerM, ...
    spatialParameter.headingDegrees, spatialParameter.observationDepthM);
magneticFigureText = sprintf([ ...
    '地磁 ENU=(%.0f, %.0f, %.0f) nT，相对磁导率 %.0f，' ...
    '磁性材料比例 %.3f'], ...
    commonParameter.geomagneticFieldNt, ...
    commonParameter.relativePermeability, ...
    commonParameter.magneticMaterialRatio);
motionFigureText = sprintf([ ...
    '初始中心(%.0f, %.0f, %.0f) m，航速 %.0f m/s，航向 %.0f°，' ...
    '传感器(%.0f, %.0f, %.0f) m'], ...
    motionParameter.initialCenterM, motionParameter.velocityMps, ...
    motionParameter.headingDegrees, motionParameter.sensorPositionM);
samplingFigureText = sprintf('时长 %.0f s，采样率 %.0f Hz，共 %d 点', ...
    motionParameter.durationS, motionParameter.sampleRateHz, ...
    motionParameter.sampleCount);

fprintf('\n========== 舰艇磁场仿真基本参数 ==========\n');
fprintf('目标：%s，尺寸 %.0f m × %.0f m × %.0f m\n', ...
    commonParameter.targetType, commonParameter.lengthM, ...
    commonParameter.widthM, commonParameter.heightM);
fprintf('地磁 ENU：(%.0f, %.0f, %.0f) nT\n', ...
    commonParameter.geomagneticFieldNt);
fprintf('相对磁导率：%.0f，磁性材料比例：%.3f\n', ...
    commonParameter.relativePermeability, ...
    commonParameter.magneticMaterialRatio);
fprintf('剩磁强度：(%.1f, %.1f, %.1f) A/m\n', ...
    commonParameter.remanentMagnetizationAm);
fprintf(['空间分布：中心(%.0f, %.0f, %.0f) m，航向 %.0f°，' ...
    '观测面 z=%.0f m，网格 %d×%d\n'], ...
    spatialParameter.centerM, spatialParameter.headingDegrees, ...
    spatialParameter.observationDepthM, spatialParameter.xCount, ...
    spatialParameter.yCount);
fprintf(['运动时序：初始中心(%.0f, %.0f, %.0f) m，航速 %.0f m/s，' ...
    '航向 %.0f°\n'], ...
    motionParameter.initialCenterM, motionParameter.velocityMps, ...
    motionParameter.headingDegrees);
fprintf(['固定传感器：(%.0f, %.0f, %.0f) m，时长 %.0f s，' ...
    '采样率 %.0f Hz，共 %d 点\n\n'], ...
    motionParameter.sensorPositionM, motionParameter.durationS, ...
    motionParameter.sampleRateHz, motionParameter.sampleCount);

%% 二、读取磁场空间分布和运动时序数据
% 脚本文件放在工程根目录，因此可据此定位 build 目录。
scriptPath = mfilename('fullpath');
projectRoot = fileparts(scriptPath);
spatialFile = fullfile(projectRoot, 'build', 'validation_magnetic.csv');
timeSeriesFile = fullfile(projectRoot, 'build', ...
    'validation_magnetic_time_series.csv');

if exist(spatialFile, 'file') ~= 2
    error('找不到磁场空间分布文件：%s', spatialFile);
end

if exist(timeSeriesFile, 'file') ~= 2
    error('找不到磁场时序文件：%s', timeSeriesFile);
end

spatialData = readtable(spatialFile);
timeData = readtable(timeSeriesFile);

%% 三、恢复指定深度的规则空间网格
% 当前示例只有水下 30 米平面；若存在多个深度，这里选择第一个深度。
% unique 默认按升序返回结果，省略可选参数以兼容较早的 MATLAB 版本。
depthValues = unique(spatialData.z_m);
plotDepth = depthValues(1);
depthTolerance = max(1.0e-9, abs(plotDepth) * 1.0e-12);
planeData = spatialData(abs(spatialData.z_m - plotDepth) <= depthTolerance, :);

% CSV 中 x 变化最快，按 y、x 排序后可恢复二维矩阵。
planeData = sortrows(planeData, {'y_m', 'x_m'});
x = unique(planeData.x_m);
y = unique(planeData.y_m);
nx = numel(x);
ny = numel(y);

% 使用通用的 size 获取表格行数，避免依赖较新的专用便捷函数。
if size(planeData, 1) ~= nx * ny
    error('选定深度的数据不是完整规则网格，无法直接重组。');
end

staticMagnitude = reshape(planeData.static_magnitude_nt, nx, ny).';
inducedMagnitude = reshape(planeData.induced_magnitude_nt, nx, ny).';
totalMagnitude = reshape(planeData.total_magnitude_nt, nx, ny).';
inducedBx = reshape(planeData.induced_bx_nt, nx, ny).';
inducedBy = reshape(planeData.induced_by_nt, nx, ny).';
inducedBz = reshape(planeData.induced_bz_nt, nx, ny).';
[X, Y] = meshgrid(x, y);

%% 四、绘制静磁场、感应磁场和综合磁场热力图
figure('Name', '磁场强度空间分布', 'Color', 'w');
% 整个图窗采用同一颜色表，使用 MATLAB 早期版本支持的经典调用形式。
colormap(parula(64));

subplot(2, 2, 1);
imagesc(x, y, staticMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title(sprintf('静磁场强度，z = %.1f m', plotDepth));

subplot(2, 2, 2);
imagesc(x, y, inducedMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('感应磁场强度');

subplot(2, 2, 3);
imagesc(x, y, totalMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('综合目标异常磁场强度');

subplot(2, 2, 4);
contourf(X, Y, totalMagnitude, 20, 'LineColor', 'none');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('综合磁场等值线');
% MATLAB 2017 不支持 sgtitle，使用图窗文本框显示所有子图的总标题。
annotation('textbox', [0.05, 0.91, 0.90, 0.08], ...
    'String', {'舰艇磁场空间分布', spatialFigureText, magneticFigureText}, ...
    'EdgeColor', 'none', ...
    'HorizontalAlignment', 'center', ...
    'VerticalAlignment', 'middle', ...
    'FontWeight', 'bold', ...
    'Interpreter', 'none');

%% 五、绘制感应磁场 Bx、By、Bz 三分量热力图
figure('Name', '感应磁场三分量空间分布', 'Color', 'w');
% 三个分量共用以零为中心的 jet 颜色表。
colormap(jet(64));

componentMatrices = {inducedBx, inducedBy, inducedBz};
componentTitles = {'感应磁场 B_x', '感应磁场 B_y', '感应磁场 B_z'};
for componentIndex = 1:3
    subplot(1, 3, componentIndex);
    componentMatrix = componentMatrices{componentIndex};
    imagesc(x, y, componentMatrix);
    set(gca, 'YDir', 'normal');
    axis equal tight;
    colorbar;

    % 三分量有正有负，令颜色范围以零为中心。
    maximumAbsoluteValue = max(abs(componentMatrix(:)));
    if maximumAbsoluteValue > 0.0
        caxis([-maximumAbsoluteValue, maximumAbsoluteValue]);
    end
    xlabel('东向位置 x / m');
    ylabel('北向位置 y / m');
    title(componentTitles{componentIndex});
end
% MATLAB 2017 不支持 sgtitle，使用图窗文本框显示所有子图的总标题。
annotation('textbox', [0.05, 0.91, 0.90, 0.08], ...
    'String', {'感应磁场三分量空间分布', spatialFigureText, ...
    magneticFigureText}, ...
    'EdgeColor', 'none', ...
    'HorizontalAlignment', 'center', ...
    'VerticalAlignment', 'middle', ...
    'FontWeight', 'bold', ...
    'Interpreter', 'none');

%% 六、绘制综合磁场三维曲面
figure('Name', '综合磁场三维曲面', 'Color', 'w');
surf(X, Y, totalMagnitude, 'EdgeColor', 'none');
colorbar;
colormap(parula);
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
zlabel('综合磁场强度 / nT');
title({'综合目标异常磁场三维曲面', spatialFigureText, ...
    magneticFigureText}, 'Interpreter', 'none');
view(45, 35);
grid on;

%% 七、绘制运动目标综合磁场三分量时间曲线
figure('Name', '运动目标综合磁场三分量', 'Color', 'w');
plot(timeData.time_s, timeData.total_bx_nt, ...
    'r-', 'LineWidth', 1.4);
hold on;
plot(timeData.time_s, timeData.total_by_nt, ...
    'g-', 'LineWidth', 1.4);
plot(timeData.time_s, timeData.total_bz_nt, ...
    'b-', 'LineWidth', 1.4);
hold off;
grid on;
xlabel('时间 / s');
ylabel('综合磁场分量 / nT');
title({'舰艇通过过程中综合磁场三分量', motionFigureText, ...
    samplingFigureText}, 'Interpreter', 'none');
legend('B_x 东向', 'B_y 北向', 'B_z 垂向', 'Location', 'best');

%% 八、绘制静磁场、感应磁场和综合磁场强度时间曲线
figure('Name', '磁场强度随时间变化', 'Color', 'w');
plot(timeData.time_s, timeData.static_magnitude_nt, ...
    'Color', [0.85, 0.33, 0.10], 'LineWidth', 1.4);
hold on;
plot(timeData.time_s, timeData.induced_magnitude_nt, ...
    'Color', [0.00, 0.45, 0.74], 'LineWidth', 1.4);
plot(timeData.time_s, timeData.total_magnitude_nt, ...
    'k-', 'LineWidth', 1.8);
hold off;
grid on;
xlabel('时间 / s');
ylabel('磁场强度 / nT');
title({'舰艇通过过程中磁场强度变化', motionFigureText, ...
    samplingFigureText}, 'Interpreter', 'none');
legend('静磁场', '感应磁场', '综合磁场', 'Location', 'best');

%% 九、绘制综合磁场与目标距离的双坐标轴曲线
figure('Name', '磁场强度与目标距离', 'Color', 'w');
yyaxis left;
plot(timeData.time_s, timeData.total_magnitude_nt, ...
    'b-', 'LineWidth', 1.7);
ylabel('综合磁场强度 / nT');

yyaxis right;
plot(timeData.time_s, timeData.distance_m, ...
    'r--', 'LineWidth', 1.4);
ylabel('目标至传感器距离 / m');

grid on;
xlabel('时间 / s');
title({'综合磁场强度与目标距离随时间变化', motionFigureText, ...
    samplingFigureText}, 'Interpreter', 'none');
legend('综合磁场强度', '目标距离', 'Location', 'best');

%% 十、绘制目标二维航迹和固定传感器位置
figure('Name', '目标航迹', 'Color', 'w');
plot(timeData.target_x_m, timeData.target_y_m, ...
    'b-', 'LineWidth', 1.8);
hold on;
plot(timeData.sensor_x_m(1), timeData.sensor_y_m(1), ...
    'rp', 'MarkerSize', 13, 'MarkerFaceColor', 'r');
plot(timeData.target_x_m(1), timeData.target_y_m(1), ...
    'go', 'MarkerSize', 8, 'MarkerFaceColor', 'g');
plot(timeData.target_x_m(end), timeData.target_y_m(end), ...
    'ks', 'MarkerSize', 8, 'MarkerFaceColor', 'k');
hold off;
axis equal;
grid on;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title({'运动目标航迹与磁传感器位置', motionFigureText, ...
    samplingFigureText}, 'Interpreter', 'none');
legend('目标航迹', '磁传感器', '起点', '终点', ...
    'Location', 'best');

disp('磁场空间分布和运动时序图形已绘制完成。');

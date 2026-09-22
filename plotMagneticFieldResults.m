%% 椭球宏观场、多偶极子局部修整与 HJC 环境合成磁场绘图
% 前两份 CSV 表示升级后的目标模型；第三份 CSV 表示 HJC 同次求得的目标、背景和合成场。

clear;
clc;
close all;

%% 一、当前 CSV 对应的仿真参数
% 这些参数与 C++ 主程序的默认磁场演示场景一致。
% 如果重新生成 CSV 时修改了仿真条件，应同步修改本节，避免图注与数据不一致。

% 两组磁场数据共用的舰艇几何、姿态和磁性参数。
commonParameter.targetType = '水面舰艇';
commonParameter.lengthM = 100.0;
commonParameter.widthM = 15.0;
commonParameter.heightM = 10.0;
commonParameter.pitchDegrees = 0.0;
commonParameter.rollDegrees = 0.0;
commonParameter.geomagneticFieldNt = [18000.0, 30000.0, -42000.0]; % 仅供独立目标模型使用，HJC 改用 WMM。
commonParameter.relativePermeability = 180.0;
commonParameter.magneticMaterialRatio = 0.035;
commonParameter.remanentMagnetizationAm = [7.5, 0.8, -0.4];
commonParameter.minimumDistanceM = 2.0;
commonParameter.dipoleArraySize = [7, 3, 3];
commonParameter.localCorrectionStrength = 0.65;

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
    '固定地磁 ENU=(%.0f, %.0f, %.0f) nT，相对磁导率 %.0f，' ...
    '阵列 %d×%d×%d，修整系数 %.2f'], ...
    commonParameter.geomagneticFieldNt, ...
    commonParameter.relativePermeability, ...
    commonParameter.dipoleArraySize, commonParameter.localCorrectionStrength);
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
fprintf('独立目标模型固定地磁 ENU：(%.0f, %.0f, %.0f) nT；HJC 目标场使用环境 WMM。\n', ...
    commonParameter.geomagneticFieldNt);
fprintf('相对磁导率：%.0f，磁性材料比例：%.3f\n', ...
    commonParameter.relativePermeability, ...
    commonParameter.magneticMaterialRatio);
fprintf('剩磁强度：(%.1f, %.1f, %.1f) A/m\n', ...
    commonParameter.remanentMagnetizationAm);
fprintf('多偶极子候选阵列：%d×%d×%d，局部修整系数：%.2f\n', ...
    commonParameter.dipoleArraySize, commonParameter.localCorrectionStrength);
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

%% 二、读取目标矩阵模型与 HJC 合成结果
% 优先使用工程根目录中的完整结果；CTest 演示结果放在 build/pressure_environment_demo。
scriptPath = mfilename('fullpath');
projectRoot = fileparts(scriptPath);
% 自定义结果目录时可直接填写绝对路径；留空则自动寻找三份同次演示输出。
resultDirectory = '';
if isempty(resultDirectory)
    candidateDirectories = {projectRoot, ...
        fullfile(projectRoot, 'build', 'pressure_environment_demo')};
    for directoryIndex = 1:numel(candidateDirectories)
        candidate = candidateDirectories{directoryIndex};
        if exist(fullfile(candidate, 'magnetic_field_distribution.csv'), 'file') == 2 && ...
                exist(fullfile(candidate, 'magnetic_field_time_series.csv'), 'file') == 2 && ...
                exist(fullfile(candidate, 'magnetic_field_combined_time_series.csv'), 'file') == 2
            resultDirectory = candidate;
            break;
        end
    end
end
if isempty(resultDirectory)
    error('找不到完整磁场结果，请先运行 seamine_simulator 或填写 resultDirectory。');
end
spatialFile = fullfile(resultDirectory, 'magnetic_field_distribution.csv');
timeSeriesFile = fullfile(resultDirectory, 'magnetic_field_time_series.csv');
combinedFile = fullfile(resultDirectory, 'magnetic_field_combined_time_series.csv');

spatialData = readtable(spatialFile);
timeData = readtable(timeSeriesFile);
combinedData = readtable(combinedFile);

% 新矩阵模型必须显式输出宏观场和局部修整量，避免误读旧版 CSV。
requiredSpatialColumns = {'macro_bx_nt', 'macro_by_nt', 'macro_bz_nt', ...
    'local_correction_bx_nt', 'local_correction_by_nt', 'local_correction_bz_nt'};
requiredCombinedColumns = {'target_macro_bx_nt', 'target_macro_by_nt', ...
    'target_macro_bz_nt', 'target_local_correction_bx_nt', ...
    'target_local_correction_by_nt', 'target_local_correction_bz_nt'};
if ~all(ismember(requiredSpatialColumns, spatialData.Properties.VariableNames)) || ...
        ~all(ismember(requiredSpatialColumns, timeData.Properties.VariableNames)) || ...
        ~all(ismember(requiredCombinedColumns, combinedData.Properties.VariableNames))
    error('磁场 CSV 不是椭球宏观场加多偶极子局部修整的新格式，请重新运行 seamine_simulator。');
end

% 三份结果需来自同一目标运动与采样时间轴，避免误把不同场景叠加展示。
if size(timeData, 1) ~= size(combinedData, 1) || ...
        any(abs(timeData.time_s - combinedData.time_s) > 1.0e-9)
    error('独立目标时序与 HJC 合成时序的采样时间轴不一致。');
end
if any(~isfinite(combinedData.signal_magnitude_nt)) || ...
        any(~isfinite(combinedData.environment_magnitude_nt)) || ...
        any(~isfinite(combinedData.total_magnitude_nt))
    error('HJC 磁场结果含有非有限数值。');
end
% 分量合成与标量异常按矢量定义逐点校验，不能将两个模长直接相加。
combinedTolerance = 1.0e-5;
for componentName = {'bx', 'by', 'bz'}
    suffix = componentName{1};
    signalColumn = combinedData.(['signal_' suffix '_nt']);
    environmentColumn = combinedData.(['environment_' suffix '_nt']);
    totalColumn = combinedData.(['total_' suffix '_nt']);
    if any(abs(signalColumn + environmentColumn - totalColumn) > combinedTolerance)
        error('HJC 磁场三分量合成校验失败：%s', suffix);
    end
    macroColumn = combinedData.(['target_macro_' suffix '_nt']);
    correctionColumn = combinedData.(['target_local_correction_' suffix '_nt']);
    if any(abs(macroColumn + correctionColumn - signalColumn) > combinedTolerance)
        error('HJC 目标宏观场与局部修整量合成校验失败：%s', suffix);
    end
end
if any(abs(combinedData.total_magnitude_nt - ...
        combinedData.environment_magnitude_nt - ...
        combinedData.scalar_anomaly_nt) > combinedTolerance)
    error('HJC 标量磁异常与合成场、背景场模长不一致。');
end
fprintf('磁场数据目录：%s\n', resultDirectory);
fprintf('HJC 背景模长：%.3f～%.3f nT；目标异常模长：%.3f～%.3f nT。\n', ...
    min(combinedData.environment_magnitude_nt), max(combinedData.environment_magnitude_nt), ...
    min(combinedData.signal_magnitude_nt), max(combinedData.signal_magnitude_nt));
fprintf('HJC 标量异常：%.3f～%.3f nT。\n', ...
    min(combinedData.scalar_anomaly_nt), max(combinedData.scalar_anomaly_nt));

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
macroMagnitude = reshape(sqrt(planeData.macro_bx_nt.^2 + ...
    planeData.macro_by_nt.^2 + planeData.macro_bz_nt.^2), nx, ny).';
localCorrectionMagnitude = reshape(sqrt(planeData.local_correction_bx_nt.^2 + ...
    planeData.local_correction_by_nt.^2 + ...
    planeData.local_correction_bz_nt.^2), nx, ny).';
inducedBx = reshape(planeData.induced_bx_nt, nx, ny).';
inducedBy = reshape(planeData.induced_by_nt, nx, ny).';
inducedBz = reshape(planeData.induced_bz_nt, nx, ny).';
[X, Y] = meshgrid(x, y);

%% 四、绘制静磁场、感应磁场、宏观场与局部修整热力图
figure('Name', '磁场强度空间分布', 'Color', 'w');
% 整个图窗采用同一颜色表，使用 MATLAB 早期版本支持的经典调用形式。
colormap(parula(64));

subplot(2, 3, 1);
imagesc(x, y, staticMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title(sprintf('目标剩磁场，z = %.1f m', plotDepth));

subplot(2, 3, 2);
imagesc(x, y, inducedMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('目标感应磁场');

subplot(2, 3, 3);
imagesc(x, y, macroMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('椭球宏观场');

subplot(2, 3, 4);
imagesc(x, y, localCorrectionMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('多偶极子局部修整量');

subplot(2, 3, 5);
imagesc(x, y, totalMagnitude);
set(gca, 'YDir', 'normal');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('修整后的综合目标异常场');

subplot(2, 3, 6);
contourf(X, Y, totalMagnitude, 20, 'LineColor', 'none');
axis equal tight;
colorbar;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('椭球-阵列目标异常场等值线');
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
title({'椭球-阵列目标异常磁场三维曲面', spatialFigureText, ...
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
title({'椭球-阵列目标异常磁场三分量', motionFigureText, ...
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
title({'椭球-阵列目标剩磁、感应与合成异常场', motionFigureText, ...
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
title({'椭球-阵列目标异常场与距离随时间变化', motionFigureText, ...
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

%% 十一、分别显示 HJC 的目标、环境与合成磁场三分量
% 背景约为数万 nT，目标异常通常远小于背景，因此三组场采用独立纵轴。
figure('Name', 'HJC 目标、环境与合成磁场', 'Color', 'w');
groupPrefixes = {'signal', 'environment', 'total'};
groupTitles = {'目标异常场 signalOnly', '环境背景场 environmentOnly', ...
    '传感器合成场 totalField'};
for groupIndex = 1:3
    subplot(3, 1, groupIndex);
    prefix = groupPrefixes{groupIndex};
    plot(combinedData.time_s, combinedData.([prefix '_bx_nt']), 'r-', 'LineWidth', 1.2);
    hold on;
    plot(combinedData.time_s, combinedData.([prefix '_by_nt']), 'g-', 'LineWidth', 1.2);
    plot(combinedData.time_s, combinedData.([prefix '_bz_nt']), 'b-', 'LineWidth', 1.2);
    hold off;
    grid on;
    ylabel('磁场 / nT');
    title(groupTitles{groupIndex});
    legend('东向 B_x', '北向 B_y', '上向 B_z', 'Location', 'best');
end
xlabel('时间 / s');

%% 十二、显示 HJC 目标宏观场与多偶极子局部修整量
targetMacroMagnitude = sqrt(combinedData.target_macro_bx_nt.^2 + ...
    combinedData.target_macro_by_nt.^2 + combinedData.target_macro_bz_nt.^2);
targetCorrectionMagnitude = sqrt(combinedData.target_local_correction_bx_nt.^2 + ...
    combinedData.target_local_correction_by_nt.^2 + ...
    combinedData.target_local_correction_bz_nt.^2);
figure('Name', 'HJC 目标矩阵模型分解', 'Color', 'w');
subplot(2, 1, 1);
plot(combinedData.time_s, targetMacroMagnitude, 'k-', 'LineWidth', 1.5);
hold on;
plot(combinedData.time_s, targetCorrectionMagnitude, ...
    'Color', [0.85, 0.33, 0.10], 'LineWidth', 1.5);
hold off;
grid on;
ylabel('磁场模值 / nT');
title('椭球宏观场与局部修整量模值');
legend('|B_{宏观}|', '|\DeltaB_{局部}|', 'Location', 'best');

subplot(2, 1, 2);
plot(combinedData.time_s, combinedData.target_local_correction_bx_nt, 'r-', 'LineWidth', 1.2);
hold on;
plot(combinedData.time_s, combinedData.target_local_correction_by_nt, 'g-', 'LineWidth', 1.2);
plot(combinedData.time_s, combinedData.target_local_correction_bz_nt, 'b-', 'LineWidth', 1.2);
hold off;
grid on;
xlabel('时间 / s');
ylabel('局部修整量 / nT');
title('多偶极子局部修整三分量');
legend('\DeltaB_x', '\DeltaB_y', '\DeltaB_z', 'Location', 'best');

%% 十三、显示目标异常模长、传感器总场模长和有符号标量异常
figure('Name', 'HJC 磁场模长与标量异常', 'Color', 'w');
subplot(3, 1, 1);
plot(timeData.time_s, timeData.total_magnitude_nt, 'k--', 'LineWidth', 1.2);
hold on;
plot(combinedData.time_s, combinedData.signal_magnitude_nt, 'b-', 'LineWidth', 1.5);
hold off;
grid on;
ylabel('目标异常 / nT');
title('独立目标模型与 HJC 目标异常模长对照');
legend('独立模型（固定地磁）', 'HJC（目标位置 WMM 地磁）', 'Location', 'best');

subplot(3, 1, 2);
plot(combinedData.time_s, combinedData.environment_magnitude_nt, 'k--', 'LineWidth', 1.2);
hold on;
plot(combinedData.time_s, combinedData.total_magnitude_nt, 'b-', 'LineWidth', 1.5);
hold off;
grid on;
ylabel('总场模长 / nT');
title('传感器处背景与合成场模长');
legend('|B_{环境}|', '|B_{环境}+B_{目标}|', 'Location', 'best');

subplot(3, 1, 3);
plot(combinedData.time_s, combinedData.scalar_anomaly_nt, ...
    'Color', [0.85, 0.33, 0.10], 'LineWidth', 1.6);
hold on;
plot(combinedData.time_s, zeros(size(combinedData.time_s)), 'k--');
hold off;
grid on;
xlabel('时间 / s');
ylabel('标量异常 / nT');
title('有符号标量异常：|B_{总}|－|B_{环境}|');

disp('椭球宏观场、多偶极子局部修整及 HJC 环境合成图已绘制完成。');

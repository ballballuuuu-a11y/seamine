%% 水下目标静电场与轴频电场仿真结果绘图
% 本脚本读取 C++ 仿真程序生成的电场 CSV，绘制目标通过曲线、
% 静电场/轴频电场/综合电场三分量、综合电场频谱和目标航迹。

clear;
clc;
close all;

%% 一、定位并读取电场仿真结果
% 根据脚本所在目录确定工程根目录，避免依赖 MATLAB 当前工作目录。
scriptPath = mfilename('fullpath');
projectRoot = fileparts(scriptPath);

% 优先读取最近一次默认运行结果；若不存在，再使用工程验证结果。
candidateFiles = {
    fullfile(projectRoot, 'electric_field_simulation.csv')
    fullfile(projectRoot, 'build', 'electric_field_simulation.csv')
    fullfile(projectRoot, 'build', 'validation_electric.csv')
};

electricFile = '';
for fileIndex = 1:numel(candidateFiles)
    if exist(candidateFiles{fileIndex}, 'file') == 2
        electricFile = candidateFiles{fileIndex};
        break;
    end
end

if isempty(electricFile)
    error(['找不到电场仿真结果。请先运行 seamine_simulator，', ...
        '生成 electric_field_simulation.csv。']);
end

electricData = readtable(electricFile);

% 合成电场文件由 HJC 输出；不存在时仍可绘制原来的纯目标结果。
combinedCandidateFiles = {
    fullfile(projectRoot, 'electric_field_combined_time_series.csv')
    fullfile(projectRoot, 'build', 'electric_field_combined_time_series.csv')
    fullfile(projectRoot, 'build', 'Release', 'electric_field_combined_time_series.csv')
};
combinedElectricFile = '';
for fileIndex = 1:numel(combinedCandidateFiles)
    if exist(combinedCandidateFiles{fileIndex}, 'file') == 2
        combinedElectricFile = combinedCandidateFiles{fileIndex};
        break;
    end
end
combinedElectricData = table();
if ~isempty(combinedElectricFile)
    combinedElectricData = readtable(combinedElectricFile);
    combinedRequiredColumns = {
        'time_s', ...
        'signal_ex_uV_m', 'signal_ey_uV_m', 'signal_ez_uV_m', ...
        'environment_ex_uV_m', 'environment_ey_uV_m', 'environment_ez_uV_m', ...
        'total_ex_uV_m', 'total_ey_uV_m', 'total_ez_uV_m', ...
        'signal_magnitude_uV_m', 'environment_magnitude_uV_m', ...
        'total_magnitude_uV_m', 'scalar_anomaly_uV_m'};
    combinedMissingColumns = setdiff(...
        combinedRequiredColumns, combinedElectricData.Properties.VariableNames);
    if ~isempty(combinedMissingColumns)
        error('合成电场 CSV 缺少字段：%s', strjoin(combinedMissingColumns, ', '));
    end
end

% 检查绘图所需字段，避免 CSV 版本不一致时得到难以理解的错误。
requiredColumns = {
    'time_s', 'distance_m', ...
    'target_x_m', 'target_y_m', 'target_z_m', ...
    'static_ex_uV_m', 'static_ey_uV_m', 'static_ez_uV_m', ...
    'shaft_ex_uV_m', 'shaft_ey_uV_m', 'shaft_ez_uV_m', ...
    'total_ex_uV_m', 'total_ey_uV_m', 'total_ez_uV_m', ...
    'static_magnitude_uV_m', 'shaft_magnitude_uV_m', ...
    'shaft_amplitude_uV_m', 'total_magnitude_uV_m', ...
    'frequency_hz'};
missingColumns = setdiff(requiredColumns, electricData.Properties.VariableNames);
if ~isempty(missingColumns)
    error('电场 CSV 缺少字段：%s', strjoin(missingColumns, ', '));
end

% 设置统一中文字体和白色图窗背景。
set(groot, 'defaultAxesFontName', 'Microsoft YaHei');
set(groot, 'defaultTextFontName', 'Microsoft YaHei');
set(groot, 'defaultFigureColor', 'w');

% 图片统一保存到构建目录，便于和 CSV 结果一起查看。
outputDirectory = fullfile(projectRoot, 'build', 'electric_field_figures');
if exist(outputDirectory, 'dir') ~= 7
    mkdir(outputDirectory);
end

time = electricData.time_s;
distance = electricData.distance_m;

% 从 CSV 时间列恢复数字采样参数，避免绘图说明与实际文件不一致。
sampleInterval = median(diff(time));
if ~isfinite(sampleInterval) || sampleInterval <= 0.0
    error('时间列不是严格递增的有效采样序列。');
end
sampleRate = 1.0 / sampleInterval;
shaftFrequency = median(electricData.frequency_hz);

% 距离最小时刻作为最近通过时刻（CPA）。
[minimumDistance, cpaIndex] = min(distance);
cpaTime = time(cpaIndex);

%% 二、记录生成当前演示数据所用的基本参数
% 以下参数对应 main.cpp 中 createDemoTarget() 和主程序的传感器、采样设置。
% CSV 本身保存了时间、位置、距离、频率和电场结果，但没有重复保存全部源参数，
% 因此这里把当前演示场景的参数集中列出，用于图像注释和命令窗口说明。
simulationParameters.targetType = '水面舰船';
simulationParameters.tonnageT = 5000.0;                    % 排水吨位，单位 t。
simulationParameters.lengthM = 100.0;                      % 目标长度，单位 m。
simulationParameters.widthM = 15.0;                        % 目标宽度，单位 m。
simulationParameters.draftM = 5.0;                         % 目标吃水，单位 m。
simulationParameters.velocityMps = 8.0;                    % 航速，单位 m/s。
simulationParameters.shaftSpeedRpm = 120.0;                % 轴转速，单位 r/min。
simulationParameters.salinity = 35.0;                      % 实用盐度。
simulationParameters.initialPositionM = [-300.0, 40.0, -5.0]; % 初始目标中心坐标，单位 m。
simulationParameters.headingDegrees = 0.0;                 % 航向角，单位度。
simulationParameters.pitchDegrees = 0.0;                   % 俯仰角，单位度。
simulationParameters.shaftPhaseDegrees = 0.0;              % 轴频基波初相位，单位度。
simulationParameters.waterTemperatureC = 18.0;             % 海水温度，单位摄氏度。
simulationParameters.seaPressureDbar = 30.0;               % 海水表压，单位 dbar。
simulationParameters.conductivityMode = '自动估算';        % 依据盐度、温度和压力估算。
simulationParameters.staticDipoleMomentMode = '自动估算';  % 依据几何和腐蚀参数估算。
simulationParameters.corrosionCurrentDensity = 1.0e-4;     % 腐蚀电流密度，单位 A/m^2。
simulationParameters.coatingDamageRatio = 0.05;            % 涂层等效破损比例。
simulationParameters.electrodeSeparationMode = '自动估算'; % 依据目标长度估算。
simulationParameters.shaftModulationRatio = 0.04;          % 轴频基波调制比例。
simulationParameters.secondHarmonicRatio = 0.20;           % 二次谐波相对基波比例。
simulationParameters.thirdHarmonicRatio = 0.05;            % 三次谐波相对基波比例。
simulationParameters.minimumDistanceM = 1.0;                % 模型允许的最小距离，单位 m。
simulationParameters.sensorPositionM = [0.0, 0.0, -30.0];  % 固定传感器坐标，单位 m。
simulationParameters.startTimeS = time(1);                  % 当前 CSV 的开始时刻，单位 s。
simulationParameters.durationS = time(end) - time(1);       % 当前 CSV 的仿真时长，单位 s。
simulationParameters.sampleRateHz = sampleRate;             % 当前 CSV 的采样率，单位 Hz。

% 主图顶部使用四行紧凑文字显示最重要的场景参数。
parameterText = {
    sprintf('目标：%s，%.0f t，尺寸 %.0f m × %.0f m × %.0f m，航速 %.1f m/s', ...
        simulationParameters.targetType, simulationParameters.tonnageT, ...
        simulationParameters.lengthM, simulationParameters.widthM, ...
        simulationParameters.draftM, simulationParameters.velocityMps)
    sprintf('运动：初始位置 (%.0f, %.0f, %.0f) m，航向 %.0f°；轴转速 %.0f r/min，轴频 %.1f Hz', ...
        simulationParameters.initialPositionM, ...
        simulationParameters.headingDegrees, ...
        simulationParameters.shaftSpeedRpm, shaftFrequency)
    sprintf('环境与源：盐度 %.0f，温度 %.0f ℃，压力 %.0f dbar；腐蚀电流密度 %.1e A/m²，涂层破损 %.0f%%，轴频调制 %.0f%%', ...
        simulationParameters.salinity, ...
        simulationParameters.waterTemperatureC, ...
        simulationParameters.seaPressureDbar, ...
        simulationParameters.corrosionCurrentDensity, ...
        simulationParameters.coatingDamageRatio * 100.0, ...
        simulationParameters.shaftModulationRatio * 100.0)
    sprintf('观测：传感器 (%.0f, %.0f, %.0f) m，时长 %.1f s，采样率 %.1f Hz，共 %d 点；最近通过 %.1f s / %.2f m', ...
        simulationParameters.sensorPositionM, ...
        simulationParameters.durationS, simulationParameters.sampleRateHz, ...
        height(electricData), cpaTime, minimumDistance)
};

%% 三、绘制目标特性通过曲线
% 上图用距离验证目标确实经历“接近—最近点—远离”；
% 下图展示静电场、轴频电场和综合电场的强度包络。
figureHandle = figure('Name', '电场目标特性通过曲线', ...
    'Position', [100, 50, 1200, 900]);

% 参数框与曲线一起保存，图片脱离工程后仍能看出基本仿真条件。
annotation(figureHandle, 'textbox', [0.08, 0.855, 0.88, 0.125], ...
    'String', parameterText, ...
    'Interpreter', 'none', ...
    'FontName', 'Microsoft YaHei', ...
    'FontSize', 9.0, ...
    'BackgroundColor', [0.94, 0.97, 1.00], ...
    'EdgeColor', [0.45, 0.60, 0.75], ...
    'LineWidth', 0.8, ...
    'Margin', 6.0);

distanceAxes = subplot(2, 1, 1);
set(distanceAxes, 'Position', [0.08, 0.54, 0.88, 0.27]);
plot(time, distance, 'Color', [0.10, 0.45, 0.80], ...
    'LineWidth', 1.8);
hold on;
plot(cpaTime, minimumDistance, 'ro', ...
    'MarkerFaceColor', 'r', 'MarkerSize', 7);
drawVerticalMarker(cpaTime, '最近通过时刻', [0.0, 0.0, 0.0], 'right');
hold off;
grid on;
xlabel('时间 / s');
ylabel('目标至传感器距离 / m');
title('目标接近、经过和远离传感器的距离变化');
legend('目标距离', '最小距离', 'Location', 'best');

fieldAxes = subplot(2, 1, 2);
set(fieldAxes, 'Position', [0.08, 0.10, 0.88, 0.27]);
plot(time, electricData.static_magnitude_uV_m, ...
    'Color', [0.85, 0.33, 0.10], 'LineWidth', 1.5);
hold on;
plot(time, electricData.shaft_magnitude_uV_m, ...
    'Color', [0.00, 0.55, 0.35], 'LineWidth', 1.0);
plot(time, electricData.total_magnitude_uV_m, ...
    'Color', [0.10, 0.10, 0.10], 'LineWidth', 1.8);
drawVerticalMarker(cpaTime, '最近通过时刻', [0.0, 0.0, 0.0], 'right');
hold off;
grid on;
xlabel('时间 / s');
ylabel('电场强度 / (\muV/m)');
title('静电场、轴频电场和综合电场强度通过曲线');
legend('静电场模值', '轴频电场瞬时模值', '综合电场模值', ...
    'Location', 'best');

saveFigure(figureHandle, ...
    fullfile(outputDirectory, '01_电场目标特性通过曲线.png'));

%% 四、绘制静电场三分量
figureHandle = figure('Name', '静电场三分量', ...
    'Position', [120, 100, 1100, 620]);
plotThreeComponents(...
    time, ...
    electricData.static_ex_uV_m, ...
    electricData.static_ey_uV_m, ...
    electricData.static_ez_uV_m, ...
    cpaTime, ...
    '静电场三分量目标通过曲线');
saveFigure(figureHandle, ...
    fullfile(outputDirectory, '02_静电场三分量.png'));

%% 五、绘制轴频电场三分量
figureHandle = figure('Name', '轴频电场三分量', ...
    'Position', [140, 120, 1100, 620]);
plotThreeComponents(...
    time, ...
    electricData.shaft_ex_uV_m, ...
    electricData.shaft_ey_uV_m, ...
    electricData.shaft_ez_uV_m, ...
    cpaTime, ...
    '轴频电场三分量及距离调制包络');
saveFigure(figureHandle, ...
    fullfile(outputDirectory, '03_轴频电场三分量.png'));

%% 六、绘制综合电场三分量
figureHandle = figure('Name', '综合电场三分量', ...
    'Position', [160, 140, 1100, 620]);
plotThreeComponents(...
    time, ...
    electricData.total_ex_uV_m, ...
    electricData.total_ey_uV_m, ...
    electricData.total_ez_uV_m, ...
    cpaTime, ...
    '静电场与轴频电场矢量叠加后的综合三分量');
saveFigure(figureHandle, ...
    fullfile(outputDirectory, '04_综合电场三分量.png'));

%% 七、绘制目标电场、环境背景和传感器合成场
if ~isempty(combinedElectricFile)
    if height(combinedElectricData) ~= height(electricData) || ...
            any(abs(combinedElectricData.time_s - time) > 1.0e-9)
        error('目标电场与合成电场 CSV 的时间轴不一致。');
    end

    figureHandle = figure('Name', '目标电场与环境背景合成', ...
        'Position', [175, 150, 1160, 820]);
    subplot(2, 1, 1);
    plot(time, combinedElectricData.signal_magnitude_uV_m, ...
        'Color', [0.85, 0.33, 0.10], 'LineWidth', 1.4);
    hold on;
    plot(time, combinedElectricData.environment_magnitude_uV_m, ...
        'Color', [0.00, 0.55, 0.35], 'LineWidth', 1.3);
    plot(time, combinedElectricData.total_magnitude_uV_m, ...
        'Color', [0.10, 0.10, 0.10], 'LineWidth', 1.7);
    drawVerticalMarker(cpaTime, '最近通过时刻', [0.0, 0.0, 0.0], 'right');
    hold off;
    grid on;
    xlabel('时间 / s');
    ylabel('电场模值 / (\muV/m)');
    title('目标场、环境背景场与传感器合成场');
    legend('目标电场', '环境背景电场', '传感器合成电场', 'Location', 'best');

    subplot(2, 1, 2);
    plot(time, combinedElectricData.total_ex_uV_m, 'r-', 'LineWidth', 1.2);
    hold on;
    plot(time, combinedElectricData.total_ey_uV_m, ...
        'Color', [0.00, 0.55, 0.25], 'LineWidth', 1.2);
    plot(time, combinedElectricData.total_ez_uV_m, 'b-', 'LineWidth', 1.2);
    drawVerticalMarker(cpaTime, '最近通过时刻', [0.0, 0.0, 0.0], 'right');
    drawHorizontalReference(0.0, [0.40, 0.40, 0.40], ':');
    hold off;
    grid on;
    xlabel('时间 / s');
    ylabel('合成电场分量 / (\muV/m)');
    title('传感器合成电场 ENU 三分量');
    legend('E_x 东向', 'E_y 北向', 'E_z 垂向', 'Location', 'best');
    saveFigure(figureHandle, ...
        fullfile(outputDirectory, '07_目标环境与合成电场.png'));
end

%% 八、绘制轴频电场分量频谱
% 频谱必须使用带正负号的分量，不使用始终非负的模值，
% 否则取绝对值会人为产生额外的二倍频成分。
componentMatrix = [
    electricData.shaft_ex_uV_m, ...
    electricData.shaft_ey_uV_m, ...
    electricData.shaft_ez_uV_m];
componentNames = {'E_x', 'E_y', 'E_z'};

% 选择标准差最大的分量，使轴频峰更容易观察。
componentStandardDeviation = std(componentMatrix, 0, 1);
[~, spectrumComponentIndex] = max(componentStandardDeviation);
spectrumSignal = componentMatrix(:, spectrumComponentIndex);

sampleCount = numel(spectrumSignal);

% 去除直流均值，并使用手工汉宁窗减少有限时长造成的频谱泄漏。
centeredSignal = spectrumSignal - mean(spectrumSignal);
if sampleCount > 1
    sampleIndex = (0:(sampleCount - 1)).';
    window = 0.5 - 0.5 * cos(2.0 * pi * sampleIndex / (sampleCount - 1));
else
    window = 1.0;
end
windowedSignal = centeredSignal .* window;
spectrum = fft(windowedSignal);
oneSidedCount = floor(sampleCount / 2) + 1;
frequencyAxis = (0:(oneSidedCount - 1)).' * sampleRate / sampleCount;

% 按窗函数有效增益归一化单边幅度谱。
windowGain = sum(window);
amplitudeSpectrum = abs(spectrum(1:oneSidedCount)) * 2.0 / windowGain;
amplitudeSpectrum(1) = amplitudeSpectrum(1) / 2.0;
if rem(sampleCount, 2) == 0
    amplitudeSpectrum(end) = amplitudeSpectrum(end) / 2.0;
end

maximumDisplayFrequency = min(sampleRate / 2.0, ...
    max(10.0, 4.0 * shaftFrequency));

figureHandle = figure('Name', '轴频电场频谱', ...
    'Position', [180, 160, 1050, 560]);
plot(frequencyAxis, amplitudeSpectrum, ...
    'Color', [0.20, 0.35, 0.75], 'LineWidth', 1.4);
hold on;
drawVerticalMarker(shaftFrequency, '轴频基波', ...
    [1.00, 0.00, 0.00], 'left');
drawVerticalMarker(2.0 * shaftFrequency, '二次谐波', ...
    [0.85, 0.45, 0.10], 'left');
drawVerticalMarker(3.0 * shaftFrequency, '三次谐波', ...
    [0.45, 0.25, 0.70], 'left');
hold off;
xlim([0.0, maximumDisplayFrequency]);
grid on;
xlabel('频率 / Hz');
ylabel('单边幅度 / (\muV/m)');
title(sprintf('轴频电场 %s 分量频谱', ...
    componentNames{spectrumComponentIndex}));
saveFigure(figureHandle, ...
    fullfile(outputDirectory, '05_轴频电场分量频谱.png'));

%% 九、绘制目标航迹
% 当前 C++ 示例的电场传感器固定在 (0, 0, -30) m。
sensorPosition = simulationParameters.sensorPositionM;

figureHandle = figure('Name', '目标航迹与电场传感器', ...
    'Position', [200, 180, 980, 650]);
plot(electricData.target_x_m, electricData.target_y_m, ...
    'b-', 'LineWidth', 1.8);
hold on;
plot(sensorPosition(1), sensorPosition(2), ...
    'rp', 'MarkerSize', 13, 'MarkerFaceColor', 'r');
plot(electricData.target_x_m(1), electricData.target_y_m(1), ...
    'go', 'MarkerSize', 8, 'MarkerFaceColor', 'g');
plot(electricData.target_x_m(end), electricData.target_y_m(end), ...
    'ks', 'MarkerSize', 8, 'MarkerFaceColor', 'k');
plot(electricData.target_x_m(cpaIndex), ...
    electricData.target_y_m(cpaIndex), ...
    'mo', 'MarkerSize', 8, 'MarkerFaceColor', 'm');
hold off;
axis equal;
grid on;
xlabel('东向位置 x / m');
ylabel('北向位置 y / m');
title('目标航迹与固定电场传感器位置');
legend('目标航迹', '电场传感器', '起点', '终点', ...
    '最近通过位置', 'Location', 'best');
saveFigure(figureHandle, ...
    fullfile(outputDirectory, '06_目标航迹.png'));

%% 十、输出主要结果和完整仿真参数
fprintf('已读取电场仿真文件：%s\n', electricFile);
fprintf('电场采样点数：%d\n', height(electricData));
fprintf('采样率：%.6f Hz\n', sampleRate);
fprintf('轴频基波：%.6f Hz\n', shaftFrequency);
fprintf('最近通过时刻：%.6f s\n', cpaTime);
fprintf('最近距离：%.6f m\n', minimumDistance);
fprintf('图片输出目录：%s\n', outputDirectory);
if ~isempty(combinedElectricFile)
    fprintf('已读取合成电场文件：%s\n', combinedElectricFile);
else
    fprintf('未找到合成电场文件，仅绘制纯目标电场。\n');
end
fprintf('\n========== 当前电场仿真基本参数 ==========\n');
fprintf('目标类型：%s\n', simulationParameters.targetType);
fprintf('排水吨位：%.0f t\n', simulationParameters.tonnageT);
fprintf('目标尺寸：长 %.1f m，宽 %.1f m，吃水 %.1f m\n', ...
    simulationParameters.lengthM, simulationParameters.widthM, ...
    simulationParameters.draftM);
fprintf('航速：%.3f m/s，航向：%.1f°，俯仰：%.1f°\n', ...
    simulationParameters.velocityMps, simulationParameters.headingDegrees, ...
    simulationParameters.pitchDegrees);
fprintf('初始位置：(%.1f, %.1f, %.1f) m\n', ...
    simulationParameters.initialPositionM);
fprintf('传感器位置：(%.1f, %.1f, %.1f) m\n', ...
    simulationParameters.sensorPositionM);
fprintf('轴转速：%.1f r/min，轴频：%.3f Hz，初相位：%.1f°\n', ...
    simulationParameters.shaftSpeedRpm, shaftFrequency, ...
    simulationParameters.shaftPhaseDegrees);
fprintf('海水：盐度 %.1f，温度 %.1f ℃，压力 %.1f dbar，电导率%s\n', ...
    simulationParameters.salinity, simulationParameters.waterTemperatureC, ...
    simulationParameters.seaPressureDbar, ...
    simulationParameters.conductivityMode);
fprintf('腐蚀电流密度：%.3e A/m²，涂层破损比例：%.1f%%\n', ...
    simulationParameters.corrosionCurrentDensity, ...
    simulationParameters.coatingDamageRatio * 100.0);
fprintf('静态偶极矩：%s，等效电极间距：%s\n', ...
    simulationParameters.staticDipoleMomentMode, ...
    simulationParameters.electrodeSeparationMode);
fprintf('轴频调制：%.1f%%，二次谐波：%.1f%%，三次谐波：%.1f%%\n', ...
    simulationParameters.shaftModulationRatio * 100.0, ...
    simulationParameters.secondHarmonicRatio * 100.0, ...
    simulationParameters.thirdHarmonicRatio * 100.0);
fprintf('仿真时间：%.1f～%.1f s，采样率：%.1f Hz，采样点：%d\n', ...
    simulationParameters.startTimeS, ...
    simulationParameters.startTimeS + simulationParameters.durationS, ...
    simulationParameters.sampleRateHz, height(electricData));
fprintf('最近通过：%.3f s，最近距离：%.3f m\n', ...
    cpaTime, minimumDistance);

%% 局部函数：绘制三分量曲线
function plotThreeComponents(time, ex, ey, ez, cpaTime, figureTitle)
%PLOTTHREECOMPONENTS 绘制带最近通过时刻标记的电场三分量曲线。
% time：时间序列，单位 s。
% ex、ey、ez：三个方向的电场分量，单位 μV/m。
% cpaTime：目标最近通过时刻，单位 s。
% figureTitle：图形标题文字。

plot(time, ex, 'r-', 'LineWidth', 1.25);
hold on;
plot(time, ey, 'Color', [0.00, 0.55, 0.25], 'LineWidth', 1.25);
plot(time, ez, 'b-', 'LineWidth', 1.25);
drawVerticalMarker(cpaTime, '最近通过时刻', [0.0, 0.0, 0.0], 'right');
drawHorizontalReference(0.0, [0.40, 0.40, 0.40], ':');
hold off;
grid on;
xlabel('时间 / s');
ylabel('电场分量 / (\muV/m)');
title(figureTitle);
legend('E_x 东向', 'E_y 北向', 'E_z 垂向', ...
    'Location', 'best');
end

%% 局部函数：绘制兼容 MATLAB 2017 的竖直标记线
function drawVerticalMarker(xValue, labelText, lineColor, horizontalAlignment)
%DRAWVERTICALMARKER 使用 line 和 text 绘制竖直标记线。
% xValue：标记线所在的横坐标。
% labelText：标记线上方显示的说明文字。
% lineColor：标记线和文字使用的 RGB 颜色。
% horizontalAlignment：文字相对标记线的水平对齐方式。
% 说明：MATLAB 2017 没有 xline，因此使用基础图形函数实现相同效果。

currentAxes = gca;
xLimits = get(currentAxes, 'XLim');
yLimits = get(currentAxes, 'YLim');
xOffset = 0.006 * (xLimits(2) - xLimits(1));
yOffset = 0.025 * (yLimits(2) - yLimits(1));

line([xValue, xValue], yLimits, ...
    'Color', lineColor, ...
    'LineStyle', '--', ...
    'LineWidth', 0.8, ...
    'HandleVisibility', 'off');

if strcmp(horizontalAlignment, 'right')
    labelX = xValue - xOffset;
else
    labelX = xValue + xOffset;
end

text(labelX, yLimits(2) - yOffset, labelText, ...
    'Color', lineColor, ...
    'FontName', 'Microsoft YaHei', ...
    'FontSize', 9.0, ...
    'HorizontalAlignment', horizontalAlignment, ...
    'VerticalAlignment', 'top', ...
    'Clipping', 'on', ...
    'HandleVisibility', 'off');
end

%% 局部函数：绘制兼容 MATLAB 2017 的水平参考线
function drawHorizontalReference(yValue, lineColor, lineStyle)
%DRAWHORIZONTALREFERENCE 使用 line 绘制水平参考线。
% yValue：参考线所在的纵坐标。
% lineColor：参考线使用的 RGB 颜色。
% lineStyle：参考线线型，例如 ':' 或 '--'。
% 说明：MATLAB 2017 没有 yline，因此使用基础 line 函数实现。

currentAxes = gca;
xLimits = get(currentAxes, 'XLim');
line(xLimits, [yValue, yValue], ...
    'Color', lineColor, ...
    'LineStyle', lineStyle, ...
    'LineWidth', 0.8, ...
    'HandleVisibility', 'off');
end

%% 局部函数：使用 MATLAB 2017 支持的方式保存图像
function saveFigure(figureHandle, outputPath)
%SAVEFIGURE 使用 print 以 180 DPI 保存 PNG 图像。
% figureHandle：需要保存的 MATLAB 图窗句柄。
% outputPath：PNG 图片的完整输出路径。
% 说明：不使用 MATLAB R2020a 才引入的 exportgraphics。

set(figureHandle, 'PaperPositionMode', 'auto');
drawnow;
print(figureHandle, outputPath, '-dpng', '-r180');
end

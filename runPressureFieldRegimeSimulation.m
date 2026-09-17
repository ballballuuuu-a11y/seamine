function results = runPressureFieldRegimeSimulation(outputDirectory, showFigures)
%RUNPRESSUREFIELDREGIMESIMULATION 运行舰艇/潜艇水压场分工况仿真并绘图。
%   兼容 MATLAB R2017a/R2017b，不使用 string、tiledlayout、sgtitle、
%   xline、yline、exportgraphics 或依赖新版本的隐式数组扩展。
%   RESULTS = RUNPRESSUREFIELDREGIMESIMULATION() 使用默认参数计算水面舰艇
%   低速、平滑过渡、浅水亚临界以及深潜潜艇四种工况，显示图窗并保存
%   CSV、MAT 和 PNG 结果。
%
%   RESULTS = RUNPRESSUREFIELDREGIMESIMULATION(OUTPUTDIRECTORY, SHOWFIGURES)
%   可指定输出目录，并控制是否显示图窗。批处理验证时可将 SHOWFIGURES
%   设为 false，正常交互运行时建议保持 true。

    %% 一、初始化输出环境
    scriptPath = mfilename('fullpath');
    projectRoot = fileparts(scriptPath);
    if nargin < 1 || isempty(outputDirectory)
        outputDirectory = fullfile( ...
            projectRoot, 'matlab_pressure_regime_results');
    end
    if nargin < 2 || isempty(showFigures)
        showFigures = true;
    end
    if exist(outputDirectory, 'dir') ~= 7
        mkdir(outputDirectory);
    end

    % 统一使用中文字体，避免图题和坐标轴标签显示为方框。
    % 使用传统根图形句柄 0，确保 MATLAB R2017 可正确设置默认字体。
    set(0, 'defaultAxesFontName', 'Microsoft YaHei');
    set(0, 'defaultTextFontName', 'Microsoft YaHei');
    figureVisibility = 'off';
    if showFigures
        figureVisibility = 'on';
    end

    %% 二、配置与 C++ 模型一致的工况判定参数
    modelOptions.lowSpeedLengthFroudeThreshold = 0.10;  % 过渡带中心。
    modelOptions.lengthFroudeTransitionHalfWidth = 0.02; % 过渡带半宽。
    modelOptions.shallowWaterRatioThreshold = 0.30;     % 浅水判据 H/L 上限。
    modelOptions.criticalDepthFroudeMargin = 0.05;      % FrH 临界区半宽。
    modelOptions.waveAttenuationTolerance = 0.01;       % 潜艇兴波衰减阈值。

    simulationStartTimeS = 0.0;  % 时序仿真开始时刻，单位 s。
    simulationDurationS = 60.0;  % 时序仿真持续时间，单位 s。
    sampleRateHz = 10.0;         % 时序采样率，单位 Hz。

    %% 三、构造四种可对比的典型工况
    surfaceBase = createSurfaceShipParameter(modelOptions);
    surfaceSensor = [0.0, 25.0, -25.0]; % 水面舰艇共用固定传感器。

    lowSpeedParameter = surfaceBase;
    lowSpeedParameter.velocity = 0.06 * sqrt( ...
        lowSpeedParameter.gravityAcceleration * lowSpeedParameter.length);
    lowSpeedParameter.initialPosition(1) = ...
        -0.5 * simulationDurationS * lowSpeedParameter.velocity;

    transitionParameter = surfaceBase;
    transitionParameter.velocity = 0.10 * sqrt( ...
        transitionParameter.gravityAcceleration * transitionParameter.length);
    transitionParameter.initialPosition(1) = ...
        -0.5 * simulationDurationS * transitionParameter.velocity;

    shallowWaveParameter = surfaceBase;
    shallowWaveParameter.velocity = 0.18 * sqrt( ...
        shallowWaveParameter.gravityAcceleration * shallowWaveParameter.length);
    shallowWaveParameter.initialPosition(1) = ...
        -0.5 * simulationDurationS * shallowWaveParameter.velocity;

    submarineParameter = createSubmarineParameter(modelOptions);
    submarineParameter.initialPosition(1) = ...
        -0.5 * simulationDurationS * submarineParameter.velocity;
    submarineSensor = [0.0, 20.0, -80.0]; % 深潜潜艇固定传感器。

    scenarioDefinitions(1) = createScenarioDefinition( ...
        '水面舰艇低速回转体', 'surface_ship_low_speed', ...
        lowSpeedParameter, surfaceSensor);
    scenarioDefinitions(2) = createScenarioDefinition( ...
        '水面舰艇平滑过渡', 'surface_ship_transition', ...
        transitionParameter, surfaceSensor);
    scenarioDefinitions(3) = createScenarioDefinition( ...
        '水面舰艇浅水亚临界', 'surface_ship_shallow_subcritical', ...
        shallowWaveParameter, surfaceSensor);
    scenarioDefinitions(4) = createScenarioDefinition( ...
        '深潜潜艇无兴波', 'submarine_no_wave', ...
        submarineParameter, submarineSensor);

    %% 四、逐工况计算时序并保存 CSV
    scenarioResults = repmat(struct(), size(scenarioDefinitions));
    summaryRows = cell(numel(scenarioDefinitions), 8);
    for scenarioIndex = 1:numel(scenarioDefinitions)
        definition = scenarioDefinitions(scenarioIndex);
        pressureTable = simulatePressureTimeSeries( ...
            definition.parameter, ...
            definition.sensorPosition, ...
            simulationStartTimeS, ...
            simulationDurationS, ...
            sampleRateHz);

        scenarioResults(scenarioIndex).name = definition.name;
        scenarioResults(scenarioIndex).fileStem = definition.fileStem;
        scenarioResults(scenarioIndex).parameter = definition.parameter;
        scenarioResults(scenarioIndex).sensorPosition = ...
            definition.sensorPosition;
        scenarioResults(scenarioIndex).pressureTable = pressureTable;

        csvPath = fullfile( ...
            outputDirectory, [definition.fileStem, '.csv']);
        writetable(pressureTable, csvPath);

        [maximumAbsolutePressure, maximumIndex] = max( ...
            abs(pressureTable.dynamic_pressure_pa));
        summaryRows(scenarioIndex, :) = { ...
            definition.name, ...
            pressureTable.regime{1}, ...
            definition.parameter.velocity, ...
            pressureTable.length_froude_number(1), ...
            pressureTable.depth_froude_number(1), ...
            definition.parameter.waterDepth / definition.parameter.length, ...
            maximumAbsolutePressure, ...
            pressureTable.time_s(maximumIndex)};
    end

    summaryTable = cell2table(summaryRows, 'VariableNames', { ...
        'scenario', 'regime', 'velocity_mps', 'length_froude_number', ...
        'depth_froude_number', 'water_depth_to_length', ...
        'maximum_absolute_dynamic_pressure_pa', 'peak_time_s'});
    writetable(summaryTable, fullfile(outputDirectory, 'scenario_summary.csv'));

    %% 五、绘制四种工况的压力分量时序
    timeSeriesFigure = plotScenarioTimeSeries( ...
        scenarioResults, figureVisibility);
    saveas(timeSeriesFigure, fullfile( ...
        outputDirectory, 'pressure_regime_time_series.png'));

    %% 六、绘制过渡权重和固定观测点压力连续性
    transitionFigure = plotTransitionBehavior( ...
        surfaceBase, surfaceSensor, figureVisibility);
    saveas(transitionFigure, fullfile( ...
        outputDirectory, 'pressure_transition_behavior.png'));

    %% 七、绘制低速、过渡和浅水兴波空间分布
    spatialFigure = plotSurfaceShipSpatialFields( ...
        scenarioDefinitions(1:3), ...
        simulationDurationS * 0.5, ...
        figureVisibility);
    saveas(spatialFigure, fullfile( ...
        outputDirectory, 'surface_ship_pressure_fields.png'));

    %% 八、汇总返回结果并保存 MAT 文件
    results.outputDirectory = outputDirectory;       % 所有输出文件所在目录。
    results.modelOptions = modelOptions;             % 本次使用的模型阈值。
    results.scenarios = scenarioResults;             % 四种工况的完整时序。
    results.summary = summaryTable;                  % 便于比较的汇总表。
    save(fullfile(outputDirectory, 'pressure_regime_results.mat'), ...
        'results');

    fprintf('\n========== MATLAB 水压场分工况仿真完成 ==========\n');
    disp(summaryTable);
    fprintf('结果目录：%s\n', outputDirectory);
    fprintf('已生成：4 个时序 CSV、1 个汇总 CSV、1 个 MAT 和 3 张 PNG。\n');
end

function parameter = createSurfaceShipParameter(modelOptions)
%CREATESURFACESHIPPARAMETER 创建浅水水面舰艇公共参数。
    parameter.type = 'surfaceShip';             % 目标类型为水面舰艇。
    parameter.length = 120.0;                   % 舰长，单位 m。
    parameter.width = 18.0;                     % 舰宽，单位 m。
    parameter.draft = 6.0;                      % 吃水，单位 m。
    parameter.height = 0.0;                     % 水面舰艇不使用潜艇高度。
    parameter.waterDepth = 36.0;                % H/L=0.3，位于浅水边界。
    parameter.velocity = 0.0;                   % 各工况在主流程中单独设置。
    parameter.initialPosition = [0.0, 0.0, -3.0]; % 回转体中心位于半吃水处。
    parameter.headingDegrees = 0.0;             % 舰首沿全局 x 轴正向。
    parameter.waterDensity = 1025.0;            % 海水密度，单位 kg/m^3。
    parameter.gravityAcceleration = 9.80665;    % 重力加速度，单位 m/s^2。
    parameter.blockCoefficient = 0.68;          % 等效排水体积方形系数。
    parameter.minimumDistance = 1.0;            % 最小偶极子距离，单位 m。
    parameter.modelOptions = modelOptions;      % 工况选择阈值。
end

function parameter = createSubmarineParameter(modelOptions)
%CREATESUBMARINEPARAMETER 创建深潜潜艇无兴波工况参数。
    parameter.type = 'submarine';               % 目标类型为潜艇。
    parameter.length = 80.0;                    % 艇长，单位 m。
    parameter.width = 10.0;                     % 艇宽，单位 m。
    parameter.draft = 0.0;                      % 新接口使用 height，不再借用 draft。
    parameter.height = 10.0;                    % 艇体垂向高度，单位 m。
    parameter.waterDepth = 100.0;               % 海深，单位 m。
    parameter.velocity = 8.0;                   % 航速，单位 m/s。
    parameter.initialPosition = [0.0, 0.0, -35.0]; % 艇体中心潜深为35 m。
    parameter.headingDegrees = 0.0;             % 艇艏沿全局 x 轴正向。
    parameter.waterDensity = 1025.0;            % 海水密度，单位 kg/m^3。
    parameter.gravityAcceleration = 9.80665;    % 重力加速度，单位 m/s^2。
    parameter.blockCoefficient = 0.55;          % 潜艇等效排水体积方形系数。
    parameter.minimumDistance = 1.0;            % 最小偶极子距离，单位 m。
    parameter.modelOptions = modelOptions;      % 工况选择阈值。
end

function definition = createScenarioDefinition( ...
    name, fileStem, parameter, sensorPosition)
%CREATESCENARIODEFINITION 组合单个仿真场景的输入信息。
    definition.name = name;                     % 图窗和汇总表使用的中文名称。
    definition.fileStem = fileStem;             % 输出文件使用的稳定英文名称。
    definition.parameter = parameter;           % 完整目标与环境参数。
    definition.sensorPosition = sensorPosition; % 固定水压传感器坐标。
end

function pressureTable = simulatePressureTimeSeries( ...
    parameter, sensorPosition, startTimeS, durationS, sampleRateHz)
%SIMULATEPRESSURETIMESERIES 计算固定传感器处的连续水压时序。
    validateParameter(parameter);
    sampleCount = floor(durationS * sampleRateHz) + 1;
    timeS = startTimeS + (0:(sampleCount - 1))' / sampleRateHz;

    % 预分配全部结果列，避免在循环中动态扩展数组。
    targetX = zeros(sampleCount, 1);
    targetY = zeros(sampleCount, 1);
    targetZ = zeros(sampleCount, 1);
    regime = cell(sampleCount, 1);
    lengthFroude = zeros(sampleCount, 1);
    depthFroude = zeros(sampleCount, 1);
    waveAttenuation = zeros(sampleCount, 1);
    transitionWeight = zeros(sampleCount, 1);
    bodyPressure = zeros(sampleCount, 1);
    freeSurfacePressure = zeros(sampleCount, 1);
    wavePressure = zeros(sampleCount, 1);
    seabedPressure = zeros(sampleCount, 1);
    dynamicPressure = zeros(sampleCount, 1);
    hydrostaticPressure = zeros(sampleCount, 1);
    totalPressure = zeros(sampleCount, 1);
    longitudinalOffset = zeros(sampleCount, 1);
    lateralOffset = zeros(sampleCount, 1);
    distanceM = zeros(sampleCount, 1);

    for sampleIndex = 1:sampleCount
        sample = calculatePressureSample( ...
            parameter, sensorPosition, timeS(sampleIndex));
        targetX(sampleIndex) = sample.targetPosition(1);
        targetY(sampleIndex) = sample.targetPosition(2);
        targetZ(sampleIndex) = sample.targetPosition(3);
        regime{sampleIndex} = sample.regime;
        lengthFroude(sampleIndex) = sample.lengthFroudeNumber;
        depthFroude(sampleIndex) = sample.depthFroudeNumber;
        waveAttenuation(sampleIndex) = sample.waveAttenuation;
        transitionWeight(sampleIndex) = sample.transitionWeight;
        bodyPressure(sampleIndex) = sample.bodyDynamicPressure;
        freeSurfacePressure(sampleIndex) = ...
            sample.freeSurfaceCorrectionPressure;
        wavePressure(sampleIndex) = sample.wavePressure;
        seabedPressure(sampleIndex) = sample.seabedCorrectionPressure;
        dynamicPressure(sampleIndex) = sample.dynamicPressure;
        hydrostaticPressure(sampleIndex) = sample.hydrostaticPressure;
        totalPressure(sampleIndex) = sample.totalGaugePressure;
        longitudinalOffset(sampleIndex) = sample.longitudinalOffset;
        lateralOffset(sampleIndex) = sample.lateralOffset;
        distanceM(sampleIndex) = sample.distance;
    end

    pressureTable = table( ...
        timeS, targetX, targetY, targetZ, ...
        repmat(sensorPosition(1), sampleCount, 1), ...
        repmat(sensorPosition(2), sampleCount, 1), ...
        repmat(sensorPosition(3), sampleCount, 1), ...
        distanceM, longitudinalOffset, lateralOffset, ...
        repmat(-sensorPosition(3), sampleCount, 1), ...
        regime, lengthFroude, depthFroude, waveAttenuation, ...
        transitionWeight, hydrostaticPressure, bodyPressure, ...
        freeSurfacePressure, wavePressure, seabedPressure, ...
        dynamicPressure, totalPressure, ...
        'VariableNames', { ...
        'time_s', 'target_x_m', 'target_y_m', 'target_z_m', ...
        'sensor_x_m', 'sensor_y_m', 'sensor_z_m', 'distance_m', ...
        'longitudinal_offset_m', 'lateral_offset_m', ...
        'observation_depth_m', 'regime', 'length_froude_number', ...
        'depth_froude_number', 'wave_attenuation', ...
        'transition_weight', 'hydrostatic_pressure_pa', ...
        'body_dynamic_pressure_pa', ...
        'free_surface_correction_pressure_pa', 'wave_pressure_pa', ...
        'seabed_correction_pressure_pa', 'dynamic_pressure_pa', ...
        'total_gauge_pressure_pa'});
end

function sample = calculatePressureSample(parameter, observationPosition, timeS)
%CALCULATEPRESSURESAMPLE 复现 C++ 模型的单点、单时刻压力计算流程。
    headingRadians = parameter.headingDegrees * pi / 180.0;
    headingX = cos(headingRadians);
    headingY = sin(headingRadians);
    traveledDistance = parameter.velocity * timeS;
    targetPosition = parameter.initialPosition + ...
        [headingX * traveledDistance, headingY * traveledDistance, 0.0];

    displacement = observationPosition - targetPosition;
    bodyDisplacement = [ ...
        headingX * displacement(1) + headingY * displacement(2), ...
        -headingY * displacement(1) + headingX * displacement(2), ...
        displacement(3)];
    distanceM = norm(displacement);
    verticalSize = effectiveVerticalSize(parameter);

    % 观测点进入等效椭球时，点偶极子模型失去适用性。
    ellipsoidCoordinate = ...
        bodyDisplacement(1)^2 / (0.5 * parameter.length)^2 + ...
        bodyDisplacement(2)^2 / (0.5 * parameter.width)^2 + ...
        bodyDisplacement(3)^2 / (0.5 * verticalSize)^2;
    if ellipsoidCoordinate <= 1.0 || distanceM < parameter.minimumDistance
        error('观测点位于目标内部或距离目标过近。');
    end

    lengthFroude = parameter.velocity / sqrt( ...
        parameter.gravityAcceleration * parameter.length);
    depthFroude = parameter.velocity / sqrt( ...
        parameter.gravityAcceleration * parameter.waterDepth);
    waveAttenuation = calculateSubmarineWaveAttenuation(parameter);
    selectedRegime = selectPressureRegime( ...
        parameter, lengthFroude, depthFroude, waveAttenuation);
    assertSupportedRegime(selectedRegime);

    dipoleStrengthPerSpeed = calculateDipoleStrengthPerSpeed(parameter);
    pressureCoefficient = parameter.waterDensity * ...
        dipoleStrengthPerSpeed * parameter.velocity^2;
    bodyPressure = calculateImagePressure( ...
        observationPosition, targetPosition, headingX, headingY, ...
        parameter.minimumDistance, pressureCoefficient);

    surfaceImage = [targetPosition(1), targetPosition(2), -targetPosition(3)];
    seabedImage = [targetPosition(1), targetPosition(2), ...
        -2.0 * parameter.waterDepth - targetPosition(3)];
    freeSurfacePressure = 0.0;
    wavePressure = 0.0;
    transitionWeight = 0.0;

    isLowSpeedSubmarine = strcmp(selectedRegime, 'submarine_no_wave') && ...
        lengthFroude <= ...
        parameter.modelOptions.lowSpeedLengthFroudeThreshold;
    if strcmp(selectedRegime, 'surface_ship_low_speed') || ...
            isLowSpeedSubmarine
        % 低速工况使用完整周期镜像刚盖结果。
        freeSurfacePressure = calculateImagePressure( ...
            observationPosition, surfaceImage, headingX, headingY, ...
            parameter.minimumDistance, pressureCoefficient);
        rigidLidTotal = calculateRigidLidTotalPressure( ...
            observationPosition, targetPosition, parameter.waterDepth, ...
            headingX, headingY, parameter.minimumDistance, ...
            pressureCoefficient);
        seabedPressure = rigidLidTotal - bodyPressure - freeSurfacePressure;
    elseif strcmp(selectedRegime, 'surface_ship_transition') || ...
            strcmp(selectedRegime, 'surface_ship_shallow_subcritical')
        shallowWavePressure = calculateShallowSubcriticalWavePressure( ...
            bodyDisplacement(1), bodyDisplacement(2), parameter, ...
            depthFroude, dipoleStrengthPerSpeed);
        shallowSeabedPressure = calculateImagePressure( ...
            observationPosition, seabedImage, headingX, headingY, ...
            parameter.minimumDistance, pressureCoefficient);

        if strcmp(selectedRegime, 'surface_ship_transition')
            % 平滑过渡只混合两套边界项，目标本体压力不重复混合。
            transitionWeight = calculateTransitionWeight( ...
                lengthFroude, parameter.modelOptions);
            lowSurfacePressure = calculateImagePressure( ...
                observationPosition, surfaceImage, headingX, headingY, ...
                parameter.minimumDistance, pressureCoefficient);
            lowTotalPressure = calculateRigidLidTotalPressure( ...
                observationPosition, targetPosition, parameter.waterDepth, ...
                headingX, headingY, parameter.minimumDistance, ...
                pressureCoefficient);
            lowSeabedPressure = ...
                lowTotalPressure - bodyPressure - lowSurfacePressure;
            lowWeight = 1.0 - transitionWeight;
            wavePressure = transitionWeight * shallowWavePressure;
            freeSurfacePressure = ...
                lowWeight * lowSurfacePressure + wavePressure;
            seabedPressure = lowWeight * lowSeabedPressure + ...
                transitionWeight * shallowSeabedPressure;
        else
            transitionWeight = 1.0;
            wavePressure = shallowWavePressure;
            freeSurfacePressure = shallowWavePressure;
            seabedPressure = shallowSeabedPressure;
        end
    else
        % 深潜高速但兴波已充分衰减时只保留一阶海床镜像。
        seabedPressure = calculateImagePressure( ...
            observationPosition, seabedImage, headingX, headingY, ...
            parameter.minimumDistance, pressureCoefficient);
    end

    dynamicPressure = bodyPressure + freeSurfacePressure + seabedPressure;
    hydrostaticPressure = parameter.waterDensity * ...
        parameter.gravityAcceleration * (-observationPosition(3));

    sample.targetPosition = targetPosition;                 % 当前目标中心位置。
    sample.distance = distanceM;                            % 目标中心距离。
    sample.longitudinalOffset = bodyDisplacement(1);        % 目标纵向偏移。
    sample.lateralOffset = bodyDisplacement(2);             % 目标横向偏移。
    sample.regime = selectedRegime;                         % 实际水动力工况。
    sample.lengthFroudeNumber = lengthFroude;               % 船长弗劳德数。
    sample.depthFroudeNumber = depthFroude;                 % 水深弗劳德数。
    sample.waveAttenuation = waveAttenuation;               % 兴波衰减估算。
    sample.transitionWeight = transitionWeight;             % 兴波模型平滑权重。
    sample.bodyDynamicPressure = bodyPressure;              % 本体压力。
    sample.freeSurfaceCorrectionPressure = freeSurfacePressure; % 自由液面修正。
    sample.wavePressure = wavePressure;                     % 兴波压力。
    sample.seabedCorrectionPressure = seabedPressure;       % 海床修正。
    sample.dynamicPressure = dynamicPressure;               % 动态压力总和。
    sample.hydrostaticPressure = hydrostaticPressure;       % 静水表压。
    sample.totalGaugePressure = hydrostaticPressure + dynamicPressure; % 总表压。
end

function selectedRegime = selectPressureRegime( ...
    parameter, lengthFroude, depthFroude, waveAttenuation)
%SELECTPRESSUREREGIME 按目标类型和无量纲参数选择水动力工况。
    options = parameter.modelOptions;
    if strcmp(parameter.type, 'submarine')
        if lengthFroude <= options.lowSpeedLengthFroudeThreshold || ...
                waveAttenuation <= options.waveAttenuationTolerance
            selectedRegime = 'submarine_no_wave';
        else
            selectedRegime = 'unsupported_submarine_free_surface_wave';
        end
        return;
    end

    transitionLowerBound = options.lowSpeedLengthFroudeThreshold - ...
        options.lengthFroudeTransitionHalfWidth;
    transitionUpperBound = options.lowSpeedLengthFroudeThreshold + ...
        options.lengthFroudeTransitionHalfWidth;
    if lengthFroude <= transitionLowerBound
        selectedRegime = 'surface_ship_low_speed';
        return;
    end

    waterDepthToLength = parameter.waterDepth / parameter.length;
    if waterDepthToLength > options.shallowWaterRatioThreshold
        if lengthFroude <= options.lowSpeedLengthFroudeThreshold
            selectedRegime = 'surface_ship_low_speed';
        else
            selectedRegime = 'unsupported_surface_ship_deep_water_wave';
        end
        return;
    end

    lowerCriticalBound = 1.0 - options.criticalDepthFroudeMargin;
    upperCriticalBound = 1.0 + options.criticalDepthFroudeMargin;
    if depthFroude < lowerCriticalBound
        if lengthFroude < transitionUpperBound
            selectedRegime = 'surface_ship_transition';
        else
            selectedRegime = 'surface_ship_shallow_subcritical';
        end
    elseif depthFroude <= upperCriticalBound
        selectedRegime = 'unsupported_surface_ship_critical';
    else
        selectedRegime = 'unsupported_surface_ship_supercritical';
    end
end

function assertSupportedRegime(selectedRegime)
%ASSERTSUPPORTEDREGIME 对尚未实现的水动力工况给出明确错误。
    if strncmp(selectedRegime, 'unsupported_', 12)
        error('当前参数识别为尚未支持的水动力工况：%s', selectedRegime);
    end
end

function weight = calculateTransitionWeight(lengthFroude, options)
%CALCULATETRANSITIONWEIGHT 计算两端一阶导数为零的三次平滑权重。
    lowerBound = options.lowSpeedLengthFroudeThreshold - ...
        options.lengthFroudeTransitionHalfWidth;
    upperBound = options.lowSpeedLengthFroudeThreshold + ...
        options.lengthFroudeTransitionHalfWidth;
    normalized = (lengthFroude - lowerBound) / ...
        (upperBound - lowerBound);
    normalized = min(1.0, max(0.0, normalized));
    weight = normalized^2 * (3.0 - 2.0 * normalized);
end

function attenuation = calculateSubmarineWaveAttenuation(parameter)
%CALCULATESUBMARINEWAVEATTENUATION 估算潜艇兴波到达自由液面的衰减。
    if ~strcmp(parameter.type, 'submarine') || parameter.velocity == 0.0
        attenuation = 0.0;
        return;
    end
    centerDepth = -parameter.initialPosition(3);
    attenuation = exp( ...
        -parameter.gravityAcceleration * centerDepth / parameter.velocity^2);
end

function verticalSize = effectiveVerticalSize(parameter)
%EFFECTIVEVERTICALSIZE 按目标类型返回吃水或潜艇高度。
    if strcmp(parameter.type, 'submarine') && parameter.height > 0.0
        verticalSize = parameter.height;
    else
        verticalSize = parameter.draft;
    end
end

function dipoleStrength = calculateDipoleStrengthPerSpeed(parameter)
%CALCULATEDIPOLESTRENGTHPERSPEED 计算单位航速的等效势流偶极矩。
    verticalSize = effectiveVerticalSize(parameter);
    displacedVolume = parameter.blockCoefficient * parameter.length * ...
        parameter.width * verticalSize;
    longitudinalFactor = calculateLongitudinalFactor( ...
        parameter.length, parameter.width, verticalSize);
    shapeFactor = 2.0 * longitudinalFactor / (1.0 - longitudinalFactor);
    dipoleStrength = (3.0 / (8.0 * pi)) * ...
        displacedVolume * shapeFactor;
end

function factor = calculateLongitudinalFactor(lengthM, widthM, heightM)
%CALCULATELONGITUDINALFACTOR 计算等效旋转椭球纵向形状因子。
    longitudinalRadius = 0.5 * lengthM;
    transverseRadius = 0.5 * sqrt(widthM * heightM);
    radiusRatio = transverseRadius / longitudinalRadius;
    if radiusRatio >= 1.0 - 1.0e-8
        factor = 1.0 / 3.0;
        return;
    end
    eccentricity = sqrt(max(0.0, 1.0 - radiusRatio^2));
    logarithm = log((1.0 + eccentricity) / (1.0 - eccentricity));
    factor = (1.0 - eccentricity^2) / (2.0 * eccentricity^3) * ...
        (logarithm - 2.0 * eccentricity);
end

function pressure = calculateImagePressure( ...
    observationPosition, imagePosition, headingX, headingY, ...
    minimumDistance, pressureCoefficient)
%CALCULATEIMAGEPRESSURE 计算单个水平偶极子或镜像点产生的动态压力。
    displacement = observationPosition - imagePosition;
    distanceM = norm(displacement);
    if ~isfinite(distanceM) || distanceM < minimumDistance
        error('观测点距离目标或镜像点过近。');
    end
    longitudinalProjection = ...
        displacement(1) * headingX + displacement(2) * headingY;
    cosineSquared = longitudinalProjection^2 / distanceM^2;
    pressure = pressureCoefficient * (3.0 * cosineSquared - 1.0) / ...
        distanceM^3;
end

function totalPressure = calculateRigidLidTotalPressure( ...
    observationPosition, targetPosition, waterDepth, headingX, headingY, ...
    minimumDistance, pressureCoefficient)
%CALCULATERIGIDLIDTOTALPRESSURE 计算刚盖自由液面和海床周期镜像压力。
    imageLayerCount = 8; % 与 C++ 模型一致的上下镜像截断层数。
    totalPressure = 0.0;
    for layer = -imageLayerCount:imageLayerCount
        verticalPeriod = 2.0 * layer * waterDepth;
        directImage = targetPosition + [0.0, 0.0, verticalPeriod];
        reflectedImage = [targetPosition(1), targetPosition(2), ...
            -targetPosition(3) - 2.0 * waterDepth + verticalPeriod];
        totalPressure = totalPressure + calculateImagePressure( ...
            observationPosition, directImage, headingX, headingY, ...
            minimumDistance, pressureCoefficient);
        totalPressure = totalPressure + calculateImagePressure( ...
            observationPosition, reflectedImage, headingX, headingY, ...
            minimumDistance, pressureCoefficient);
    end
end

function pressure = calculateShallowSubcriticalWavePressure( ...
    longitudinalOffset, lateralOffset, parameter, ...
    depthFroude, dipoleStrengthPerSpeed)
%CALCULATESHALLOWSUBCRITICALWAVEPRESSURE 计算线性深度平均浅水兴波压力。
    betaSquared = max(1.0e-12, 1.0 - depthFroude^2);
    beta = sqrt(betaSquared);
    stretchedLongitudinalOffset = longitudinalOffset / beta;
    coreRadius = max(parameter.minimumDistance, 0.5 * parameter.width);
    rawRadiusSquared = stretchedLongitudinalOffset^2 + lateralOffset^2;
    radiusSquared = max(rawRadiusSquared, coreRadius^2);
    angularTerm = (stretchedLongitudinalOffset^2 - lateralOffset^2) / ...
        radiusSquared^2;
    pressureScale = parameter.waterDensity * dipoleStrengthPerSpeed * ...
        parameter.velocity^2 / (parameter.waterDepth * beta);
    pressure = pressureScale * angularTerm;
end

function validateParameter(parameter)
%VALIDATEPARAMETER 校验本 MATLAB 流程依赖的核心物理参数。
    verticalSize = effectiveVerticalSize(parameter);
    if parameter.length <= 0.0 || parameter.width <= 0.0 || ...
            verticalSize <= 0.0 || parameter.waterDepth < verticalSize
        error('目标尺寸或海深不合法。');
    end
    if parameter.velocity < 0.0 || parameter.waterDensity <= 0.0 || ...
            parameter.gravityAcceleration <= 0.0
        error('航速、海水密度或重力加速度不合法。');
    end
    if strcmp(parameter.type, 'surfaceShip')
        expectedCenterZ = -0.5 * parameter.draft;
        if abs(parameter.initialPosition(3) - expectedCenterZ) > ...
                max(1.0e-8, parameter.draft * 1.0e-8)
            error('水面舰艇中心 z 坐标必须等于 -draft/2。');
        end
    else
        targetTop = parameter.initialPosition(3) + 0.5 * verticalSize;
        targetBottom = parameter.initialPosition(3) - 0.5 * verticalSize;
        if targetTop > 0.0 || targetBottom < -parameter.waterDepth
            error('潜艇必须完整位于静水面和海床之间。');
        end
    end
end

function resultFigure = plotScenarioTimeSeries(scenarioResults, visibility)
%PLOTSCENARIOTIMESERIES 绘制四种工况的压力分量时序对比图。
    resultFigure = figure( ...
        'Name', '舰艇与潜艇分工况水压时序', ...
        'Color', 'w', 'Visible', visibility, ...
        'Position', [80, 60, 1280, 820]);
    for scenarioIndex = 1:numel(scenarioResults)
        pressureTable = scenarioResults(scenarioIndex).pressureTable;
        subplot(2, 2, scenarioIndex);
        plot(pressureTable.time_s, pressureTable.dynamic_pressure_pa, ...
            'k-', 'LineWidth', 1.8);
        hold on;
        plot(pressureTable.time_s, ...
            pressureTable.body_dynamic_pressure_pa, ...
            'Color', [0.00, 0.45, 0.74], 'LineWidth', 1.1);
        plot(pressureTable.time_s, ...
            pressureTable.free_surface_correction_pressure_pa, ...
            'Color', [0.85, 0.33, 0.10], 'LineWidth', 1.1);
        plot(pressureTable.time_s, ...
            pressureTable.seabed_correction_pressure_pa, ...
            'Color', [0.47, 0.67, 0.19], 'LineWidth', 1.1);
        grid on;
        xlabel('时间 / s');
        ylabel('压力变化 / Pa');
        title(sprintf('%s\nFr_L=%.3f，Fr_H=%.3f，w=%.3f', ...
            scenarioResults(scenarioIndex).name, ...
            pressureTable.length_froude_number(1), ...
            pressureTable.depth_froude_number(1), ...
            pressureTable.transition_weight(1)));
        legend('动态压力总和', '本体压力', '自由液面修正', ...
            '海床修正', 'Location', 'best');
    end
end

function resultFigure = plotTransitionBehavior( ...
    surfaceBase, sensorPosition, visibility)
%PLOTTRANSITIONBEHAVIOR 展示平滑权重及压力随 FrL 的连续变化。
    froudeVector = linspace(0.04, 0.18, 281)'; % 覆盖低速、过渡和兴波区。
    weightVector = zeros(size(froudeVector));
    pressureVector = zeros(size(froudeVector));
    for index = 1:numel(froudeVector)
        parameter = surfaceBase;
        parameter.velocity = froudeVector(index) * sqrt( ...
            parameter.gravityAcceleration * parameter.length);
        parameter.initialPosition(1) = 0.0;
        sample = calculatePressureSample(parameter, sensorPosition, 0.0);
        weightVector(index) = sample.transitionWeight;
        pressureVector(index) = sample.dynamicPressure;
    end

    resultFigure = figure( ...
        'Name', '低速与兴波模型平滑过渡', ...
        'Color', 'w', 'Visible', visibility, ...
        'Position', [120, 100, 1120, 760]);

    subplot(2, 1, 1);
    plot(froudeVector, weightVector, 'b-', 'LineWidth', 2.0);
    hold on;
    plot([0.08, 0.08], [0.0, 1.0], 'k--', 'LineWidth', 1.0);
    plot([0.12, 0.12], [0.0, 1.0], 'k--', 'LineWidth', 1.0);
    grid on;
    ylim([-0.05, 1.05]);
    xlabel('船长弗劳德数 Fr_L');
    ylabel('兴波模型权重 w');
    title('三次平滑过渡权重 w=s^2(3-2s)');
    legend('兴波权重', '过渡下界0.08', '过渡上界0.12', ...
        'Location', 'best');

    subplot(2, 1, 2);
    plot(froudeVector, pressureVector, 'm-', 'LineWidth', 2.0);
    hold on;
    pressureLimit = max(abs(pressureVector));
    plot([0.08, 0.08], [-pressureLimit, pressureLimit], ...
        'k--', 'LineWidth', 1.0);
    plot([0.12, 0.12], [-pressureLimit, pressureLimit], ...
        'k--', 'LineWidth', 1.0);
    grid on;
    xlabel('船长弗劳德数 Fr_L');
    ylabel('固定观测点动态压力 / Pa');
    title('模型交接区间内的动态压力连续性');
end

function resultFigure = plotSurfaceShipSpatialFields( ...
    scenarioDefinitions, closestTimeS, visibility)
%PLOTSURFACESHIPSPATIALFIELDS 绘制三种水面舰艇工况的水平压力场。
    xCoordinates = linspace(-150.0, 150.0, 81); % 东向网格，单位 m。
    yCoordinates = linspace(-80.0, 80.0, 61);   % 北向网格，单位 m。
    observationZ = -25.0;                       % 固定切片深度，单位 m。
    [xGrid, yGrid] = meshgrid(xCoordinates, yCoordinates);

    resultFigure = figure( ...
        'Name', '水面舰艇低速—过渡—兴波压力场', ...
        'Color', 'w', 'Visible', visibility, ...
        'Position', [70, 120, 1380, 440]);

    for scenarioIndex = 1:numel(scenarioDefinitions)
        definition = scenarioDefinitions(scenarioIndex);
        pressureGrid = zeros(size(xGrid));
        for rowIndex = 1:size(xGrid, 1)
            for columnIndex = 1:size(xGrid, 2)
                observationPosition = [ ...
                    xGrid(rowIndex, columnIndex), ...
                    yGrid(rowIndex, columnIndex), observationZ];
                sample = calculatePressureSample( ...
                    definition.parameter, ...
                    observationPosition, closestTimeS);
                pressureGrid(rowIndex, columnIndex) = sample.dynamicPressure;
            end
        end

        subplot(1, 3, scenarioIndex);
        imagesc(xCoordinates, yCoordinates, pressureGrid);
        set(gca, 'YDir', 'normal');
        axis equal tight;
        colorbar;
        colormap(parula(256));
        pressureLimit = max(abs(pressureGrid(:)));
        if pressureLimit > 0.0
            caxis([-pressureLimit, pressureLimit]);
        end
        hold on;
        plot(0.0, 0.0, 'kp', 'MarkerFaceColor', 'y', 'MarkerSize', 10);
        xlabel('东向位置 x / m');
        ylabel('北向位置 y / m');
        title(sprintf('%s\n动态压力 / Pa', definition.name));
    end
end

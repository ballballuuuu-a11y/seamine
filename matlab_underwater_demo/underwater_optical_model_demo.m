function results = underwater_optical_model_demo(outputDir, showFigures)
%UNDERWATER_OPTICAL_MODEL_DEMO 水下光学成像三分量模型原理演示
%   RESULTS = UNDERWATER_OPTICAL_MODEL_DEMO(OUTPUTDIR, SHOWFIGURES)
%   生成合成清晰场景和逐像素深度图，计算直接分量、前向散射分量、
%   后向散射分量，合成水下观测图像，并演示两分量与三分量复原。
%
%   输入参数：
%   OUTPUTDIR   演示图像和 MAT 数据的输出目录。
%   SHOWFIGURES 是否显示图窗；自动测试时可设为 false。
%
%   输出参数：
%   RESULTS     包含场景、深度、三分量、观测图和复原结果的结构体。
%
%   本示例仅使用 MATLAB 基础函数，不依赖 Image Processing Toolbox。

    % 根据调用方式补全默认参数，保证脚本可直接运行。
    if nargin < 1 || isempty(outputDir)
        scriptDir = fileparts(mfilename('fullpath'));
        outputDir = fullfile(scriptDir, 'output');
    end
    if nargin < 2
        showFigures = true;
    end

    % 固定随机种子，使噪声和输出图像每次运行保持一致。
    rng(7, 'twister');
    if ~exist(outputDir, 'dir')
        mkdir(outputDir);
    end

    % 生成不依赖外部图片的彩色场景与逐像素目标距离。
    imageHeight = 360;
    imageWidth = 640;
    [clearScene, depthMap] = createSyntheticScene(imageHeight, imageWidth);

    % 设置 RGB 宽带水下光学参数，单位均按米制距离解释。
    params.betaDirect = [0.34, 0.16, 0.08];       % 直接信号衰减系数，红光衰减最快
    params.betaBackscatter = [0.19, 0.12, 0.075]; % 后向散射增长系数
    params.backgroundInf = [0.035, 0.34, 0.56];   % 无限远背景光，呈蓝绿色
    params.forwardRatio = [0.045, 0.065, 0.090];  % 前向散射进入相机的能量比例
    params.noiseStd = 0.0015;                     % 传感器读出噪声标准差
    params.transmissionFloor = 0.040;             % 复原时的最小透射率

    % 计算每个颜色通道的直接透射率 t_k(x)=exp[-beta_k^D z(x)]。
    transmission = zeros(imageHeight, imageWidth, 3);
    for channelIndex = 1:3
        transmission(:, :, channelIndex) = exp( ...
            -params.betaDirect(channelIndex) .* depthMap);
    end

    % 直接分量保留场景结构，但随距离和波长发生指数衰减。
    directComponent = clearScene .* transmission;

    % 前向散射通过距离相关高斯点扩散函数模拟，远距离区域更模糊。
    forwardComponent = computeForwardScatter( ...
        directComponent, transmission, depthMap, params.forwardRatio);

    % 后向散射随距离单调趋近无限远背景光。
    backscatterComponent = zeros(imageHeight, imageWidth, 3);
    for channelIndex = 1:3
        backscatterComponent(:, :, channelIndex) = ...
            params.backgroundInf(channelIndex) .* ...
            (1 - exp(-params.betaBackscatter(channelIndex) .* depthMap));
    end

    % 三分量相加后加入小幅高斯读出噪声，得到水下观测图像。
    sensorNoise = params.noiseStd .* randn(imageHeight, imageWidth, 3);
    observedImage = clip01( ...
        directComponent + forwardComponent + backscatterComponent + sensorNoise);

    % 两分量复原忽略前向散射，因此会保留一定的雾化和模糊残差。
    stableTransmission = max(transmission, params.transmissionFloor);
    restoredTwoComponent = clip01( ...
        (observedImage - backscatterComponent) ./ stableTransmission);

    % 三分量示范复原使用已知的前向散射项，展示完整模型的理想上限。
    restoredThreeComponent = clip01( ...
        (observedImage - backscatterComponent - forwardComponent) ./ stableTransmission);

    % 计算均方误差、平均绝对误差和峰值信噪比，量化复原差异。
    metrics.twoComponent = calculateMetrics(clearScene, restoredTwoComponent);
    metrics.threeComponent = calculateMetrics(clearScene, restoredThreeComponent);

    % 整理所有核心变量，便于用户在 MATLAB 工作区继续分析。
    results.clearScene = clearScene;
    results.depthMap = depthMap;
    results.transmission = transmission;
    results.directComponent = directComponent;
    results.forwardComponent = forwardComponent;
    results.backscatterComponent = backscatterComponent;
    results.observedImage = observedImage;
    results.restoredTwoComponent = restoredTwoComponent;
    results.restoredThreeComponent = restoredThreeComponent;
    results.parameters = params;
    results.metrics = metrics;

    % 导出单幅结果、原理总览和参数曲线图。
    saveDemoImages(results, outputDir);
    overviewFigure = createOverviewFigure(results, showFigures);
    curvesFigure = createCurvesFigure(results, showFigures);
    saveFigureCompat(overviewFigure, fullfile(outputDir, 'underwater_model_overview.png'));
    saveFigureCompat(curvesFigure, fullfile(outputDir, 'underwater_model_curves.png'));

    % 保存数值数据，便于复现实验和修改参数后对比。
    save(fullfile(outputDir, 'underwater_model_demo_data.mat'), 'results');

    % 在命令窗口输出关键指标和文件位置。
    fprintf('\n水下光学成像模型演示完成。\n');
    fprintf('两分量复原：PSNR = %.2f dB, MAE = %.4f\n', ...
        metrics.twoComponent.psnr, metrics.twoComponent.mae);
    fprintf('三分量复原：PSNR = %.2f dB, MAE = %.4f\n', ...
        metrics.threeComponent.psnr, metrics.threeComponent.mae);
    fprintf('输出目录：%s\n\n', outputDir);
end


function [scene, depthMap] = createSyntheticScene(imageHeight, imageWidth)
%CREATESYNTHETICSCENE 生成具有颜色块、纹理和深度突变的合成场景

    % 归一化像素坐标用于生成背景、纹理和几何目标。
    [xCoord, yCoord] = meshgrid( ...
        linspace(0, 1, imageWidth), linspace(0, 1, imageHeight));

    % 构造带缓慢梯度和细微纹理的中性海底背景。
    texture = 0.035 .* sin(2 * pi .* (4.2 .* xCoord + 2.1 .* yCoord)) + ...
              0.020 .* cos(2 * pi .* (1.3 .* xCoord - 5.5 .* yCoord));
    scene = zeros(imageHeight, imageWidth, 3);
    scene(:, :, 1) = 0.56 + 0.17 .* (1 - yCoord) + texture;
    scene(:, :, 2) = 0.52 + 0.16 .* (1 - yCoord) + 0.8 .* texture;
    scene(:, :, 3) = 0.43 + 0.13 .* (1 - yCoord) + 0.5 .* texture;

    % 背景从左到右逐渐远离相机，形成连续距离变化。
    depthMap = 2.4 + 6.6 .* xCoord + 0.8 .* yCoord;

    % 添加近距离红色圆形目标，用于突出红通道随距离的快速衰减。
    redMask = (xCoord - 0.18).^2 + ((yCoord - 0.31) ./ 1.15).^2 <= 0.085^2;
    scene = paintMask(scene, redMask, [0.95, 0.07, 0.045]);
    depthMap(redMask) = 2.8;

    % 添加中距离绿色矩形目标，并嵌入明暗条纹显示散射模糊。
    greenMask = abs(xCoord - 0.43) <= 0.105 & abs(yCoord - 0.31) <= 0.105;
    scene = paintMask(scene, greenMask, [0.08, 0.86, 0.14]);
    greenStripes = greenMask & mod(floor((xCoord - 0.325) .* 120), 2) == 0;
    scene = paintMask(scene, greenStripes, [0.02, 0.23, 0.04]);
    depthMap(greenMask) = 5.2;

    % 添加较远黄色椭圆目标，便于比较直接衰减和后向散射。
    yellowMask = ((xCoord - 0.69) ./ 0.11).^2 + ...
                 ((yCoord - 0.31) ./ 0.075).^2 <= 1;
    scene = paintMask(scene, yellowMask, [0.96, 0.84, 0.06]);
    depthMap(yellowMask) = 7.4;

    % 添加十二块彩色色卡，每列采用不同距离，展示宽带颜色失真。
    chartColors = [ ...
        0.90, 0.08, 0.07;  0.08, 0.75, 0.12;  0.06, 0.18, 0.90; ...
        0.95, 0.80, 0.08;  0.88, 0.08, 0.76;  0.04, 0.80, 0.86; ...
        0.93, 0.93, 0.93;  0.48, 0.48, 0.48;  0.95, 0.42, 0.05; ...
        0.04, 0.48, 0.45;  0.40, 0.12, 0.63;  0.34, 0.18, 0.08];
    chartBackground = xCoord >= 0.08 & xCoord <= 0.92 & ...
                      yCoord >= 0.58 & yCoord <= 0.86;
    scene = paintMask(scene, chartBackground, [0.055, 0.065, 0.075]);
    depthMap(chartBackground) = 7.8;

    patchIndex = 1;
    for rowIndex = 1:2
        for columnIndex = 1:6
            centerX = 0.145 + (columnIndex - 1) .* 0.142;
            centerY = 0.655 + (rowIndex - 1) .* 0.125;
            patchMask = abs(xCoord - centerX) <= 0.052 & ...
                        abs(yCoord - centerY) <= 0.042;
            scene = paintMask(scene, patchMask, chartColors(patchIndex, :));
            depthMap(patchMask) = 3.5 + 0.85 .* (columnIndex - 1) + ...
                                       0.45 .* (rowIndex - 1);
            patchIndex = patchIndex + 1;
        end
    end

    % 添加底部黑白分辨率条纹，用于观察前向散射造成的细节损失。
    resolutionMask = yCoord >= 0.90 & yCoord <= 0.965 & ...
                     xCoord >= 0.18 & xCoord <= 0.82;
    stripePattern = mod(floor((xCoord - 0.18) .* 95), 2) == 0;
    scene = paintMask(scene, resolutionMask & stripePattern, [0.94, 0.94, 0.94]);
    scene = paintMask(scene, resolutionMask & ~stripePattern, [0.03, 0.03, 0.03]);
    depthMap(resolutionMask) = 6.6;

    % 所有通道限制在显示范围内。
    scene = clip01(scene);
end


function imageData = paintMask(imageData, mask, color)
%PAINTMASK 把指定掩膜区域填充为给定 RGB 颜色

    for channelIndex = 1:3
        channelData = imageData(:, :, channelIndex);
        channelData(mask) = color(channelIndex);
        imageData(:, :, channelIndex) = channelData;
    end
end


function forwardComponent = computeForwardScatter( ...
    directComponent, transmission, depthMap, forwardRatio)
%COMPUTEFORWARDSCATTER 用多深度层高斯 PSF 近似空间变化前向散射

    [imageHeight, imageWidth, ~] = size(directComponent);
    forwardComponent = zeros(imageHeight, imageWidth, 3);

    % 深度越大，高斯核越宽；最后一层覆盖所有更远距离。
    depthEdges = [0, 4.0, 6.0, 8.0, inf];
    baseSigma = [0.75, 1.35, 2.20, 3.50];
    channelSigmaScale = [0.90, 1.00, 1.18];

    for channelIndex = 1:3
        channelDirect = directComponent(:, :, channelIndex);
        scatterWeight = forwardRatio(channelIndex) .* ...
            (1 - transmission(:, :, channelIndex));

        for layerIndex = 1:numel(baseSigma)
            layerMask = depthMap >= depthEdges(layerIndex) & ...
                        depthMap < depthEdges(layerIndex + 1);
            sigmaValue = baseSigma(layerIndex) .* ...
                         channelSigmaScale(channelIndex);
            blurredChannel = normalizedGaussianBlur(channelDirect, sigmaValue);
            forwardComponent(:, :, channelIndex) = ...
                forwardComponent(:, :, channelIndex) + ...
                layerMask .* scatterWeight .* blurredChannel;
        end
    end
end


function blurredImage = normalizedGaussianBlur(inputImage, sigmaValue)
%NORMALIZEDGAUSSIANBLUR 使用 conv2 实现无工具箱高斯模糊
%   通过同时卷积全一矩阵校正边界能量，避免图像边缘被零填充压暗。

    halfSize = max(1, ceil(3 .* sigmaValue));
    coordinate = -halfSize:halfSize;
    [kernelX, kernelY] = meshgrid(coordinate, coordinate);
    gaussianKernel = exp( ...
        -(kernelX.^2 + kernelY.^2) ./ (2 .* sigmaValue.^2));
    gaussianKernel = gaussianKernel ./ sum(gaussianKernel(:));

    numerator = conv2(inputImage, gaussianKernel, 'same');
    denominator = conv2(ones(size(inputImage)), gaussianKernel, 'same');
    blurredImage = numerator ./ max(denominator, eps);
end


function metrics = calculateMetrics(referenceImage, estimatedImage)
%CALCULATEMETRICS 计算 MSE、MAE 和峰值信噪比

    errorData = estimatedImage - referenceImage;
    metrics.mse = mean(errorData(:).^2);
    metrics.mae = mean(abs(errorData(:)));
    metrics.psnr = 10 .* log10(1 ./ max(metrics.mse, eps));
end


function saveDemoImages(results, outputDir)
%SAVEDEMOIMAGES 导出便于单独查看的主要演示图像

    imwrite(results.clearScene, fullfile(outputDir, '01_clear_scene.png'));
    normalizedDepth = (results.depthMap - min(results.depthMap(:))) ./ ...
        max(max(results.depthMap(:)) - min(results.depthMap(:)), eps);
    imwrite(normalizedDepth, fullfile(outputDir, '02_depth_map.png'));
    imwrite(results.observedImage, fullfile(outputDir, '03_observed_underwater.png'));
    imwrite(results.restoredTwoComponent, ...
        fullfile(outputDir, '04_restored_two_component.png'));
    imwrite(results.restoredThreeComponent, ...
        fullfile(outputDir, '05_restored_three_component.png'));
end


function figureHandle = createOverviewFigure(results, showFigures)
%CREATEOVERVIEWFIGURE 创建九宫格原理讲解总览图

    figureVisibility = logicalToVisibility(showFigures);
    figureHandle = figure('Color', 'w', 'Visible', figureVisibility, ...
        'Position', [80, 60, 1500, 900]);

    % 使用 subplot 代替 MATLAB R2019b 才提供的 tiledlayout/nexttile。
    subplot(3, 3, 1);
    showRgb(results.clearScene);
    title('1. 清晰场景 J');

    depthAxes = subplot(3, 3, 2);
    imagesc(results.depthMap);
    axis image off;
    % MATLAB R2017 没有 turbo 色图，使用旧版本自带的 jet 色图。
    colormap(depthAxes, jet(256));
    title(sprintf('2. 距离图 z(x)，范围 %.1f–%.1f m', ...
        min(results.depthMap(:)), max(results.depthMap(:))));

    transmissionAxes = subplot(3, 3, 3);
    imagesc(mean(results.transmission, 3), [0, 1]);
    axis image off;
    colormap(transmissionAxes, parula(256));
    title('3. 平均透射率（范围 0–1）');

    subplot(3, 3, 4);
    showRgb(results.directComponent);
    title('4. 直接分量 E_D');

    subplot(3, 3, 5);
    showRgb(clip01(8 .* results.forwardComponent));
    title('5. 前向散射 E_{FS}（显示增强 8 倍）');

    subplot(3, 3, 6);
    showRgb(results.backscatterComponent);
    title('6. 后向散射 E_{BS}');

    subplot(3, 3, 7);
    showRgb(results.observedImage);
    title('7. 合成水下观测图 I');

    subplot(3, 3, 8);
    showRgb(results.restoredTwoComponent);
    title(sprintf('8. 两分量复原，PSNR %.2f dB', ...
        results.metrics.twoComponent.psnr));

    subplot(3, 3, 9);
    showRgb(results.restoredThreeComponent);
    title(sprintf('9. 三分量复原，PSNR %.2f dB', ...
        results.metrics.threeComponent.psnr));

    % 用注释文本框模拟总标题，避免使用较新版本的布局标题接口。
    addFigureTitle(figureHandle, ...
        'Jaffe–McGlamery 三分量水下成像与复原演示');
end


function figureHandle = createCurvesFigure(results, showFigures)
%CREATECURVESFIGURE 绘制距离相关参数曲线和分量统计

    figureVisibility = logicalToVisibility(showFigures);
    figureHandle = figure('Color', 'w', 'Visible', figureVisibility, ...
        'Position', [100, 80, 1300, 760]);

    distanceVector = linspace(0, 12, 300).';
    % 使用 bsxfun 明确执行逐通道运算，兼容不支持隐式扩展的旧版本。
    transmissionCurve = exp(-bsxfun(@times, distanceVector, ...
        results.parameters.betaDirect));
    backscatterCurve = bsxfun(@times, results.parameters.backgroundInf, ...
        1 - exp(-bsxfun(@times, distanceVector, ...
        results.parameters.betaBackscatter)));
    lineColors = [0.85, 0.15, 0.12; 0.10, 0.62, 0.20; 0.08, 0.32, 0.90];

    subplot(2, 2, 1);
    hold on;
    for channelIndex = 1:3
        plot(distanceVector, transmissionCurve(:, channelIndex), ...
            'LineWidth', 2.2, 'Color', lineColors(channelIndex, :));
    end
    hold off;
    grid on;
    ylim([0, 1]);
    xlabel('目标距离 z / m');
    ylabel('直接透射率 t_k(z)');
    legend({'R', 'G', 'B'}, 'Location', 'northeast');
    title('直接信号：红通道衰减最快');

    subplot(2, 2, 2);
    hold on;
    for channelIndex = 1:3
        plot(distanceVector, backscatterCurve(:, channelIndex), ...
            'LineWidth', 2.2, 'Color', lineColors(channelIndex, :));
    end
    hold off;
    grid on;
    xlabel('目标距离 z / m');
    ylabel('后向散射 B_k(z)');
    legend({'R', 'G', 'B'}, 'Location', 'southeast');
    title('路径幕光：逐渐趋近 B_k^{\infty}');

    subplot(2, 2, 3);
    componentMeans = [ ...
        squeeze(mean(mean(results.directComponent, 1), 2)).'; ...
        squeeze(mean(mean(results.forwardComponent, 1), 2)).'; ...
        squeeze(mean(mean(results.backscatterComponent, 1), 2)).'];
    bar(componentMeans, 'grouped');
    grid on;
    % 直接设置坐标轴刻度标签，避免依赖较新的 xticklabels 函数。
    set(gca, 'XTick', 1:3, ...
        'XTickLabel', {'直接', '前向散射', '后向散射'});
    ylabel('全图平均强度');
    legend({'R', 'G', 'B'}, 'Location', 'northwest');
    title('三分量的通道平均贡献');

    subplot(2, 2, 4);
    depthEdges = linspace(min(results.depthMap(:)), max(results.depthMap(:)), 9);
    depthCenters = 0.5 .* (depthEdges(1:end-1) + depthEdges(2:end));
    directLuminance = rgbLuminance(results.directComponent);
    forwardLuminance = rgbLuminance(results.forwardComponent);
    backscatterLuminance = rgbLuminance(results.backscatterComponent);
    binnedMeans = nan(numel(depthCenters), 3);
    for binIndex = 1:numel(depthCenters)
        binMask = results.depthMap >= depthEdges(binIndex) & ...
                  results.depthMap < depthEdges(binIndex + 1);
        if any(binMask(:))
            binnedMeans(binIndex, 1) = mean(directLuminance(binMask));
            binnedMeans(binIndex, 2) = mean(forwardLuminance(binMask));
            binnedMeans(binIndex, 3) = mean(backscatterLuminance(binMask));
        end
    end
    plot(depthCenters, binnedMeans(:, 1), '-o', 'LineWidth', 2.0);
    hold on;
    plot(depthCenters, binnedMeans(:, 2), '-s', 'LineWidth', 2.0);
    plot(depthCenters, binnedMeans(:, 3), '-^', 'LineWidth', 2.0);
    hold off;
    grid on;
    xlabel('距离分箱中心 / m');
    ylabel('亮度均值');
    legend({'直接', '前向散射', '后向散射'}, 'Location', 'best');
    title('三分量随距离的变化');

    % 用旧版本支持的注释文本框创建整幅图的总标题。
    addFigureTitle(figureHandle, '水下成像参数与物理分量曲线');
end


function addFigureTitle(figureHandle, titleText)
%ADDFIGURETITLE 使用注释文本框创建兼容 MATLAB R2017 的总标题

    annotation(figureHandle, 'textbox', [0, 0.955, 1, 0.04], ...
        'String', titleText, 'EdgeColor', 'none', ...
        'HorizontalAlignment', 'center', 'VerticalAlignment', 'middle', ...
        'FontWeight', 'bold', 'FontSize', 16);
end


function luminance = rgbLuminance(rgbImage)
%RGBLUMINANCE 按 Rec.709 权重计算亮度，仅用于统计和绘图

    luminance = 0.2126 .* rgbImage(:, :, 1) + ...
                0.7152 .* rgbImage(:, :, 2) + ...
                0.0722 .* rgbImage(:, :, 3);
end


function showRgb(rgbImage)
%SHOWRGB 使用基础 image 函数显示 RGB 图像，避免依赖 imshow

    image(clip01(rgbImage));
    axis image off;
end


function outputData = clip01(inputData)
%CLIP01 把数值限制在显示图像的 [0,1] 范围

    outputData = min(max(inputData, 0), 1);
end


function visibilityValue = logicalToVisibility(showFigures)
%LOGICALTOVISIBILITY 把逻辑开关转换为 MATLAB 图窗可见性字符串

    if showFigures
        visibilityValue = 'on';
    else
        visibilityValue = 'off';
    end
end


function saveFigureCompat(figureHandle, outputPath)
%SAVEFIGURECOMPAT 使用兼容性稳定的 print 导出 PNG 图像
%   MATLAB R2021b 的无界面批处理环境中，print 比 exportgraphics 更稳定。

    set(figureHandle, 'PaperPositionMode', 'auto');
    print(figureHandle, outputPath, '-dpng', '-r160');
end

function render_latex_formulas(outputDir)
%RENDER_LATEX_FORMULAS 使用 MATLAB 的 LaTeX 解释器生成文档公式图片。
%   这些图片用于 Word 文档排版；每条公式的源字符串均采用 LaTeX 语法。

    if nargin < 1 || isempty(outputDir)
        outputDir = fullfile(fileparts(mfilename('fullpath')), 'doc_assets', 'formulas');
    end
    if ~exist(outputDir, 'dir')
        mkdir(outputDir);
    end

    % 公式编号、LaTeX 源码与画布宽度。
    formulas = {
        'f01_projection', '$s\tilde{\mathbf p}=\mathbf K[\,\mathbf R\mid\mathbf t\,]\tilde{\mathbf P}$', 900;
        'f02_intrinsic', '$\mathbf K=\left[\begin{array}{ccc}f_x&\gamma&c_x\\0&f_y&c_y\\0&0&1\end{array}\right]$', 780;
        'f03_total', '$I_c(\mathbf x)=D_c(\mathbf x)+F_c(\mathbf x)+B_c(\mathbf x)+n_c(\mathbf x)$', 1050;
        'f04_iop', '$c(\lambda)=a(\lambda)+b(\lambda)$', 650;
        'f05_transmission', '$T_\lambda(d)=\exp[-c(\lambda)d]$', 650;
        'f06_direct_general', '$D_c(\mathbf x)=\int S_c(\lambda)\,L_0(\mathbf x,\lambda)\,\exp[-c(\lambda)d(\mathbf x)]\,\mathrm d\lambda$', 1200;
        'f07_direct_active', '$D_c(\mathbf x)=G_c(\mathbf x)\rho_c(\mathbf x)E_{0,c}\exp\{-c_c[d_s(\mathbf x)+d_c(\mathbf x)]\}$', 1250;
        'f08_forward', '$F_c(\mathbf x)=\int_{\Omega}D_c(\xi)\,h_{c,d(\mathbf x)}(\mathbf x-\xi)\,\mathrm d\xi$', 1200;
        'f09_back_integral', '$B_c(\mathbf x)=\int_0^{d(\mathbf x)}q_c(s)\exp(-\beta_c^B s)\,\mathrm ds$', 1050;
        'f10_back_simple', '$B_c(\mathbf x)=B_{\infty,c}\{1-\exp[-\beta_c^B d(\mathbf x)]\}$', 1050;
        'f11_sensor', '$I_c=\mathcal Q\!\left\{g_c\!\left(\int S_c(\lambda)L(\lambda)\,\mathrm d\lambda\right)+\eta_c\right\}$', 1100;
        'f12_demo', '$I_c=J_c t_c+\alpha_c(J_ct_c)*g_{\sigma(d)}+B_{\infty,c}(1-e^{-\beta_c^B d})+n_c$', 1250;
        'f13_inverse', '$\widehat J_c=\frac{I_c-\widehat F_c-\widehat B_c}{\widehat t_c}$', 950;
        'f14_metrics', '$\mathrm{PSNR}=10\log_{10}\!\left(\frac{1}{\mathrm{MSE}}\right)$', 720
    };

    for index = 1:size(formulas, 1)
        % 每次使用独立画布，避免公式尺寸彼此影响。
        figureWidth = formulas{index, 3};
        figureHandle = figure('Visible', 'off', 'Color', 'white', ...
            'Units', 'pixels', 'Position', [100, 100, figureWidth, 180]);
        axesHandle = axes('Parent', figureHandle, 'Position', [0, 0, 1, 1]);
        axis(axesHandle, 'off');
        text(axesHandle, 0.5, 0.5, formulas{index, 2}, ...
            'Interpreter', 'latex', 'FontSize', 22, ...
            'HorizontalAlignment', 'center', 'VerticalAlignment', 'middle', ...
            'Color', [0.08, 0.12, 0.16]);

        % 采用高分辨率 PNG，保证插入 Word 后仍然清晰。
        outputPath = fullfile(outputDir, [formulas{index, 1}, '.png']);
        print(figureHandle, outputPath, '-dpng', '-r180');
        close(figureHandle);
    end
end

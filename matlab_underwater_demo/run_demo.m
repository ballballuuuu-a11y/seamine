%RUN_DEMO 一键运行水下光学成像与图像复原原理演示

clear;
clc;
close all;

% 获取脚本目录，保证从任意当前工作目录启动都能正确找到主函数。
scriptDir = fileparts(mfilename('fullpath'));
addpath(scriptDir);

% 演示结果统一写入 output 子目录，并在交互运行时显示图窗。
outputDir = fullfile(scriptDir, 'output');
results = underwater_optical_model_demo(outputDir, true);

fprintf('可在工作区变量 results 中查看全部物理分量和参数。\n');

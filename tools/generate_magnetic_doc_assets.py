"""生成磁场特性说明文档所需的示意图和仿真图。"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import pandas as pd
from PIL import Image, ImageDraw, ImageFont


# 工程目录和输出目录。
PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = PROJECT_ROOT / "docs" / "magnetic_field_word_assets"
SPATIAL_CSV = PROJECT_ROOT / "build" / "validation_magnetic.csv"
TIME_CSV = PROJECT_ROOT / "build" / "validation_magnetic_time_series.csv"

# 文档图形使用的统一色板。
NAVY = "#15324B"
BLUE = "#2374AB"
CYAN = "#33A6B8"
TEAL = "#159A9C"
GOLD = "#D49B27"
ORANGE = "#E8753D"
RED = "#C94C4C"
GREEN = "#4F9D69"
INK = "#243746"
MUTED = "#607786"
LIGHT = "#F3F7FA"
GRID = "#D8E2E8"
WHITE = "#FFFFFF"

# 中文字体使用微软雅黑，公式和英文使用 Cambria。
CHINESE_FONT_PATH = Path("C:/Windows/Fonts/msyh.ttc")
CHINESE_BOLD_FONT_PATH = Path("C:/Windows/Fonts/msyhbd.ttc")
MATH_FONT_PATH = Path("C:/Windows/Fonts/cambria.ttc")


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    """创建支持中文的字体。"""

    font_path = CHINESE_BOLD_FONT_PATH if bold else CHINESE_FONT_PATH
    return ImageFont.truetype(str(font_path), size=size)


def math_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    """创建适合数字和公式的字体。"""

    if MATH_FONT_PATH.exists():
        return ImageFont.truetype(str(MATH_FONT_PATH), size=size)
    return font(size, bold=bold)


def hex_rgb(value: str) -> tuple[int, int, int]:
    """把十六进制颜色转换为 RGB 元组。"""

    value = value.lstrip("#")
    return tuple(int(value[index:index + 2], 16) for index in (0, 2, 4))


def add_arrow(
    draw: ImageDraw.ImageDraw,
    start: tuple[float, float],
    end: tuple[float, float],
    color: str,
    width: int = 5,
    head: int = 16,
) -> None:
    """绘制带箭头的直线。"""

    draw.line([start, end], fill=color, width=width)
    angle = math.atan2(end[1] - start[1], end[0] - start[0])
    left = (
        end[0] - head * math.cos(angle - math.pi / 6),
        end[1] - head * math.sin(angle - math.pi / 6),
    )
    right = (
        end[0] - head * math.cos(angle + math.pi / 6),
        end[1] - head * math.sin(angle + math.pi / 6),
    )
    draw.polygon([end, left, right], fill=color)


def draw_centered_text(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    text: str,
    text_font: ImageFont.FreeTypeFont,
    fill: str,
    spacing: int = 5,
) -> None:
    """在矩形区域内水平和垂直居中绘制多行文字。"""

    left, top, right, bottom = box
    bbox = draw.multiline_textbbox((0, 0), text, font=text_font, spacing=spacing, align="center")
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = left + (right - left - width) / 2
    y = top + (bottom - top - height) / 2
    draw.multiline_text((x, y), text, font=text_font, fill=fill, spacing=spacing, align="center")


def gradient_color(
    values: np.ndarray,
    stops: list[tuple[float, str]],
) -> np.ndarray:
    """依据颜色节点把归一化数组映射为 RGB 图像。"""

    values = np.clip(values, 0.0, 1.0)
    output = np.zeros((*values.shape, 3), dtype=np.uint8)
    positions = np.array([item[0] for item in stops], dtype=float)
    colors = np.array([hex_rgb(item[1]) for item in stops], dtype=float)

    for channel in range(3):
        output[..., channel] = np.interp(values, positions, colors[:, channel]).astype(np.uint8)
    return output


def draw_colorbar(
    image: Image.Image,
    box: tuple[int, int, int, int],
    minimum: float,
    maximum: float,
    stops: list[tuple[float, str]],
    label: str,
) -> None:
    """绘制竖直颜色条和数值刻度。"""

    draw = ImageDraw.Draw(image)
    left, top, right, bottom = box
    height = max(bottom - top, 2)
    values = np.linspace(1.0, 0.0, height).reshape(height, 1)
    color_array = gradient_color(values, stops)
    color_image = Image.fromarray(color_array, mode="RGB").resize((right - left, height))
    image.paste(color_image, (left, top))
    draw.rectangle(box, outline=MUTED, width=2)

    tick_font = font(19)
    for ratio in np.linspace(0.0, 1.0, 5):
        y = bottom - ratio * (bottom - top)
        value = minimum + ratio * (maximum - minimum)
        draw.line([(right, y), (right + 8, y)], fill=INK, width=2)
        draw.text((right + 12, y - 11), f"{value:.1f}", font=tick_font, fill=INK)
    draw.text((left - 5, bottom + 12), label, font=font(19, bold=True), fill=INK)


def draw_heatmap_panel(
    canvas: Image.Image,
    origin: tuple[int, int],
    matrix: np.ndarray,
    title: str,
    x_values: np.ndarray,
    y_values: np.ndarray,
    *,
    symmetric: bool = False,
) -> None:
    """在画布上绘制一幅带坐标轴和颜色条的热力图。"""

    x0, y0 = origin
    panel_width = 565
    plot_left = x0 + 74
    plot_top = y0 + 70
    plot_width = 390
    plot_height = 430
    colorbar_left = plot_left + plot_width + 20
    draw = ImageDraw.Draw(canvas)

    if symmetric:
        maximum = float(np.max(np.abs(matrix)))
        minimum = -maximum
        normalized = (matrix - minimum) / max(maximum - minimum, 1.0e-12)
        stops = [(0.0, "#214A9A"), (0.5, "#F8FBFD"), (1.0, "#C83E4D")]
    else:
        minimum = float(np.min(matrix))
        maximum = float(np.max(matrix))
        normalized = (matrix - minimum) / max(maximum - minimum, 1.0e-12)
        stops = [
            (0.0, "#163B6D"),
            (0.25, "#1976A3"),
            (0.5, "#21A69A"),
            (0.75, "#E4B532"),
            (1.0, "#C7433E"),
        ]

    heatmap = Image.fromarray(gradient_color(normalized, stops), mode="RGB")
    heatmap = heatmap.resize((plot_width, plot_height), resample=Image.Resampling.BICUBIC)
    canvas.paste(heatmap, (plot_left, plot_top))
    draw.rectangle(
        [plot_left, plot_top, plot_left + plot_width, plot_top + plot_height],
        outline=INK,
        width=2,
    )

    draw.text((x0 + 22, y0 + 10), title, font=font(27, bold=True), fill=NAVY)
    tick_font = font(18)
    for ratio in np.linspace(0.0, 1.0, 5):
        x = plot_left + ratio * plot_width
        value = x_values[0] + ratio * (x_values[-1] - x_values[0])
        draw.line([(x, plot_top + plot_height), (x, plot_top + plot_height + 7)], fill=INK, width=2)
        label = f"{value:.0f}"
        label_width = draw.textbbox((0, 0), label, font=tick_font)[2]
        draw.text((x - label_width / 2, plot_top + plot_height + 10), label, font=tick_font, fill=INK)

        y = plot_top + plot_height - ratio * plot_height
        y_value = y_values[0] + ratio * (y_values[-1] - y_values[0])
        draw.line([(plot_left - 7, y), (plot_left, y)], fill=INK, width=2)
        draw.text((plot_left - 62, y - 10), f"{y_value:.0f}", font=tick_font, fill=INK)

    draw.text(
        (plot_left + plot_width / 2 - 55, plot_top + plot_height + 44),
        "东向 x / m",
        font=font(20),
        fill=INK,
    )
    draw.text((plot_left - 4, plot_top - 31), "y / m", font=font(19, bold=True), fill=INK)
    draw_colorbar(
        canvas,
        (colorbar_left, plot_top, colorbar_left + 24, plot_top + plot_height),
        minimum,
        maximum,
        stops,
        "nT",
    )


def generate_principle_schematic() -> None:
    """生成舰艇磁场形成机理示意图。"""

    image = Image.new("RGB", (1800, 980), LIGHT)
    draw = ImageDraw.Draw(image)
    draw.text((70, 40), "舰艇磁场形成机理：背景地磁 + 船体磁化 + 剩磁", font=font(42, bold=True), fill=NAVY)
    draw.text(
        (72, 100),
        "把复杂舰体等效为一个有方向的磁性椭球，再计算其对外部传感器产生的异常磁场。",
        font=font(25),
        fill=MUTED,
    )

    # 绘制海水区域和海面。
    draw.rectangle([0, 205, 1800, 980], fill="#EAF5F8")
    draw.line([(0, 205), (1800, 205)], fill=CYAN, width=7)
    draw.text((78, 164), "海面", font=font(23, bold=True), fill=CYAN)

    # 绘制目标等效椭球和舰首方向。
    hull_box = (440, 380, 1320, 665)
    draw.ellipse(hull_box, fill="#D7E5ED", outline=NAVY, width=6)
    draw.ellipse((515, 425, 1245, 620), outline=BLUE, width=3)
    draw_centered_text(draw, hull_box, "等效磁性椭球\n长度 L · 宽度 W · 高度 H", font(34, bold=True), NAVY)
    add_arrow(draw, (875, 385), (1250, 385), ORANGE, width=7, head=22)
    draw.text((1020, 335), "舰首方向 / 航向", font=font(25, bold=True), fill=ORANGE)

    # 绘制背景地磁和两类磁矩。
    add_arrow(draw, (200, 275), (390, 470), BLUE, width=9, head=28)
    draw.text((80, 235), "背景地磁 B_g", font=font(29, bold=True), fill=BLUE)
    draw.text((78, 510), "地磁使铁磁船体产生\n感应磁矩 m_i", font=font(24), fill=INK, spacing=8)

    add_arrow(draw, (690, 700), (1110, 700), RED, width=8, head=24)
    draw.text((680, 730), "剩磁静态磁矩 m_r", font=font(27, bold=True), fill=RED)
    add_arrow(draw, (690, 790), (1180, 790), TEAL, width=8, head=24)
    draw.text((680, 820), "地磁感应磁矩 m_i", font=font(27, bold=True), fill=TEAL)

    # 绘制传感器与目标异常磁场方向。
    sensor = (1500, 790)
    draw.ellipse([sensor[0] - 24, sensor[1] - 24, sensor[0] + 24, sensor[1] + 24], fill=GOLD, outline=INK, width=4)
    draw.text((1400, 830), "固定磁传感器 / 水雷", font=font(25, bold=True), fill=INK)
    add_arrow(draw, (1300, 585), (1480, 760), GOLD, width=6, head=20)
    draw.text((1370, 520), "目标异常磁场\nB_anom", font=font(26, bold=True), fill=GOLD, spacing=6)

    # 绘制坐标系。
    origin = (165, 865)
    add_arrow(draw, origin, (320, 865), RED, width=5, head=16)
    add_arrow(draw, origin, (165, 710), GREEN, width=5, head=16)
    draw.text((326, 842), "x 东", font=font(23, bold=True), fill=RED)
    draw.text((105, 680), "z 上", font=font(23, bold=True), fill=GREEN)
    draw.text((88, 895), "y 北：垂直纸面", font=font(22), fill=MUTED)

    image.save(ASSET_DIR / "principle_schematic.png", quality=95)


def generate_flowchart() -> None:
    """生成磁场仿真完整流程图。"""

    image = Image.new("RGB", (1800, 1120), WHITE)
    draw = ImageDraw.Draw(image)
    draw.text((70, 40), "舰艇磁场特性仿真流程", font=font(44, bold=True), fill=NAVY)
    draw.text((72, 102), "一次参数配置，同时服务于空间分布和运动时序两类输出。", font=font(25), fill=MUTED)

    def box(
        rectangle: tuple[int, int, int, int],
        text: str,
        fill: str,
        outline: str = NAVY,
        text_color: str = NAVY,
    ) -> None:
        draw.rounded_rectangle(rectangle, radius=22, fill=fill, outline=outline, width=4)
        draw_centered_text(draw, rectangle, text, font(26, bold=True), text_color, spacing=7)

    box((660, 170, 1140, 270), "输入目标与环境参数", "#E7F0F6")
    box((660, 320, 1140, 420), "校验参数并建立等效椭球", "#E7F0F6")
    box((170, 495, 760, 610), "剩磁路径\nV_m × M_r → 静态磁矩", "#FCEBE7", outline=RED, text_color=RED)
    box((1040, 495, 1630, 610), "感应路径\n退磁因子 + 地磁 → 感应磁矩", "#E5F5F2", outline=TEAL, text_color=TEAL)
    box((660, 685, 1140, 790), "磁偶极子公式\n计算 Bx、By、Bz", "#FFF4D9", outline=GOLD, text_color=INK)
    box((205, 870, 735, 990), "空间分布\n零时刻遍历 x-y-z 网格", "#EAF1FB", outline=BLUE, text_color=BLUE)
    box((1065, 870, 1595, 990), "运动时序\n更新位置并按采样率计算", "#EAF1FB", outline=BLUE, text_color=BLUE)

    add_arrow(draw, (900, 270), (900, 320), NAVY, width=5, head=17)
    add_arrow(draw, (900, 420), (470, 495), RED, width=5, head=17)
    add_arrow(draw, (900, 420), (1335, 495), TEAL, width=5, head=17)
    add_arrow(draw, (470, 610), (780, 685), RED, width=5, head=17)
    add_arrow(draw, (1335, 610), (1020, 685), TEAL, width=5, head=17)
    add_arrow(draw, (820, 790), (500, 870), BLUE, width=5, head=17)
    add_arrow(draw, (980, 790), (1330, 870), BLUE, width=5, head=17)

    draw.text((255, 1022), "输出：磁场空间分布 CSV → 热力图 / 等值线 / 三维曲面", font=font(24), fill=BLUE)
    draw.text((1065, 1022), "输出：磁场时序 CSV → 分量曲线 / 距离曲线 / 航迹", font=font(24), fill=BLUE)
    image.save(ASSET_DIR / "simulation_flow.png", quality=95)


def load_spatial_grid() -> tuple[pd.DataFrame, np.ndarray, np.ndarray, dict[str, np.ndarray]]:
    """读取空间 CSV 并恢复规则网格矩阵。"""

    data = pd.read_csv(SPATIAL_CSV)
    depth = float(np.sort(data["z_m"].unique())[0])
    plane = data[np.isclose(data["z_m"], depth)].sort_values(["y_m", "x_m"])
    x_values = np.sort(plane["x_m"].unique())
    y_values = np.sort(plane["y_m"].unique())
    matrices: dict[str, np.ndarray] = {}
    for column in [
        "static_magnitude_nt",
        "induced_magnitude_nt",
        "total_magnitude_nt",
        "induced_bx_nt",
        "induced_by_nt",
        "induced_bz_nt",
    ]:
        matrices[column] = plane[column].to_numpy().reshape(len(y_values), len(x_values))
    return plane, x_values, y_values, matrices


def generate_spatial_figures() -> None:
    """根据真实仿真结果生成空间分布图。"""

    _, x_values, y_values, matrices = load_spatial_grid()

    image = Image.new("RGB", (1800, 650), WHITE)
    draw = ImageDraw.Draw(image)
    draw.text((55, 15), "实际仿真示例：水下 30 m 平面的磁场强度分布", font=font(36, bold=True), fill=NAVY)
    draw_heatmap_panel(
        image,
        (15, 55),
        matrices["static_magnitude_nt"],
        "静磁场强度",
        x_values,
        y_values,
    )
    draw_heatmap_panel(
        image,
        (610, 55),
        matrices["induced_magnitude_nt"],
        "感应磁场强度",
        x_values,
        y_values,
    )
    draw_heatmap_panel(
        image,
        (1205, 55),
        matrices["total_magnitude_nt"],
        "综合异常磁场",
        x_values,
        y_values,
    )
    image.save(ASSET_DIR / "spatial_heatmaps.png", quality=95)

    component_image = Image.new("RGB", (1800, 650), WHITE)
    component_draw = ImageDraw.Draw(component_image)
    component_draw.text((55, 15), "实际仿真示例：地磁感应磁场三分量", font=font(36, bold=True), fill=NAVY)
    draw_heatmap_panel(
        component_image,
        (15, 55),
        matrices["induced_bx_nt"],
        "东向分量 Bx",
        x_values,
        y_values,
        symmetric=True,
    )
    draw_heatmap_panel(
        component_image,
        (610, 55),
        matrices["induced_by_nt"],
        "北向分量 By",
        x_values,
        y_values,
        symmetric=True,
    )
    draw_heatmap_panel(
        component_image,
        (1205, 55),
        matrices["induced_bz_nt"],
        "垂向分量 Bz",
        x_values,
        y_values,
        symmetric=True,
    )
    component_image.save(ASSET_DIR / "induced_components.png", quality=95)


def draw_line_chart(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    x_values: np.ndarray,
    series: list[tuple[str, np.ndarray, str]],
    title: str,
    x_label: str,
    y_label: str,
) -> None:
    """绘制多序列折线图。"""

    left, top, right, bottom = box
    draw.rectangle(box, fill=WHITE, outline=GRID, width=2)
    x_min = float(np.min(x_values))
    x_max = float(np.max(x_values))
    all_y = np.concatenate([values for _, values, _ in series])
    y_min = float(np.min(all_y))
    y_max = float(np.max(all_y))
    y_padding = max((y_max - y_min) * 0.08, 1.0e-9)
    y_min -= y_padding
    y_max += y_padding

    def point(x_value: float, y_value: float) -> tuple[float, float]:
        x = left + (x_value - x_min) / max(x_max - x_min, 1.0e-12) * (right - left)
        y = bottom - (y_value - y_min) / max(y_max - y_min, 1.0e-12) * (bottom - top)
        return x, y

    # 绘制网格线和刻度。
    tick_font = font(18)
    for ratio in np.linspace(0.0, 1.0, 6):
        x = left + ratio * (right - left)
        y = top + ratio * (bottom - top)
        draw.line([(x, top), (x, bottom)], fill=GRID, width=1)
        draw.line([(left, y), (right, y)], fill=GRID, width=1)
        x_value = x_min + ratio * (x_max - x_min)
        y_value = y_max - ratio * (y_max - y_min)
        draw.text((x - 18, bottom + 8), f"{x_value:.0f}", font=tick_font, fill=MUTED)
        draw.text((left - 70, y - 10), f"{y_value:.1f}", font=tick_font, fill=MUTED)

    # 对时间序列降采样后绘制，减少图片节点数量。
    sample_step = max(len(x_values) // 600, 1)
    for name, values, color in series:
        points = [
            point(float(x_value), float(y_value))
            for x_value, y_value in zip(x_values[::sample_step], values[::sample_step])
        ]
        draw.line(points, fill=color, width=4, joint="curve")

    draw.rectangle(box, outline=INK, width=2)
    draw.text((left, top - 48), title, font=font(29, bold=True), fill=NAVY)
    draw.text(((left + right) / 2 - 45, bottom + 42), x_label, font=font(21), fill=INK)
    draw.text((left, top - 32), y_label, font=font(20), fill=INK)

    legend_x = right - 520
    legend_y = top + 18
    for name, _, color in series:
        draw.line([(legend_x, legend_y + 12), (legend_x + 40, legend_y + 12)], fill=color, width=5)
        draw.text((legend_x + 50, legend_y), name, font=font(20), fill=INK)
        legend_x += 170


def generate_time_series_figure() -> None:
    """根据真实仿真结果生成运动目标时间曲线。"""

    data = pd.read_csv(TIME_CSV)
    time_values = data["time_s"].to_numpy()
    image = Image.new("RGB", (1800, 1050), LIGHT)
    draw = ImageDraw.Draw(image)
    draw.text((65, 35), "实际仿真示例：运动舰艇通过固定磁传感器", font=font(38, bold=True), fill=NAVY)
    draw.text(
        (67, 92),
        "场强峰值通常出现在目标最近通过附近；三分量保留正负号，能够体现磁场方向变化。",
        font=font(24),
        fill=MUTED,
    )

    draw_line_chart(
        draw,
        (155, 205, 1690, 545),
        time_values,
        [
            ("Bx", data["total_bx_nt"].to_numpy(), RED),
            ("By", data["total_by_nt"].to_numpy(), GREEN),
            ("Bz", data["total_bz_nt"].to_numpy(), BLUE),
        ],
        "综合目标异常磁场三分量",
        "时间 / s",
        "磁场 / nT",
    )
    draw_line_chart(
        draw,
        (155, 665, 1690, 965),
        time_values,
        [
            ("静磁场", data["static_magnitude_nt"].to_numpy(), ORANGE),
            ("感应场", data["induced_magnitude_nt"].to_numpy(), CYAN),
            ("综合场", data["total_magnitude_nt"].to_numpy(), NAVY),
        ],
        "静磁场、感应场和综合场强度",
        "时间 / s",
        "场强 / nT",
    )
    image.save(ASSET_DIR / "time_series.png", quality=95)


def generate_sensitivity_figure() -> None:
    """生成距离衰减和材料比例敏感性趋势图。"""

    image = Image.new("RGB", (1800, 760), WHITE)
    draw = ImageDraw.Draw(image)
    draw.text((60, 30), "参数敏感性：理解哪些量最影响结果", font=font(38, bold=True), fill=NAVY)
    draw.text(
        (62, 85),
        "以下为归一化趋势，用于讲解公式关系；工程数值仍应以实测标定为准。",
        font=font(23),
        fill=MUTED,
    )

    # 左图展示偶极场的距离三次方反比。
    distances = np.linspace(1.0, 5.0, 300)
    attenuation = 1.0 / distances**3
    draw_line_chart(
        draw,
        (145, 210, 840, 625),
        distances,
        [("B/B₀", attenuation, BLUE)],
        "距离衰减：B ∝ 1/r³",
        "归一化距离 r/r₀",
        "相对场强",
    )

    # 右图展示材料等效体积比例对磁矩的线性影响。
    ratios = np.linspace(0.01, 0.10, 300)
    relative_moment = ratios / 0.04
    draw_line_chart(
        draw,
        (1050, 210, 1745, 625),
        ratios,
        [("m/m₀", relative_moment, TEAL)],
        "磁性材料比例：m ∝ η",
        "等效材料比例 η",
        "相对磁矩",
    )
    image.save(ASSET_DIR / "parameter_sensitivity.png", quality=95)


def main() -> None:
    """生成全部文档插图。"""

    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    if not SPATIAL_CSV.exists() or not TIME_CSV.exists():
        raise FileNotFoundError("缺少验证 CSV，请先运行 seamine_simulator 生成仿真结果。")

    generate_principle_schematic()
    generate_flowchart()
    generate_spatial_figures()
    generate_time_series_figure()
    generate_sensitivity_figure()
    print(f"已生成文档插图：{ASSET_DIR}")


if __name__ == "__main__":
    main()

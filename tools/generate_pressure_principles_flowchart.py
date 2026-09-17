"""生成与当前分工况水压场算法一致的文档流程图。"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


# 输出目录放在构建产物中，文档更新脚本会把该图片嵌入 Word。
ROOT_DIR = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT_DIR / "build" / "pressure_principles_assets"
OUTPUT_PATH = OUTPUT_DIR / "pressure_regime_flowchart.png"
FONT_PATH = Path("C:/Windows/Fonts/msyh.ttc")


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    """加载微软雅黑字体；标题优先使用粗体字体文件。"""

    bold_path = Path("C:/Windows/Fonts/msyhbd.ttc")
    selected_path = bold_path if bold and bold_path.exists() else FONT_PATH
    return ImageFont.truetype(str(selected_path), size=size)


def wrap_text(
    drawing: ImageDraw.ImageDraw,
    text: str,
    font: ImageFont.FreeTypeFont,
    maximum_width: int,
) -> list[str]:
    """按像素宽度对中英文混排文字换行，并保留显式换行。"""

    wrapped_lines: list[str] = []
    for source_line in text.split("\n"):
        current_line = ""
        for character in source_line:
            candidate = current_line + character
            if drawing.textbbox((0, 0), candidate, font=font)[2] <= maximum_width:
                current_line = candidate
            else:
                if current_line:
                    wrapped_lines.append(current_line)
                current_line = character
        wrapped_lines.append(current_line)
    return wrapped_lines


def add_box(
    drawing: ImageDraw.ImageDraw,
    bounds: tuple[int, int, int, int],
    title: str,
    details: str,
    border_color: str,
    fill_color: str,
) -> None:
    """绘制一个圆角步骤框，并写入标题与关键说明。"""

    left, top, right, bottom = bounds
    drawing.rounded_rectangle(
        bounds,
        radius=28,
        fill=fill_color,
        outline=border_color,
        width=4,
    )
    title_font = load_font(38, bold=True)
    detail_font = load_font(25)
    drawing.text((left + 36, top + 22), title, font=title_font, fill="#173b57")

    detail_lines = wrap_text(drawing, details, detail_font, right - left - 72)
    line_height = 38
    detail_y = top + 78
    for line in detail_lines:
        drawing.text((left + 36, detail_y), line, font=detail_font, fill="#263746")
        detail_y += line_height


def add_arrow(
    drawing: ImageDraw.ImageDraw,
    start_y: int,
    end_y: int,
    center_x: int = 800,
) -> None:
    """在相邻步骤框之间绘制流程箭头。"""

    drawing.line((center_x, start_y, center_x, end_y - 16), fill="#6b7c88", width=5)
    drawing.polygon(
        [(center_x - 13, end_y - 18), (center_x + 13, end_y - 18), (center_x, end_y)],
        fill="#6b7c88",
    )


def build_flowchart() -> None:
    """按输入、判据、分支、合成和输出顺序生成完整流程图。"""

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    canvas = Image.new("RGB", (1600, 1800), "white")
    drawing = ImageDraw.Draw(canvas)

    drawing.text((120, 75), "水压场分工况仿真流程", font=load_font(58, bold=True), fill="#173b57")
    drawing.text(
        (120, 160),
        "舰艇与潜艇先判工况，再选择低速刚盖、平滑过渡或浅水亚临界压力模型",
        font=load_font(28),
        fill="#5b6d78",
    )

    # 每一步只保留实现中真正参与计算和工况选择的关键量。
    steps = [
        ((130, 250, 1470, 430), "1  输入与合法性校验", "目标类型、L/B/T(H)、U、航向、初始位置、海深、观测点", "#2f75b5", "#edf5fb"),
        ((130, 500, 1470, 680), "2  本体等效", "计算排水体积 V、形状因子 Ks、单位航速偶极子强度 q", "#2a9d8f", "#edf9f7"),
        ((130, 750, 1470, 930), "3  更新位置并计算判据", "FrL=U/√(gL)，FrH=U/√(gH)，H/L；潜艇另算 Ew=exp(−gh/U²)", "#d99a24", "#fff6df"),
        (
            (130, 1000, 1470, 1240),
            "4  自动选择水动力工况",
            "水面舰艇：低速 / 0.08～0.12 过渡 / 浅水亚临界\n潜艇：FrL≤0.10 或 Ew≤0.01 时采用无兴波分支；其余不支持工况显式报告",
            "#7b6bb3",
            "#f3f0fb",
        ),
        (
            (130, 1310, 1470, 1545),
            "5  计算分支压力",
            "低速：本体 + 刚盖周期镜像    过渡：smoothstep 加权\n浅水亚临界：本体 + 深度平均兴波/海床修正    深潜潜艇：本体 + 第一海床镜像",
            "#2f75b5",
            "#edf5fb",
        ),
        ((130, 1610, 1470, 1760), "6  分量合成与结果表达", "输出压力分量、总动压、静水压、总表压和 regime", "#2a9d8f", "#edf9f7"),
    ]

    for bounds, title, details, border_color, fill_color in steps:
        add_box(drawing, bounds, title, details, border_color, fill_color)

    # 箭头连接每个步骤框，突出判据选择对后续压力分支的控制关系。
    for index in range(len(steps) - 1):
        current_bounds = steps[index][0]
        next_bounds = steps[index + 1][0]
        add_arrow(drawing, current_bounds[3], next_bounds[1])

    canvas.save(OUTPUT_PATH, format="PNG", optimize=True)
    print(f"已生成：{OUTPUT_PATH}")


if __name__ == "__main__":
    # 直接运行即可刷新文档所用的新工况流程图。
    build_flowchart()

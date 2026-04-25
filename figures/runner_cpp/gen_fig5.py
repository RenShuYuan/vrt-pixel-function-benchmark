#!/usr/bin/env python3
"""
Generate Figure 5: Two-Layer Evaluation Protocol
Replaces the old "three-experiment" layout with the correct two-layer design
matching the revised manuscript Section 6.

Output: manuscript/revision_r1_cpp_final_2026-04-10/figures/05_experiment_protocol.png

Usage:
    python gen_fig5.py [output_path]
"""
import sys
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch
import matplotlib.patheffects as pe

# ── Output path ───────────────────────────────────────────────────────────
_HERE = Path(__file__).resolve().parent
_REPO_ROOT = _HERE.parent.parent  # revision_r1_cpp_final_2026-04-10/
DEFAULT_OUT = _REPO_ROOT / 'figures' / '05_experiment_protocol.png'
OUT_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
OUT_PATH.parent.mkdir(parents=True, exist_ok=True)

# ── Style (matches regenerate_arch_figures.py) ───────────────────────────
plt.rcParams.update({
    'font.family': 'serif',
    'font.serif': ['Times New Roman', 'DejaVu Serif'],
    'font.size': 10,
    'axes.linewidth': 0,
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'savefig.pad_inches': 0.15,
})

SHADOW = [pe.withSimplePatchShadow(offset=(1.5, -1.5),
                                    shadow_rgbFace='#CCCCCC', alpha=0.3)]

# ── Color palette ─────────────────────────────────────────────────────────
C = {
    # Layer panels
    'L1_bg':    '#FFFDE7', 'L1_edge':  '#F57F17',   # warm amber – mechanism
    'L2_bg':    '#E8EAF6', 'L2_edge':  '#283593',   # indigo     – production
    # Workload tiers (Layer 1)
    'light_bg': '#E8F5E9', 'light_edge': '#2E7D32',
    'med_bg':   '#E3F2FD', 'med_edge':   '#1565C0',
    'heavy_bg': '#FCE4EC', 'heavy_edge': '#C62828',
    # Production chains (Layer 2)
    'popt_bg':  '#E8F5E9', 'popt_edge':  '#2E7D32',
    'pmed_bg':  '#E3F2FD', 'pmed_edge':  '#1565C0',
    'phvy_bg':  '#FCE4EC', 'phvy_edge':  '#C62828',
    # Comparison / datasets
    'mode_bg':  '#ECEFF1', 'mode_edge':  '#455A64',
    'data_bg':  '#EDE7F6', 'data_edge':  '#4527A0',
    # Mode indicator colors (matches Figures 6-9)
    'vrt':      '#4C78A8',
    'geotiff':  '#F58518',
    'vsimem':   '#59A14F',
    # Misc
    'arrow':    '#546E7A',
    'divider':  '#B0BEC5',
}


def _box(ax, x, y, w, h, fc, ec, lw=1.6, radius=0.04, zorder=2, shadow=True):
    box = FancyBboxPatch((x, y), w, h,
                         boxstyle=f'round,pad=0.02,rounding_size={radius}',
                         facecolor=fc, edgecolor=ec, linewidth=lw, zorder=zorder)
    if shadow:
        box.set_path_effects(SHADOW)
    ax.add_patch(box)
    return box


def _txt(ax, x, y, text, size=9, weight='normal', style='normal',
         color='#222222', align='center', zorder=4, **kw):
    ax.text(x, y, text, ha=align, va='center', fontsize=size,
            fontweight=weight, fontstyle=style, color=color, zorder=zorder,
            linespacing=1.35, **kw)


def _arrow(ax, x1, y1, x2, y2, lw=1.3, color=None):
    c = color or C['arrow']
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle='->', color=c, lw=lw,
                                connectionstyle='arc3,rad=0'),
                zorder=5)


# ══════════════════════════════════════════════════════════════════════════
# Main figure
# ══════════════════════════════════════════════════════════════════════════
def build():
    W, H = 10.8, 7.2
    fig, ax = plt.subplots(figsize=(W, H))
    ax.set_xlim(0, W)
    ax.set_ylim(0, H)
    ax.axis('off')

    # ── Section title ────────────────────────────────────────────────────
    ax.set_title('Two-Layer Evaluation Protocol for Chained VRT Computation',
                 fontsize=13, fontweight='bold', pad=12)

    # ── Vertical divider between Layer 1 and Layer 2 ────────────────────
    mid_x = 5.4
    ax.plot([mid_x, mid_x], [1.85, 7.0], color=C['divider'],
            lw=1.2, ls='--', zorder=1)

    # ─────────────────────────────────────────────────────────────────────
    # LAYER 1 PANEL (left)
    # ─────────────────────────────────────────────────────────────────────
    L1_x, L1_w = 0.15, 5.1
    # Panel background
    _box(ax, L1_x, 1.85, L1_w, 5.0, C['L1_bg'], C['L1_edge'], lw=2.0,
         radius=0.06, zorder=1, shadow=False)

    # Header
    _txt(ax, L1_x + L1_w / 2, 6.55,
         'Layer 1: Mechanism Experiment', size=11, weight='bold')
    _txt(ax, L1_x + L1_w / 2, 6.23,
         '7 cases  ·  2-step chains  ·  isolation of intermediate-storage effect',
         size=8.2, color='#555')

    # ── Tier boxes (Light / Medium / Heavy) ──────────────────────────────
    tier_y, tier_h = 4.05, 1.88
    tier_gap = 0.12
    tier_total_w = L1_w - 0.30
    tier_w = (tier_total_w - 2 * tier_gap) / 3
    tier_x0 = L1_x + 0.15

    tiers = [
        ('Light\n(×3 cases)', C['light_bg'], C['light_edge'],
         'decimal adj. + pixel mod.',
         'small DEM · medium DEM\n1.9 GB optical PAN'),
        ('Medium\n(×2 cases)', C['med_bg'], C['med_edge'],
         'DSM/DEM diff. + decimal adj.',
         'small DEM · medium DEM'),
        ('Heavy\n(×2 cases)', C['heavy_bg'], C['heavy_edge'],
         'decimal adj. + roughness 5×5',
         'small DEM · medium DEM'),
    ]

    for i, (title, fc, ec, ops, data) in enumerate(tiers):
        tx = tier_x0 + i * (tier_w + tier_gap)
        _box(ax, tx, tier_y, tier_w, tier_h, fc, ec, lw=1.6, radius=0.05)
        # Tier title
        _txt(ax, tx + tier_w / 2, tier_y + tier_h - 0.28,
             title, size=9.5, weight='bold')
        # Op description
        _txt(ax, tx + tier_w / 2, tier_y + tier_h / 2 + 0.08,
             ops, size=7.8, color='#333',
             bbox=dict(boxstyle='round,pad=0.18', fc='white',
                       ec='#CCCCCC', alpha=0.75))
        # Dataset label
        _txt(ax, tx + tier_w / 2, tier_y + 0.35,
             data, size=7.2, color='#555', style='italic')

    # Step-count annotation strip
    strip_y = tier_y - 0.08
    ax.annotate('', xy=(tier_x0 + tier_total_w, strip_y - 0.01),
                xytext=(tier_x0, strip_y - 0.01),
                arrowprops=dict(arrowstyle='<->', color=C['arrow'], lw=1.1))
    _txt(ax, tier_x0 + tier_total_w / 2, strip_y - 0.22,
         '2 processing steps per case (VRT chain)', size=7.8,
         style='italic', color=C['arrow'])

    # Metrics strip
    m1_y = 2.22
    _box(ax, tier_x0, m1_y, tier_total_w, 0.55,
         '#FAFAFA', '#90A4AE', lw=1.0, radius=0.03, shadow=False)
    _txt(ax, tier_x0 + tier_total_w / 2, m1_y + 0.28,
         'Metrics: storage reduction (%) · runtime (ms) · per-step I/O timing decomposition',
         size=7.8, color='#444')

    # Repetitions note
    _txt(ax, tier_x0 + tier_total_w / 2, m1_y - 0.14,
         '10 repetitions (DEM cases) · 5 repetitions (optical cases)',
         size=7.2, style='italic', color='#666')

    # ─────────────────────────────────────────────────────────────────────
    # LAYER 2 PANEL (right)
    # ─────────────────────────────────────────────────────────────────────
    L2_x, L2_w = mid_x + 0.15, W - mid_x - 0.30
    _box(ax, L2_x, 1.85, L2_w, 5.0, C['L2_bg'], C['L2_edge'], lw=2.0,
         radius=0.06, zorder=1, shadow=False)

    # Header
    _txt(ax, L2_x + L2_w / 2, 6.55,
         'Layer 2: Production-Like Workflow', size=11, weight='bold')
    _txt(ax, L2_x + L2_w / 2, 6.23,
         '3 cases  ·  3-step chains  ·  end-to-end production scenario',
         size=8.2, color='#555')

    # ── Chain boxes ───────────────────────────────────────────────────────
    ch_y, ch_h = 4.05, 1.88
    ch_total_w = L2_w - 0.30
    ch_w = (ch_total_w - 2 * tier_gap) / 3
    ch_x0 = L2_x + 0.15

    chains = [
        ('Optical\nLight', C['popt_bg'], C['popt_edge'],
         'px.adj → px.mod → px.adj',
         '1.9 GB PAN  ·  5 reps'),
        ('DEM/DSM\nMedium', C['pmed_bg'], C['pmed_edge'],
         'diff → decimal → normalize',
         'small DEM  ·  10 reps'),
        ('DEM/DSM\nHeavy', C['phvy_bg'], C['phvy_edge'],
         'decimal → roughness → decimal',
         'small DEM  ·  10 reps'),
    ]

    for i, (title, fc, ec, ops, info) in enumerate(chains):
        cx = ch_x0 + i * (ch_w + tier_gap)
        _box(ax, cx, ch_y, ch_w, ch_h, fc, ec, lw=1.6, radius=0.05)
        _txt(ax, cx + ch_w / 2, ch_y + ch_h - 0.28,
             title, size=9.5, weight='bold')
        _txt(ax, cx + ch_w / 2, ch_y + ch_h / 2 + 0.10,
             ops, size=7.4, color='#333', style='italic',
             bbox=dict(boxstyle='round,pad=0.18', fc='white',
                       ec='#CCCCCC', alpha=0.75))
        # Step count chips
        for si in range(3):
            sx = cx + 0.14 + si * (ch_w - 0.28) / 3
            _box(ax, sx, ch_y + 0.32, (ch_w - 0.28) / 3 - 0.06, 0.28,
                 '#FFFFFF', ec, lw=0.9, radius=0.02, shadow=False)
            _txt(ax, sx + (ch_w - 0.28) / 6 - 0.03, ch_y + 0.46,
                 f'Step {si+1}', size=6.5, color=ec)
        _txt(ax, cx + ch_w / 2, ch_y + 0.12,
             info, size=7.0, color='#666', style='italic')

    # Step-count annotation
    ax.annotate('', xy=(ch_x0 + ch_total_w, strip_y - 0.01),
                xytext=(ch_x0, strip_y - 0.01),
                arrowprops=dict(arrowstyle='<->', color=C['arrow'], lw=1.1))
    _txt(ax, ch_x0 + ch_total_w / 2, strip_y - 0.22,
         '3 processing steps per case (VRT chain)', size=7.8,
         style='italic', color=C['arrow'])

    # Metrics strip (Layer 2)
    _box(ax, ch_x0, m1_y, ch_total_w, 0.55,
         '#FAFAFA', '#90A4AE', lw=1.0, radius=0.03, shadow=False)
    _txt(ax, ch_x0 + ch_total_w / 2, m1_y + 0.28,
         'Metrics: storage reduction (%) · end-to-end runtime (ms)',
         size=7.8, color='#444')
    _txt(ax, ch_x0 + ch_total_w / 2, m1_y - 0.14,
         '10 repetitions (DEM cases) · 5 repetitions (optical cases)',
         size=7.2, style='italic', color='#666')

    # ─────────────────────────────────────────────────────────────────────
    # BOTTOM BAR: Comparison Modes + Datasets
    # ─────────────────────────────────────────────────────────────────────
    bot_y, bot_h = 0.18, 1.48
    bsep = 0.15

    # Left: Comparison modes
    cm_w = mid_x - 0.15 - bsep / 2 + L1_x
    _box(ax, L1_x, bot_y, cm_w, bot_h, C['mode_bg'], C['mode_edge'],
         lw=1.5, radius=0.05, shadow=False)
    _txt(ax, L1_x + cm_w / 2, bot_y + bot_h - 0.24,
         'Comparison Modes (all cases)', size=9, weight='bold', color='#263238')

    mode_items = [
        (C['vrt'],     'VRT',       'deferred evaluation, no intermediate materialization'),
        (C['geotiff'], 'GeoTIFF',   'on-disk materialization at every step'),
        (C['vsimem'],  '/vsimem/',  'in-memory GeoTIFF materialization at every step'),
    ]
    for mi, (col, name, desc) in enumerate(mode_items):
        iy = bot_y + bot_h - 0.52 - mi * 0.30
        # Colored dot
        ax.plot(L1_x + 0.30, iy, 's', color=col, ms=7, zorder=5)
        _txt(ax, L1_x + 0.50, iy, f'{name}:', size=8.2,
             weight='bold', color=col, align='left')
        _txt(ax, L1_x + 1.20, iy, desc, size=7.8, color='#444', align='left')

    # Right: Datasets
    dm_x = mid_x + 0.15
    dm_w = W - dm_x - 0.15
    _box(ax, dm_x, bot_y, dm_w, bot_h, C['data_bg'], C['data_edge'],
         lw=1.5, radius=0.05, shadow=False)
    _txt(ax, dm_x + dm_w / 2, bot_y + bot_h - 0.24,
         'Benchmark Datasets', size=9, weight='bold', color='#1A237E')

    dataset_items = [
        'DEM/DSM small   2697×1957 px  Float32  10 m/px  (single map sheet)',
        'DEM/DSM medium  5299×3816 px  Float32  10 m/px  (2×2 mosaic)',
        'Optical PAN     30839×30948 px  UInt16  ~0.5 m/px  ≈ 1.9 GB',
    ]
    for di, dstr in enumerate(dataset_items):
        dy = bot_y + bot_h - 0.52 - di * 0.30
        ax.plot(dm_x + 0.22, dy, 'D', color=C['data_edge'], ms=4.5, zorder=5)
        _txt(ax, dm_x + 0.38, dy, dstr, size=7.6, color='#333', align='left')

    # ─────────────────────────────────────────────────────────────────────
    # Layer labels (outside panels, at left/right of divider)
    # ─────────────────────────────────────────────────────────────────────
    for lx, label, col in [
        (L1_x + 0.08, 'L1', C['L1_edge']),
        (mid_x + 0.22, 'L2', C['L2_edge']),
    ]:
        ax.text(lx, 7.0, label, ha='center', va='center',
                fontsize=13, fontweight='bold', color='white', zorder=7,
                bbox=dict(boxstyle='round,pad=0.3', fc=col, ec='none'))

    fig.tight_layout(rect=[0, 0, 1, 0.97])
    fig.savefig(str(OUT_PATH))
    plt.close(fig)
    print(f'Figure 5 saved → {OUT_PATH}')


if __name__ == '__main__':
    build()

"""Add race panels driven by the two local actors to the native OSD page."""
import re

# The native page uses a centered 1920 x 1080 coordinate system.
PANELS = (
    ("SpeedometerPanel", "Speed", "speedometer", 767.0, -122.0, 0.70),
    ("RacePosPanel", "Pos", "race_position", -823.0, -442.0, 0.78),
    ("LapCountPanel", "Lap", "lap_count", 789.5, -432.0, 0.78),
    ("PercentageIconPanel", "Progress", "percentage", 789.5, -432.0, 0.78),
    ("WrongWayPanel", "Wrong", "wrong_way", 0.0, -158.0, 0.78),
    ("TimerPanel", "Timer", "racetimer", -823.0, -376.0, 0.78),
)


def prepare_stock_hud(mods, panels, inactive=False):
    assets = {name.decode("ascii"): bytes(data) for name, _, data in panels}
    panel_dir = mods / "newui/panels"
    panel_dir.mkdir(parents=True, exist_ok=True)
    original = assets["newui/panels/osd_layout_race.txt"]
    if not inactive:
        (panel_dir / "osd_timetrial.txt").write_bytes(assets["newui/panels/osd_timetrial.txt"])
    controls = []
    for cls, role, asset, x, y, scale in PANELS:
        source = assets[f"newui/panels/osd_{asset}.txt"]
        for seat in (1, 2):
            name = f"Split{role}{seat}"
            filename = f"osd_split_{role.lower()}{seat}.txt"
            panel = re.sub(rb'(\bNAME\s+")[^"]+', rb'\g<1>' + name.encode("ascii"), source, count=1)
            if inactive:
                panel = re.sub(rb'\bACTIVE\s+(TRUE|FALSE)', b'ACTIVE FALSE', panel, count=1)
            panel = re.sub(rb'(\bTRANSFORM_R\s+)[^\r\n]+',
                           rb'\g<1>' + f"{scale:.4f} 0.0000 0.0000".encode(), panel, count=1)
            panel = re.sub(rb'(\bTRANSFORM_U\s+)[^\r\n]+',
                           rb'\g<1>' + f"0.0000 {scale:.4f} 0.0000".encode(), panel, count=1)
            panel = re.sub(rb'(\bTRANSLATION\s+)[^\r\n]+',
                           rb'\g<1>' + f"{x:.3f} {y + (seat - 1) * 540:.3f} 0.000".encode(), panel, count=1)
            (panel_dir / filename).write_bytes(panel)
            controls.append(f'\tPanel("{cls}", "{name}", "{filename}")\r\n')

    anchor = b'\tPanel("MirrorAlignedPanel"'
    if original.count(anchor) != 1:
        raise ValueError("Unsupported race OSD layout")
    (panel_dir / "osd_layout_race.txt").write_bytes(
        original.replace(anchor, "".join(controls).encode("ascii") + anchor))

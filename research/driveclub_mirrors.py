"""Enable native mirror materials in the retail two-player quality profile."""
import re
from driveclub_probe import test_assets


def prepare_mirrors(game, mods):
    source = mods / "global.rpk"
    data = source.read_bytes() if source.exists() else next(
        data for _, _, data in test_assets(game, "global.rpk"))
    start = data.index(b"<ViewportQuality>")
    end = data.index(b"</ViewportQuality>", start) + len(b"</ViewportQuality>")
    original = data[start:end]
    xml = re.sub(rb"<!--.*?-->", b"", original, flags=re.S)
    xml = re.sub(rb"\s+", b" ", xml)
    pattern = rb'(<Configuration name="TwoPlayer2D"[^>]*>.*?<General\b)([^>]*)(/>)'
    def enable(match):
        attrs = re.sub(rb'\s+enableRearViewMirrorTextures="[^"]*"', b"", match[2])
        return match[1] + attrs + b' enableRearViewMirrorTextures="true" ' + match[3]
    xml, count = re.subn(pattern, enable, xml, count=1, flags=re.S)
    if count != 1 or len(xml) > len(original):
        raise ValueError("Unsupported two-player viewport quality asset")
    xml = xml[:-len(b"</ViewportQuality>")] + b" " * (len(original) - len(xml)) + b"</ViewportQuality>"
    assert len(xml) == len(original)
    source.write_bytes(data[:start] + xml + data[end:])

# hd-tiles (R-3)

HD tile replacement plugin for the zelda3 `FrameDesc` renderer.

```sh
make -C plugins/hd-tiles

# original 2x example pack (grass, bush, dungeon floor/wall)
ZELDA3_HD_PACK=plugins/hd-tiles/example-pack \
  ./zelda3 --render-plugin plugins/hd-tiles/hd-tiles.so zelda3.sfc
```

The pack path comes from `ZELDA3_HD_PACK` (a directory with `pack.json`, or a
`pack.json` file). Without it the plugin is a pass-through. A pack is a JSON
manifest plus PNGs; the full format, the tile hash and the
`--frame-desc-dump` tile-sheet exporter are documented in
`docs/engine/RENDER.md`.

`example-pack/tiles.png` is original geometric art (no pixels from the ROM).
Build artifacts (`hd-tiles.so`, `.dll`, `.dylib`) are not committed.

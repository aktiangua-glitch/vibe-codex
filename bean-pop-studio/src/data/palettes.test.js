import { describe, expect, it } from "vitest";
import {
  brandCatalog,
  brandMeta,
  getBrandDefaultMaxColors,
  getBrandOutputColorLimit,
  getBrandPalette,
  getBrandPaletteSize,
  getBrandRecommendedColorCounts,
} from "./palettes.js";

describe("palette catalog", () => {
  it("loads the full MARD 221 palette into the data layer", () => {
    const palette = getBrandPalette("MARD");

    expect(palette).toHaveLength(221);
    expect(getBrandPaletteSize("MARD")).toBe(221);
    expect(palette[0]).toMatchObject({ code: "A1", hex: "#faf4c8", family: "A" });
    expect(palette.at(-1)).toMatchObject({ code: "M15", hex: "#757d78", family: "M" });
    expect(new Set(palette.map((color) => color.code)).size).toBe(221);
  });

  it("separates brand palette size from the output color cap", () => {
    expect(getBrandOutputColorLimit("MARD")).toBe(221);
    expect(getBrandDefaultMaxColors("MARD")).toBe(221);
    expect(getBrandRecommendedColorCounts("MARD")).toEqual([12, 18, 24, 32, 40, 48, 64, 96, 128, 160, 192, 221]);
  });

  it("keeps metadata available for the UI without exposing raw color arrays", () => {
    expect(brandMeta.MARD).toMatchObject({
      badge: "CN Mainstream",
      coverage: "full",
      label: "MARD 221",
      maxOutputColors: 221,
      paletteSize: 221,
    });
    expect("colors" in brandMeta.MARD).toBe(false);
  });

  it("preserves starter palettes for Perler and Hama inside the new schema", () => {
    expect(brandCatalog.Perler.colors).toHaveLength(24);
    expect(brandCatalog.Hama.colors).toHaveLength(24);
    expect(brandCatalog.Perler.coverage).toBe("starter");
    expect(brandCatalog.Hama.coverage).toBe("starter");
  });
});

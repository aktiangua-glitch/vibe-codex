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
  it("loads the full MARD 291 palette into the data layer", () => {
    const palette = getBrandPalette("MARD");

    expect(palette).toHaveLength(291);
    expect(getBrandPaletteSize("MARD")).toBe(291);
    expect(palette[0]).toMatchObject({ code: "A1", hex: "#fdeaea", family: "A" });
    expect(palette.at(-1)).toMatchObject({ code: "ZG8", hex: "#cab9c2", family: "ZG" });
    expect(new Set(palette.map((color) => color.code)).size).toBe(291);
  });

  it("separates brand palette size from the output color cap", () => {
    expect(getBrandOutputColorLimit("MARD")).toBe(48);
    expect(getBrandDefaultMaxColors("MARD")).toBe(18);
    expect(getBrandRecommendedColorCounts("MARD")).toEqual([12, 18, 24, 32, 40, 48]);
  });

  it("keeps metadata available for the UI without exposing raw color arrays", () => {
    expect(brandMeta.MARD).toMatchObject({
      badge: "CN Mainstream",
      coverage: "full",
      label: "MARD 291",
      maxOutputColors: 48,
      paletteSize: 291,
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

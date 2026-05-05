import { describe, expect, it } from "vitest";
import {
  getPreviewCellSize,
  getScaledGridSize,
  selectActivePaletteForUsage,
  smoothIsolatedCells,
  sortCountsOuterFirst,
  summarizeCellSamples,
} from "./beadEngine.js";

function color(code, lab) {
  return {
    code,
    hex: "#ffffff",
    lab,
    name: code,
    rgb: { r: 255, g: 255, b: 255 },
  };
}

describe("beadEngine", () => {
  it("keeps dominant colors while protecting rare dark and vivid accents", () => {
    const common = color("COMMON", { l: 62, a: 2, b: 2 });
    const dark = color("DARK", { l: 18, a: 1, b: 1 });
    const vivid = color("VIVID", { l: 56, a: 52, b: 38 });
    const light = color("LIGHT", { l: 92, a: 1, b: 4 });
    const muted = color("MUTED", { l: 58, a: 3, b: 4 });

    const selected = selectActivePaletteForUsage(
      [
        { color: common, count: 900, weight: 980 },
        { color: muted, count: 55, weight: 72 },
        { color: dark, count: 18, weight: 24 },
        { color: vivid, count: 16, weight: 22 },
        { color: light, count: 11, weight: 14 },
      ],
      3,
      1000
    ).map((item) => item.code);

    expect(selected).toEqual(["COMMON", "DARK", "VIVID"]);
  });

  it("protects tiny dark accents when the max color count is tight", () => {
    const common = color("COMMON", { l: 72, a: 3, b: 8 });
    const mid = color("MID", { l: 58, a: 8, b: 10 });
    const dark = color("DARK", { l: 16, a: 0, b: 1 });

    const selected = selectActivePaletteForUsage(
      [
        { color: common, count: 980, weight: 980 },
        { color: mid, count: 18, weight: 40 },
        { color: dark, count: 2, weight: 7 },
      ],
      2,
      1000
    ).map((item) => item.code);

    expect(selected).toEqual(["COMMON", "DARK"]);
  });

  it("merges similar color families before dropping a distinct accent", () => {
    const warmA = color("WARM_A", { l: 63, a: 11, b: 14 });
    const warmB = color("WARM_B", { l: 66, a: 10, b: 13 });
    const warmC = color("WARM_C", { l: 60, a: 13, b: 16 });
    const cool = color("COOL", { l: 54, a: -12, b: -18 });
    const light = color("LIGHT", { l: 90, a: 1, b: 3 });

    const selected = selectActivePaletteForUsage(
      [
        { color: warmA, count: 420, weight: 510 },
        { color: warmB, count: 300, weight: 360 },
        { color: warmC, count: 160, weight: 210 },
        { color: cool, count: 110, weight: 150 },
        { color: light, count: 90, weight: 120 },
      ],
      3,
      1080
    ).map((item) => item.code);

    expect(selected).toEqual(["WARM_A", "LIGHT", "COOL"]);
  });

  it("removes similar isolated speckles without deleting contrast details", () => {
    const base = color("BASE", { l: 82, a: 1, b: 4 });
    const speckle = color("SPECKLE", { l: 80, a: 2, b: 5 });
    const detail = color("DETAIL", { l: 18, a: 0, b: 0 });

    const similarMatrix = [
      [base, base, base],
      [base, speckle, base],
      [base, base, base],
    ];
    const contrastMatrix = [
      [base, base, base],
      [base, detail, base],
      [base, base, base],
    ];

    expect(smoothIsolatedCells(similarMatrix, 3, 3)[1][1].code).toBe("BASE");
    expect(smoothIsolatedCells(contrastMatrix, 3, 3)[1][1].code).toBe("DETAIL");
  });

  it("sorts palette counts from outer colors to inner details", () => {
    const sorted = sortCountsOuterFirst([
      { code: "INNER", count: 40, outerScore: 0.15 },
      { code: "OUTER", count: 12, outerScore: 0.92 },
      { code: "MID", count: 70, outerScore: 0.5 },
    ]).map((item) => item.code);

    expect(sorted).toEqual(["OUTER", "MID", "INNER"]);
  });

  it("preserves small high-contrast details during cell sampling", () => {
    const skin = { r: 236, g: 190, b: 146 };
    const eye = { r: 22, g: 20, b: 24 };
    const samples = [
      ...Array.from({ length: 15 }, () => ({ rgb: skin, alpha: 255 })),
      { rgb: eye, alpha: 255 },
    ];
    const naiveLuma = ((15 * (skin.r * 0.299 + skin.g * 0.587 + skin.b * 0.114))
      + (eye.r * 0.299 + eye.g * 0.587 + eye.b * 0.114)) / 16;
    const summary = summarizeCellSamples(samples);
    const summaryLuma = summary.rgb.r * 0.299 + summary.rgb.g * 0.587 + summary.rgb.b * 0.114;

    expect(summaryLuma).toBeLessThan(naiveLuma - 18);
    expect(summary.detailScore).toBeGreaterThan(40);
  });

  it("does not over-amplify low-contrast sampling noise", () => {
    const base = { r: 232, g: 224, b: 210 };
    const noise = { r: 219, g: 214, b: 204 };
    const samples = [
      ...Array.from({ length: 15 }, () => ({ rgb: base, alpha: 255 })),
      { rgb: noise, alpha: 255 },
    ];
    const summary = summarizeCellSamples(samples);

    expect(Math.abs(summary.rgb.r - 231)).toBeLessThanOrEqual(2);
    expect(Math.abs(summary.rgb.g - 223)).toBeLessThanOrEqual(2);
    expect(Math.abs(summary.rgb.b - 210)).toBeLessThanOrEqual(2);
  });

  it("scales bead dimensions by the longest side without stretching portrait art", () => {
    expect(getScaledGridSize({ width: 900, height: 1200 }, 32)).toEqual({
      width: 24,
      height: 32,
    });
    expect(getScaledGridSize({ width: 1200, height: 900 }, 32)).toEqual({
      width: 32,
      height: 24,
    });
  });

  it("fits large preview grids inside the available canvas", () => {
    expect(getPreviewCellSize({
      containerWidth: 520,
      containerHeight: 430,
      gridWidth: 72,
      gridHeight: 96,
      padding: 22,
    })).toBe(4);
  });
});

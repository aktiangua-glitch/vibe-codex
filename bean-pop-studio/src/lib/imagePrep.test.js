import { describe, expect, it } from "vitest";
import {
  applyStyleToImageData,
  computeCropWindow,
  findSubjectBoxFromRgba,
  getRatioValue,
} from "./imagePrep.js";

function createSolidRgba(width, height, background, subject) {
  const data = new Uint8ClampedArray(width * height * 4);

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const offset = (y * width + x) * 4;
      const active = subject && x >= subject.x && x < subject.x + subject.width && y >= subject.y && y < subject.y + subject.height;
      const color = active ? subject.color : background;
      data[offset] = color[0];
      data[offset + 1] = color[1];
      data[offset + 2] = color[2];
      data[offset + 3] = 255;
    }
  }

  return data;
}

describe("imagePrep", () => {
  it("uses the source image ratio when auto crop is selected", () => {
    expect(getRatioValue("auto", 900, 1180)).toBeCloseTo(900 / 1180, 5);
  });

  it("finds a clear subject box from rgba pixels", () => {
    const data = createSolidRgba(
      20,
      20,
      [255, 255, 255],
      { x: 5, y: 4, width: 8, height: 9, color: [40, 40, 40] }
    );

    const subject = findSubjectBoxFromRgba(data, 20, 20);

    expect(subject.x).toBeLessThan(0.35);
    expect(subject.x).toBeGreaterThan(0.15);
    expect(subject.y).toBeLessThan(0.3);
    expect(subject.width).toBeGreaterThan(0.3);
    expect(subject.height).toBeGreaterThanOrEqual(0.4);
    expect(subject.confidence).toBeGreaterThan(0.18);
  });

  it("uses alpha bounds for transparent artwork", () => {
    const data = new Uint8ClampedArray(20 * 20 * 4);
    for (let y = 4; y < 18; y += 1) {
      for (let x = 2; x < 19; x += 1) {
        const offset = (y * 20 + x) * 4;
        data[offset] = 248;
        data[offset + 1] = 198;
        data[offset + 2] = 45;
        data[offset + 3] = 255;
      }
    }

    const subject = findSubjectBoxFromRgba(data, 20, 20);

    expect(subject.hasTransparentBackground).toBe(true);
    expect(subject.x).toBeCloseTo(0.1, 2);
    expect(subject.y).toBeCloseTo(0.2, 2);
    expect(subject.width).toBeCloseTo(0.85, 2);
    expect(subject.height).toBeCloseTo(0.7, 2);
  });

  it("computes a crop window that stays inside image bounds", () => {
    const crop = computeCropWindow({
      imageWidth: 1200,
      imageHeight: 900,
      ratioKey: "portrait",
      framingKey: "tight",
      zoomAdjust: 1.12,
      offsetX: -0.4,
      offsetY: 0.25,
      subjectBox: {
        x: 0.08,
        y: 0.1,
        width: 0.32,
        height: 0.5,
        confidence: 0.7,
      },
    });

    expect(crop.x).toBeGreaterThanOrEqual(0);
    expect(crop.y).toBeGreaterThanOrEqual(0);
    expect(crop.x + crop.width).toBeLessThanOrEqual(1200);
    expect(crop.y + crop.height).toBeLessThanOrEqual(900);
    expect(crop.zoom).toBeGreaterThanOrEqual(1);
  });

  it("applies style changes only when a style is selected", () => {
    const source = {
      data: new Uint8ClampedArray([
        120, 110, 100, 255,
        30, 60, 160, 255,
        240, 220, 120, 255,
        80, 30, 30, 255,
      ]),
    };

    const untouched = applyStyleToImageData(
      { data: new Uint8ClampedArray(source.data) },
      2,
      2,
      "none",
      0.7
    );
    const stylized = applyStyleToImageData(
      { data: new Uint8ClampedArray(source.data) },
      2,
      2,
      "sticker_pop",
      0.8
    );

    expect(Array.from(untouched.data)).toEqual(Array.from(source.data));
    expect(Array.from(stylized.data)).not.toEqual(Array.from(source.data));
  });
});

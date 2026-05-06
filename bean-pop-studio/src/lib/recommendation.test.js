import { describe, expect, it } from "vitest";
import { getRecommendedTargetWidth } from "./recommendation.js";

describe("recommendation", () => {
  it("falls back to a larger default board size when source dimensions are missing", () => {
    expect(getRecommendedTargetWidth({ imageWidth: 0, imageHeight: 0 })).toBe(87);
  });

  it("recommends a larger board when the subject is small in frame", () => {
    expect(getRecommendedTargetWidth({
      imageWidth: 2200,
      imageHeight: 2200,
      subjectBox: {
        width: 0.34,
        height: 0.44,
        confidence: 0.82,
      },
    })).toBe(145);
  });

  it("still defaults to a larger board size even when the subject already fills most of the frame", () => {
    expect(getRecommendedTargetWidth({
      imageWidth: 2200,
      imageHeight: 2200,
      subjectBox: {
        width: 0.86,
        height: 0.92,
        confidence: 0.88,
      },
    })).toBe(116);
  });

  it("leans larger for mid-sized photos so the generated pattern stays readable", () => {
    expect(getRecommendedTargetWidth({
      imageWidth: 1600,
      imageHeight: 1600,
      subjectBox: {
        width: 0.78,
        height: 0.86,
        confidence: 0.88,
      },
    })).toBe(116);
  });

  it("boosts transparent small subjects more aggressively for a cleaner final effect", () => {
    expect(getRecommendedTargetWidth({
      imageWidth: 1400,
      imageHeight: 1400,
      subjectBox: {
        width: 0.32,
        height: 0.46,
        confidence: 0.76,
        visibleRatio: 0.12,
      },
    })).toBe(145);
  });

  it("still keeps small subjects large even when subject detection confidence is weak", () => {
    expect(getRecommendedTargetWidth({
      imageWidth: 2200,
      imageHeight: 2200,
      subjectBox: {
        width: 0.34,
        height: 0.44,
        confidence: 0.18,
      },
    })).toBe(145);
  });
});

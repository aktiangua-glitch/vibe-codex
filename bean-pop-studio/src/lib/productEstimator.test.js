import { describe, expect, it } from "vitest";
import {
  estimateBoardCount,
  estimateColorPacks,
  estimateMaterialPlan,
  getWorkloadBand,
} from "./productEstimator.js";

describe("productEstimator", () => {
  it("estimates board count using 29x29 boards", () => {
    expect(estimateBoardCount(32, 42)).toBe(4);
    expect(estimateBoardCount(58, 58)).toBe(4);
  });

  it("adds waste allowance when estimating packs", () => {
    const packs = estimateColorPacks(
      [
        { code: "M01", count: 980 },
        { code: "M02", count: 1500 },
      ],
      1000,
      0.1
    );

    expect(packs[0].adjustedCount).toBe(1078);
    expect(packs[0].packs).toBe(2);
    expect(packs[1].packs).toBe(2);
  });

  it("calculates total material cost when pricing is provided", () => {
    const plan = estimateMaterialPlan({
      counts: [
        { code: "M01", count: 820 },
        { code: "M02", count: 1180 },
      ],
      width: 40,
      height: 50,
      packSize: 1000,
      packPrice: 9.9,
      boardPrice: 4.5,
      wasteRate: 0.08,
    });

    expect(plan.boardCount).toBe(4);
    expect(plan.totalPacks).toBe(3);
    expect(plan.beadCost).toBeCloseTo(29.7, 5);
    expect(plan.boardCost).toBeCloseTo(18, 5);
    expect(plan.totalCost).toBeCloseTo(47.7, 5);
  });

  it("returns a higher workload band for large projects", () => {
    expect(getWorkloadBand(1400, 10).label).toBe("新手友好");
    expect(getWorkloadBand(2800, 16).label).toBe("周末可完成");
    expect(getWorkloadBand(5600, 24).label).toBe("进阶难度");
  });
});

import { describe, expect, it } from "vitest";
import { getBudgetStatus, getProjectPulse } from "./studioStatus.js";

describe("studioStatus", () => {
  it("marks budget ready only when both prices are provided", () => {
    const ready = getBudgetStatus({
      hasResult: true,
      packPrice: 9.9,
      boardPrice: 4.5,
      totalCost: 47.7,
      totalPacks: 3,
      boardCount: 4,
    });
    const warning = getBudgetStatus({
      hasResult: true,
      packPrice: 9.9,
      boardPrice: 0,
      totalCost: 29.7,
      totalPacks: 3,
      boardCount: 4,
    });

    expect(ready.tone).toBe("ready");
    expect(ready.note).toContain("¥47.7");
    expect(warning.tone).toBe("warning");
  });

  it("treats a generated pattern as ready, with pricing as optional context", () => {
    const ready = getProjectPulse({
      hasImage: true,
      hasPrepared: true,
      hasResult: true,
      budgetReady: true,
      totalBeads: 2048,
      colorCount: 14,
    });
    const draft = getProjectPulse({
      hasImage: true,
      hasPrepared: true,
      hasResult: true,
      budgetReady: false,
      totalBeads: 2048,
      colorCount: 14,
    });

    expect(ready.score).toBe(100);
    expect(ready.label).toBe("可以开做");
    expect(draft.score).toBe(100);
    expect(draft.label).toBe("可以保存图纸");
  });
});

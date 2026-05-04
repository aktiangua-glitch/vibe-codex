export const estimatorDefaults = {
  boardPrice: "",
  boardSize: 29,
  packPrice: "",
  packSize: 1000,
  wasteRate: 0.08,
};

export function estimateBoardCount(width, height, boardSize = 29) {
  return Math.ceil(width / boardSize) * Math.ceil(height / boardSize);
}

export function estimateColorPacks(counts, packSize, wasteRate = 0.08) {
  return counts.map((entry) => {
    const adjustedCount = Math.ceil(entry.count * (1 + wasteRate));
    const packs = Math.max(1, Math.ceil(adjustedCount / Math.max(packSize, 1)));

    return {
      ...entry,
      adjustedCount,
      packs,
    };
  });
}

export function estimateMaterialPlan({
  counts,
  width,
  height,
  boardSize = 29,
  packSize = 1000,
  packPrice = 0,
  boardPrice = 0,
  wasteRate = 0.08,
}) {
  const boardCount = estimateBoardCount(width, height, boardSize);
  const colorPacks = estimateColorPacks(counts, packSize, wasteRate);
  const totalPacks = colorPacks.reduce((sum, entry) => sum + entry.packs, 0);
  const beadCost = packPrice > 0 ? totalPacks * packPrice : 0;
  const boardCost = boardPrice > 0 ? boardCount * boardPrice : 0;
  const totalCost = beadCost + boardCost;

  return {
    beadCost,
    boardCost,
    boardCount,
    colorPacks,
    totalCost,
    totalPacks,
  };
}

export function getWorkloadBand(totalBeads, colorCount) {
  if (totalBeads <= 1800 && colorCount <= 12) {
    return {
      label: "新手友好",
      note: "适合第一次尝试完整作品。",
    };
  }

  if (totalBeads <= 3200 && colorCount <= 18) {
    return {
      label: "周末可完成",
      note: "适合周末慢慢拼完一张礼物图。",
    };
  }

  return {
    label: "进阶难度",
    note: "建议先确认材料和时间，再开始备料。",
  };
}

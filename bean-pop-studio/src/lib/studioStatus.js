function formatMoney(value) {
  return `¥${Number(value || 0).toFixed(1)}`;
}

export function getBudgetStatus({
  hasResult = true,
  packPrice = 0,
  boardPrice = 0,
  totalCost = 0,
  totalPacks = 0,
  boardCount = 0,
}) {
  if (!hasResult) {
    return {
      tone: "pending",
      title: "先看拼豆效果",
      note: "有了图纸后，就能知道需要多少豆子和拼板。",
    };
  }

  const hasPackPrice = Number(packPrice) > 0;
  const hasBoardPrice = Number(boardPrice) > 0;

  if (hasPackPrice && hasBoardPrice) {
    return {
      tone: "ready",
      title: "材料费用已估算",
      note: `大约 ${formatMoney(totalCost)}，包含 ${totalPacks} 包豆子和 ${boardCount} 块拼板。`,
    };
  }

  if (hasPackPrice || hasBoardPrice) {
    return {
      tone: "warning",
      title: "还差一个价格",
      note: hasPackPrice
        ? "再补拼板价格，就能看到完整材料费用。"
        : "再补豆子价格，就能看到完整材料费用。",
    };
  }

  return {
    tone: "pending",
    title: "可选填材料价格",
    note: "如果准备买材料，填上单价就能看到大概花费。",
  };
}

export function getProjectPulse({
  hasImage = false,
  hasPrepared = false,
  hasResult = false,
  budgetReady = false,
  totalBeads = 0,
  colorCount = 0,
}) {
  const score = [
    hasImage ? 30 : 0,
    hasPrepared ? 30 : 0,
    hasResult ? 40 : 0,
  ].reduce((sum, value) => sum + value, 0);

  if (!hasImage) {
    return {
      score,
      label: "先上传照片",
      note: "上传后就能直接看到拼豆效果、图纸和色号。",
    };
  }

  if (hasResult && budgetReady) {
    return {
      score,
      label: "可以开做",
      note: `这一版用了 ${colorCount} 种颜色，图纸和色号都准备好了。`,
    };
  }

  if (hasResult) {
    return {
      score,
      label: "可以保存图纸",
      note: `这版大约 ${totalBeads.toLocaleString("zh-CN")} 颗。喜欢就保存，不满意再微调。`,
    };
  }

  if (hasPrepared) {
    return {
      score,
      label: "画面已经放好",
      note: "接下来选择尺寸、颜色数和豆子品牌。",
    };
  }

  return {
    score,
    label: "先放好画面",
    note: "主体和构图会影响拼豆效果，先让画面看起来舒服。",
  };
}
